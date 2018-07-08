package com.polygraphene.alvr;

import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.SurfaceTexture;
import android.media.MediaCodec;
import android.opengl.EGL14;
import android.opengl.EGLContext;
import android.util.Log;
import android.view.Surface;

class VrThread extends Thread {
    private static final String TAG = "VrThread";

    private static final String KEY_SERVER_ADDRESS = "serverAddress";
    private static final String KEY_SERVER_PORT = "serverPort";

    private static final int PORT = 9944;

    private MainActivity mMainActivity;

    private VrContext mVrContext = new VrContext();
    private ThreadQueue mQueue = null;
    private boolean mResumed = false;

    private SurfaceTexture mSurfaceTexture;
    private Surface mSurface;

    private boolean mRendered = false;
    private boolean mRenderRequested = false;
    private long mFrameIndex = 0;
    private final Object mWaiter = new Object();

    private LoadingTexture mLoadingTexture = new LoadingTexture();

    // Worker threads
    private DecoderThread mDecoderThread;
    private UdpReceiverThread mReceiverThread;

    private EGLContext mEGLContext;

    public VrThread(MainActivity mainActivity) {
        this.mMainActivity = mainActivity;
    }

    public void onSurfaceCreated(final Surface surface) {
        Log.v(TAG, "VrThread.onSurfaceCreated.");
        send(new Runnable() {
            @Override
            public void run() {
                mVrContext.onSurfaceCreated(surface);
            }
        });
    }

    public void onSurfaceChanged(final Surface surface) {
        Log.v(TAG, "VrThread.onSurfaceChanged.");
        send(new Runnable() {
            @Override
            public void run() {
                mVrContext.onSurfaceChanged(surface);
            }
        });
    }

    public void onSurfaceDestroyed() {
        Log.v(TAG, "VrThread.onSurfaceDestroyed.");
        send(new Runnable() {
            @Override
            public void run() {
                mVrContext.onSurfaceDestroyed();
            }
        });
    }

    public void onResume() {
        synchronized (mWaiter) {
            mResumed = true;
            mWaiter.notifyAll();
        }

        send(new Runnable() {
            @Override
            public void run() {
                Log.v(TAG, "VrThread.onResume: Starting worker threads.");

                mReceiverThread = new UdpReceiverThread(mUdpReceiverCallback, mVrContext);
                mReceiverThread.setPort(PORT);
                loadConnectionState();
                mDecoderThread = new DecoderThread(mReceiverThread, mRenderCallback, mMainActivity);

                try {
                    mDecoderThread.start();
                    if (!mReceiverThread.start(mVrContext.is72Hz(), mEGLContext, mMainActivity)) {
                        Log.e(TAG, "FATAL: Initialization of ReceiverThread failed.");
                        return;
                    }
                } catch (IllegalArgumentException | IllegalStateException | SecurityException e) {
                    e.printStackTrace();
                }

                Log.v(TAG, "VrThread.onResume: mVrContext.onResume().");
                mVrContext.onResume();
            }
        });
        Log.v(TAG, "VrThread.onResume: Worker threads has started.");
    }

    public void onPause() {
        Log.v(TAG, "VrThread.onPause: Stopping worker threads.");
        synchronized (mWaiter) {
            mResumed = false;
            mWaiter.notifyAll();
        }
        // DecoderThread must be stopped before ReceiverThread
        if (mDecoderThread != null) {
            Log.v(TAG, "VrThread.onPause: Stopping DecoderThread.");
            mDecoderThread.stopAndWait();
        }
        if (mReceiverThread != null) {
            Log.v(TAG, "VrThread.onPause: Stopping ReceiverThread.");
            mReceiverThread.stopAndWait();
        }

        Log.v(TAG, "VrThread.onPause: mVrContext.onPause().");
        send(new Runnable() {
            @Override
            public void run() {
                mVrContext.onPause();
            }
        });
        Log.v(TAG, "VrThread.onPause: All worker threads has stopped.");
    }

    public void onKeyEvent(final int keyCode, final int action) {
        post(new Runnable() {
            @Override
            public void run() {
                mVrContext.onKeyEvent(keyCode, action);
            }
        });
    }

    // Called from onDestroy
    @Override
    public void interrupt() {
        post(new Runnable() {
            @Override
            public void run() {
                mLoadingTexture.destroyTexture();
                mQueue.interrupt();
            }
        });
    }

    private void post(Runnable runnable) {
        waitLooperPrepared();
        mQueue.post(runnable);
    }

    private void send(Runnable runnable) {
        waitLooperPrepared();
        mQueue.send(runnable);
    }

    private void waitLooperPrepared() {
        synchronized (this) {
            while (mQueue == null) {
                try {
                    wait();
                } catch (InterruptedException e) {
                    e.printStackTrace();
                }
            }
        }
    }

    @Override
    public void run() {
        setName("VrThread");
        Log.v(TAG, "VrThread started.");

        synchronized (this) {
            mQueue = new ThreadQueue();
            notifyAll();
        }

        mVrContext.initialize(mMainActivity, Constants.IS_ARCORE_BUILD);

        mSurfaceTexture = new SurfaceTexture(mVrContext.getSurfaceTextureID());
        mSurfaceTexture.setOnFrameAvailableListener(new SurfaceTexture.OnFrameAvailableListener() {
            @Override
            public void onFrameAvailable(SurfaceTexture surfaceTexture) {
                Utils.frameLog(mFrameIndex, "onFrameAvailable");

                synchronized (mWaiter) {
                    mRenderRequested = false;
                    mRendered = true;
                    mWaiter.notifyAll();
                }
            }
        });
        mSurface = new Surface(mSurfaceTexture);

        mLoadingTexture.initializeMessageCanvas(mVrContext.getLoadingTexture());
        mLoadingTexture.drawMessage(mMainActivity.getVersionName() + "\nLoading...");

        mEGLContext = EGL14.eglGetCurrentContext();

        Log.v(TAG, "Start loop of VrThread.");
        while(mQueue.waitIdle()) {
            if(!mVrContext.isVrMode() || !mResumed) {
                mQueue.waitNext();
                continue;
            }
            render();
        }

        Log.v(TAG, "Destroying vrapi state.");
        mVrContext.destroy();
    }

    private void render(){
        if (mReceiverThread.isConnected() && mDecoderThread.isFrameAvailable() && mReceiverThread.getErrorMessage() == null) {
            long renderedFrameIndex = waitFrame();
            if(renderedFrameIndex != -1) {
                mVrContext.render(renderedFrameIndex);
            }
        } else {
            if (mReceiverThread.getErrorMessage() != null) {
                mLoadingTexture.drawMessage(mMainActivity.getVersionName() + "\n \n!!! Error on ARCore initialization !!!\n" + mReceiverThread.getErrorMessage());
            } else {
                if (mReceiverThread.isConnected()) {
                    mLoadingTexture.drawMessage(mMainActivity.getVersionName() + "\n \nConnected!\nStreaming will begin soon!");
                } else {
                    mLoadingTexture.drawMessage(mMainActivity.getVersionName() + "\n \nPress CONNECT button\non ALVR server.");
                }
            }
            mVrContext.renderLoading();
            try {
                Thread.sleep(100);
            } catch (InterruptedException e) {
                e.printStackTrace();
            }
        }
    }

    private long waitFrame() {
        long startTime = System.nanoTime();
        synchronized (mWaiter) {
            if (mRendered) {
                Log.v(TAG, "updateTexImage(discard)");
                mSurfaceTexture.updateTexImage();
            }
            mRenderRequested = true;
            mRendered = false;
            mWaiter.notifyAll();
        }
        while (true) {
            synchronized (mWaiter) {
                if (!mResumed) {
                    return -1;
                }
                if (mRendered) {
                    mRendered = false;
                    mRenderRequested = false;
                    mSurfaceTexture.updateTexImage();
                    break;
                }
                if(System.nanoTime() - startTime > 1000 * 1000 * 1000L) {
                    // Idle for 1-sec.
                    Log.v(TAG, "Wait failed.");
                    return -1;
                }
                try {
                    mWaiter.wait(100);
                } catch (InterruptedException e) {
                    e.printStackTrace();
                }
            }
        }

        return mFrameIndex;
    }

    private UdpReceiverThread.Callback mUdpReceiverCallback = new UdpReceiverThread.Callback() {
        @Override
        public void onConnected(final int width, final int height, final int codec) {
            // We must wait completion of notifyGeometryChange
            // to ensure the first video frame arrives after notifyGeometryChange.
            send(new Runnable() {
                @Override
                public void run() {
                    mVrContext.setFrameGeometry(width, height);
                    mDecoderThread.notifyCodecChange(codec);
                }
            });
        }

        @Override
        public void onChangeSettings(int enableTestMode, int suspend) {
            mVrContext.onChangeSettings(enableTestMode, suspend);
        }

        @Override
        public void onShutdown(String serverAddr, int serverPort) {
            saveConnectionState(serverAddr, serverPort);
        }
    };

    private DecoderThread.RenderCallback mRenderCallback = new DecoderThread.RenderCallback() {
        @Override
        public Surface getSurface() {
            return mSurface;
        }

        @Override
        public int renderIf(MediaCodec codec, int queuedOutputBuffer, long frameIndex) {
            //Log.v(TAG, "renderIf " + queuedOutputBuffer);
            synchronized (mWaiter) {
                long startTime = System.nanoTime();
                while(!mRenderRequested && System.nanoTime() - startTime < 50 * 1000 * 1000) {
                    try {
                        Log.v(TAG, "Waiting mRenderRequested=" + mRenderRequested);
                        mWaiter.wait(10);
                    } catch (InterruptedException e) {
                        e.printStackTrace();
                    }
                }
                if (!mRenderRequested) {
                    return queuedOutputBuffer;
                }
            }

            if (queuedOutputBuffer == -1) {
                return queuedOutputBuffer;
            }

            codec.releaseOutputBuffer(queuedOutputBuffer, true);
            synchronized (mWaiter) {
                //mRendered = true;
                mFrameIndex = frameIndex;
                //waiter.notifyAll();
            }
            return -1;
        }
    };

    private void saveConnectionState(String serverAddress, int serverPort) {
        Log.v(TAG, "save connection state: " + serverAddress + " " + serverPort);
        SharedPreferences pref = mMainActivity.getSharedPreferences("pref", Context.MODE_PRIVATE);
        SharedPreferences.Editor edit = pref.edit();
        // If server address is NULL, it means no preserved connection.
        edit.putString(KEY_SERVER_ADDRESS, serverAddress);
        edit.putInt(KEY_SERVER_PORT, serverPort);
        edit.apply();
    }

    private void loadConnectionState() {
        SharedPreferences pref = mMainActivity.getSharedPreferences("pref", Context.MODE_PRIVATE);
        String serverAddress = pref.getString(KEY_SERVER_ADDRESS, null);
        int serverPort = pref.getInt(KEY_SERVER_PORT, 0);

        saveConnectionState(null, 0);

        Log.v(TAG, "load connection state: " + serverAddress + " " + serverPort);
        mReceiverThread.recoverConnectionState(serverAddress, serverPort);
    }

    public boolean isTracking() {
        return mVrContext != null && mReceiverThread != null
                && mVrContext.isVrMode() && mReceiverThread.isConnected();
    }
}

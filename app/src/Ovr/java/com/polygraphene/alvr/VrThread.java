package com.polygraphene.alvr;

import android.app.Activity;
import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.SurfaceTexture;
import android.opengl.EGL14;
import android.opengl.EGLContext;
import android.util.Log;
import android.view.Surface;

class VrThread extends Thread {
    private static final String TAG = "VrThread";

    private Activity mActivity;

    private OvrContext mOvrContext = new OvrContext();
    private ThreadQueue mQueue = null;
    private boolean mResumed = false;

    private SurfaceTexture mSurfaceTexture;
    private Surface mSurface;

    private final Object mWaiter = new Object();
    private boolean mFrameAvailable = false;

    private LoadingTexture mLoadingTexture = new LoadingTexture();

    // Worker threads
    private DecoderThread mDecoderThread;
    private UdpReceiverThread mReceiverThread;

    private EGLContext mEGLContext;

    public VrThread(Activity activity) {
        this.mActivity = activity;
    }

    public void onSurfaceCreated(final Surface surface) {
        Log.v(TAG, "VrThread.onSurfaceCreated.");
        send(new Runnable() {
            @Override
            public void run() {
                mOvrContext.onSurfaceCreated(surface);
            }
        });
    }

    public void onSurfaceChanged(final Surface surface) {
        Log.v(TAG, "VrThread.onSurfaceChanged.");
        send(new Runnable() {
            @Override
            public void run() {
                mOvrContext.onSurfaceChanged(surface);
            }
        });
    }

    public void onSurfaceDestroyed() {
        Log.v(TAG, "VrThread.onSurfaceDestroyed.");
        send(new Runnable() {
            @Override
            public void run() {
                mOvrContext.onSurfaceDestroyed();
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

                mReceiverThread = new UdpReceiverThread(mUdpReceiverCallback);

                ConnectionStateHolder.ConnectionState connectionState = new ConnectionStateHolder.ConnectionState();
                ConnectionStateHolder.loadConnectionState(mActivity, connectionState);

                if(connectionState.serverAddr != null && connectionState.serverPort != 0) {
                    Log.v(TAG, "load connection state: " + connectionState.serverAddr + " " + connectionState.serverPort);
                    mReceiverThread.recoverConnectionState(connectionState.serverAddr, connectionState.serverPort);
                }

                // Sometimes previous decoder output remains not updated (when previous call of waitFrame() didn't call updateTexImage())
                // and onFrameAvailable won't be called after next output.
                // To avoid deadlock caused by it, we need to flush last output.
                mSurfaceTexture.updateTexImage();

                mDecoderThread = new DecoderThread(mReceiverThread, mSurface, mActivity);

                try {
                    mDecoderThread.start();

                    DeviceDescriptor deviceDescriptor = new DeviceDescriptor();
                    mOvrContext.getDeviceDescriptor(deviceDescriptor);
                    if (!mReceiverThread.start(mEGLContext, mActivity, deviceDescriptor, mOvrContext.getCameraTexture())) {
                        Log.e(TAG, "FATAL: Initialization of ReceiverThread failed.");
                        return;
                    }
                } catch (IllegalArgumentException | IllegalStateException | SecurityException e) {
                    e.printStackTrace();
                }

                Log.v(TAG, "VrThread.onResume: mVrContext.onResume().");
                mOvrContext.onResume();
            }
        });
        Log.v(TAG, "VrThread.onResume: Worker threads has started.");
    }

    public void onPause() {
        Log.v(TAG, "VrThread.onPause: Stopping worker threads.");
        // DecoderThread must be stopped before ReceiverThread and setting mResumed=false.
        if (mDecoderThread != null) {
            Log.v(TAG, "VrThread.onPause: Stopping DecoderThread.");
            mDecoderThread.stopAndWait();
        }
        if (mReceiverThread != null) {
            Log.v(TAG, "VrThread.onPause: Stopping ReceiverThread.");
            mReceiverThread.stopAndWait();
        }
        // VrThread rendering loop calls mDecoderThread.render and which captures mWaiter lock.
        // So we need to stop DecoderThread before gain the lock.
        synchronized (mWaiter) {
            mResumed = false;
            mWaiter.notifyAll();
        }

        Log.v(TAG, "VrThread.onPause: mVrContext.onPause().");
        send(new Runnable() {
            @Override
            public void run() {
                mOvrContext.onPause();
            }
        });
        Log.v(TAG, "VrThread.onPause: All worker threads has stopped.");
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

        mOvrContext.initialize(mActivity, mActivity.getAssets(), Constants.IS_ARCORE_BUILD, 60);

        mSurfaceTexture = new SurfaceTexture(mOvrContext.getSurfaceTextureID());
        mSurfaceTexture.setOnFrameAvailableListener(new SurfaceTexture.OnFrameAvailableListener() {
            @Override
            public void onFrameAvailable(SurfaceTexture surfaceTexture) {
                Utils.log("VrThread: waitFrame: onFrameAvailable is called.");
                synchronized (mWaiter) {
                    mFrameAvailable = true;
                    mWaiter.notifyAll();
                }
            }
        });
        mSurface = new Surface(mSurfaceTexture);

        mLoadingTexture.initializeMessageCanvas(mOvrContext.getLoadingTexture());
        mLoadingTexture.drawMessage(Utils.getVersionName(mActivity) + "\nLoading...");

        mEGLContext = EGL14.eglGetCurrentContext();

        Log.v(TAG, "Start loop of VrThread.");
        while (mQueue.waitIdle()) {
            if (!mOvrContext.isVrMode() || !mResumed) {
                mQueue.waitNext();
                continue;
            }
            render();
        }

        Log.v(TAG, "Destroying vrapi state.");
        mOvrContext.destroy();
    }

    private void render() {
        if (mReceiverThread.isConnected() && mDecoderThread.isFrameAvailable() && mReceiverThread.getErrorMessage() == null) {
            long renderedFrameIndex = waitFrame();
            if (renderedFrameIndex != -1) {
                mOvrContext.render(renderedFrameIndex);
            }
        } else {
            if (mReceiverThread.getErrorMessage() != null) {
                mLoadingTexture.drawMessage(Utils.getVersionName(mActivity) + "\n \n!!! Error on ARCore initialization !!!\n" + mReceiverThread.getErrorMessage());
            } else {
                if (mReceiverThread.isConnected()) {
                    mLoadingTexture.drawMessage(Utils.getVersionName(mActivity) + "\n \nConnected!\nStreaming will begin soon!");
                } else {
                    mLoadingTexture.drawMessage(Utils.getVersionName(mActivity) + "\n \nPress CONNECT button\non ALVR server.");
                }
            }
            mOvrContext.renderLoading();
            try {
                Thread.sleep(100);
            } catch (InterruptedException e) {
                e.printStackTrace();
            }
        }
    }

    private long waitFrame() {
        synchronized (mWaiter) {
            mFrameAvailable = false;
            long frameIndex = mDecoderThread.render();
            if (frameIndex == -1) {
                return -1;
            }
            while (!mFrameAvailable && mResumed) {
                try {
                    mWaiter.wait();
                } catch (InterruptedException e) {
                    e.printStackTrace();
                }
            }
            if(!mResumed) {
                Log.i(TAG, "Exit waitFrame. mResumed=false.");
                return -1;
            }
            mSurfaceTexture.updateTexImage();
            return frameIndex;
        }
    }

    private UdpReceiverThread.Callback mUdpReceiverCallback = new UdpReceiverThread.Callback() {
        @Override
        public void onConnected(final int width, final int height, final int codec, final int frameQueueSize, final int refreshRate) {
            // We must wait completion of notifyGeometryChange
            // to ensure the first video frame arrives after notifyGeometryChange.
            send(new Runnable() {
                @Override
                public void run() {
                    mOvrContext.setRefreshRate(refreshRate);
                    mOvrContext.setFrameGeometry(width, height);
                    mDecoderThread.onConnect(codec, frameQueueSize);
                }
            });
        }

        @Override
        public void onChangeSettings(int enableTestMode, int suspend, int frameQueueSize) {
            mOvrContext.onChangeSettings(enableTestMode, suspend);
            mDecoderThread.setFrameQueueSize(frameQueueSize);
        }

        @Override
        public void onShutdown(String serverAddr, int serverPort) {
            Log.v(TAG, "save connection state: " + serverAddr + " " + serverPort);
            ConnectionStateHolder.saveConnectionState(mActivity, serverAddr, serverPort);
        }

        @Override
        public void onDisconnect() {
            mDecoderThread.onDisconnect();
        }

        @Override
        public void onTracking(float[] position, float[] orientation) {
            if(mOvrContext.isVrMode()) {
                mOvrContext.fetchTrackingInfo(mReceiverThread, position, orientation);
            }
        }
    };
}

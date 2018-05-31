package com.polygraphene.alvr;

import android.graphics.SurfaceTexture;
import android.media.MediaCodec;
import android.util.Log;
import android.view.Surface;

import java.util.concurrent.TimeUnit;

class VrThread extends Thread {
    private static final String TAG = "VrThread";

    private static final int PORT = 9944;

    private MainActivity mMainActivity;

    private VrAPI mVrAPI = new VrAPI();
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
    private TrackingThread mTrackingThread;
    private DecoderThread mDecoderThread;
    private UdpReceiverThread mReceiverThread;

    private StatisticsCounter mCounter = new StatisticsCounter();

    private int m_RefreshRate = 60;

    // Called from native
    public interface VrFrameCallback {
        @SuppressWarnings("unused")
        long waitFrame();
    }

    public VrThread(MainActivity mainActivity) {
        this.mMainActivity = mainActivity;
    }

    public void onSurfaceCreated(final Surface surface) {
        post(new Runnable() {
            @Override
            public void run() {
                mVrAPI.onSurfaceCreated(surface);
            }
        });
    }

    public void onSurfaceChanged(final Surface surface) {
        post(new Runnable() {
            @Override
            public void run() {
                mVrAPI.onSurfaceChanged(surface);
            }
        });
    }

    public void onSurfaceDestroyed() {
        post(new Runnable() {
            @Override
            public void run() {
                mVrAPI.onSurfaceDestroyed();
            }
        });
    }

    public void onResume() {
        synchronized (mWaiter) {
            mResumed = true;
            mWaiter.notifyAll();
        }
        if (mReceiverThread != null) {
            mReceiverThread.interrupt();
            try {
                mReceiverThread.join();
            } catch (InterruptedException e) {
                e.printStackTrace();
            }
        }
        if (mDecoderThread != null) {
            mDecoderThread.interrupt();
            try {
                mDecoderThread.join();
            } catch (InterruptedException e) {
                e.printStackTrace();
            }
        }
        if (mTrackingThread != null) {
            mTrackingThread.interrupt();
            try {
                mTrackingThread.join();
            } catch (InterruptedException e) {
                e.printStackTrace();
            }
        }

        post(new Runnable() {
            @Override
            public void run() {
                Log.v(TAG, "VrThread.onResume: Starting worker threads.");
                mReceiverThread = new UdpReceiverThread(mCounter, mOnChangeSettingsCallback);
                mReceiverThread.setPort(PORT);
                mReceiverThread.set72Hz(mVrAPI.is72Hz());
                mDecoderThread = new DecoderThread(mReceiverThread
                        , mMainActivity.getAvcDecoder(), mCounter, mRenderCallback, mMainActivity);
                mTrackingThread = new TrackingThread();

                try {
                    mDecoderThread.start();
                    if (!mReceiverThread.start()) {
                        Log.e(TAG, "FATAL: Initialization of ReceiverThread failed.");
                        return;
                    }
                    // TrackingThread relies on ReceiverThread.
                    mTrackingThread.start();
                } catch (IllegalArgumentException | IllegalStateException | SecurityException e) {
                    e.printStackTrace();
                }

                Log.v(TAG, "VrThread.onResume: Worker threads has started.");

                mVrAPI.onResume();
            }
        });
    }

    public void onPause() {
        Log.v(TAG, "VrThread.onPause: Stopping worker threads.");
        synchronized (mWaiter) {
            mResumed = false;
            mWaiter.notifyAll();
        }
        // DecoderThread must be stopped before ReceiverThread
        if (mDecoderThread != null) {
            mDecoderThread.interrupt();
            try {
                mDecoderThread.join();
            } catch (InterruptedException e) {
                e.printStackTrace();
            }
        }
        if (mReceiverThread != null) {
            mReceiverThread.interrupt();
            try {
                mReceiverThread.join();
            } catch (InterruptedException e) {
                e.printStackTrace();
            }
        }
        if (mTrackingThread != null) {
            mTrackingThread.interrupt();
            try {
                mTrackingThread.join();
            } catch (InterruptedException e) {
                e.printStackTrace();
            }
        }

        Log.v(TAG, "VrThread.onPause: All worker threads has stopped.");
        post(new Runnable() {
            @Override
            public void run() {
                mVrAPI.onPause();
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

        synchronized (this) {
            mQueue = new ThreadQueue();
            notifyAll();
        }

        mVrAPI.initialize(mMainActivity);

        if(mVrAPI.is72Hz()) {
            m_RefreshRate = 72;
        }else{
            m_RefreshRate = 60;
        }

        mSurfaceTexture = new SurfaceTexture(mVrAPI.getSurfaceTextureID());
        mSurfaceTexture.setOnFrameAvailableListener(new SurfaceTexture.OnFrameAvailableListener() {
            @Override
            public void onFrameAvailable(SurfaceTexture surfaceTexture) {
                Log.v(TAG, "onFrameAvailable " + mFrameIndex);

                synchronized (mWaiter) {
                    mRenderRequested = false;
                    mRendered = true;
                    mWaiter.notifyAll();
                }
            }
        });
        mSurface = new Surface(mSurfaceTexture);

        mLoadingTexture.initializeMessageCanvas(mVrAPI.createLoadingTexture());
        mLoadingTexture.drawMessage(mMainActivity.getVersionName() + "\nLoading...");

        Log.v(TAG, "Start loop of VrThread.");
        while(mQueue.waitIdle()) {
            if(!mVrAPI.isVrMode() || !mResumed) {
                mQueue.waitNext();
                continue;
            }
            render();
        }

        Log.v(TAG, "Destroying vrapi state.");
        mVrAPI.destroy();
    }

    private void render(){
        if (mReceiverThread.isConnected()) {
            Log.v(TAG, "Render received texture.");
            mVrAPI.render(new VrFrameCallback() {
                @Override
                public long waitFrame() {
                    long startTime = System.nanoTime();
                    synchronized (mWaiter) {
                        if (mRendered) {
                            Log.v(TAG, "updateTexImage(discard)");
                            mSurfaceTexture.updateTexImage();
                        }
                        Log.v(TAG, "waitFrame Enter");
                        mRenderRequested = true;
                        mRendered = false;
                    }
                    while (true) {
                        synchronized (mWaiter) {
                            if (!mResumed) {
                                return -1;
                            }
                            if (mRendered) {
                                Log.v(TAG, "waited:" + mFrameIndex);
                                mSurfaceTexture.updateTexImage();
                                break;
                            }
                            if(System.nanoTime() - startTime > 1000 * 1000 * 1000L) {
                                // Idle for 1-sec.
                                return -1;
                            }
                            try {
                                Log.v(TAG, "waiting");
                                mWaiter.wait(100);
                            } catch (InterruptedException e) {
                                e.printStackTrace();
                            }
                        }
                    }

                    return mFrameIndex;
                }
            });
        } else {
            mLoadingTexture.drawMessage(mMainActivity.getVersionName() + "\n \nPress CONNECT button\non ALVR server.");
            mVrAPI.renderLoading();
            try {
                Thread.sleep(100);
            } catch (InterruptedException e) {
                e.printStackTrace();
            }
        }
    }

    private UdpReceiverThread.OnChangeSettingsCallback mOnChangeSettingsCallback = new UdpReceiverThread.OnChangeSettingsCallback() {
        @Override
        public void onChangeSettings(int enableTestMode, int suspend) {
            mVrAPI.onChangeSettings(enableTestMode, suspend);
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
                if (!mRenderRequested) {
                    return queuedOutputBuffer;
                }
            }

            if (queuedOutputBuffer == -1) {
                return queuedOutputBuffer;
            }

            Log.v(TAG, "releaseOutputBuffer " + frameIndex);
            codec.releaseOutputBuffer(queuedOutputBuffer, true);
            synchronized (mWaiter) {
                //mRendered = true;
                mFrameIndex = frameIndex;
                //waiter.notifyAll();
            }
            return -1;
        }
    };

    // Called from native
    interface OnSendTrackingCallback {
        @SuppressWarnings("unused")
        void onSendTracking(byte[] buf, int len, long frameIndex);
    }

    class TrackingThread extends Thread {
        private static final String TAG = "TrackingThread";
        boolean mStopped = false;

        public void interrupt() {
            Log.v(TAG, "Stopping TrackingThread.");
            mStopped = true;
        }

        @Override
        public void run() {
            long previousFetchTime = System.nanoTime();
            while (!mStopped) {
                if (mVrAPI.isVrMode() && mReceiverThread.isConnected()) {
                    mVrAPI.fetchTrackingInfo(mOnSendTrackingCallback);
                }
                try {
                    previousFetchTime += 1000 * 1000 * 1000 / m_RefreshRate;
                    long next = previousFetchTime - System.nanoTime();
                    if(next < 0) {
                        // Exceed time!
                        previousFetchTime = System.nanoTime();
                    }else {
                        TimeUnit.NANOSECONDS.sleep(next);
                    }
                } catch (InterruptedException e) {
                    e.printStackTrace();
                }
            }
            Log.v(TAG, "TrackingThread has stopped.");
        }
    }

    private OnSendTrackingCallback mOnSendTrackingCallback = new OnSendTrackingCallback() {
        @Override
        public void onSendTracking(byte[] buf, int len, long frameIndex) {
            mReceiverThread.send(buf, len);
        }
    };
}

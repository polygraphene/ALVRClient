package com.polygraphene.alvr;

import android.graphics.SurfaceTexture;
import android.media.MediaCodec;
import android.os.Handler;
import android.os.Looper;
import android.os.MessageQueue;
import android.util.Log;
import android.view.Surface;

class VrThread extends Thread {
    private static final String TAG = "VrThread";

    private MainActivity mainActivity;

    private VrAPI vrAPI = new VrAPI();
    private Looper looper = null;
    private Handler handler = null;
    private boolean mIdleHandlerAdded = false;
    private boolean mResumed = false;

    private SurfaceTexture surfaceTexture;
    private Surface surface;

    private boolean rendered = false;
    private boolean renderRequested = false;
    private long frameIndex = 0;
    private final Object waiter = new Object();

    // Worker threads
    private TrackingThread mTrackingThread;
    private DecoderThread mDecoderThread;
    private UdpReceiverThread mReceiverThread;

    private StatisticsCounter mCounter = new StatisticsCounter();

    public interface VrFrameCallback {
        @SuppressWarnings("unused")
        long waitFrame();
    }

    public VrThread(MainActivity mainActivity) {
        this.mainActivity = mainActivity;
    }

    public void onSurfaceCreated(final Surface surface) {
        post(new Runnable() {
            @Override
            public void run() {
                vrAPI.onSurfaceCreated(surface);
                onChangeVrMode();
            }
        });
    }

    public void onSurfaceChanged(final Surface surface) {
        post(new Runnable() {
            @Override
            public void run() {
                vrAPI.onSurfaceChanged(surface);
                onChangeVrMode();
            }
        });
    }

    public void onSurfaceDestroyed() {
        post(new Runnable() {
            @Override
            public void run() {
                vrAPI.onSurfaceDestroyed();
                onChangeVrMode();
            }
        });
    }

    public void onResume() {
        synchronized (waiter) {
            mResumed = true;
            waiter.notifyAll();
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

        Log.v(TAG, "VrThread.onResume: Starting worker threads.");
        mReceiverThread = new UdpReceiverThread(mCounter, mOnChangeSettingsCallback);
        mReceiverThread.setHost("10.1.0.2", 9944);
        mDecoderThread = new DecoderThread(mReceiverThread
                , mainActivity.getAvcCodecInfoes().get(0), mCounter, mRenderCallback);
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

        post(new Runnable() {
            @Override
            public void run() {
                vrAPI.onResume();
                onChangeVrMode();
            }
        });
    }

    public void onPause() {
        Log.v(TAG, "VrThread.onPause: Stopping worker threads.");
        synchronized (waiter) {
            mResumed = false;
            waiter.notifyAll();
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
                vrAPI.onPause();
                onChangeVrMode();
            }
        });
    }

    @Override
    public void interrupt() {
        post(new Runnable() {
            @Override
            public void run() {
                looper.quit();
            }
        });
    }

    private void post(Runnable runnable) {
        waitLooperPrepared();
        handler.post(runnable);
    }

    private void waitLooperPrepared() {
        synchronized (this) {
            while (looper == null) {
                try {
                    wait();
                } catch (InterruptedException e) {
                    e.printStackTrace();
                }
            }
        }
    }

    private void onChangeVrMode() {
        if (vrAPI.isVrMode() && !mIdleHandlerAdded) {
            Log.v(TAG, "Add idle handler.");
            looper.getQueue().addIdleHandler(idleHandler);
            mIdleHandlerAdded = true;
        } else if (!vrAPI.isVrMode() && mIdleHandlerAdded) {
            Log.v(TAG, "Remove idle handler.");
            looper.getQueue().removeIdleHandler(idleHandler);
            mIdleHandlerAdded = false;
        }
    }

    @Override
    public void run() {
        setName("VrThread");

        Looper.prepare();

        synchronized (this) {
            looper = Looper.myLooper();
            handler = new Handler();
            notifyAll();
        }

        vrAPI.initialize(mainActivity);

        surfaceTexture = new SurfaceTexture(vrAPI.getSurfaceTextureID());
        surfaceTexture.setOnFrameAvailableListener(new SurfaceTexture.OnFrameAvailableListener() {
            @Override
            public void onFrameAvailable(SurfaceTexture surfaceTexture) {
                Log.v(TAG, "onFrameAvailable " + frameIndex);

                synchronized (waiter) {
                    renderRequested = false;
                    rendered = true;
                    waiter.notifyAll();
                }
            }
        });
        surface = new Surface(surfaceTexture);

        Log.v(TAG, "Start loop of VrThread.");
        Looper.loop();

        Log.v(TAG, "Destroying vrapi state.");
        vrAPI.destroy();
    }

    MessageQueue.IdleHandler idleHandler = new MessageQueue.IdleHandler() {
        @Override
        public boolean queueIdle() {
            vrAPI.render(new VrFrameCallback() {
                @Override
                public long waitFrame() {
                    synchronized (waiter) {
                        if (rendered) {
                            Log.v(TAG, "updateTexImage(discard)");
                            surfaceTexture.updateTexImage();
                        }
                        Log.v(TAG, "waitFrame Enter");
                        renderRequested = true;
                        rendered = false;
                    }
                    while (true) {
                        synchronized (waiter) {
                            if (!mResumed) {
                                return -1;
                            }
                            if (rendered) {
                                Log.v(TAG, "waited:" + frameIndex);
                                surfaceTexture.updateTexImage();
                                break;
                            }
                            try {
                                Log.v(TAG, "waiting");
                                waiter.wait();
                            } catch (InterruptedException e) {
                                e.printStackTrace();
                            }
                        }
                    }

                    return frameIndex;
                }
            });
            return true;
        }
    };

    private UdpReceiverThread.OnChangeSettingsCallback mOnChangeSettingsCallback = new UdpReceiverThread.OnChangeSettingsCallback() {
        @Override
        public void onChangeSettings(int enableTestMode, int suspend) {
            vrAPI.onChangeSettings(enableTestMode, suspend);
        }
    };

    private DecoderThread.RenderCallback mRenderCallback = new DecoderThread.RenderCallback() {
        @Override
        public Surface getSurface() {
            return surface;
        }

        @Override
        public int renderIf(MediaCodec codec, int queuedOutputBuffer, long frameIndex) {
            //Log.v(TAG, "renderIf " + queuedOutputBuffer);
            synchronized (waiter) {
                if (!renderRequested) {
                    return queuedOutputBuffer;
                }
            }

            if (queuedOutputBuffer == -1) {
                return queuedOutputBuffer;
            }

            Log.v(TAG, "releaseOutputBuffer " + frameIndex);
            codec.releaseOutputBuffer(queuedOutputBuffer, true);
            synchronized (waiter) {
                rendered = true;
                VrThread.this.frameIndex = frameIndex;
                waiter.notifyAll();
            }
            return -1;
        }
    };

    interface OnSendTrackingCallback {
        void onSendTracking(byte[] buf, int len, long frame);
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
            while (!mStopped) {
                if (vrAPI.isVrMode()) {
                    vrAPI.fetchTrackingInfo(mOnSendTrackingCallback);
                }
                try {
                    Thread.sleep(16);
                } catch (InterruptedException e) {
                    e.printStackTrace();
                }
            }
            Log.v(TAG, "TrackingThread has stopped.");
        }
    }

    private OnSendTrackingCallback mOnSendTrackingCallback = new OnSendTrackingCallback() {
        @Override
        public void onSendTracking(byte[] buf, int len, long frame) {
            Log.v(TAG, "sending " + len + " fr:" + frame);
            mReceiverThread.send(buf, len);
        }
    };

    public Surface getSurface() {
        return surface;
    }
}

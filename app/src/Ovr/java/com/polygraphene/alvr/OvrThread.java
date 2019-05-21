package com.polygraphene.alvr;

import android.app.Activity;
import android.graphics.SurfaceTexture;
import android.opengl.EGL14;
import android.opengl.EGLContext;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.Looper;
import android.util.Log;
import android.view.Surface;

import java.util.concurrent.TimeUnit;

class OvrThread {
    private static final String TAG = "OvrThread";

    private Activity mActivity;

    private OvrContext mOvrContext = new OvrContext();
    private Handler mHandler;
    private HandlerThread mHandlerThread;

    private SurfaceTexture mSurfaceTexture;
    private Surface mSurface;

    private LoadingTexture mLoadingTexture = new LoadingTexture();

    // Worker threads
    private DecoderThread mDecoderThread;
    private UdpReceiverThread mReceiverThread;

    private EGLContext mEGLContext;

    private boolean mVrMode = false;
    private boolean mDecoderPrepared = false;
    private int mRefreshRate = 60;

    private long mPreviousRender = 0;

    public OvrThread(Activity activity) {
        this.mActivity = activity;

        mHandlerThread = new HandlerThread("OvrThread");
        mHandlerThread.start();
        mHandler = new Handler(mHandlerThread.getLooper());
        mHandler.post(new Runnable() {
            @Override
            public void run() {
                startup();
            }
        });
    }

    public void onSurfaceCreated(final Surface surface) {
        Log.v(TAG, "OvrThread.onSurfaceCreated.");
        mHandler.post(new Runnable() {
            @Override
            public void run() {
                mOvrContext.onSurfaceCreated(surface);
            }
        });
    }

    public void onSurfaceChanged(final Surface surface) {
        Log.v(TAG, "OvrThread.onSurfaceChanged.");
        mHandler.post(new Runnable() {
            @Override
            public void run() {
                mOvrContext.onSurfaceChanged(surface);
            }
        });
    }

    public void onSurfaceDestroyed() {
        Log.v(TAG, "OvrThread.onSurfaceDestroyed.");
        mHandler.post(new Runnable() {
            @Override
            public void run() {
                mOvrContext.onSurfaceDestroyed();
            }
        });
    }

    public void onResume() {
        Log.v(TAG, "OvrThread.onResume: Starting worker threads.");
        // Sometimes previous decoder output remains not updated (when previous call of waitFrame() didn't call updateTexImage())
        // and onFrameAvailable won't be called after next output.
        // To avoid deadlock caused by it, we need to flush last output.
        mHandler.post(new Runnable() {
            @Override
            public void run() {
                mReceiverThread = new UdpReceiverThread(mUdpReceiverCallback);

                ConnectionStateHolder.ConnectionState connectionState = new ConnectionStateHolder.ConnectionState();
                ConnectionStateHolder.loadConnectionState(mActivity, connectionState);

                if (connectionState.serverAddr != null && connectionState.serverPort != 0) {
                    Log.v(TAG, "load connection state: " + connectionState.serverAddr + " " + connectionState.serverPort);
                    mReceiverThread.recoverConnectionState(connectionState.serverAddr, connectionState.serverPort);
                }

                // Sometimes previous decoder output remains not updated (when previous call of waitFrame() didn't call updateTexImage())
                // and onFrameAvailable won't be called after next output.
                // To avoid deadlock caused by it, we need to flush last output.
                mSurfaceTexture.updateTexImage();

                mDecoderThread = new DecoderThread(mSurface, mActivity, mDecoderCallback);

                try {
                    mDecoderThread.start();

                    DeviceDescriptor deviceDescriptor = new DeviceDescriptor();
                    mOvrContext.getDeviceDescriptor(deviceDescriptor);
                    mRefreshRate = deviceDescriptor.mRefreshRates[0];
                    if (!mReceiverThread.start(mEGLContext, mActivity, deviceDescriptor, mOvrContext.getCameraTexture(), mDecoderThread)) {
                        Log.e(TAG, "FATAL: Initialization of ReceiverThread failed.");
                        return;
                    }
                } catch (IllegalArgumentException | IllegalStateException | SecurityException e) {
                    e.printStackTrace();
                }

                Log.v(TAG, "OvrThread.onResume: mVrContext.onResume().");
                mOvrContext.onResume();
            }
        });
        mHandler.post(mRenderRunnable);
        Log.v(TAG, "OvrThread.onResume: Worker threads has started.");
    }

    public void onPause() {
        Log.v(TAG, "OvrThread.onPause: Stopping worker threads.");
        // DecoderThread must be stopped before ReceiverThread and setting mResumed=false.
        mHandler.post(new Runnable() {
            @Override
            public void run() {
                // DecoderThread must be stopped before ReceiverThread and setting mResumed=false.
                if (mDecoderThread != null) {
                    Log.v(TAG, "OvrThread.onPause: Stopping DecoderThread.");
                    mDecoderThread.stopAndWait();
                }
                if (mReceiverThread != null) {
                    Log.v(TAG, "OvrThread.onPause: Stopping ReceiverThread.");
                    mReceiverThread.stopAndWait();
                }

                mOvrContext.onPause();
            }
        });
        Log.v(TAG, "OvrThread.onPause: All worker threads has stopped.");
    }

    private Runnable mRenderRunnable = new Runnable() {
        @Override
        public void run() {
            render();
        }
    };
    private Runnable mIdleRenderRunnable = new Runnable() {
        @Override
        public void run() {
            render();
        }
    };

    // Called from onDestroy
    public void quit() {
        mHandler.post(new Runnable() {
            @Override
            public void run() {
                mLoadingTexture.destroyTexture();
                Log.v(TAG, "Destroying vrapi state.");
                mOvrContext.destroy();
            }
        });
        mHandlerThread.quitSafely();
    }

    public void startup() {
        Log.v(TAG, "OvrThread started.");

        mOvrContext.initialize(mActivity, mActivity.getAssets(), this, Constants.IS_ARCORE_BUILD, 60);

        mSurfaceTexture = new SurfaceTexture(mOvrContext.getSurfaceTextureID());
        mSurfaceTexture.setOnFrameAvailableListener(new SurfaceTexture.OnFrameAvailableListener() {
            @Override
            public void onFrameAvailable(SurfaceTexture surfaceTexture) {
                Utils.log("OvrThread: waitFrame: onFrameAvailable is called.");
                mDecoderThread.onFrameAvailable();
                mHandler.removeCallbacks(mIdleRenderRunnable);
                mHandler.post(mRenderRunnable);
            }
        }, new Handler(Looper.getMainLooper()));
        mSurface = new Surface(mSurfaceTexture);

        mLoadingTexture.initializeMessageCanvas(mOvrContext.getLoadingTexture());
        mLoadingTexture.drawMessage(Utils.getVersionName(mActivity) + "\nLoading...");

        mEGLContext = EGL14.eglGetCurrentContext();
    }

    private void render() {
        if (mReceiverThread.isConnected() && mReceiverThread.getErrorMessage() == null) {
            if (mDecoderThread.discartStaleFrames(mSurfaceTexture)) {
                Utils.log(TAG, "Discard stale frame. Wait next onFrameAvailable.");
                mHandler.removeCallbacks(mIdleRenderRunnable);
                mHandler.postDelayed(mIdleRenderRunnable, 50);
                return;
            }
            long next = checkRenderTiming();
            if(next > 0) {
                mHandler.postDelayed(mRenderRunnable, next);
                return;
            }
            long renderedFrameIndex = mDecoderThread.clearAvailable(mSurfaceTexture);
            if (renderedFrameIndex != -1) {
                mOvrContext.render(renderedFrameIndex);
                mPreviousRender = System.nanoTime();

                mHandler.postDelayed(mRenderRunnable, 5);
            } else {
                mHandler.removeCallbacks(mIdleRenderRunnable);
                mHandler.postDelayed(mIdleRenderRunnable, 50);
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
            mHandler.removeCallbacks(mIdleRenderRunnable);
            mHandler.postDelayed(mIdleRenderRunnable, 100);
        }
    }

    private long checkRenderTiming() {
        long current = System.nanoTime();
        long threashold = TimeUnit.SECONDS.toNanos(1) / mRefreshRate -
                TimeUnit.MILLISECONDS.toNanos(5);
        return TimeUnit.NANOSECONDS.toMillis(threashold - (current - mPreviousRender));
    }

    // Called on OvrThread.
    public void onVrModeChanged(boolean enter) {
        mVrMode = enter;
        Log.i(TAG, "onVrModeChanged. mVrMode=" + mVrMode + " mDecoderPrepared=" + mDecoderPrepared);
        mReceiverThread.setSinkPrepared(mVrMode && mDecoderPrepared);
    }

    private UdpReceiverThread.Callback mUdpReceiverCallback = new UdpReceiverThread.Callback() {
        @Override
        public void onConnected(final int width, final int height, final int codec, final int frameQueueSize, final int refreshRate) {
            // We must wait completion of notifyGeometryChange
            // to ensure the first video frame arrives after notifyGeometryChange.
            mHandler.post(new Runnable() {
                @Override
                public void run() {
                    mOvrContext.setRefreshRate(refreshRate);
                    mOvrContext.setFrameGeometry(width, height);
                    mDecoderThread.onConnect(codec, frameQueueSize);
                }
            });
        }

        @Override
        public void onChangeSettings(int suspend, int frameQueueSize) {
            mOvrContext.onChangeSettings(suspend);
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
            if (mOvrContext.isVrMode()) {
                mOvrContext.fetchTrackingInfo(mReceiverThread, position, orientation);
            }
        }
    };

    private DecoderThread.DecoderCallback mDecoderCallback = new DecoderThread.DecoderCallback() {
        @Override
        public void onPrepared() {
            mDecoderPrepared = true;
            Log.i(TAG, "DecoderCallback.onPrepared. mVrMode=" + mVrMode + " mDecoderPrepared=" + mDecoderPrepared);
            mReceiverThread.setSinkPrepared(mVrMode && mDecoderPrepared);
        }

        @Override
        public void onDestroy() {
            mDecoderPrepared = false;
            Log.i(TAG, "DecoderCallback.onDestroy. mVrMode=" + mVrMode + " mDecoderPrepared=" + mDecoderPrepared);
            mReceiverThread.setSinkPrepared(mVrMode && mDecoderPrepared);
        }

        @Override
        public void onFrameDecoded() {
            mDecoderThread.releaseBuffer();
        }
    };
}

package com.polygraphene.alvr;

import android.app.Activity;
import android.opengl.EGLContext;
import android.util.Log;

import java.util.concurrent.TimeUnit;

class TrackingThread extends ThreadBase {
    private static final String TAG = "TrackingThread";
    private int mRefreshRate = 200;

    interface TrackingCallback {
        void onTracking(float[] position, float[] orientation);
    }

    private TrackingCallback mCallback;
    private ArThread mArThread;

    public TrackingThread() {
    }

    public void setCallback(TrackingCallback callback) {
        mCallback = callback;
    }

    void changeRefreshRate(int refreshRate) {
        //mRefreshRate = refreshRate;
    }

    public void start(EGLContext mEGLContext, Activity activity, int cameraTexture) {
        mArThread = new ArThread(mEGLContext);
        mArThread.initialize(activity);
        mArThread.setCameraTexture(cameraTexture);

        super.startBase();
        mArThread.start();
    }

    public void onConnect() {
        mArThread.onConnect();
    }

    public void onDisconnect() {
        mArThread.onDisconnect();
    }

    @Override
    public void stopAndWait() {
        mArThread.stopAndWait();
        super.stopAndWait();
    }

    @Override
    public void run() {
        long previousFetchTime = System.nanoTime();
        while (!isStopped()) {
            mCallback.onTracking(mArThread.getPosition(), mArThread.getOrientation());
            try {
                previousFetchTime += 1000 * 1000 * 1000 / mRefreshRate;
                long next = previousFetchTime - System.nanoTime();
                if (next < 0) {
                    // Exceed time!
                    previousFetchTime = System.nanoTime();
                } else {
                    TimeUnit.NANOSECONDS.sleep(next);
                }
            } catch (InterruptedException e) {
            }
        }
        Log.v(TAG, "TrackingThread has stopped.");
    }

    public boolean onRequestPermissionsResult(BaseActivity activity) {
        return mArThread.onRequestPermissionsResult(activity);
    }

    public String getErrorMessage() {
        return mArThread.getErrorMessage();
    }
}

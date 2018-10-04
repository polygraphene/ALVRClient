package com.polygraphene.alvr;

import android.opengl.EGLContext;
import android.util.Log;

import java.util.concurrent.TimeUnit;

class TrackingThread extends ThreadBase {
    private static final String TAG = "TrackingThread";
    private int mRefreshRate = 60;

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
        mRefreshRate = refreshRate;
    }

    public void start(EGLContext mEGLContext, MainActivity mainActivity, int cameraTexture) {
        mArThread = new ArThread(mEGLContext);
        mArThread.initialize(mainActivity);
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
                e.printStackTrace();
            }
        }
        Log.v(TAG, "TrackingThread has stopped.");
    }

    public boolean onRequestPermissionsResult(MainActivity activity) {
        return mArThread.onRequestPermissionsResult(activity);
    }

    public String getErrorMessage() {
        return mArThread.getErrorMessage();
    }
}

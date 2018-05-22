package com.polygraphene.remoteglass;

import android.util.Log;

class UdpReceiverThread {
    private static final String TAG = "SrtReceiverThread";

    static {
        System.loadLibrary("srt");
        System.loadLibrary("native-lib");
    }

    Thread mThread;
    StatisticsCounter mCounter;
    MainActivity mMainActivity;
    String mHost;
    int mPort;
    boolean mInitialized = false;
    boolean mInitializeFailed = false;

    UdpReceiverThread(StatisticsCounter counter, MainActivity activity) {
        mCounter = counter;
        mMainActivity = activity;
    }

    public void setHost(String host, int port) {
        mHost = host;
        mPort = port;
    }

    public boolean isStopped() {
        return mMainActivity.isStopped();
    }

    public String getDeviceName() {
        String manufacturer = android.os.Build.MANUFACTURER;
        String model = android.os.Build.MODEL;
        if (model.toLowerCase().startsWith(manufacturer.toLowerCase())) {
            return model;
        } else {
            return manufacturer + " " + model;
        }
    }

    public boolean start() {
        mThread = new Thread() {
            @Override
            public void run() {
                runThread();
            }
        };
        mThread.start();

        synchronized (this) {
            while (!mInitialized && !mInitializeFailed) {
                try {
                    wait();
                } catch (InterruptedException e) {
                    e.printStackTrace();
                }
            }
        }

        return !mInitializeFailed;
    }

    private void runThread() {
        mThread.setName(SrtReceiverThread.class.getName());

        try {
            int ret = initializeSocket(mHost, mPort, getDeviceName());
            if (ret != 0) {
                Log.e(TAG, "Error on initialize srt socket. Code=" + ret + ".");
                synchronized (this) {
                    mInitializeFailed = true;
                    notifyAll();
                }
                return;
            }
            synchronized (this) {
                mInitialized = true;
                notifyAll();
            }

            runLoop();
        } finally {
            closeSocket();
        }


        Log.v(TAG, "SrtReceiverThread stopped.");
    }

    public void interrupt() {
        mThread.interrupt();
    }

    public void join() throws InterruptedException {
        mThread.join();
    }

    // called from native
    public void onChangeSettings(int EnableTestMode, int suspend) {
        mMainActivity.onChangeSettings(EnableTestMode, suspend);
    }

    native int initializeSocket(String host, int port, String deviceName);

    native void closeSocket();

    native void runLoop();

    native int send(byte[] buf, int length);

    native int getNalListSize();

    native NAL waitNal();

    native NAL getNal();

    native NAL peekNal();

    native void flushNALList();

}
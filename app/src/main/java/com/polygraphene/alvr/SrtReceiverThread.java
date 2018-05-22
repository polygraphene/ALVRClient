package com.polygraphene.alvr;

import android.util.Log;

class SrtReceiverThread extends Thread {
    private static final String TAG = "SrtReceiverThread";

    static {
        System.loadLibrary("srt");
        System.loadLibrary("native-lib");
    }

    MainActivity mMainActivity;
    String mHost;
    int mPort;

    SrtReceiverThread(StatisticsCounter counter, MainActivity activity) {
        mMainActivity = activity;
    }

    public boolean isStopped() {
        return mMainActivity.isStopped();
    }

    public void setHost(String host, int port) {
        mHost = host;
        mPort = port;
    }

    @Override
    public void run() {
        setName(SrtReceiverThread.class.getName());

        try {
            int ret = initializeSocket(mHost, mPort);
            if (ret != 0) {
                Log.e(TAG, "Error on initialize srt socket. Code=" + ret + ".");
                return;
            }

            runLoop();
        } finally {
            closeSocket();
        }


        Log.v(TAG, "SrtReceiverThread stopped.");
    }

    native int initializeSocket(String host, int port);

    native void closeSocket();

    native void runLoop();

    native int send(byte[] buf, int length);

    native int getNalListSize();

    native NAL getNal();

    native void flushNALList();
}
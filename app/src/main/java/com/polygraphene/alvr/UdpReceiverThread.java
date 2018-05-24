package com.polygraphene.alvr;

import android.util.Log;

class UdpReceiverThread implements NALParser {
    private static final String TAG = "UdpReceiverThread";

    static {
        System.loadLibrary("native-lib");
    }

    private static final String BROADCAST_ADDRESS = "255.255.255.255";

    private Thread mThread;
    private StatisticsCounter mCounter;
    private int mPort;
    private boolean mInitialized = false;
    private boolean mInitializeFailed = false;

    public native boolean isConnected();

    interface OnChangeSettingsCallback {
        void onChangeSettings(int enableTestMode, int suspend);
    }
    private OnChangeSettingsCallback mOnChangeSettingsCallback;

    UdpReceiverThread(StatisticsCounter counter, OnChangeSettingsCallback onChangeSettingsCallback) {
        mCounter = counter;
        mOnChangeSettingsCallback = onChangeSettingsCallback;
    }

    public void setPort(int port) {
        mPort = port;
    }

    private String getDeviceName() {
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
        mThread.setName(UdpReceiverThread.class.getName());

        try {
            int ret = initializeSocket(mPort, getDeviceName(), BROADCAST_ADDRESS);
            if (ret != 0) {
                Log.e(TAG, "Error on initializing socket. Code=" + ret + ".");
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
            Log.v(TAG, "UdpReceiverThread initialized.");

            runLoop();
        } finally {
            closeSocket();
        }

        Log.v(TAG, "UdpReceiverThread stopped.");
    }

    public void join() throws InterruptedException {
        mThread.join();
    }

    // called from native
    @SuppressWarnings("unused")
    public void onChangeSettings(int EnableTestMode, int suspend) {
        mOnChangeSettingsCallback.onChangeSettings(EnableTestMode, suspend);
    }

    native int initializeSocket(int port, String deviceName, String broardcastAddr);

    native void closeSocket();

    native void runLoop();

    native void interrupt();

    native int send(byte[] buf, int length);

    //
    // NALParser interface
    //

    @Override
    public native int getNalListSize();
    @Override
    public native NAL waitNal();
    @Override
    public native NAL getNal();
    @Override
    public native NAL peekNal();
    @Override
    public native void flushNALList();
    @Override
    public native void notifyWaitingThread();
}
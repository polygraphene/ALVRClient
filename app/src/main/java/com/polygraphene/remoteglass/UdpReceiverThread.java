package com.polygraphene.remoteglass;

import android.util.Log;

class UdpReceiverThread extends MainActivity.ReceiverThread {
    private static final String TAG = "SrtReceiverThread";

    static {
        System.loadLibrary("srt");
        System.loadLibrary("native-lib");
    }

    UdpReceiverThread(NALParser nalParser, StatisticsCounter counter, MainActivity activity) {
        super(nalParser, counter, activity);
    }

    public boolean isStopped(){
        return mainActivity.isStopped();
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


    @Override
    public void run() {
        setName(SrtReceiverThread.class.getName());

        try {
            int ret = initializeSocket(host, port, getDeviceName());
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

    // called from native
    public void onChangeSettings(int EnableTestMode) {
        mainActivity.onChangeSettings(EnableTestMode);
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
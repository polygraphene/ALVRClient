package com.polygraphene.remoteglass;

import android.util.Log;

import java.nio.ByteBuffer;

class SrtReceiverThread extends MainActivity.ReceiverThread {
    private static final String TAG = "SrtReceiverThread";

    static {
        System.loadLibrary("srt");
        System.loadLibrary("native-lib");
    }

    SrtReceiverThread(NALParser nalParser, StatisticsCounter counter, MainActivity activity) {
        super(nalParser, counter, activity);
    }

    class Packet {
        public long sequence;
        byte[] buf;
        ByteBuffer byte_buf;
    }

    @Override
    public void run() {
        byte[] socket = new byte[8];
        try {
            int ret = initializeSocket(host, port, socket);
            if (ret != 0) {
                Log.e(TAG, "Error on initialize srt socket. Code=" + ret + ".");
                return;
            }

            Packet[] packets = new Packet[3000];
            for (int i = 0; i < 3000; i++) {
                packets[i] = new Packet();
                packets[i].buf = new byte[2000];
                packets[i].byte_buf = ByteBuffer.wrap(packets[i].buf);
            }
            for (int i = 0; ; i++) {
                if (mainActivity.isStopped()) {
                    break;
                }

                Packet packet = packets[i % 3000];

                ret = recv(socket, packet);
                if (ret == -1) {
                    break;
                }
                counter.countPacket(ret);

            }
        } finally {
            closeSocket(socket);
        }


        Log.v(TAG, "SrtReceiverThread stopped.");
    }

    native int initializeSocket(String host, int port, byte[] socket);

    native void closeSocket(byte[] socket);

    native int recv(byte[] socket, Packet packet);

    native int getNalListSize();

    native NAL getNal();

    native void flushNALList();
}
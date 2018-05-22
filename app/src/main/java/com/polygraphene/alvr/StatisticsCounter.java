package com.polygraphene.alvr;

import android.util.Log;

public class StatisticsCounter {
    private static final String TAG = "StatisticsCounter";

    long prev = 0;
    long counter = 0;
    long sizeCounter = 0;
    long frameCounter = 0;
    long PFrameCounter = 0;
    long StallCounter = 0;

    synchronized private void resetCounterIf() {
        long current = System.currentTimeMillis() / 1000;
        if (prev != 0 && prev != current) {
            Log.v(TAG, counter + " Packets/s " + ((float) sizeCounter) / 1000 * 8 + " kb/s " + frameCounter + " fps" + " fed PFrames " + PFrameCounter + " " + StallCounter + " stalled");
            counter = 0;
            sizeCounter = 0;
            frameCounter = 0;
            PFrameCounter = 0;
            StallCounter = 0;
        }
        prev = current;
    }

    void countPacket(int size) {
        resetCounterIf();
        if (size != 0) counter++;
        sizeCounter += size;
    }

    void countOutputFrame(int frame) {
        resetCounterIf();
        frameCounter += frame;
    }

    void countPFrame() {
        resetCounterIf();
        PFrameCounter++;
    }

    void countStall() {
        resetCounterIf();
        StallCounter++;
    }

}

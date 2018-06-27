package com.polygraphene.alvr;

public interface NALParser {
    int getNalListSize();

    // Wait next NAL and return.
    // If notifyWaitingThread was called, interrupt call and return null.
    NAL waitNal();
    NAL getNal();
    void recycleNal(NAL nal);
    void flushNALList();
    void notifyWaitingThread();
    void clearStopped();
}

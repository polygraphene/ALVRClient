package com.polygraphene.alvr;

public class LatencyCollector {
    public static native void DecoderInput(long frameIndex);
    public static native void DecoderOutput(long frameIndex);
}

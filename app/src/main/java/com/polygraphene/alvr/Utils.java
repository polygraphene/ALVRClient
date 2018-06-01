package com.polygraphene.alvr;

import android.util.Log;

public class Utils {
    public static void frameLog(long frameIndex, String s) {
        Log.v("FrameTracking", "[Frame " + frameIndex + "] " + s);
    }
}

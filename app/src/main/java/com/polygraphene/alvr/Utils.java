package com.polygraphene.alvr;

import android.content.Context;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.util.Log;

public class Utils {
    public static boolean sEnableLog = false;

    public static native void setFrameLogEnabled(long debugFlags);

    public static void frameLog(long frameIndex, String s) {
        if(sEnableLog) {
            Log.v("FrameTracking", "[Frame " + frameIndex + "] " + s);
        }
    }

    public static void log(String s) {
        if(sEnableLog) {
            Log.v("FrameTracking", s);
        }
    }

    public static void log(String tag, String s) {
        if(sEnableLog) {
            Log.v(tag, s);
        }
    }

    public static void logi(String tag, String s) {
        Log.i(tag, s);
    }

    public static void loge(String tag, String s) {
        Log.e(tag, s);
    }


    public static String getVersionName(Context context){
        try {
            PackageInfo pInfo = context.getPackageManager().getPackageInfo(context.getPackageName(), 0);
            String version = pInfo.versionName;
            return context.getString(R.string.app_name) + " v" + version;
        } catch (PackageManager.NameNotFoundException e) {
            e.printStackTrace();
            return context.getString(R.string.app_name) + " Unknown version";
        }
    }

    public static void setDebugFlags(long debugFlags) {
        sEnableLog = (debugFlags & 1) != 0;
        Log.i("ALVR", "DebugFlags is changed. New=" + debugFlags);
        setFrameLogEnabled(debugFlags);
    }
}

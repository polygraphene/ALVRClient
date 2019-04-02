package com.polygraphene.alvr;

import android.content.Context;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.util.Log;

public class Utils {
    public static void frameLog(long frameIndex, String s) {
        Log.v("FrameTracking", "[Frame " + frameIndex + "] " + s);
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
}

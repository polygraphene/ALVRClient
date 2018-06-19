package com.polygraphene.alvr;

import android.opengl.EGLContext;

class ArThreadHelper {
    static ArThread newArThread(VrThread vrThread, EGLContext mEGLContext) {
        if (Constants.IS_ARCORE_BUILD) {
            return new ArThreadARCore(vrThread, mEGLContext);
        } else {
            return new ArThreadNormal(vrThread, mEGLContext);
        }
    }

    static boolean requireCameraTexture() {
        return Constants.IS_ARCORE_BUILD;
    }

    public static void requestPermissions(MainActivity activity) {
        if (Constants.IS_ARCORE_BUILD) {
            ArThreadARCore.requestPermissions(activity);
        }
    }
}

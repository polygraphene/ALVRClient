package com.polygraphene.alvr;

import android.opengl.EGLContext;

import java.lang.reflect.Constructor;
import java.lang.reflect.Method;

class ArThreadHelper {
    static ArThread newArThread(VrThread vrThread, EGLContext mEGLContext) {
        if (Constants.IS_ARCORE_BUILD) {
            try {
                Class clazz = Class.forName("com.polygraphene.alvr.ArThreadArCore");
                Constructor ctor = clazz.getConstructor(VrThread.class, EGLContext.class);
                return (ArThread) ctor.newInstance(vrThread, mEGLContext);
            } catch (Exception e) {
                e.printStackTrace();
                return null;
            }
        } else {
            return new ArThreadNormal(vrThread, mEGLContext);
        }
    }

    static boolean requireCameraTexture() {
        return Constants.IS_ARCORE_BUILD;
    }

    public static void requestPermissions(MainActivity activity) {
        if (Constants.IS_ARCORE_BUILD) {
            try {
                Class clazz = Class.forName("com.polygraphene.alvr.ArThreadArCore");
                Method method = clazz.getMethod("requestPermissions", MainActivity.class);
                method.invoke(activity);
            } catch (Exception e) {
                e.printStackTrace();
            }
        }
    }
}

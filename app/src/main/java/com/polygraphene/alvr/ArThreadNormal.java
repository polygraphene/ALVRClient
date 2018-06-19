package com.polygraphene.alvr;

import android.opengl.EGLContext;

class ArThreadNormal implements ArThread {
    ArThreadNormal(VrThread vrThread, EGLContext mEGLContext) {
    }

    @Override
    public void start(MainActivity activity) {
    }

    @Override
    public void interrupt() {
    }

    @Override
    public void join() throws InterruptedException {
    }

    @Override
    public void initialize(MainActivity activity) {
    }

    @Override
    public boolean onRequestPermissionsResult(MainActivity activity) {
        return true;
    }

    @Override
    public void setCameraTexture(int texture) {
    }

    @Override
    public float[] getOrientation() {
        return null;
    }

    @Override
    public float[] getPosition() {
        return null;
    }

    @Override
    public String getErrorMessage() {
        return null;
    }
}

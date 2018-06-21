package com.polygraphene.alvr;

import android.app.Activity;
import android.view.Surface;

public class VrContext {

    static {
        System.loadLibrary("native-lib");
    }

    private long handle;

    public void initialize(Activity activity, boolean ARMode) {
        handle = initializeNative(activity, ARMode);
    }

    public void destroy() {
        destroyNative(handle);
    }

    public void onResume() {
        onResumeNative(handle);
    }

    public void onPause() {
        onPauseNative(handle);
    }

    public void onSurfaceCreated(Surface surface) {
        onSurfaceCreatedNative(handle, surface);
    }

    public void onSurfaceChanged(Surface surface) {
        onSurfaceChangedNative(handle, surface);
    }

    public void onSurfaceDestroyed() {
        onSurfaceDestroyedNative(handle);
    }

    public void render(VrThread.VrFrameCallback callback, LatencyCollector latencyCollector) {
        renderNative(handle, callback, latencyCollector);
    }

    public void renderLoading() {
        renderLoadingNative(handle);
    }

    public long fetchTrackingInfo(VrThread.OnSendTrackingCallback callback, float[] position, float[] orientation) {
        return fetchTrackingInfoNative(handle, callback, position, orientation);
    }

    public void onChangeSettings(int EnableTestMode, int suspend) {
        onChangeSettingsNative(handle, EnableTestMode, suspend);
    }

    public boolean onKeyEvent(int keyCode, int action) {
        return onKeyEventNative(handle, keyCode, action);
    }

    public int getLoadingTexture() {
        return getLoadingTextureNative(handle);
    }

    public int getSurfaceTextureID() {
        return getSurfaceTextureIDNative(handle);
    }

    public int getCameraTexture() {
        return getCameraTextureNative(handle);
    }

    public boolean isVrMode() {
        return isVrModeNative(handle);
    }

    public boolean is72Hz() {
        return is72HzNative(handle);
    }

    public void setFrameGeometry(int width, int height) {
        setFrameGeometryNative(handle, width, height);
    }
    
    private native long initializeNative(Activity activity, boolean ARMode);
    private native void destroyNative(long handle);

    private native void onResumeNative(long handle);
    private native void onPauseNative(long handle);

    private native void onSurfaceCreatedNative(long handle, Surface surface);
    private native void onSurfaceChangedNative(long handle, Surface surface);
    private native void onSurfaceDestroyedNative(long handle);
    private native void renderNative(long handle, VrThread.VrFrameCallback callback, LatencyCollector latencyCollector);
    private native void renderLoadingNative(long handle);
    private native long fetchTrackingInfoNative(long handle, VrThread.OnSendTrackingCallback callback, float[] position, float[] orientation);

    private native void onChangeSettingsNative(long handle, int EnableTestMode, int suspend);
    private native boolean onKeyEventNative(long handle, int keyCode, int action);

    private native int getLoadingTextureNative(long handle);
    private native int getSurfaceTextureIDNative(long handle);
    public native int getCameraTextureNative(long handle);

    private native boolean isVrModeNative(long handle);
    private native boolean is72HzNative(long handle);

    private native void setFrameGeometryNative(long handle, int width, int height);
}

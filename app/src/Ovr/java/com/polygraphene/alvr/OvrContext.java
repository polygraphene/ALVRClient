package com.polygraphene.alvr;

import android.app.Activity;
import android.content.res.AssetManager;
import android.view.Surface;

public class OvrContext {

    static {
        System.loadLibrary("native-lib");
    }

    private long handle;

    public void initialize(Activity activity, AssetManager assetManager, OvrThread ovrThread, boolean ARMode, int initialRefreshRate) {
        handle = initializeNative(activity, assetManager, ovrThread, ARMode, initialRefreshRate);
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

    public void render(long renderedFrameIndex) {
        renderNative(handle, renderedFrameIndex);
    }

    public void renderLoading() {
        renderLoadingNative(handle);
    }

    public void fetchTrackingInfo(UdpReceiverThread udpReceiverThread, float[] position, float[] orientation) {
        fetchTrackingInfoNative(handle, udpReceiverThread, position, orientation);
    }

    public void onChangeSettings(int suspend) {
        onChangeSettingsNative(handle, suspend);
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

    public void getDeviceDescriptor(DeviceDescriptor deviceDescriptor) {
        getDeviceDescriptorNative(handle, deviceDescriptor);
    }

    public void setFrameGeometry(int width, int height) {
        setFrameGeometryNative(handle, width, height);
    }

    public void setRefreshRate(int refreshRate) {
        setRefreshRateNative(handle, refreshRate);
    }

    private native long initializeNative(Activity activity, AssetManager assetManager, OvrThread ovrThread, boolean ARMode, int initialRefreshRate);
    private native void destroyNative(long handle);

    private native void onResumeNative(long handle);
    private native void onPauseNative(long handle);

    private native void onSurfaceCreatedNative(long handle, Surface surface);
    private native void onSurfaceChangedNative(long handle, Surface surface);
    private native void onSurfaceDestroyedNative(long handle);
    private native void renderNative(long handle, long renderedFrameIndex);
    private native void renderLoadingNative(long handle);
    private native void fetchTrackingInfoNative(long handle, UdpReceiverThread udpReceiverThread, float[] position, float[] orientation);

    private native void onChangeSettingsNative(long handle, int suspend);

    private native int getLoadingTextureNative(long handle);
    private native int getSurfaceTextureIDNative(long handle);
    public native int getCameraTextureNative(long handle);

    private native boolean isVrModeNative(long handle);
    private native void getDeviceDescriptorNative(long handle, DeviceDescriptor deviceDescriptor);

    private native void setFrameGeometryNative(long handle, int width, int height);
    private native void setRefreshRateNative(long handle, int refreshRate);
}

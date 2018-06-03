package com.polygraphene.alvr;

import android.app.Activity;
import android.view.Surface;

public class VrAPI {

    static {
        System.loadLibrary("native-lib");
    }

    native void initialize(Activity activity);
    native void destroy();

    native int createLoadingTexture();

    native void onResume();
    native void onPause();

    native void onSurfaceCreated(Surface surface);
    native void onSurfaceChanged(Surface surface);
    native void onSurfaceDestroyed();
    native int getSurfaceTextureID();
    native void render(VrThread.VrFrameCallback callback, LatencyCollector latencyCollector);
    native void renderLoading();
    native long fetchTrackingInfo(VrThread.OnSendTrackingCallback callback, float[] position);
    native void onChangeSettings(int EnableTestMode, int suspend);
    native void onKeyEvent(int keyCode, int action);

    native boolean isVrMode();
    native boolean is72Hz();

    native void setFrameGeometry(int width, int height);
}

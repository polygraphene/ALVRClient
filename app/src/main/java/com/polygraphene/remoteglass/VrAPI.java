package com.polygraphene.remoteglass;

import android.app.Activity;
import android.view.Surface;

public class VrAPI {
    native void init();
    native void onSurfaceCreated(Surface surface, Activity activity);
    native int getSurfaceTextureID();
    native void render(MainActivity.VrFrameCallback callback);
    native void fetchTrackingInfo(MainActivity.OnSendTrackingCallback callback);
    native void onSurfaceDestroyed();
    native void onChangeSettings(int EnableTestMode, int suspend);
}

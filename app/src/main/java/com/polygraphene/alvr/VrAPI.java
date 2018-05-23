package com.polygraphene.alvr;

import android.app.Activity;
import android.view.Surface;

public class VrAPI {
    native void initialize(Activity activity);
    native void destroy();

    native void onResume();
    native void onPause();

    native void onSurfaceCreated(Surface surface);
    native void onSurfaceChanged(Surface surface);
    native void onSurfaceDestroyed();
    native int getSurfaceTextureID();
    native void render(VrThread.VrFrameCallback callback);
    native void fetchTrackingInfo(VrThread.OnSendTrackingCallback callback);
    native void onChangeSettings(int EnableTestMode, int suspend);

    native boolean isVrMode();
}

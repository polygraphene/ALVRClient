#ifndef ALVRCLIENT_VR_CONTEXT_H
#define ALVRCLIENT_VR_CONTEXT_H

#include <memory>
#include <map>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/input.h>
#include "packet_types.h"
#include "render.h"
#include "utils.h"
#include "udp.h"


class VrContext {
public:
    void initialize(JNIEnv *env, jobject activity, jobject assetManager, bool ARMode, int initialRefreshRate);
    void destroy();

    void onChangeSettings(int EnableTestMode, int Suspend);
    void onSurfaceCreated(jobject surface);
    void onSurfaceDestroyed();
    void onSurfaceChanged(jobject surface);
    void onResume();
    void onPause();
    bool onKeyEvent(int keyCode, int action);

    void render(uint64_t renderedFrameIndex);
    void renderLoading();

    void fetchTrackingInfo(JNIEnv *env_, UdpManager *udpManager,
                           ovrVector3f *position, ovrQuatf *orientation);

    void setFrameGeometry(int width, int height);

    bool isVrMode() { return Ovr != NULL; }

    int getLoadingTexture(){
        return loadingTexture;
    }
    int getSurfaceTextureID(){
        return SurfaceTextureID;
    }
    int getCameraTexture(){
        return CameraTexture;
    }

    void getRefreshRates(JNIEnv *env_, jintArray refreshRates);

    void setRefreshRate(int refreshRate, bool forceChange = true);

private:
    ANativeWindow *window = NULL;
    ovrMobile *Ovr;
    ovrJava java;
    JNIEnv *env;

    bool UseMultiview = true;
    GLuint SurfaceTextureID = 0;
    GLuint loadingTexture = 0;
    GLuint CameraTexture = 0;
    int enableTestMode = 0;
    int suspend = 0;
    bool Resumed = false;
    int FrameBufferWidth = 0;
    int FrameBufferHeight = 0;

    static const int DEFAULT_REFRESH_RATE = 60;
    int m_currentRefreshRate = DEFAULT_REFRESH_RATE;

    uint64_t FrameIndex = 0;
    uint64_t WantedFrameIndex = 0;

    // For ARCore
    bool m_ARMode = false;
    float position_offset_y = 0.0f;
    bool previousHeadsetTrackpad = false;
    float previousHeadsetY = 0.0f;
    int g_AROverlayMode = 0; // 0: VR only, 1: AR 30% 2: AR 70% 3: AR 100%

    static const int MAXIMUM_TRACKING_FRAMES = 180;

    struct TrackingFrame {
        ovrTracking2 tracking;
        uint64_t frameIndex;
        uint64_t fetchTime;
        double displayTime;
    };
    typedef std::map<uint64_t, std::shared_ptr<TrackingFrame> > TRACKING_FRAME_MAP;

    typedef enum
    {
        BACK_BUTTON_STATE_NONE,
        BACK_BUTTON_STATE_PENDING_SHORT_PRESS,
        BACK_BUTTON_STATE_SKIP_UP
    } ovrBackButtonState;

    TRACKING_FRAME_MAP trackingFrameMap;
    Mutex trackingFrameMutex;

    ovrBackButtonState BackButtonState;
    bool BackButtonDown;
    double BackButtonDownStartTime;

    ovrRenderer Renderer;

    void setControllerInfo(TrackingInfo *packet, double displayTime);

    void sendTrackingInfo(TrackingInfo *packet, double displayTime, ovrTracking2 *tracking,
                          const ovrVector3f *other_tracking_position,
                          const ovrQuatf *other_tracking_orientation);

    void setInitialRefreshRate(int initialRefreshRate);

    void enterVrMode();
    void leaveVrMode();
    void onVrModeChange();
};

#endif //ALVRCLIENT_VR_CONTEXT_H

#ifndef ALVRCLIENT_VR_CONTEXT_H
#define ALVRCLIENT_VR_CONTEXT_H

#include "utils.h"
#include "render.h"
#include "vr_context.h"
#include "latency_collector.h"
#include "packet_types.h"
#include "udp.h"
#include "asset.h"

class VrContextBase {
public:
    virtual void initialize(JNIEnv *env, jobject activity, jobject assetManager, bool ARMode,
                    int initialRefreshRate) = 0;

    virtual void destroy() = 0;

    virtual void onChangeSettings(int EnableTestMode, int Suspend) = 0;

    virtual void onSurfaceCreated(jobject surface) = 0;

    virtual void onSurfaceDestroyed() = 0;

    virtual void onSurfaceChanged(jobject surface) = 0;

    virtual void onResume() = 0;

    virtual void onPause() = 0;

    virtual bool onKeyEvent(int keyCode, int action) = 0;

    virtual void render(uint64_t renderedFrameIndex) = 0;

    virtual void renderLoading() = 0;

    virtual void fetchTrackingInfo(JNIEnv *env_, UdpManager *udpManager,
                           ovrVector3f *position, ovrQuatf *orientation) = 0;

    virtual void setFrameGeometry(int width, int height) = 0;

    virtual bool isVrMode() = 0;

    virtual int getLoadingTexture() = 0;

    virtual int getSurfaceTextureID() = 0;

    virtual int getCameraTexture() = 0;

    virtual void getRefreshRates(JNIEnv *env_, jintArray refreshRates) = 0;

    virtual void setRefreshRate(int refreshRate, bool forceChange = true) = 0;
};

#endif //ALVRCLIENT_VR_CONTEXT_H

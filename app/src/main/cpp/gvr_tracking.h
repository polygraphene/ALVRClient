#ifndef ALVRCLIENT_DAYDREAM_GVRCONTEXT_H
#define ALVRCLIENT_DAYDREAM_GVRCONTEXT_H

#include <memory>
#include <map>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/input.h>
#include "packet_types.h"
#include "render.h"
#include "utils.h"
#include "udp.h"
#include "ovr_context.h"

#include "vr/gvr/capi/include/gvr.h"
#include "vr/gvr/capi/include/gvr_controller.h"
#include "vr/gvr/capi/include/gvr_types.h"

class GvrTracking {
public:

    void initialize(jlong nativeGvrContext);
    void destroy();

    void sendTrackingInfo(UdpManager *udpManager, uint64_t frameIndex, const float *headOrientation,
                             const float *headPosition);
private:
    /*
    // For ARCore
    bool m_ARMode = false;
    float position_offset_y = 0.0f;
    bool previousHeadsetTrackpad = false;
    float previousHeadsetY = 0.0f;
    int g_AROverlayMode = 0; // 0: VR only, 1: AR 30% 2: AR 70% 3: AR 100%
*/

    // GVR APIs
    gvr::ControllerApi* controllerApi;
    gvr::ControllerState controllerState;

    void fillControllerInfo(TrackingInfo *packet);
    void buildTrackingInfo(TrackingInfo *packet, UdpManager *udpManager, double displayTime,
                           uint64_t frameIndex,
                           const float *headOrientation, const float *headPosition);
};


#endif //ALVRCLIENT_DAYDREAM_GVRCONTEXT_H

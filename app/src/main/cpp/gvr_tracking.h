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

    void initialize(JNIEnv *env, jlong nativeGvrContext);
    void destroy();

    void sendTrackingInfo(JNIEnv *env, jobject udpManager, uint64_t frameIndex,
                          const float *headOrientation, const float *headPosition);
private:
    // GVR APIs
    gvr::ControllerApi* controllerApi;
    gvr::ControllerState controllerState;

    jmethodID mUdpReceiverThread_send;

    void fillControllerInfo(TrackingInfo *packet);
};


#endif //ALVRCLIENT_DAYDREAM_GVRCONTEXT_H

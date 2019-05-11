#include "gvr_tracking.h"

#include <unistd.h>
#include <jni.h>
#include <VrApi.h>
#include <VrApi_Types.h>
#include <VrApi_Helpers.h>
#include <VrApi_SystemUtils.h>
#include <VrApi_Input.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <string>
#include <map>
#include <vector>
#include <vr/gvr/capi/include/gvr_types.h>
#include "utils.h"
#include "render.h"
#include "ovr_context.h"
#include "latency_collector.h"
#include "packet_types.h"
#include "udp.h"
#include "asset.h"

#include "vr/gvr/capi/include/gvr.h"
#include "vr/gvr/capi/include/gvr_controller.h"
#include "vr/gvr/capi/include/gvr_types.h"

void GvrTracking::initialize(JNIEnv *env, jlong nativeGvrContext) {
    controllerApi = new gvr::ControllerApi();
    controllerApi->Init(gvr::ControllerApi::DefaultOptions() | GVR_CONTROLLER_ENABLE_ARM_MODEL, (gvr_context*)nativeGvrContext);
    controllerApi->Resume();

    jclass clazz = env->FindClass("com/polygraphene/alvr/UdpReceiverThread");
    mUdpReceiverThread_send = env->GetMethodID(clazz, "send", "(JI)V");
    env->DeleteLocalRef(clazz);
}

void GvrTracking::destroy() {
    LOG("Destroying GvrTracking.");
}

void GvrTracking::fillControllerInfo(TrackingInfo *packet) {
    gvr_mat4f identity;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            identity.m[j][i] = (i==j)? 1 : 0;
        }
    }
    controllerApi->ApplyArmModel(gvr::kControllerRightHanded, gvr::kArmModelBehaviorIgnoreGaze, identity);
    controllerState.Update(*controllerApi);
    if (controllerState.GetConnectionState() != gvr::kControllerConnected) {
        LOG("No Daydream controller %x", controllerState.GetConnectionState());
        return;
    }

    packet->flags |= TrackingInfo::FLAG_CONTROLLER_ENABLE;
    packet->flags |= TrackingInfo::FLAG_CONTROLLER_OCULUSGO;
    packet->controllerButtons = 0;

    if (controllerState.GetButtonState(GVR_CONTROLLER_BUTTON_HOME)) {
        // For this hack to work, the Home button needs to be held down >300ms to starting the
        // Daydream Dashboard. Then the volume buttons have to be pressed <300ms so that the
        // screen recording chord isn't triggered.
        if (controllerState.GetButtonState(GVR_CONTROLLER_BUTTON_VOLUME_DOWN)) {
            packet->controllerButtons |= 0x00200000; //ovrButton_Back
        }
        if (controllerState.GetButtonState(GVR_CONTROLLER_BUTTON_VOLUME_UP)) {
            // Map to SteamVR Home button?
        }
        if (controllerState.GetButtonState(GVR_CONTROLLER_BUTTON_CLICK)) {
            // ???
        }
        if (controllerState.GetButtonState(GVR_CONTROLLER_BUTTON_APP)) {
            // Map to SteamVR Home button?
        }

        if (controllerState.IsTouching()) {
            // Support clicking on the 4 sides of the touchpad???
            if (controllerState.GetTouchPos().x > .75f) {
                // ???
            }
            // http://blog.riftcat.com/2018/07/dev-update-41-vridge-22-beta.html has special
            // behavior at the sides.
        }
    } else {
        if (controllerState.GetButtonState(GVR_CONTROLLER_BUTTON_CLICK)) {
            packet->controllerButtons |= 0x00100000; //ovrButton_Enter == OVR controller touchpad click
        }
        if (controllerState.GetButtonState(GVR_CONTROLLER_BUTTON_APP)) {
            packet->controllerButtons |= 0x00000001; //ovrButton_A == OVR controller trigger
        }
        if (controllerState.IsTouching()) {
            packet->flags |= TrackingInfo::FLAG_CONTROLLER_TRACKPAD_TOUCH;
            packet->controllerTrackpadPosition.x =   controllerState.GetTouchPos().x * 2.f - 1.f;
            packet->controllerTrackpadPosition.y = -(controllerState.GetTouchPos().y * 2.f - 1.f);
        }
    }

    packet->controllerBatteryPercentRemaining = controllerState.GetBatteryLevel() * 20;

    auto q = controllerState.GetOrientation();
    packet->controller_Pose_Orientation.x = q.qx;
    packet->controller_Pose_Orientation.y = q.qy;
    packet->controller_Pose_Orientation.z = q.qz;
    packet->controller_Pose_Orientation.w = q.qw;
    // TODO ApplyArmModel(..)
    auto p = controllerState.GetPosition();
    packet->controller_Pose_Position.x = p.x;
    packet->controller_Pose_Position.y = p.y + 1.8;
    packet->controller_Pose_Position.z = p.z;
}

// Called TrackingThread. So, we can't use this->env.
void GvrTracking::sendTrackingInfo(JNIEnv *env, jobject udpReceiverThread, uint64_t frameIndex,
                                   const float *headOrientation, const float *headPosition) {
    uint64_t clientTime = getTimestampUs();

    TrackingInfo info = {};

    info.type = ALVR_PACKET_TYPE_TRACKING_INFO;
    info.flags = 0;
    info.clientTime = clientTime;
    info.FrameIndex = frameIndex;
    info.predictedDisplayTime = 0;

    memcpy(&info.HeadPose_Pose_Orientation, headOrientation, sizeof(TrackingQuat));
    memcpy(&info.HeadPose_Pose_Position, headPosition, sizeof(TrackingVector3));

    fillControllerInfo(&info);

    FrameLog(frameIndex, "Sending tracking info.");
    LatencyCollector::Instance().tracking(frameIndex);

    env->CallVoidMethod(udpReceiverThread, mUdpReceiverThread_send, reinterpret_cast<jlong>(&info), static_cast<int>(sizeof(info)));
}

extern "C"
JNIEXPORT jlong JNICALL
Java_com_polygraphene_alvr_GvrTracking_initializeNative(JNIEnv *env, jobject instance, jlong nativeGvrContext) {
    GvrTracking *gvrTracking = new GvrTracking();
    gvrTracking->initialize(env, nativeGvrContext);
    return (jlong) gvrTracking;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_GvrTracking_sendTrackingInfoNative(JNIEnv *env, jobject instance,
                                                              jlong nativeHandle, jobject udpReceiverThread,
                                                              jlong frameIndex,
                                                              jfloatArray headOrientation_,
                                                              jfloatArray headPosition_) {
    jfloat *headOrientation = env->GetFloatArrayElements(headOrientation_, NULL);
    jfloat *headPosition = env->GetFloatArrayElements(headPosition_, NULL);

    ((GvrTracking *) nativeHandle)->sendTrackingInfo(env, udpReceiverThread,
                                                     (uint64_t) frameIndex, headOrientation, headPosition);

    env->ReleaseFloatArrayElements(headOrientation_, headOrientation, 0);
    env->ReleaseFloatArrayElements(headPosition_, headPosition, 0);
}
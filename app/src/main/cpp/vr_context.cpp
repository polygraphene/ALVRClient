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
#include "utils.h"
#include "render.h"
#include "vr_context.h"
#include "latency_collector.h"
#include "packet_types.h"
#include "udp.h"
#include "asset.h"
#include "ovr_context.h"
#include "gvr_context.h"

extern "C"
JNIEXPORT jlong JNICALL
Java_com_polygraphene_alvr_VrContext_initializeNative(JNIEnv *env, jobject instance,
                                                      jobject activity, jobject assetManager, bool ARMode, int initialRefreshRate) {
    LOG("VrContext initialize.");
#ifdef OVR_SDK
    VrContextBase *context = new OvrContext();
#else
    VrContextBase *context = new GvrContext();
#endif
    context->initialize(env, activity, assetManager, ARMode, initialRefreshRate);
    LOG("VrContext %p", context);
    return (jlong) context;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_VrContext_destroyNative(JNIEnv *env, jobject instance, jlong handle) {
    ((VrContextBase *) handle)->destroy();
}


extern "C"
JNIEXPORT jint JNICALL
Java_com_polygraphene_alvr_VrContext_getLoadingTextureNative(JNIEnv *env, jobject instance,
                                                             jlong handle) {
    return ((VrContextBase *) handle)->getLoadingTexture();
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_polygraphene_alvr_VrContext_getSurfaceTextureIDNative(JNIEnv *env, jobject instance,
                                                               jlong handle) {
    return ((VrContextBase *) handle)->getSurfaceTextureID();
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_polygraphene_alvr_VrContext_getCameraTextureNative(JNIEnv *env, jobject instance,
                                                            jlong handle) {
    return ((VrContextBase *) handle)->getCameraTexture();
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_VrContext_renderNative(JNIEnv *env, jobject instance, jlong handle,
                                                  jlong renderedFrameIndex) {
    return ((VrContextBase *) handle)->render(renderedFrameIndex);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_VrContext_renderLoadingNative(JNIEnv *env, jobject instance,
                                                         jlong handle) {
    return ((VrContextBase *) handle)->renderLoading();
}

// Called from TrackingThread
extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_VrContext_fetchTrackingInfoNative(JNIEnv *env, jobject instance,
                                                             jlong handle,
                                                             jlong udpManager,
                                                             jfloatArray position_,
                                                             jfloatArray orientation_) {
    if(position_ != NULL && orientation_ != NULL) {
        ovrVector3f position;
        ovrQuatf orientation;

        jfloat *position_c = env->GetFloatArrayElements(position_, NULL);
        memcpy(&position, position_c, sizeof(float) * 3);
        env->ReleaseFloatArrayElements(position_, position_c, 0);

        jfloat *orientation_c = env->GetFloatArrayElements(orientation_, NULL);
        memcpy(&orientation, orientation_c, sizeof(float) * 4);
        env->ReleaseFloatArrayElements(orientation_, orientation_c, 0);

        ((VrContextBase *) handle)->fetchTrackingInfo(env, (UdpManager *)udpManager, &position, &orientation);
    }else {
        ((VrContextBase *) handle)->fetchTrackingInfo(env, (UdpManager *) udpManager, NULL, NULL);
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_VrContext_onChangeSettingsNative(JNIEnv *env, jobject instance,
                                                            jlong handle,
                                                            jint EnableTestMode, jint Suspend) {
    ((VrContextBase *) handle)->onChangeSettings(EnableTestMode, Suspend);
}

//
// Life cycle management.
//

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_VrContext_onSurfaceCreatedNative(JNIEnv *env, jobject instance,
                                                            jlong handle,
                                                            jobject surface) {
    ((VrContextBase *) handle)->onSurfaceCreated(surface);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_VrContext_onSurfaceDestroyedNative(JNIEnv *env, jobject instance,
                                                              jlong handle) {
    ((VrContextBase *) handle)->onSurfaceDestroyed();
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_VrContext_onSurfaceChangedNative(JNIEnv *env, jobject instance,
                                                            jlong handle, jobject surface) {
    ((VrContextBase *) handle)->onSurfaceChanged(surface);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_VrContext_onResumeNative(JNIEnv *env, jobject instance, jlong handle) {
    ((VrContextBase *) handle)->onResume();
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_VrContext_onPauseNative(JNIEnv *env, jobject instance, jlong handle) {
    ((VrContextBase *) handle)->onPause();
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_polygraphene_alvr_VrContext_isVrModeNative(JNIEnv *env, jobject instance, jlong handle) {
    return ((VrContextBase *) handle)->isVrMode();
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_VrContext_getRefreshRatesNative(JNIEnv *env, jobject instance, jlong handle, jintArray refreshRates) {
    ((VrContextBase *) handle)->getRefreshRates(env, refreshRates);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_VrContext_setFrameGeometryNative(JNIEnv *env, jobject instance,
                                                            jlong handle, jint width,
                                                            jint height) {
    ((VrContextBase *) handle)->setFrameGeometry(width, height);
}

extern "C"
JNIEXPORT bool JNICALL
Java_com_polygraphene_alvr_VrContext_onKeyEventNative(JNIEnv *env, jobject instance, jlong handle,
                                                      jint keyCode,
                                                      jint action) {
    return ((VrContextBase *) handle)->onKeyEvent(keyCode, action);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_VrContext_setRefreshRateNative(JNIEnv *env, jobject instance,
                                                          jlong handle, jint refreshRate) {
    return ((VrContextBase *) handle)->setRefreshRate(refreshRate);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_VrContext_sendTrackingNative(JNIEnv *env, jobject instance,
                                                        jlong handle, jlong udpManager,
                                                        jlong frameIndex,
                                                        jfloatArray headOrientation_,
                                                        jfloatArray headPosition_) {
    jfloat *position_c = env->GetFloatArrayElements(headPosition_, NULL);

    jfloat *orientation_c = env->GetFloatArrayElements(headOrientation_, NULL);

    ((GvrContext *) handle)->sendTrackingInfoGvr(env, (UdpManager *)udpManager, (uint64_t)frameIndex, orientation_c, position_c);

    env->ReleaseFloatArrayElements(headPosition_, position_c, 0);
    env->ReleaseFloatArrayElements(headOrientation_, orientation_c, 0);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_VrContext_initializeGvrNative(JNIEnv *env, jobject instance,
                                                         jlong handle, jlong gvrNativeHandle) {
    ((GvrContext *) handle)->initializeGvr(gvrNativeHandle);
}
#include "gvr_context.h"

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
#include "vr_context.h"
#include "latency_collector.h"
#include "packet_types.h"
#include "udp.h"
#include "asset.h"

#include "vr/gvr/capi/include/gvr.h"
#include "vr/gvr/capi/include/gvr_controller.h"
#include "vr/gvr/capi/include/gvr_types.h"

void GvrContext::initialize(JNIEnv *env, jobject activity, jobject assetManager, bool ARMode, int initialRefreshRate) {
    LOG("Initializing EGL.");

    setAssetManager(env, assetManager);

    this->env = env;
    java.Env = env;
    env->GetJavaVM(&java.Vm);
    java.ActivityObject = env->NewGlobalRef(activity);

    eglInit();

    bool multi_view;
    EglInitExtensions(&multi_view);

    UseMultiview = multi_view;
    LOG("UseMultiview:%d", UseMultiview);

    GLint textureUnits;
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &textureUnits);
    LOGI("GL_VENDOR=%s", glGetString(GL_VENDOR));
    LOGI("GL_RENDERER=%s", glGetString(GL_RENDERER));
    LOGI("GL_VERSION=%s", glGetString(GL_VERSION));
    LOGI("GL_MAX_TEXTURE_IMAGE_UNITS=%d", textureUnits);

    m_currentRefreshRate = DEFAULT_REFRESH_RATE;
    setInitialRefreshRate(initialRefreshRate);

    //
    // Generate texture for SurfaceTexture which is output of MediaCodec.
    //
    m_ARMode = ARMode;

    int textureCount = 2;
    if (m_ARMode) {
        textureCount++;
    }
    GLuint textures[textureCount];
    glGenTextures(textureCount, textures);

    SurfaceTextureID = textures[0];
    loadingTexture = textures[1];

    glBindTexture(GL_TEXTURE_EXTERNAL_OES, SurfaceTextureID);

    glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER,
                    GL_NEAREST);
    glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER,
                    GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S,
                    GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T,
                    GL_CLAMP_TO_EDGE);

    if (m_ARMode) {
        CameraTexture = textures[2];
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, CameraTexture);

        glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER,
                        GL_NEAREST);
        glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER,
                        GL_LINEAR);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S,
                        GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T,
                        GL_CLAMP_TO_EDGE);
    }

    FrameBufferWidth = 1024;
    FrameBufferHeight = 1024;
    ovrRenderer_Create(&Renderer, &java, UseMultiview, FrameBufferWidth, FrameBufferHeight,
                       SurfaceTextureID, loadingTexture, CameraTexture, m_ARMode);
    ovrRenderer_CreateScene(&Renderer);

    BackButtonState = BACK_BUTTON_STATE_NONE;
    BackButtonDown = false;
    BackButtonDownStartTime = 0.0;

    position_offset_y = 0.0;
}

void GvrContext::initializeGvr(jlong nativeGvrContext) {
    controllerApi = new gvr::ControllerApi();
    controllerApi->Init(gvr::ControllerApi::DefaultOptions() | GVR_CONTROLLER_ENABLE_ARM_MODEL, (gvr_context*)nativeGvrContext);
    controllerApi->Resume();
}


void GvrContext::destroy() {
    LOG("Destroying EGL.");

    ovrRenderer_Destroy(&Renderer);

    GLuint textures[2] = {SurfaceTextureID, loadingTexture};
    glDeleteTextures(2, textures);
    if (m_ARMode) {
        glDeleteTextures(1, &CameraTexture);
    }

    eglDestroy();
}


void GvrContext::setControllerInfo(TrackingInfo *packet, double displayTime) {
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

    LOG("P %f %f %f", p.x, packet->controller_Pose_Position.y, p.z);


}

// Called TrackingThread. So, we can't use this->env.
void GvrContext::sendTrackingInfo(TrackingInfo *packet, UdpManager *udpManager,
                                  double displayTime, uint64_t frameIndex,
                                  const float *headOrientation,
                                  const float *headPosition) {
    memset(packet, 0, sizeof(TrackingInfo));

    uint64_t clientTime = getTimestampUs();

    packet->type = ALVR_PACKET_TYPE_TRACKING_INFO;
    packet->flags = 0;
    packet->clientTime = clientTime;
    packet->FrameIndex = frameIndex;
    packet->predictedDisplayTime = displayTime;

    memcpy(&packet->HeadPose_Pose_Orientation, headOrientation,
           sizeof(float) * 4);
    memcpy(&packet->HeadPose_Pose_Position, headPosition, sizeof(ovrVector3f));

    setControllerInfo(packet, displayTime);

    FrameLog(frameIndex, "Sending tracking info.");
}

// Called TrackingThread. So, we can't use this->env.
void GvrContext::sendTrackingInfoGvr(JNIEnv *env_, UdpManager *udpManager,
                                   uint64_t frameIndex,
                                   const float *headOrientation,
                                   const float *headPosition) {
    std::shared_ptr<TrackingFrame> frame(new TrackingFrame());

    frame->frameIndex = frameIndex;
    frame->fetchTime = getTimestampUs();

    frame->displayTime = 0;

    {
        MutexLock lock(trackingFrameMutex);
        trackingFrameMap.insert(
                std::pair<uint64_t, std::shared_ptr<TrackingFrame> >(frameIndex, frame));
        if (trackingFrameMap.size() > MAXIMUM_TRACKING_FRAMES) {
            trackingFrameMap.erase(trackingFrameMap.cbegin());
        }
    }

    TrackingInfo info;
    sendTrackingInfo(&info, udpManager, frame->displayTime, frameIndex, headOrientation, headPosition);

    LatencyCollector::Instance().tracking(frame->frameIndex);

    udpManager->send(&info, sizeof(info));
}


void GvrContext::onChangeSettings(int EnableTestMode, int Suspend) {
    enableTestMode = EnableTestMode;
    suspend = Suspend;
}

void GvrContext::onSurfaceCreated(jobject surface) {
    LOG("onSurfaceCreated called. Resumed=%d Window=%p Ovr=%p", Resumed, window, Ovr);
    window = ANativeWindow_fromSurface(env, surface);
}

void GvrContext::onSurfaceDestroyed() {
    LOG("onSurfaceDestroyed called. Resumed=%d Window=%p Ovr=%p", Resumed, window, Ovr);
    if (window != NULL) {
        ANativeWindow_release(window);
    }
    window = NULL;
}

void GvrContext::onSurfaceChanged(jobject surface) {
    LOG("onSurfaceChanged called. Resumed=%d Window=%p Ovr=%p", Resumed, window, Ovr);
}

void GvrContext::onResume() {
    LOG("onResume called. Resumed=%d Window=%p Ovr=%p", Resumed, window, Ovr);
    Resumed = true;
}

void GvrContext::onPause() {
    LOG("onPause called. Resumed=%d Window=%p Ovr=%p", Resumed, window, Ovr);
    Resumed = false;
}

bool GvrContext::onKeyEvent(int keyCode, int action) {
    LOG("HandleKeyEvent: keyCode=%d action=%d", keyCode, action);
    return false;
}

void GvrContext::render(uint64_t renderedFrameIndex) {
    double currentTime = GetTimeInSeconds();

    LatencyCollector::Instance().rendered1(renderedFrameIndex);
    FrameLog(renderedFrameIndex,
             "Got frame for render. wanted FrameIndex=%lu waiting=%.3f ms delay=%lu",
             WantedFrameIndex,
             (GetTimeInSeconds() - currentTime) * 1000,
             WantedFrameIndex - renderedFrameIndex);
    if (WantedFrameIndex > renderedFrameIndex) {
        return;
    }
}


void GvrContext::renderLoading() {
}

void GvrContext::setFrameGeometry(int width, int height) {
    int eye_width = width / 2;
    if (eye_width != FrameBufferWidth || height != FrameBufferHeight) {
        LOG("Changing FrameBuffer geometry. Old=%dx%d New=%dx%d", FrameBufferWidth,
            FrameBufferHeight, eye_width, height);
        FrameBufferWidth = eye_width;
        FrameBufferHeight = height;
        ovrRenderer_Destroy(&Renderer);
        ovrRenderer_Create(&Renderer, &java, UseMultiview, FrameBufferWidth, FrameBufferHeight,
                           SurfaceTextureID, loadingTexture, CameraTexture, m_ARMode);
        ovrRenderer_CreateScene(&Renderer);
    }
}

void GvrContext::getRefreshRates(JNIEnv *env_, jintArray refreshRates) {
    jint *refreshRates_ = env_->GetIntArrayElements(refreshRates, NULL);

    // Fill empty entry with 0.
    memset(refreshRates_, 0, sizeof(jint) * ALVR_REFRESH_RATE_LIST_SIZE);
    refreshRates_[0] = 60;
    refreshRates_[1] = 75;

    env_->ReleaseIntArrayElements(refreshRates, refreshRates_, 0);
}

void GvrContext::setRefreshRate(int refreshRate, bool forceChange) {
    if(m_currentRefreshRate == refreshRate) {
        LOG("Refresh rate not changed. %d Hz", refreshRate);
        return;
    }
}

void GvrContext::setInitialRefreshRate(int initialRefreshRate) {
    setRefreshRate(initialRefreshRate, false);
}

void GvrContext::fetchTrackingInfo(JNIEnv *env_, UdpManager *udpManager, ovrVector3f *position,
                                   ovrQuatf *orientation) {
    return;
}

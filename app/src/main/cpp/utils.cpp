#include "utils.h"
#include <jni.h>

int gGeneralLogLevel = ANDROID_LOG_VERBOSE;
int gSoundLogLevel = ANDROID_LOG_INFO;
int gSocketLogLevel = ANDROID_LOG_INFO;

bool gEnableFrameLog = false;

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_Utils_setFrameLogEnabled(JNIEnv *env, jclass type, jboolean enabled) {
    gEnableFrameLog = static_cast<bool>(enabled);
}
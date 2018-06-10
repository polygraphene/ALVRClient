#ifndef ALVRCLIENT_UTILS_H
#define ALVRCLIENT_UTILS_H

#include <stdint.h>
#include <time.h>
#include <android/log.h>
#include <pthread.h>
#include <string>

#include <VrApi.h>
#include <VrApi_Types.h>
#include <VrApi_Helpers.h>

#define LOG(...) __android_log_print(ANDROID_LOG_DEBUG, "ALVR Native", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ALVR Native", __VA_ARGS__)

inline void FrameLog(uint64_t frameIndex, const char *format, ...)
{
    char buf[10000];

    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    __android_log_print(ANDROID_LOG_VERBOSE, "FrameTracking", "[Frame %lu] %s", frameIndex, buf);
}

inline uint64_t getTimestampUs(){
    timeval tv;
    gettimeofday(&tv, NULL);

    uint64_t Current = (uint64_t)tv.tv_sec * 1000 * 1000 + tv.tv_usec;
    return Current;
}


class Mutex {
    pthread_mutex_t mutex;
public:
    Mutex() { pthread_mutex_init(&mutex, NULL); }
    ~Mutex() { pthread_mutex_destroy(&mutex); }

    void Lock(){
        pthread_mutex_lock(&mutex);
    }

    void Unlock(){
        pthread_mutex_unlock(&mutex);
    }

    void CondWait(pthread_cond_t *cond){
        pthread_cond_wait(cond, &mutex);
    }
};

class MutexLock {
    Mutex *mutex;
public:
    MutexLock(Mutex& mutex) {
        this->mutex = &mutex;
        this->mutex->Lock();
    }
    ~MutexLock() {
        this->mutex->Unlock();
    }
};

inline std::string GetStringFromJNIString(JNIEnv *env, jstring string){
    const char *buf = env->GetStringUTFChars(string, 0);
    std::string ret = buf;
    env->ReleaseStringUTFChars(string, buf);

    return ret;
}

inline double PitchFromQuaternion(double x, double y, double z, double w) {
    // (xx, yy, zz) = rotate (0, 0, -1) by quaternion
    double xx = -2 * y * w
                - 2 * x * y;
    double zz = -w * w
                + x * x
                + y * y
                - z * z;
    return atan2(xx, zz);
}

inline double PitchFromQuaternion(const ovrQuatf *qt) {
    return PitchFromQuaternion(qt->x, qt->y, qt->z, qt->w);
}

inline ovrQuatf quatMultipy(const ovrQuatf *a, const ovrQuatf *b){
    ovrQuatf dest;
    dest.x = a->x * b->w + a->w * b->x + a->y * b->z - a->z * b->y;
    dest.y = a->y * b->w + a->w * b->y + a->z * b->x - a->x * b->z;
    dest.z = a->z * b->w + a->w * b->z + a->x * b->y - a->y * b->x;
    dest.w = a->w * b->w - a->x * b->x - a->y * b->y - a->z * b->z;
    return dest;
}

inline std::string DumpQuat(const ovrQuatf &a) {
    char buf[100];
    snprintf(buf, sizeof(buf), "(%f,%f,%f,%f)", a.x, a.y, a.z, a.w);
    return buf;
}

#endif //ALVRCLIENT_UTILS_H

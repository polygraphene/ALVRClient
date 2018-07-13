#ifndef ALVRCLIENT_UTILS_H
#define ALVRCLIENT_UTILS_H

#include <stdint.h>
#include <math.h>
#include <time.h>
#include <pthread.h>
#include <android/log.h>
#include <string>
#include <VrApi_Types.h>
#include <GLES3/gl3.h>

//
// Logging
//

#define LOG(...) __android_log_print(ANDROID_LOG_DEBUG, "ALVR Native", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ALVR Native", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ALVR Native", __VA_ARGS__)

#define LOGSOUND(...) __android_log_print(ANDROID_LOG_DEBUG, "ALVR Sound", __VA_ARGS__)
#define LOGSOUNDI(...) __android_log_print(ANDROID_LOG_INFO, "ALVR Sound", __VA_ARGS__)
#define LOGSOUNDE(...) __android_log_print(ANDROID_LOG_ERROR, "ALVR Sound", __VA_ARGS__)

static const int64_t USECS_IN_SEC = 1000 * 1000;

inline void FrameLog(uint64_t frameIndex, const char *format, ...)
{
    char buf[10000];

    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    __android_log_print(ANDROID_LOG_VERBOSE, "FrameTracking", "[Frame %lu] %s", frameIndex, buf);
}

//
// GL Logging
//

#define CHECK_GL_ERRORS 1
#ifdef CHECK_GL_ERRORS

static const char *GlErrorString(GLenum error) {
    switch (error) {
        case GL_NO_ERROR:
            return "GL_NO_ERROR";
        case GL_INVALID_ENUM:
            return "GL_INVALID_ENUM";
        case GL_INVALID_VALUE:
            return "GL_INVALID_VALUE";
        case GL_INVALID_OPERATION:
            return "GL_INVALID_OPERATION";
        case GL_INVALID_FRAMEBUFFER_OPERATION:
            return "GL_INVALID_FRAMEBUFFER_OPERATION";
        case GL_OUT_OF_MEMORY:
            return "GL_OUT_OF_MEMORY";
        default:
            return "unknown";
    }
}

static void GLCheckErrors(int line) {
    for (int i = 0; i < 10; i++) {
        const GLenum error = glGetError();
        if (error == GL_NO_ERROR) {
            break;
        }
        LOGE("GL error on line %d: %s", line, GlErrorString(error));
    }
}

#define GL(func)        func; GLCheckErrors( __LINE__ );
#else // CHECK_GL_ERRORS
#define GL(func)        func;
#endif // CHECK_GL_ERRORS

//
// Utility
//

inline uint64_t getTimestampUs(){
    timeval tv;
    gettimeofday(&tv, NULL);

    uint64_t Current = (uint64_t)tv.tv_sec * 1000 * 1000 + tv.tv_usec;
    return Current;
}

//
// Mutex
//

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

//
// Utility
//

inline std::string GetStringFromJNIString(JNIEnv *env, jstring string){
    const char *buf = env->GetStringUTFChars(string, 0);
    std::string ret = buf;
    env->ReleaseStringUTFChars(string, buf);

    return ret;
}

inline double GetTimeInSeconds() {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec * 1e9 + now.tv_nsec) * 0.000000001;
}

inline std::string DumpMatrix(const ovrMatrix4f *matrix) {
    char buf[1000];
    sprintf(buf, "%.5f, %.5f, %.5f, %.5f\n"
                    "%.5f, %.5f, %.5f, %.5f\n"
                    "%.5f, %.5f, %.5f, %.5f\n"
                    "%.5f, %.5f, %.5f, %.5f\n", matrix->M[0][0], matrix->M[0][1], matrix->M[0][2],
            matrix->M[0][3], matrix->M[1][0], matrix->M[1][1], matrix->M[1][2], matrix->M[1][3],
            matrix->M[2][0], matrix->M[2][1], matrix->M[2][2], matrix->M[2][3], matrix->M[3][0],
            matrix->M[3][1], matrix->M[3][2], matrix->M[3][3]
    );
    return std::string(buf);
}

inline ovrQuatf quatMultipy(const ovrQuatf *a, const ovrQuatf *b){
    ovrQuatf dest;
    dest.x = a->x * b->w + a->w * b->x + a->y * b->z - a->z * b->y;
    dest.y = a->y * b->w + a->w * b->y + a->z * b->x - a->x * b->z;
    dest.z = a->z * b->w + a->w * b->z + a->x * b->y - a->y * b->x;
    dest.w = a->w * b->w - a->x * b->x - a->y * b->y - a->z * b->z;
    return dest;
}

#endif //ALVRCLIENT_UTILS_H

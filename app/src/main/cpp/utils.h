#ifndef ALVRCLIENT_UTILS_H
#define ALVRCLIENT_UTILS_H

#include <stdint.h>
#include <time.h>
#include <android/log.h>
#include <pthread.h>
#include <string>
#include <VrApi_Types.h>

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

#endif //ALVRCLIENT_UTILS_H

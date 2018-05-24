#ifndef ALVRCLIENT_UTILS_H
#define ALVRCLIENT_UTILS_H

#include <stdint.h>
#include <time.h>
#include <android/log.h>
#include <pthread.h>

#define LOG(...) __android_log_print(ANDROID_LOG_DEBUG, "ALVR Native", __VA_ARGS__)

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


#endif //ALVRCLIENT_UTILS_H

//
// Created by lll on 2018/05/14.
//

#ifndef REMOTEGLASS_UTILS_H
#define REMOTEGLASS_UTILS_H

#include <stdint.h>
#include <time.h>
#include <android/log.h>

#define LOG(...) __android_log_print(ANDROID_LOG_DEBUG, "Native", __VA_ARGS__)

inline uint64_t getTimestampUs(){
    timeval tv;
    gettimeofday(&tv, NULL);

    uint64_t Current = (uint64_t)tv.tv_sec * 1000 * 1000 + tv.tv_usec;
    return Current;
}

#endif //REMOTEGLASS_UTILS_H

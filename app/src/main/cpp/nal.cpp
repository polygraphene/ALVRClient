/// H.264 NAL Parser functions
// Extract NAL Units from packet by UDP/SRT socket.
////////////////////////////////////////////////////////////////////

#include <jni.h>
#include <string>
#include <stdlib.h>
#include <list>
#include <android/log.h>
#include <pthread.h>
#include "nal.h"
#include "utils.h"
#include "packet_types.h"

static const int MAXIMUM_NAL_BUFFER = 10;
static const int MAXIMUM_NAL_OBJECT = 20;

static const int NAL_TYPE_SPS = 7;

static const int H265_NAL_TYPE_VPS = 32;

static bool initialized = false;
static bool stopped = false;

// NAL buffer
jbyteArray currentBuf = NULL;
char *cbuf = NULL;
int bufferLength = 0;
int bufferPos = 0;

// Current NAL meta data from packet
uint64_t prevSequence;
uint32_t frameByteSize;
jobject currentNAL = NULL;
bool processingIDR = false;
int g_codec = 1;

static JNIEnv *env_;

// Parsed NAL queue
std::list<jobject> nalList;
std::list<jobject> nalRecycleList;
Mutex nalMutex;

static pthread_cond_t cond_nonzero = PTHREAD_COND_INITIALIZER;

jclass NAL_clazz;

jfieldID NAL_length;
jfieldID NAL_presentationTime;
jfieldID NAL_frameIndex;
jfieldID NAL_buf;

static void releaseBuffer();

static void dumpCurrentQueue() {
    MutexLock lock(nalMutex);
    LOG("Current Queue State:");
    int i = 0;
    for (auto it = nalRecycleList.begin(); it != nalRecycleList.end(); ++it, i++) {
        jbyteArray buf = (jbyteArray) env_->GetObjectField(*it, NAL_buf);
        if (buf != NULL) {
            jsize len = env_->GetArrayLength(buf);
            LOG("r:%d/%lu %d", i, nalRecycleList.size(), len);
            env_->DeleteLocalRef(buf);
        } else {
            LOG("r:%d/%lu (NULL)", i, nalRecycleList.size());
        }
    }
    i = 0;
    for (auto it = nalList.begin(); it != nalList.end(); ++it, i++) {
        jbyteArray buf = (jbyteArray) env_->GetObjectField(*it, NAL_buf);
        if (buf != NULL) {
            jsize len = env_->GetArrayLength(buf);
            LOG("b:%d/%lu %d", i, nalList.size(), len);
            env_->DeleteLocalRef(buf);
        } else {
            LOG("b:%d/%lu (NULL)", i, nalList.size());
        }
    }
}

static void recycleNalNoGlobal(jobject nal) {
    MutexLock lock(nalMutex);
    nalRecycleList.push_front(nal);
}

static void clearNalList(JNIEnv *env) {
    for (auto it = nalList.begin();
         it != nalList.end(); ++it) {
        nalRecycleList.push_back(*it);
    }
    nalList.clear();
}

static void initializeBuffer(){
    currentBuf = NULL;
    cbuf = NULL;
    bufferLength = 0;
    bufferPos = 0;
}

static void allocateBuffer(int length, uint64_t presentationTime, uint64_t frameIndex) {
    if(currentNAL != NULL) {
        releaseBuffer();
        recycleNalNoGlobal(currentNAL);
        currentNAL = NULL;
    }

    //dumpCurrentQueue();
    {
        MutexLock lock(nalMutex);

        if(nalRecycleList.size() == 0) {
            LOGE("NAL Queue is full (nalRecycleList is empty).");
            return;
        }
        currentNAL = nalRecycleList.front();
        nalRecycleList.pop_front();
    }

    currentBuf = (jbyteArray) env_->GetObjectField(currentNAL, NAL_buf);

    jsize len = 0;
    if(currentBuf != NULL) {
        len = env_->GetArrayLength(currentBuf);
    }

    if(len < length) {
        if(currentBuf != NULL) {
            env_->DeleteLocalRef(currentBuf);
        }
        currentBuf = env_->NewByteArray(length);
        if(currentBuf == NULL){
            LOGE("Error: NewByteArray return NULL. Memory is full?");
            return;
        }
        env_->SetObjectField(currentNAL, NAL_buf, currentBuf);
    }
    jobject tmp = currentBuf;
    currentBuf = (jbyteArray) env_->NewGlobalRef(currentBuf);
    env_->DeleteLocalRef(tmp);

    env_->SetIntField(currentNAL, NAL_length, length);
    env_->SetLongField(currentNAL, NAL_presentationTime, presentationTime);
    env_->SetLongField(currentNAL, NAL_frameIndex, frameIndex);

    bufferLength = length;
    bufferPos = 0;

    cbuf = (char *) env_->GetByteArrayElements(currentBuf, NULL);
}

static bool push(char *buf, int len) {
    if (bufferPos + len > bufferLength) {
        // Full!
        return false;
    }
    memcpy(cbuf + bufferPos, buf, len);
    bufferPos += len;
    return true;
}

// Only release buffer. Caller can use currentBuf after release.
static void releaseBuffer(){
    if (currentBuf != NULL) {
        env_->ReleaseByteArrayElements(currentBuf, (jbyte *) cbuf, 0);
        env_->DeleteGlobalRef(currentBuf);
        currentBuf = NULL;
    }
}

void initNAL(JNIEnv *env) {
    pthread_cond_init(&cond_nonzero, NULL);

    initializeBuffer();
    initialized = true;
    prevSequence = 0;
    stopped = false;
    processingIDR = false;

    NAL_clazz = env->FindClass("com/polygraphene/alvr/NAL");
    jmethodID NAL_ctor = env->GetMethodID(NAL_clazz, "<init>", "()V");

    NAL_length = env->GetFieldID(NAL_clazz, "length", "I");
    NAL_presentationTime = env->GetFieldID(NAL_clazz, "presentationTime", "J");
    NAL_frameIndex = env->GetFieldID(NAL_clazz, "frameIndex", "J");
    NAL_buf = env->GetFieldID(NAL_clazz, "buf", "[B");

    for(int i = 0; i < MAXIMUM_NAL_OBJECT; i++){
        jobject nal = env->NewObject(NAL_clazz, NAL_ctor);
        jobject tmp = nal;
        nal = env->NewGlobalRef(nal);
        env->DeleteLocalRef(tmp);

        nalRecycleList.push_back(nal);
    }
}

void destroyNAL(JNIEnv *env){
    clearNalList(env);

    for (auto it = nalRecycleList.begin(); it != nalRecycleList.end(); ++it) {
        env->DeleteGlobalRef(*it);
    }
    nalRecycleList.clear();
    stopped = true;
    pthread_cond_destroy(&cond_nonzero);
    initialized = false;
}

void setNalCodec(int codec) {
    g_codec = codec;
}

static void putNAL() {
    assert(currentNAL != NULL);

    releaseBuffer();

    {
        MutexLock lock(nalMutex);

        if(nalList.size() < MAXIMUM_NAL_BUFFER) {
            nalList.push_back(currentNAL);

            pthread_cond_broadcast(&cond_nonzero);
        }else {
            // Discard buffer
            LOG("NAL Queue is too large. Discard. Size=%lu Limit=%d", nalList.size(), MAXIMUM_NAL_BUFFER);
            nalRecycleList.push_front(currentNAL);
        }
    }
    currentNAL = NULL;
}

bool processPacket(JNIEnv *env, char *buf, int len) {
    if (!initialized) {
        initNAL(env);
    }

    int pos = 0;

    env_ = env;

    uint32_t type = *(uint32_t *) buf;

    if (type == ALVR_PACKET_TYPE_VIDEO_FRAME_START) {
        VideoFrameStart *header = (VideoFrameStart *)buf;
        uint64_t presentationTime = header->presentationTime;
        uint64_t frameIndex = header->frameIndex;
        prevSequence = header->packetCounter;

        pos = sizeof(VideoFrameStart);

        uint8_t NALType = buf[pos + 4] & 0x1F;
        if(g_codec == ALVR_CODEC_H265) {
            NALType = ((buf[pos + 4] >> 1) & 0x3F);
        }

        if(g_codec == ALVR_CODEC_H264 && NALType == NAL_TYPE_SPS) {
            // H264 SPS NAL

            // This frame contains SPS + PPS + IDR on NVENC H.264 stream.
            // SPS + PPS has short size (8bytes + 28bytes in some environment), so we can assume SPS + PPS is contained in first fragment.

            int zeroes = 0;
            bool parsingSPS = true;
            int SPSEnd = -1;
            int PPSEnd = -1;
            for (int i = pos + 4; i < len; i++) {
                if (buf[i] == 0) {
                    zeroes++;
                } else if (buf[i] == 1) {
                    if (zeroes == 3) {
                        if (parsingSPS) {
                            parsingSPS = false;
                            SPSEnd = i - 3;
                        } else {
                            PPSEnd = i - 3;
                            break;
                        }
                    }
                    zeroes = 0;
                } else {
                    zeroes = 0;
                }
            }
            if (SPSEnd == -1 || PPSEnd == -1) {
                // Invalid frame.
                LOG("Got invalid frame. Too large SPS or PPS?");
                return false;
            }
            allocateBuffer(SPSEnd - pos, presentationTime, frameIndex);
            push(buf + pos, SPSEnd - pos);
            putNAL();

            pos = SPSEnd;

            allocateBuffer(PPSEnd - pos, presentationTime, frameIndex);
            push(buf + pos, PPSEnd - pos);

            putNAL();

            pos = PPSEnd;

            processingIDR = true;

            // Allocate IDR frame buffer
            allocateBuffer(header->frameByteSize - (PPSEnd - sizeof(VideoFrameStart)),
                           presentationTime, frameIndex);
            frameByteSize = header->frameByteSize - (PPSEnd - sizeof(VideoFrameStart));
            // Note that if previous frame packet has lost, we dispose that incomplete buffer implicitly here.
        }else if(g_codec == ALVR_CODEC_H265 && NALType == H265_NAL_TYPE_VPS) {
            int zeroes = 0;
            int foundNals = 0;
            int end = -1;
            for (int i = pos; i < len; i++) {
                if (buf[i] == 0) {
                    zeroes++;
                } else if (buf[i] == 1) {
                    if (zeroes == 3) {
                        foundNals++;
                        if(foundNals >= 4) {
                            // NAL header of IDR frame
                            end = i - 3;
                            break;
                        }
                    }
                    zeroes = 0;
                } else {
                    zeroes = 0;
                }
            }
            if(end < 0) {
                LOG("Got invalid frame. Too large VPS?");
                return false;
            }
            // Allocate VPS+SPS+PPS NAL buffer
            allocateBuffer(end - pos, presentationTime, frameIndex);
            push(buf + pos, end - pos);
            putNAL();

            pos = end;

            LOG("Parsing H265 IDR frame. total frameByteSize=%d VPS+SPS+PPS Size=%d", header->frameByteSize, pos - sizeof(VideoFrameStart));

            // Allocate IDR frame buffer
            allocateBuffer(header->frameByteSize - (pos - sizeof(VideoFrameStart)),
                           presentationTime, frameIndex);
            frameByteSize = header->frameByteSize - (pos - sizeof(VideoFrameStart));
        }else {
            processingIDR = false;
            // Allocate P-frame buffer
            allocateBuffer(header->frameByteSize, presentationTime, frameIndex);
            frameByteSize = header->frameByteSize;
            // Note that if previous frame packet has lost, we dispose that incomplete buffer implicitly here.
        }
    }else {
        VideoFrame *header = (VideoFrame *)buf;
        pos = sizeof(VideoFrame);

        if(header->packetCounter != 1 && prevSequence + 1 != header->packetCounter) {
            // Packet loss
            LOGE("Ignore this frame because of packet loss. prevSequence=%lu currentSequence=%d"
            , prevSequence, header->packetCounter);

            // We don't update prevSequence here. This lead to ignore all packet until next VideoFrameStart packet arrives.
            return false;
        }
        prevSequence = header->packetCounter;
    }

    if(!push(buf + pos, len - pos)) {
        // Too large frame. It may be caused by packet loss.
        // We ignore this frame.
        LOGE("Error: Too large frame. Current buffer pos=%d buffer length=%d added size=%d"
        , bufferPos, bufferLength, len - pos);
        releaseBuffer();
        recycleNalNoGlobal(currentNAL);
        currentNAL = NULL;
        return false;
    }
    if(bufferPos >= frameByteSize) {
        // End of frame.
        putNAL();
        return true;
    }

    return false;
}

bool processingIDRFrame() {
    return processingIDR;
}

jobject waitNal(JNIEnv *env) {
    if (!initialized) {
        initNAL(env);
    }

    jobject nal;

    while (true) {
        MutexLock lock(nalMutex);
        if (stopped) {
            return NULL;
        }
        if (nalList.size() != 0) {
            nal = nalList.front();
            jobject tmp = nal;
            nal = env->NewLocalRef(nal);
            env->DeleteGlobalRef(tmp);
            nalList.pop_front();
            break;
        }
        nalMutex.CondWait(&cond_nonzero);
    }

    return nal;
}

jobject getNal(JNIEnv *env) {
    if (!initialized) {
        initNAL(env);
    }

    jobject nal;
    {
        MutexLock lock(nalMutex);
        if (nalList.size() == 0) {
            return NULL;
        }
        nal = nalList.front();
        jobject tmp = nal;
        nal = env->NewLocalRef(nal);
        env->DeleteGlobalRef(tmp);
        nalList.pop_front();
    }

    return nal;
}

void recycleNal(JNIEnv *env, jobject nal) {
    if (!initialized) {
        initNAL(env);
    }

    {
        MutexLock lock(nalMutex);
        nalRecycleList.push_front(env->NewGlobalRef(nal));
    }
}

int getNalListSize(JNIEnv *env) {
    if (!initialized) {
        initNAL(env);
    }
    MutexLock lock(nalMutex);
    return nalList.size();
}


void flushNalList(JNIEnv *env) {
    if (!initialized) {
        initNAL(env);
    }
    MutexLock lock(nalMutex);
    clearNalList(env);
}

void notifyNALWaitingThread(JNIEnv *env) {
    MutexLock lock(nalMutex);
    clearNalList(env);

    stopped = true;

    pthread_cond_broadcast(&cond_nonzero);
}

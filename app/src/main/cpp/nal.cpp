/// H.264 NAL Parser functions
// Extract NAL Units from packet by UDP/SRT socket.
////////////////////////////////////////////////////////////////////

#include <string>
#include <stdlib.h>
#include <android/log.h>
#include <pthread.h>
#include "nal.h"
#include "packet_types.h"

static const int MAXIMUM_NAL_BUFFER = 10;
static const int MAXIMUM_NAL_OBJECT = 20;

static const int NAL_TYPE_SPS = 7;

static const int H265_NAL_TYPE_VPS = 32;


NALParser::NALParser(JNIEnv *env) {
    LOGE("NALParser initialized %p", this);
    pthread_cond_init(&m_cond_nonzero, NULL);

    m_env = env;
    m_stopped = false;

    jclass NAL_clazz = env->FindClass("com/polygraphene/alvr/NAL");
    jmethodID NAL_ctor = env->GetMethodID(NAL_clazz, "<init>", "()V");

    NAL_length = env->GetFieldID(NAL_clazz, "length", "I");
    NAL_frameIndex = env->GetFieldID(NAL_clazz, "frameIndex", "J");
    NAL_buf = env->GetFieldID(NAL_clazz, "buf", "[B");

    for (int i = 0; i < MAXIMUM_NAL_OBJECT; i++) {
        jobject nal = env->NewObject(NAL_clazz, NAL_ctor);
        jobject tmp = nal;
        nal = env->NewGlobalRef(nal);
        env->DeleteLocalRef(tmp);

        m_nalRecycleList.push_back(nal);
    }
}

NALParser::~NALParser() {
    notifyWaitingThread(m_env);
    clearNalList(m_env);

    for (auto nal : m_nalRecycleList) {
        m_env->DeleteGlobalRef(nal);
    }
    m_nalRecycleList.clear();
    m_stopped = true;
    pthread_cond_destroy(&m_cond_nonzero);
}

void NALParser::setCodec(int codec) {
    m_codec = codec;
}

bool NALParser::processPacket(VideoFrame *packet, int packetSize, bool &fecFailure) {
    m_queue.addVideoPacket(packet, packetSize, fecFailure);

    bool result = m_queue.reconstruct();
    if (result) {
        // Reconstructed
        const char *frameBuffer = m_queue.getFrameBuffer();
        int frameByteSize = m_queue.getFrameByteSize();

        int NALType;
        if (m_codec == ALVR_CODEC_H264) {
            NALType = frameBuffer[4] & 0x1F;
        } else {
            NALType = (frameBuffer[4] >> 1) & 0x3F;
        }

        if ((m_codec == ALVR_CODEC_H264 && NALType == NAL_TYPE_SPS) ||
                (m_codec == ALVR_CODEC_H265 && NALType == H265_NAL_TYPE_VPS)) {
            // This frame contains (VPS + )SPS + PPS + IDR on NVENC H.264 (H.265) stream.
            // (VPS + )SPS + PPS has short size (8bytes + 28bytes in some environment), so we can assume SPS + PPS is contained in first fragment.

            int end = findVPSSPS(frameBuffer, frameByteSize);
            if (end == -1) {
                // Invalid frame.
                LOG("Got invalid frame. Too large SPS or PPS?");
                return false;
            }
            LOGI("Got frame=%d %d, Codec=%d", NALType, end, m_codec);
            push(&frameBuffer[0], end, packet->frameIndex);
            push(&frameBuffer[end], frameByteSize - end, packet->frameIndex);

            m_queue.clearFecFailure();
        } else {
            push(&frameBuffer[0], frameByteSize, packet->frameIndex);
        }
        return true;
    }
    return false;
}

jobject NALParser::wait(JNIEnv *env) {
    jobject nal;

    while (true) {
        MutexLock lock(m_nalMutex);
        if (m_stopped) {
            return NULL;
        }
        if (m_nalList.size() != 0) {
            nal = m_nalList.front();
            jobject tmp = nal;
            nal = env->NewLocalRef(nal);
            env->DeleteGlobalRef(tmp);
            m_nalList.pop_front();
            break;
        }
        m_nalMutex.CondWait(&m_cond_nonzero);
    }

    return nal;
}

jobject NALParser::get(JNIEnv *env) {
    jobject nal;
    {
        MutexLock lock(m_nalMutex);
        if (m_nalList.size() == 0) {
            return NULL;
        }
        nal = m_nalList.front();
        jobject tmp = nal;
        nal = env->NewLocalRef(nal);
        env->DeleteGlobalRef(tmp);
        m_nalList.pop_front();
    }

    return nal;
}

void NALParser::recycle(JNIEnv *env, jobject nal) {

    MutexLock lock(m_nalMutex);
    m_nalRecycleList.push_front(env->NewGlobalRef(nal));

}

int NALParser::getQueueSize(JNIEnv *env) {
    MutexLock lock(m_nalMutex);
    return m_nalList.size();
}


void NALParser::flush(JNIEnv *env) {
    MutexLock lock(m_nalMutex);
    clearNalList(env);
}

void NALParser::notifyWaitingThread(JNIEnv *env) {
    MutexLock lock(m_nalMutex);
    clearNalList(env);

    m_stopped = true;

    pthread_cond_broadcast(&m_cond_nonzero);
}

void NALParser::clearStopped() {
    m_stopped = false;
}

void NALParser::clearNalList(JNIEnv *env) {
    for (auto it = m_nalList.begin();
         it != m_nalList.end(); ++it) {
        m_nalRecycleList.push_back(*it);
    }
    m_nalList.clear();
}

void NALParser::push(const char *buffer, int length, uint64_t frameIndex) {
    jobject nal;
    jbyteArray buf;
    {
        MutexLock lock(m_nalMutex);

        if (m_nalRecycleList.size() == 0) {
            LOGE("NAL Queue is full (nalRecycleList is empty).");
            return;
        }
        nal = m_nalRecycleList.front();
        m_nalRecycleList.pop_front();
    }

    buf = (jbyteArray) m_env->GetObjectField(nal, NAL_buf);
    if (buf == NULL) {
        buf = m_env->NewByteArray(length);
        m_env->SetObjectField(nal, NAL_buf, buf);
    } else {
        if (m_env->GetArrayLength(buf) < length) {
            // Expand array
            m_env->DeleteLocalRef(buf);
            buf = m_env->NewByteArray(length);
            m_env->SetObjectField(nal, NAL_buf, buf);
        }
    }

    m_env->SetIntField(nal, NAL_length, length);
    m_env->SetLongField(nal, NAL_frameIndex, frameIndex);

    char *cbuf = (char *) m_env->GetByteArrayElements(buf, NULL);

    memcpy(cbuf, buffer, length);
    m_env->ReleaseByteArrayElements(buf, (jbyte *) cbuf, 0);
    m_env->DeleteLocalRef(buf);

    pushNal(nal);
}

void NALParser::pushNal(jobject nal) {
    MutexLock lock(m_nalMutex);

    if (m_nalList.size() < MAXIMUM_NAL_BUFFER) {
        m_nalList.push_back(nal);

        pthread_cond_broadcast(&m_cond_nonzero);
    } else {
        // Discard buffer
        LOG("NAL Queue is too large. Discard. Size=%lu Limit=%d", m_nalList.size(),
            MAXIMUM_NAL_BUFFER);
        m_nalRecycleList.push_front(nal);
    }
}

bool NALParser::fecFailure() {
    return m_queue.fecFailure();
}

int NALParser::findVPSSPS(const char *frameBuffer, int frameByteSize) {
    int zeroes = 0;
    int foundNals = 0;
    for (int i = 0; i < frameByteSize; i++) {
        if (frameBuffer[i] == 0) {
            zeroes++;
        } else if (frameBuffer[i] == 1) {
            if (zeroes == 3) {
                foundNals++;
                if (m_codec == ALVR_CODEC_H264 && foundNals >= 3) {
                    // Find end of SPS+PPS on H.264.
                    return i - 3;
                } else if (m_codec == ALVR_CODEC_H265 && foundNals >= 4) {
                    // Find end of VPS+SPS+PPS on H.264.
                    return i - 3;
                }
            }
            zeroes = 0;
        } else {
            zeroes = 0;
        }
    }
    return -1;
}
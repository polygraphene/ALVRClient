/// H.264 NAL Parser functions
// Extract NAL Units from packet by UDP/SRT socket.
////////////////////////////////////////////////////////////////////

#include <string>
#include <stdlib.h>
#include <android/log.h>
#include <pthread.h>
#include "nal.h"
#include "packet_types.h"

static const int NAL_TYPE_SPS = 7;

static const int H265_NAL_TYPE_VPS = 32;


NALParser::NALParser(JNIEnv *env, jobject udpManager, UdpManager *udpManager_C) : m_queue(udpManager_C) {
    LOGE("NALParser initialized %p", this);

    m_env = env;
    mUdpManager = env->NewGlobalRef(udpManager);

    jclass NAL_clazz = env->FindClass("com/polygraphene/alvr/NAL");
    NAL_length = env->GetFieldID(NAL_clazz, "length", "I");
    NAL_frameIndex = env->GetFieldID(NAL_clazz, "frameIndex", "J");
    NAL_buf = env->GetFieldID(NAL_clazz, "buf", "[B");
    env->DeleteLocalRef(NAL_clazz);

    jclass udpManagerClazz = env->FindClass("com/polygraphene/alvr/UdpReceiverThread");
    mObtainNALMethodID = env->GetMethodID(udpManagerClazz, "obtainNAL", "(I)Lcom/polygraphene/alvr/NAL;");
    mPushNALMethodID = env->GetMethodID(udpManagerClazz, "pushNAL", "(Lcom/polygraphene/alvr/NAL;)V");
    env->DeleteLocalRef(udpManagerClazz);
}

NALParser::~NALParser() {
    m_env->DeleteGlobalRef(mUdpManager);
}

void NALParser::reset() {
    m_queue.reset();
}

void NALParser::setCodec(int codec) {
    m_codec = codec;
}

bool NALParser::processPacket(VideoFrame *packet, int packetSize) {
    m_queue.addVideoPacket(packet, packetSize);

    bool result = m_queue.reconstruct();
    if (!result) {
        return false;
    }

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
        push(&frameBuffer[0], end, packet->trackingFrameIndex);
        push(&frameBuffer[end], frameByteSize - end, packet->trackingFrameIndex);
    } else {
        push(&frameBuffer[0], frameByteSize, packet->trackingFrameIndex);
    }
    return true;
}

void NALParser::push(const char *buffer, int length, uint64_t frameIndex) {
    jobject nal;
    jbyteArray buf;

    nal = m_env->CallObjectMethod(mUdpManager, mObtainNALMethodID, static_cast<jint>(length));
    if (nal == nullptr) {
        LOGE("NAL Queue is full.");
        return;
    }

    m_env->SetIntField(nal, NAL_length, length);
    m_env->SetLongField(nal, NAL_frameIndex, frameIndex);

    buf = (jbyteArray) m_env->GetObjectField(nal, NAL_buf);
    char *cbuf = (char *) m_env->GetByteArrayElements(buf, NULL);

    memcpy(cbuf, buffer, length);
    m_env->ReleaseByteArrayElements(buf, (jbyte *) cbuf, 0);
    m_env->DeleteLocalRef(buf);

    m_env->CallVoidMethod(mUdpManager, mPushNALMethodID, nal);

    m_env->DeleteLocalRef(nal);
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

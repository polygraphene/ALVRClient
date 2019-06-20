#include <vector>
#include <algorithm>
#include <stdlib.h>
#include <inttypes.h>
#include "fec.h"
#include "packet_types.h"
#include "utils.h"
#include "udp.h"
#include "latency_collector.h"

bool FECQueue::reed_solomon_initialized = false;

FECQueue::FECQueue(UdpManager *udpManager) : mUdpManager(udpManager) {
    reset();

    if (!reed_solomon_initialized) {
        reed_solomon_init();
        reed_solomon_initialized = true;
    }
}

FECQueue::~FECQueue() {
    if (m_rs != nullptr) {
        reed_solomon_release(m_rs);
    }
}

void FECQueue::reset() {
    LOG("FECQueue: Reset.");
    m_currentFrame.videoFrameIndex = UINT64_MAX;
    m_recovered = true;

    mLastSuccessfulVideoFrame = -1;
    mIDRProcessed = false;
}

// Add packet to queue. packet must point to buffer whose size=ALVR_MAX_PACKET_SIZE.
void FECQueue::addVideoPacket(const VideoFrame *packet, int packetSize) {
    if (m_recovered && m_currentFrame.videoFrameIndex == packet->videoFrameIndex) {
        // Ignore unused parity packets.
        return;
    }
    //
    // Check new frame.
    //
    if (m_currentFrame.videoFrameIndex != packet->videoFrameIndex) {
        if (!m_recovered) {
            frameLost(packet->videoFrameIndex, false);
        }
        if(m_currentFrame.videoFrameIndex != UINT64_MAX && m_currentFrame.videoFrameIndex + 1 != packet->videoFrameIndex) {
            frameLost(packet->videoFrameIndex, true);
        }
        // Prepare FEC related variables.
        newFrame(packet);
    }
    //
    // Process current packet.
    //

    size_t shardIndex = packet->fecIndex / m_shardPackets;
    size_t packetIndex = packet->fecIndex % m_shardPackets;
    if (m_marks[packetIndex][shardIndex] == 0) {
        // Duplicate packet.
        LOGI("Packet duplication. packetCounter=%d fecIndex=%d", packet->packetCounter,
             packet->fecIndex);
        return;
    }
    LOG("[FEC]. videoFrameIndex=%" PRId64 " packetCounter=%d fecIndex=%d shardIndex=%zu packetIndex=%zu shardPackets=%zu", packet->videoFrameIndex, packet->packetCounter,
         packet->fecIndex, shardIndex, packetIndex, m_shardPackets);
    m_marks[packetIndex][shardIndex] = 0;
    if (shardIndex < m_totalDataShards) {
        m_receivedDataShards[packetIndex]++;
    } else {
        m_receivedParityShards[packetIndex]++;
    }

    //
    // Copy packet buffer.
    //

    char *p = &m_frameBuffer[packet->fecIndex * ALVR_MAX_VIDEO_BUFFER_SIZE];
    char *payload = ((char *) packet) + sizeof(VideoFrame);
    int payloadSize = packetSize - sizeof(VideoFrame);
    memcpy(p, payload, payloadSize);
    if (payloadSize != ALVR_MAX_VIDEO_BUFFER_SIZE) {
        // Fill padding
        memset(p + payloadSize, 0, ALVR_MAX_VIDEO_BUFFER_SIZE - payloadSize);
    }
}

bool FECQueue::reconstruct() {
    if (m_recovered) {
        return false;
    }

    bool ret = true;
    // On server side, we encoded all buffer in one call of reed_solomon_encode.
    // But client side, we should split shards for more resilient recovery.
    for (int packet = 0; packet < m_shardPackets; packet++) {
        if (m_recoveredPacket[packet]) {
            continue;
        }
        if (m_receivedDataShards[packet] == m_totalDataShards) {
            // We've received a full packet with no need for FEC.
            //FrameLog(m_currentFrame.frameIndex, "No need for FEC. packetIndex=%d", packet);
            m_recoveredPacket[packet] = true;
            continue;
        }
        m_rs->shards = m_receivedDataShards[packet] +
                       m_receivedParityShards[packet]; //Don't let RS complain about missing parity packets

        if (m_rs->shards < m_totalDataShards) {
            // Not enough parity data
            ret = false;
            continue;
        }

        FrameLog(m_currentFrame.trackingFrameIndex,
                 "[FEC] Recovering. packetIndex=%d receivedDataShards=%d/%d receivedParityShards=%d/%d",
                 packet, m_receivedDataShards[packet], m_totalDataShards,
                 m_receivedParityShards[packet], m_totalParityShards);

        for (int i = 0; i < m_totalShards; i++) {
            m_shards[i] = &m_frameBuffer[(i * m_shardPackets + packet) *
                                         ALVR_MAX_VIDEO_BUFFER_SIZE];
        }

        int result = reed_solomon_reconstruct(m_rs, (unsigned char **) &m_shards[0],
                                              &m_marks[packet][0],
                                              m_totalShards, ALVR_MAX_VIDEO_BUFFER_SIZE);
        m_recoveredPacket[packet] = true;
        // We should always provide enough parity to recover the missing data successfully.
        // If this fails, something is probably wrong with our FEC state.
        if (result != 0) {
            LOGE("reed_solomon_reconstruct failed.");
            return false;
        }
        /*
        for(int i = 0; i < m_totalShards * m_shardPackets; i++) {
            char *p = &frameBuffer[ALVR_MAX_VIDEO_BUFFER_SIZE * i];
            LOGI("Reconstructed packets. i=%d shardIndex=%d buffer=[%02X %02X %02X %02X %02X ...]", i, i / m_shardPackets, p[0], p[1], p[2], p[3], p[4]);
        }*/
    }
    if (ret) {
        m_recovered = true;
        bool isIDR = !mIDRProcessed;
        mUdpManager->sendVideoFrameAck(true, isIDR,
                                       m_currentFrame.videoFrameIndex, m_currentFrame.videoFrameIndex);
        mLastSuccessfulVideoFrame = m_currentFrame.videoFrameIndex;
        FrameLog(m_currentFrame.trackingFrameIndex, "[FEC] Frame was successfully recovered by FEC. VideoFrameIndex=%llu", m_currentFrame.videoFrameIndex);
    }
    return ret;
}

const char *FECQueue::getFrameBuffer() {
    return &m_frameBuffer[0];
}

int FECQueue::getFrameByteSize() {
    return m_currentFrame.frameByteSize;
}

void FECQueue::OnIDRProcessed() {
    mIDRProcessed = true;
}

void FECQueue::frameLost(uint64_t currentVideoFrame, bool wholeLost) {
    FrameLog(m_currentFrame.trackingFrameIndex,
             "[FEC] Frame cannot be recovered. videoFrame=%llu(%d bytes) shards=%u:%u frameByteSize=%d"
             " fecPercentage=%d m_totalShards=%u m_shardPackets=%u m_blockSize=%u",
             m_currentFrame.videoFrameIndex, m_currentFrame.frameByteSize,
             m_totalDataShards, m_totalParityShards,
             m_currentFrame.fecPercentage, m_totalShards,
             m_shardPackets, m_blockSize);
    for (int packet = 0; packet < m_shardPackets; packet++) {
        FrameLog(m_currentFrame.trackingFrameIndex,
                 "packetIndex=%d/%d, shards=%u:%u(%u/%u) Okay=%d",
                 packet, m_shardPackets, m_receivedDataShards[packet], m_receivedParityShards[packet],
                 m_receivedDataShards[packet] + m_receivedParityShards[packet],
                 m_totalShards, m_receivedDataShards[packet] + m_receivedParityShards[packet] >= m_totalShards);
    }

    LatencyCollector::Instance().fecFailure();

    bool isIDR = !mIDRProcessed;
    mUdpManager->sendVideoFrameAck(false, isIDR,
                                   static_cast<uint64_t>(mLastSuccessfulVideoFrame + 1), currentVideoFrame - 1);
    LOG("[FEC] VideoFrameFailed (%s lost): %" PRId64 " - %" PRId64 " IDR=%d Previous=%" PRId64 " Current=%" PRId64,
        wholeLost ? "Whole" : "Partial", mLastSuccessfulVideoFrame + 1, currentVideoFrame - 1, isIDR, currentVideoFrame,
        m_currentFrame.videoFrameIndex);

    mLastSuccessfulVideoFrame = currentVideoFrame - 1;
}

void FECQueue::newFrame(const VideoFrame *packet) {
    m_currentFrame = *packet;
    m_recovered = false;
    if (m_rs != nullptr) {
        reed_solomon_release(m_rs);
    }

    uint32_t fecDataPackets = (packet->frameByteSize + ALVR_MAX_VIDEO_BUFFER_SIZE - 1) /
                              ALVR_MAX_VIDEO_BUFFER_SIZE;
    m_shardPackets = static_cast<size_t>(CalculateFECShardPackets(m_currentFrame.frameByteSize,
                                                                  m_currentFrame.fecPercentage));
    m_blockSize = m_shardPackets * ALVR_MAX_VIDEO_BUFFER_SIZE;

    m_totalDataShards = (m_currentFrame.frameByteSize + m_blockSize - 1) / m_blockSize;
    m_totalParityShards = static_cast<size_t>(CalculateParityShards(m_totalDataShards,
                                                                    m_currentFrame.fecPercentage));
    m_totalShards = m_totalDataShards + m_totalParityShards;

    m_recoveredPacket.clear();
    m_recoveredPacket.resize(m_shardPackets);

    m_receivedDataShards.clear();
    m_receivedDataShards.resize(m_shardPackets);
    m_receivedParityShards.clear();
    m_receivedParityShards.resize(m_shardPackets);

    m_shards.resize(m_totalShards);

    m_rs = reed_solomon_new(static_cast<int>(m_totalDataShards),
                            static_cast<int>(m_totalParityShards));
    if (m_rs == nullptr) {
        return;
    }

    m_marks.resize(m_shardPackets);
    for (int i = 0; i < m_shardPackets; i++) {
        m_marks[i].resize(m_totalShards);
        memset(&m_marks[i][0], 1, m_totalShards);
    }

    if (m_frameBuffer.size() < m_totalShards * m_blockSize) {
        // Only expand buffer for performance reason.
        m_frameBuffer.resize(m_totalShards * m_blockSize);
    }
    memset(&m_frameBuffer[0], 0, m_totalShards * m_blockSize);

    // Padding packets are not sent, so we can fill bitmap by default.
    size_t padding = (m_shardPackets - fecDataPackets % m_shardPackets) % m_shardPackets;
    for (size_t i = 0; i < padding; i++) {
        m_marks[m_shardPackets - i - 1][m_totalDataShards - 1] = 0;
        m_receivedDataShards[m_shardPackets - i - 1]++;
    }

    FrameLog(m_currentFrame.trackingFrameIndex,
             "Start new frame. videoFrame=%llu frameByteSize=%d fecPercentage=%d m_totalDataShards=%u m_totalParityShards=%u"
             " m_totalShards=%u m_shardPackets=%u m_blockSize=%u",
             m_currentFrame.videoFrameIndex, m_currentFrame.frameByteSize, m_currentFrame.fecPercentage, m_totalDataShards,
             m_totalParityShards, m_totalShards, m_shardPackets, m_blockSize);
}

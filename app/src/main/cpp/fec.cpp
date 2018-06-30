#include <vector>
#include <algorithm>
#include <stdlib.h>
#include "fec.h"
#include "packet_types.h"
#include "utils.h"

FECQueue::FECQueue() {
    m_currentFrame.frameIndex = UINT64_MAX;
    m_recovered = false;
    reed_solomon_init();
}

FECQueue::~FECQueue() {
}

// Add packet to queue. packet must point to buffer whose size=ALVR_MAX_PACKET_SIZE.
void FECQueue::addVideoPacket(const VideoFrame *packet, int packetSize) {
    if (m_recovered && m_currentFrame.frameIndex == packet->frameIndex) {
        return;
    }
    if (m_currentFrame.frameIndex != packet->frameIndex) {
        // New frame
        if (m_queue.size() > 0) {
            FrameLog(m_currentFrame.frameIndex, "Previous frame cannot be recovered. dataShards=%u/%u parityShards=%u/%u frameByteSize=%d fecPercentage=%d m_totalShards=%u m_shardPackets=%u m_blockSize=%u",
                 m_receivedDataShards, m_totalDataShards, m_receivedParityShards,
                 m_totalParityShards,
                 m_currentFrame.frameByteSize, m_currentFrame.fecPercentage, m_totalShards,
                 m_shardPackets, m_blockSize);
        }
        clear();
        m_currentFrame = *packet;
        m_recovered = false;

        m_receivedDataShards = 0;
        m_receivedParityShards = 0;

        m_fecDataPackets = packet->frameByteSize / ALVR_MAX_VIDEO_BUFFER_SIZE;
        m_shardPackets = CalculateFECShardPackets(m_currentFrame.frameByteSize,
                                                  m_currentFrame.fecPercentage);
        m_blockSize = m_shardPackets * ALVR_MAX_VIDEO_BUFFER_SIZE;

        m_totalDataShards = (m_currentFrame.frameByteSize + m_blockSize - 1) / m_blockSize;
        m_totalParityShards = CalculateParityShards(m_totalDataShards,
                                                    m_currentFrame.fecPercentage);
        m_totalShards = m_totalDataShards + m_totalParityShards;

        m_packetBitmap.clear();
        m_packetBitmap.resize(m_shardPackets * m_totalShards);
        m_marks.resize(m_totalShards);
        memset(&m_marks[0], 1, m_totalShards);

        // Padding packets are not sent, so we can fill bitmap by default.
        size_t padding = (m_shardPackets - m_fecDataPackets % m_shardPackets) % m_shardPackets;
        for (size_t i = 0; i < padding; i++) {
            m_packetBitmap[m_shardPackets * m_totalDataShards - i - 1] = true;
        }

        FrameLog(m_currentFrame.frameIndex,
                 "Start new frame. frameByteSize=%d fecPercentage=%d m_totalDataShards=%u m_totalParityShards=%u m_totalShards=%u m_shardPackets=%u m_blockSize=%u",
                 m_currentFrame.frameByteSize, m_currentFrame.fecPercentage, m_totalDataShards,
                 m_totalParityShards, m_totalShards, m_shardPackets, m_blockSize);
    }
    size_t shardIndex = packet->fecIndex / m_shardPackets;
    m_packetBitmap[packet->fecIndex] = true;
    bool filled = true;
    for (int i = 0; i < m_shardPackets; i++) {
        filled = filled && m_packetBitmap[shardIndex * m_shardPackets + i];
    }
    if (filled) {
        m_marks[shardIndex] = 0;
        if (shardIndex < m_totalDataShards) {
            m_receivedDataShards++;
        } else {
            m_receivedParityShards++;
        }
    }

    VideoFrame *newPacket = (VideoFrame *) new char[ALVR_MAX_PACKET_SIZE];
    memcpy(newPacket, packet, packetSize);
    memset(((char *) newPacket) + packetSize, 0, ALVR_MAX_PACKET_SIZE - packetSize);

    m_queue.push_back(newPacket);
}

bool FECQueue::reconstruct(std::vector<char> &frameBuffer) {
    if (m_receivedDataShards + m_receivedParityShards < m_totalDataShards) {
        // Not enough parity data
        return false;
    }

    if (m_recovered) {
        return false;
    }

    FrameLog(m_currentFrame.frameIndex, "Reconstruct frame. dataShards=%u/%u parityShards=%u/%u",
             m_receivedDataShards,
             m_totalDataShards, m_receivedParityShards, m_totalParityShards);

    m_recovered = true;
    if (m_receivedDataShards == m_totalDataShards) {
        // We've received a full packet with no need for FEC.
        FrameLog(m_currentFrame.frameIndex, "All data shards has alived.");
        frameBuffer.resize(m_currentFrame.frameByteSize);
        for (auto queuePacket : m_queue) {
            int pos = queuePacket->fecIndex * ALVR_MAX_VIDEO_BUFFER_SIZE;
            size_t copyLength = std::min((size_t) ALVR_MAX_VIDEO_BUFFER_SIZE,
                                         (size_t) (m_currentFrame.frameByteSize - pos));
            //LOGI("Copying packets. index=%d copyLength=%d pos=%d size=%d", queuePacket->fecIndex, copyLength, pos, m_currentFrame.frameByteSize);
            memcpy(&frameBuffer[pos],
                   ((char *) queuePacket) + sizeof(VideoFrame), copyLength);
        }
        clear();
        return true;
    }

    reed_solomon *rs = reed_solomon_new(m_totalDataShards, m_totalParityShards);
    if (rs == NULL) {
        return false;
    }

    rs->shards = m_receivedDataShards +
                 m_receivedParityShards; //Don't let RS complain about missing parity packets

    std::vector<char *> shards((size_t) m_totalShards);

    frameBuffer.clear();
    frameBuffer.resize((size_t) (m_blockSize * m_totalShards));

    for (int i = 0; i < m_totalShards; i++) {
        shards[i] = &frameBuffer[i * m_blockSize];
    }

    for (auto queuePacket : m_queue) {
        int shardIndex = queuePacket->fecIndex / m_shardPackets;
        if (m_marks[shardIndex] == 0) {
            char *p = shards[shardIndex] + queuePacket->fecIndex % m_shardPackets;
            memcpy(p,
                   ((char *) queuePacket) + sizeof(VideoFrame), ALVR_MAX_VIDEO_BUFFER_SIZE);
        }
    }
    clear();

    int ret = reed_solomon_reconstruct(rs, (unsigned char **) &shards[0], &m_marks[0],
                                       m_totalShards, m_blockSize);
    reed_solomon_release(rs);

    // We should always provide enough parity to recover the missing data successfully.
    // If this fails, something is probably wrong with our FEC state.
    if (ret != 0) {
        LOGE("reed_solomon_reconstruct failed.");
        frameBuffer.clear();
        return false;
    }
    FrameLog(m_currentFrame.frameIndex, "Frame was successfully recovered by FEC.");

    frameBuffer.resize(m_currentFrame.frameByteSize);

    return ret == 0;
}

void FECQueue::clear() {
    for (auto queuePacket : m_queue) {
        delete[] (char *) queuePacket;
    }
    m_queue.clear();
}
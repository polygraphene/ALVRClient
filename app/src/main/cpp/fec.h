#ifndef ALVRCLIENT_FEC_H
#define ALVRCLIENT_FEC_H

#include <list>
#include <vector>
#include "packet_types.h"
#include "reedsolomon/rs.h"

class FECQueue {
public:
    FECQueue();
    ~FECQueue();

    // Add packet to queue. packet must point to buffer whose size=ALVR_MAX_PACKET_SIZE.
    void addVideoPacket(const VideoFrame *packet, int packetSize);
    bool reconstruct(std::vector<char> &frameBuffer);

private:
    void clear();

    std::list<const VideoFrame *> m_queue;
    VideoFrame m_currentFrame;
    size_t m_shardPackets;
    size_t m_blockSize;
    size_t m_totalDataShards;
    size_t m_totalParityShards;
    size_t m_totalShards;
    std::vector<bool> m_packetBitmap;
    std::vector<unsigned char> m_marks;
    uint32_t m_fecDataPackets;
    uint32_t m_receivedDataShards;
    uint32_t m_receivedParityShards;
    bool m_recovered;
};

#endif //ALVRCLIENT_FEC_H

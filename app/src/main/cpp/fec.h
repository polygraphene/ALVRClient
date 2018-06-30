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

    void addVideoPacket(const VideoFrame *packet, int packetSize, bool &fecFailure);
    bool reconstruct(std::vector<char> &frameBuffer);

    bool fecFailure();
    void clearFecFailure();
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
    uint32_t m_receivedDataShards;
    uint32_t m_receivedParityShards;
    bool m_recovered;
    bool m_fecFailure;

    static bool reed_solomon_initialized;
};

#endif //ALVRCLIENT_FEC_H

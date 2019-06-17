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
    bool reconstruct();
    const char *getFrameBuffer();
    int getFrameByteSize();

    bool fecFailure(uint64_t *startOfFailedFrame, uint64_t *endOfFailedFrame);
    void clearFecFailure();
private:

    VideoFrame m_currentFrame;
    size_t m_shardPackets;
    size_t m_blockSize;
    size_t m_totalDataShards;
    size_t m_totalParityShards;
    size_t m_totalShards;
    uint32_t m_firstPacketOfNextFrame = 0;
    std::vector<std::vector<unsigned char>> m_marks;
    std::vector<char> m_frameBuffer;
    std::vector<uint32_t> m_receivedDataShards;
    std::vector<uint32_t> m_receivedParityShards;
    std::vector<bool> m_recoveredPacket;
    std::vector<char *> m_shards;
    bool m_recovered;
    bool m_fecFailure;
    reed_solomon *m_rs = NULL;
    uint64_t mLastSuccessfulVideoFrame = 0;
    uint64_t mStartOfFailedVideoFrame = 0;
    uint64_t mEndOfFailedVideoFrame = 0;

    static bool reed_solomon_initialized;
};

#endif //ALVRCLIENT_FEC_H

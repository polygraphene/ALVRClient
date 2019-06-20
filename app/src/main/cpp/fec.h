#ifndef ALVRCLIENT_FEC_H
#define ALVRCLIENT_FEC_H

#include <list>
#include <vector>
#include "packet_types.h"
#include "reedsolomon/rs.h"

class UdpManager;

class FECQueue {
public:
    FECQueue(UdpManager *udpManager);
    ~FECQueue();

    void reset();

    void addVideoPacket(const VideoFrame *packet, int packetSize);
    bool reconstruct();
    const char *getFrameBuffer();
    int getFrameByteSize();

    void OnIDRProcessed();
private:
    UdpManager *mUdpManager;

    VideoFrame m_currentFrame;
    size_t m_shardPackets;
    size_t m_blockSize;
    size_t m_totalDataShards;
    size_t m_totalParityShards;
    size_t m_totalShards;
    std::vector<std::vector<unsigned char>> m_marks;
    std::vector<char> m_frameBuffer;
    std::vector<uint32_t> m_receivedDataShards;
    std::vector<uint32_t> m_receivedParityShards;
    std::vector<bool> m_recoveredPacket;
    std::vector<char *> m_shards;
    bool m_recovered;
    reed_solomon *m_rs = nullptr;
    int64_t mLastSuccessfulVideoFrame;
    bool mIDRProcessed;

    static bool reed_solomon_initialized;

    void newFrame(const VideoFrame *packet);
    void frameLost(uint64_t currentVideoFrame, bool wholeLost);
};

#endif //ALVRCLIENT_FEC_H

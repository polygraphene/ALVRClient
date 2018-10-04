#ifndef ALVRCLIENT_UDP_H
#define ALVRCLIENT_UDP_H

#include <functional>
#include <list>
#include <string>
#include <memory>
#include <jni.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "packet_types.h"
#include "nal.h"
#include "sound.h"

// Maximum UDP packet size
static const int MAX_PACKET_SIZE = 2000;

class Socket {
public:
    Socket();
    ~Socket();

    void initialize(JNIEnv *env, int port,
                    jobjectArray broadcastAddrList_);

    int send(const void *buf, size_t len);
    void sendBroadcast(const void *buf, size_t len);
    void recv();

    void recoverConnection(std::string serverAddress, int serverPort);

    void disconnect();

    //
    // Callback
    //

    void setOnConnect(std::function<void(const ConnectionMessage &connectionMessage)> onConnect) {
        m_onConnect = onConnect;
    }
    void setOnBroadcastRequest(std::function<void()> onBroadcastRequest) {
        m_onBroadcastRequest = onBroadcastRequest;
    }
    void setOnPacketRecv(std::function<void(const char *buf, size_t len)> onPacketRecv) {
        m_onPacketRecv = onPacketRecv;
    }

    //
    // Getter
    //

    bool isConnected() {
        return m_connected;
    }
    jstring getServerAddress(JNIEnv *env);
    int getServerPort();
    int getSocket();
private:
    int m_sock = -1;
    bool m_connected = false;

    bool m_hasServerAddress = false;
    sockaddr_in m_serverAddr = {};

    std::list<sockaddr_in> m_broadcastAddrList;

    std::function<void(const ConnectionMessage &connectionMessage)> m_onConnect;
    std::function<void()> m_onBroadcastRequest;
    std::function<void(const char *buf, size_t len)> m_onPacketRecv;

    void parse(char *packet, int packetSize, const sockaddr_in &addr);

    void setBroadcastAddrList(JNIEnv *env, int port, jobjectArray broadcastAddrList_);
};

class UdpManager {
public:
    UdpManager();
    ~UdpManager();
    void initialize(JNIEnv *env, jint port, jstring deviceName_, jobjectArray broadcastAddrList_,
                    jintArray refreshRates_);

    NALParser &getNalParser() {
        return *m_nalParser;
    }

    void send(const void *packet, int length);

    void runLoop(JNIEnv *env, jobject instance, jstring serverAddress, int serverPort);
    void interrupt();
    void notifyWaitingThread(JNIEnv *env);

    bool isConnected();

    jstring getServerAddress(JNIEnv *env);
    int getServerPort();
private:
// Connection has lost when elapsed 3 seconds from last packet.
    static const uint64_t CONNECTION_TIMEOUT = 3 * 1000 * 1000;

    bool m_stopped = false;

    Socket m_socket;
    time_t m_prevSentSync = 0;
    time_t m_prevSentBroadcast = 0;
    int64_t m_timeDiff = 0;
    uint64_t timeSyncSequence = (uint64_t) -1;
    uint64_t m_lastReceived = 0;
    uint64_t m_lastFrameIndex = 0;
    std::string m_deviceName;
    ConnectionMessage m_connectionMessage = {};

    uint32_t m_prevVideoSequence = 0;
    uint32_t m_prevSoundSequence = 0;
    std::shared_ptr<SoundPlayer> m_soundPlayer;
    std::shared_ptr<NALParser> m_nalParser;

    uint8_t m_refreshRates[ALVR_REFRESH_RATE_LIST_SIZE];

    JNIEnv *m_env;
    jobject m_instance;

    //
    // Send buffer
    //
    struct SendBuffer {
        char buf[MAX_PACKET_SIZE];
        int len;
    };

    int m_notifyPipe[2] = {-1, -1};
    Mutex pipeMutex;
    std::list<SendBuffer> m_sendQueue;

    void sendPacketLossReport(ALVR_LOST_FRAME_TYPE frameType, uint32_t fromPacketCounter,
                              uint32_t toPacketCounter);
    void processVideoSequence(uint32_t sequence);
    void processSoundSequence(uint32_t sequence);

    void processReadPipe(int pipefd);

    void sendTimeSync();
    void sendBroadcast();
    void doPeriodicWork();

    void recoverConnection(std::string serverAddress, int serverPort);

    void checkConnection();
    void updateTimeout();

    void onConnect(const ConnectionMessage &connectionMessage);
    void onBroadcastRequest();
    void onPacketRecv(const char *packet, size_t packetSize);

    void loadRefreshRates(JNIEnv *refreshRates, jintArray pArray);
};

#endif //ALVRCLIENT_UDP_H

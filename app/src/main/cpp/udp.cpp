/// UdpReceiverThread jni functions using UDP socket
// Send tracking information and lost packet feedback to server.
// And receive screen video stream.
////////////////////////////////////////////////////////////////////

#include <stdlib.h>
#include <pthread.h>
#include <endian.h>
#include <algorithm>
#include <errno.h>
#include <sys/ioctl.h>
#include "utils.h"
#include "latency_collector.h"
#include "udp.h"
#include "exception.h"

std::shared_ptr<UdpManager> g_udpManager;


Socket::Socket() {
}

Socket::~Socket() {
    if (m_sock >= 0) {
        close(m_sock);
    }
}

void Socket::initialize(JNIEnv *env, int port, jobjectArray broadcastAddrList_) {
    int val;
    socklen_t len;

    m_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (m_sock < 0) {
        throw FormatException("socket error : %d %s", errno, strerror(errno));
    }
    val = 1;
    ioctl(m_sock, FIONBIO, &val);

    val = 1;
    setsockopt(m_sock, SOL_SOCKET, SO_BROADCAST, (char *) &val, sizeof(val));

    //
    // Socket recv buffer
    //

    //setMaxSocketBuffer();
    // 30Mbps 50ms buffer
    getsockopt(m_sock, SOL_SOCKET, SO_RCVBUF, (char *) &val, &len);
    LOGI("Default socket recv buffer is %d bytes", val);

    val = 30 * 1000 * 500 / 8;
    setsockopt(m_sock, SOL_SOCKET, SO_RCVBUF, (char *) &val, sizeof(val));
    len = sizeof(val);
    getsockopt(m_sock, SOL_SOCKET, SO_RCVBUF, (char *) &val, &len);
    LOGI("Current socket recv buffer is %d bytes", val);

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(m_sock, (sockaddr *) &addr, sizeof(addr)) < 0) {
        throw FormatException("bind error : %d %s", errno, strerror(errno));
    }

    //
    // Parse broadcast address list.
    //

    setBroadcastAddrList(env, port, broadcastAddrList_);
}

void Socket::setBroadcastAddrList(JNIEnv *env, int port, jobjectArray broadcastAddrList_) {
    int broadcastCount = env->GetArrayLength(broadcastAddrList_);
    for (int i = 0; i < broadcastCount; i++) {
        jstring address = (jstring) env->GetObjectArrayElement(broadcastAddrList_, i);
        auto addressStr = GetStringFromJNIString(env, address);
        env->DeleteLocalRef(address);

        sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(addr.sin_family, addressStr.c_str(), &addr.sin_addr);

        m_broadcastAddrList.push_back(addr);
    }
}

int Socket::send(const void *buf, size_t len) {
    return sendto(m_sock, buf, len, 0, (sockaddr *) &m_serverAddr,
                  sizeof(m_serverAddr));
}

void Socket::recv() {
    char packet[MAX_PACKET_SIZE];
    sockaddr_in addr;
    socklen_t socklen = sizeof(addr);

    while (true) {
        int packetSize = recvfrom(m_sock, packet, MAX_PACKET_SIZE, 0, (sockaddr *) &addr,
                                  &socklen);
        if (packetSize <= 0) {
            return;
        }
        parse(packet, packetSize, addr);
    }
}

void Socket::disconnect() {
    m_connected = false;
    memset(&m_serverAddr, 0, sizeof(m_serverAddr));
}

jstring Socket::getServerAddress(JNIEnv *env) {
    if (m_hasServerAddress) {
        char serverAddress[100];
        inet_ntop(m_serverAddr.sin_family, &m_serverAddr.sin_addr, serverAddress,
                  sizeof(serverAddress));
        return env->NewStringUTF(serverAddress);
    }
    return NULL;
}

int Socket::getServerPort() {
    if (m_hasServerAddress) {
        return htons(m_serverAddr.sin_port);
    }
    return 0;
}

int Socket::getSocket() {
    return m_sock;
}

void Socket::sendBroadcast(const void *buf, size_t len) {
    for (const sockaddr_in &address : m_broadcastAddrList) {
        sendto(m_sock, buf, len, 0, (sockaddr *) &address,
               sizeof(address));
    }
}

void Socket::parse(char *packet, int packetSize, const sockaddr_in &addr) {
    if (m_connected) {
        if (addr.sin_port != m_serverAddr.sin_port ||
            addr.sin_addr.s_addr != m_serverAddr.sin_addr.s_addr) {
            char str[1000];
            // Invalid source address. Ignore.
            inet_ntop(addr.sin_family, &addr.sin_addr, str, sizeof(str));
            LOGE("Received packet from invalid source address. Address=%s:%d", str,
                 htons(addr.sin_port));
            return;
        }
        m_onPacketRecv(packet, packetSize);
    } else {
        uint32_t type = *(uint32_t *) packet;
        if (type == ALVR_PACKET_TYPE_BROADCAST_REQUEST_MESSAGE) {
            m_onBroadcastRequest();
        } else if (type == ALVR_PACKET_TYPE_CONNECTION_MESSAGE) {
            if (packetSize < sizeof(ConnectionMessage)) {
                return;
            }
            m_serverAddr = addr;
            m_connected = true;
            m_hasServerAddress = true;

            ConnectionMessage *connectionMessage = (ConnectionMessage *) packet;

            if (connectionMessage->version != ALVR_PROTOCOL_VERSION) {
                LOGE("Received connection message which has unsupported version. Received Version=%d Our Version=%d",
                     connectionMessage->version, ALVR_PROTOCOL_VERSION);
                return;
            }

            LOGI("Try setting recv buffer size = %d bytes", connectionMessage->bufferSize);
            int val = connectionMessage->bufferSize;
            setsockopt(m_sock, SOL_SOCKET, SO_RCVBUF, (char *) &val, sizeof(val));
            socklen_t socklen = sizeof(val);
            getsockopt(m_sock, SOL_SOCKET, SO_RCVBUF, (char *) &val, &socklen);
            LOGI("Current socket recv buffer is %d bytes", val);

            m_onConnect(*connectionMessage);

            return;
        }
    }
}

void Socket::recoverConnection(std::string serverAddress, int serverPort) {
    LOGI("Sending recover connection request. server=%s:%d", serverAddress.c_str(), serverPort);
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(serverPort);
    inet_pton(AF_INET, serverAddress.c_str(), &addr.sin_addr);

    RecoverConnection message = {};
    message.type = ALVR_PACKET_TYPE_RECOVER_CONNECTION;

    sendto(m_sock, &message, sizeof(message), 0, (sockaddr *) &addr, sizeof(addr));
}


UdpManager::UdpManager() {
}

UdpManager::~UdpManager() {
    if (m_notifyPipe[0] >= 0) {
        close(m_notifyPipe[0]);
        close(m_notifyPipe[1]);
    }

    m_nalParser.reset();
    m_sendQueue.clear();
}

void
UdpManager::initialize(JNIEnv *env, jint port, jstring deviceName_, jobjectArray broadcastAddrList_,
                       jintArray refreshRates_) {
    //
    // Initialize variables
    //

    m_stopped = false;
    m_lastReceived = 0;
    m_prevSentSync = 0;
    m_prevSentBroadcast = 0;
    m_prevVideoSequence = 0;
    m_prevSoundSequence = 0;
    m_timeDiff = 0;

    loadRefreshRates(env, refreshRates_);

    m_deviceName = GetStringFromJNIString(env, deviceName_);

    m_nalParser = std::make_shared<NALParser>(env);

    //
    // Socket
    //

    m_socket.setOnConnect(std::bind(&UdpManager::onConnect, this, std::placeholders::_1));
    m_socket.setOnBroadcastRequest(std::bind(&UdpManager::onBroadcastRequest, this));
    m_socket.setOnPacketRecv(std::bind(&UdpManager::onPacketRecv, this, std::placeholders::_1,
                                       std::placeholders::_2));
    m_socket.initialize(env, port, broadcastAddrList_);

    //
    // Sound
    //

    m_soundPlayer = std::make_shared<SoundPlayer>();
    if (m_soundPlayer->initialize() != 0) {
        LOGE("Failed on SoundPlayer initialize.");
        m_soundPlayer.reset();
    }
    LOGI("SoundPlayer successfully initialize.");

    //
    // Pipe used for send buffer notification.
    //

    if (pipe2(m_notifyPipe, O_NONBLOCK) < 0) {
        throw FormatException("pipe2 error : %d %s", errno, strerror(errno));
    }

    LOGI("UdpManager initialized.");
}

void UdpManager::sendPacketLossReport(ALVR_LOST_FRAME_TYPE frameType, uint32_t fromPacketCounter,
                                      uint32_t toPacketCounter) {
    PacketErrorReport report;
    report.type = ALVR_PACKET_TYPE_PACKET_ERROR_REPORT;
    report.lostFrameType = frameType;
    report.fromPacketCounter = fromPacketCounter;
    report.toPacketCounter = toPacketCounter;
    int ret = m_socket.send(&report, sizeof(report));
    LOGI("Sent packet loss report. ret=%d", ret);
}

void UdpManager::processVideoSequence(uint32_t sequence) {
    if (m_prevVideoSequence != 0 && m_prevVideoSequence + 1 != sequence) {
        int32_t lost = sequence - (m_prevVideoSequence + 1);
        if (lost < 0) {
            // lost become negative on out-of-order packet.
            // TODO: This is not accurate statistics.
            lost = -lost;
        }
        LatencyCollector::Instance().packetLoss(lost);

        LOGE("VideoPacket loss %d (%d -> %d)", lost, m_prevVideoSequence + 1,
             sequence - 1);
    }
    m_prevVideoSequence = sequence;
}

void UdpManager::processSoundSequence(uint32_t sequence) {
    if (m_prevSoundSequence != 0 && m_prevSoundSequence + 1 != sequence) {
        int32_t lost = sequence - (m_prevSoundSequence + 1);
        if (lost < 0) {
            // lost become negative on out-of-order packet.
            // TODO: This is not accurate statistics.
            lost = -lost;
        }
        LatencyCollector::Instance().packetLoss(lost);

        sendPacketLossReport(ALVR_LOST_FRAME_TYPE_AUDIO, m_prevSoundSequence + 1, sequence - 1);

        LOGE("SoundPacket loss %d (%d -> %d)", lost, m_prevSoundSequence + 1,
             sequence - 1);
    }
    m_prevSoundSequence = sequence;
}

void UdpManager::processReadPipe(int pipefd) {
    char buf[2000];
    int len = 1;

    int ret = read(pipefd, buf, len);
    if (ret <= 0) {
        return;
    }

    SendBuffer sendBuffer;
    while (1) {
        {
            MutexLock lock(pipeMutex);

            if (m_sendQueue.empty()) {
                break;
            } else {
                sendBuffer = m_sendQueue.front();
                m_sendQueue.pop_front();
            }
        }
        if (m_stopped) {
            return;
        }

        //LOG("Sending tracking packet %d", sendBuffer.len);
        m_socket.send(sendBuffer.buf, sendBuffer.len);
    }

    return;
}

void UdpManager::sendTimeSync() {
    time_t current = time(NULL);
    if (m_prevSentSync != current && m_socket.isConnected()) {
        LOGI("Sending timesync.");

        TimeSync timeSync = {};
        timeSync.type = ALVR_PACKET_TYPE_TIME_SYNC;
        timeSync.mode = 0;
        timeSync.clientTime = getTimestampUs();
        timeSync.sequence = ++timeSyncSequence;

        timeSync.packetsLostTotal = LatencyCollector::Instance().getPacketsLostTotal();
        timeSync.packetsLostInSecond = LatencyCollector::Instance().getPacketsLostInSecond();

        timeSync.averageTotalLatency = (uint32_t) LatencyCollector::Instance().getLatency(0, 0);
        timeSync.maxTotalLatency = (uint32_t) LatencyCollector::Instance().getLatency(0, 1);
        timeSync.minTotalLatency = (uint32_t) LatencyCollector::Instance().getLatency(0, 2);

        timeSync.averageTransportLatency = (uint32_t) LatencyCollector::Instance().getLatency(1, 0);
        timeSync.maxTransportLatency = (uint32_t) LatencyCollector::Instance().getLatency(1, 1);
        timeSync.minTransportLatency = (uint32_t) LatencyCollector::Instance().getLatency(1, 2);

        timeSync.averageDecodeLatency = (uint32_t) LatencyCollector::Instance().getLatency(2, 0);
        timeSync.maxDecodeLatency = (uint32_t) LatencyCollector::Instance().getLatency(2, 1);
        timeSync.minDecodeLatency = (uint32_t) LatencyCollector::Instance().getLatency(2, 2);

        timeSync.fecFailure = m_nalParser->fecFailure() ? 1 : 0;
        timeSync.fecFailureTotal = LatencyCollector::Instance().getFecFailureTotal();
        timeSync.fecFailureInSecond = LatencyCollector::Instance().getFecFailureInSecond();

        timeSync.fps = LatencyCollector::Instance().getFramesInSecond();

        m_socket.send(&timeSync, sizeof(timeSync));
    }
    m_prevSentSync = current;
}

void UdpManager::sendBroadcast() {
    time_t current = time(NULL);
    if (m_prevSentBroadcast != current && !m_socket.isConnected()) {
        LOGI("Sending broadcast hello.");

        HelloMessage helloMessage = {};
        helloMessage.type = 1;
        helloMessage.version = ALVR_PROTOCOL_VERSION;
        memcpy(helloMessage.deviceName, m_deviceName.c_str(),
               std::min(m_deviceName.length(), sizeof(helloMessage.deviceName)));
        memcpy(helloMessage.refreshRate, m_refreshRates, sizeof(m_refreshRates));

        m_socket.sendBroadcast(&helloMessage, sizeof(helloMessage));
    }
    m_prevSentBroadcast = current;
}

void UdpManager::doPeriodicWork() {
    sendTimeSync();
    sendBroadcast();
    checkConnection();
}

void UdpManager::recoverConnection(std::string serverAddress, int serverPort) {
    m_socket.recoverConnection(serverAddress, serverPort);
}

void UdpManager::send(const void *packet, int length) {
    if (m_stopped) {
        return;
    }
    SendBuffer sendBuffer;

    memcpy(sendBuffer.buf, packet, length);
    sendBuffer.len = length;

    {
        MutexLock lock(pipeMutex);
        m_sendQueue.push_back(sendBuffer);
    }
    // Notify enqueue to loop thread
    write(m_notifyPipe[1], "", 1);
}

void UdpManager::runLoop(JNIEnv *env, jobject instance, jstring serverAddress, int serverPort) {
    fd_set fds, fds_org;

    FD_ZERO(&fds_org);
    FD_SET(m_socket.getSocket(), &fds_org);
    FD_SET(m_notifyPipe[0], &fds_org);
    int nfds = std::max(m_socket.getSocket(), m_notifyPipe[0]) + 1;

    m_env = env;
    m_instance = instance;

    if (serverAddress != NULL) {
        recoverConnection(GetStringFromJNIString(env, serverAddress), serverPort);
    }

    while (!m_stopped) {
        timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 10 * 1000;
        memcpy(&fds, &fds_org, sizeof(fds));
        int ret = select(nfds, &fds, NULL, NULL, &timeout);

        if (ret == 0) {
            doPeriodicWork();

            // timeout
            continue;
        }

        if (FD_ISSET(m_notifyPipe[0], &fds)) {
            //LOG("select pipe");
            processReadPipe(m_notifyPipe[0]);
        }

        if (FD_ISSET(m_socket.getSocket(), &fds)) {
            m_socket.recv();
        }
        doPeriodicWork();
    }

    LOGI("Exited select loop.");

    if (m_socket.isConnected()) {
        // Stop stream.
        StreamControlMessage message = {};
        message.type = ALVR_PACKET_TYPE_STREAM_CONTROL_MESSAGE;
        message.mode = 2;
        m_socket.send(&message, sizeof(message));
    }

    m_soundPlayer.reset();

    LOGI("Exiting UdpReceiverThread runLoop");

    return;
}

void UdpManager::interrupt() {
    m_stopped = true;

    // Notify stop to loop thread.
    write(m_notifyPipe[1], "", 1);
}

void UdpManager::notifyWaitingThread(JNIEnv *env) {
    // Notify NAL waiting thread
    if (m_nalParser) {
        m_nalParser->notifyWaitingThread(env);
    }
}

bool UdpManager::isConnected() {
    return m_socket.isConnected();
}

jstring UdpManager::getServerAddress(JNIEnv *env) {
    return m_socket.getServerAddress(env);
}

int UdpManager::getServerPort() {
    return m_socket.getServerPort();
}

void UdpManager::onConnect(const ConnectionMessage &connectionMessage) {
    // Save video width and height
    m_connectionMessage = connectionMessage;

    updateTimeout();
    m_prevVideoSequence = 0;
    m_prevSoundSequence = 0;
    m_timeDiff = 0;
    LatencyCollector::Instance().resetAll();
    m_nalParser->setCodec(m_connectionMessage.codec);

    jclass clazz = m_env->GetObjectClass(m_instance);
    jmethodID method = m_env->GetMethodID(clazz, "onConnected", "(IIIII)V");
    m_env->CallVoidMethod(m_instance, method, m_connectionMessage.videoWidth
            , m_connectionMessage.videoHeight, m_connectionMessage.codec
            , m_connectionMessage.frameQueueSize, m_connectionMessage.refreshRate);
    m_env->DeleteLocalRef(clazz);

    // Start stream.
    StreamControlMessage message = {};
    message.type = ALVR_PACKET_TYPE_STREAM_CONTROL_MESSAGE;
    message.mode = 1;
    m_socket.send(&message, sizeof(message));
}

void UdpManager::onBroadcastRequest() {
    // Respond with hello message.
    HelloMessage message = {};
    message.type = ALVR_PACKET_TYPE_HELLO_MESSAGE;
    memcpy(message.deviceName, m_deviceName.c_str(),
           std::min(m_deviceName.length(), sizeof(message.deviceName)));
    m_socket.send(&message, sizeof(message));
}

void UdpManager::onPacketRecv(const char *packet, size_t packetSize) {
    updateTimeout();

    uint32_t type = *(uint32_t *) packet;
    if (type == ALVR_PACKET_TYPE_VIDEO_FRAME) {
        VideoFrame *header = (VideoFrame *) packet;

        if (m_lastFrameIndex != header->frameIndex) {
            LatencyCollector::Instance().receivedFirst(header->frameIndex);
            if ((int64_t) header->sentTime - m_timeDiff > getTimestampUs()) {
                LatencyCollector::Instance().estimatedSent(header->frameIndex, 0);
            } else {
                LatencyCollector::Instance().estimatedSent(header->frameIndex,
                                                           (int64_t) header->sentTime -
                                                           m_timeDiff - getTimestampUs());
            }
            m_lastFrameIndex = header->frameIndex;
        }

        processVideoSequence(header->packetCounter);

        // Following packets of a video frame
        bool fecFailure = false;
        bool ret2 = m_nalParser->processPacket(header, packetSize, fecFailure);
        if (ret2) {
            LatencyCollector::Instance().receivedLast(header->frameIndex);
        }
        if (fecFailure) {
            LatencyCollector::Instance().fecFailure();
            sendPacketLossReport(ALVR_LOST_FRAME_TYPE_VIDEO, 0, 0);
        }
    } else if (type == ALVR_PACKET_TYPE_TIME_SYNC) {
        // Time sync packet
        if (packetSize < sizeof(TimeSync)) {
            return;
        }
        TimeSync *timeSync = (TimeSync *) packet;
        uint64_t Current = getTimestampUs();
        if (timeSync->mode == 1) {
            uint64_t RTT = Current - timeSync->clientTime;
            m_timeDiff = ((int64_t) timeSync->serverTime + (int64_t) RTT / 2) - (int64_t) Current;
            LOGI("TimeSync: server - client = %ld us RTT = %lu us", m_timeDiff, RTT);

            TimeSync sendBuf = *timeSync;
            sendBuf.mode = 2;
            sendBuf.clientTime = Current;
            m_socket.send(&sendBuf, sizeof(sendBuf));
        }
    } else if (type == ALVR_PACKET_TYPE_CHANGE_SETTINGS) {
        // Change settings
        if (packetSize < sizeof(ChangeSettings)) {
            return;
        }
        ChangeSettings *settings = (ChangeSettings *) packet;

        jclass clazz = m_env->GetObjectClass(m_instance);
        jmethodID method = m_env->GetMethodID(clazz, "onChangeSettings", "(III)V");
        m_env->CallVoidMethod(m_instance, method, settings->enableTestMode, settings->suspend, settings->frameQueueSize);
        m_env->DeleteLocalRef(clazz);
    } else if (type == ALVR_PACKET_TYPE_AUDIO_FRAME_START) {
        // Change settings
        if (packetSize < sizeof(AudioFrameStart)) {
            return;
        }
        auto header = (AudioFrameStart *) packet;

        processSoundSequence(header->packetCounter);

        if (m_soundPlayer) {
            m_soundPlayer->putData((uint8_t *) packet + sizeof(*header),
                                   packetSize - sizeof(*header));
        }

        LOG("Received audio frame start: Counter=%d Size=%d PresentationTime=%lu",
            header->packetCounter, header->frameByteSize, header->presentationTime);
    } else if (type == ALVR_PACKET_TYPE_AUDIO_FRAME) {
        // Change settings
        if (packetSize < sizeof(AudioFrame)) {
            return;
        }
        auto header = (AudioFrame *) packet;

        processSoundSequence(header->packetCounter);

        if (m_soundPlayer) {
            m_soundPlayer->putData((uint8_t *) packet + sizeof(*header),
                                   packetSize - sizeof(*header));
        }

        LOG("Received audio frame: Counter=%d", header->packetCounter);
    }
}

void UdpManager::checkConnection() {
    if (m_socket.isConnected()) {
        if (m_lastReceived + CONNECTION_TIMEOUT < getTimestampUs()) {
            // Timeout
            LOGE("Connection timeout.");
            m_socket.disconnect();

            jclass clazz = m_env->GetObjectClass(m_instance);
            jmethodID method = m_env->GetMethodID(clazz, "onDisconnected", "()V");
            m_env->CallVoidMethod(m_instance, method);
            m_env->DeleteLocalRef(clazz);

            if (m_soundPlayer) {
                m_soundPlayer->Stop();
            }
        }
    }
}

void UdpManager::updateTimeout() {
    m_lastReceived = getTimestampUs();
}

void UdpManager::loadRefreshRates(JNIEnv *env, jintArray refreshRates_) {
    jint *refreshRates = env->GetIntArrayElements(refreshRates_, NULL);
    for(int i = 0; i < ALVR_REFRESH_RATE_LIST_SIZE; i++) {
        m_refreshRates[i] = refreshRates[i];
    }
    env->ReleaseIntArrayElements(refreshRates_, refreshRates, 0);
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_polygraphene_alvr_UdpReceiverThread_initializeSocket(JNIEnv *env, jobject instance,
                                                              jint port,
                                                              jstring deviceName_,
                                                              jobjectArray broadcastAddrList_,
                                                              jintArray refreshRates_) {
    g_udpManager = std::make_shared<UdpManager>();
    try {
        g_udpManager->initialize(env, port, deviceName_, broadcastAddrList_, refreshRates_);
    } catch (Exception e) {
        LOGE("Exception on initializing UdpManager. e=%ls", e.what());
        return 1;
    }
    return 0;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_UdpReceiverThread_closeSocket(JNIEnv *env, jobject instance) {
    g_udpManager.reset();
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_polygraphene_alvr_UdpReceiverThread_getNalListSize(JNIEnv *env, jobject instance) {
    return g_udpManager->getNalParser().getQueueSize(env);
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_polygraphene_alvr_UdpReceiverThread_waitNal(JNIEnv *env, jobject instance) {
    return g_udpManager->getNalParser().wait(env);
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_polygraphene_alvr_UdpReceiverThread_getNal(JNIEnv *env, jobject instance) {
    return g_udpManager->getNalParser().get(env);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_UdpReceiverThread_recycleNal(JNIEnv *env, jobject instance,
                                                        jobject nal) {
    g_udpManager->getNalParser().recycle(env, nal);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_UdpReceiverThread_flushNALList(JNIEnv *env, jobject instance) {
    g_udpManager->getNalParser().flush(env);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_UdpReceiverThread_runLoop(JNIEnv *env, jobject instance,
                                                     jstring serverAddress, int serverPort) {
    g_udpManager->runLoop(env, instance, serverAddress, serverPort);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_UdpReceiverThread_interruptNative(JNIEnv *env, jobject instance) {
    if (g_udpManager) {
        g_udpManager->interrupt();
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_UdpReceiverThread_notifyWaitingThread(JNIEnv *env, jobject instance) {
    if (g_udpManager) {
        g_udpManager->notifyWaitingThread(env);
    }
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_polygraphene_alvr_UdpReceiverThread_isConnected(JNIEnv *env, jobject instance) {
    return g_udpManager && g_udpManager->isConnected();
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_polygraphene_alvr_UdpReceiverThread_getServerAddress(JNIEnv *env, jobject instance) {
    return g_udpManager->getServerAddress(env);
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_polygraphene_alvr_UdpReceiverThread_getServerPort(JNIEnv *env, jobject instance) {
    return g_udpManager->getServerPort();
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_UdpReceiverThread_clearStopped(JNIEnv *env, jobject instance) {
    g_udpManager->getNalParser().clearStopped();
}

extern "C"
JNIEXPORT jlong JNICALL
Java_com_polygraphene_alvr_UdpReceiverThread_getPointer(JNIEnv *env, jobject instance) {
    return (jlong) g_udpManager.get();
}
/// UdpReceiverThread jni functions using UDP socket
// Send tracking information and lost packet feedback to server.
// And receive screen video stream.
////////////////////////////////////////////////////////////////////

#include <jni.h>
#include <string>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>
#include <endian.h>
#include <arpa/inet.h>
#include <algorithm>
#include <errno.h>
#include <sys/ioctl.h>
#include "nal.h"
#include "utils.h"
#include "packet_types.h"
#include "sound.h"

// Maximum UDP packet size
static const int MAX_PACKET_SIZE = 2000;
// Connection has lost when elapsed 3 seconds from last packet.
static const uint64_t CONNECTION_TIMEOUT = 3 * 1000 * 1000;

static int sock = -1;
static sockaddr_in serverAddr;
static std::list<sockaddr_in> broadcastAddrList;
static int notify_pipe[2];
static time_t prevSentSync = 0;
static time_t prevSentBroadcast = 0;
static int64_t TimeDiff;
static uint64_t timeSyncSequence = (uint64_t) -1;
static bool stopped = false;
static bool connected = false;
static bool hasServerAddress = false;
static uint64_t lastReceived = 0;
static uint64_t lastFrameIndex = 0;
static std::string deviceName;
static ConnectionMessage g_connectionMessage;
static bool g_is72Hz = false;
static uint32_t prevVideoSequence = 0;
static uint32_t prevSoundSequence = 0;
static std::shared_ptr<SoundPlayer> g_soundPlayer;

static JNIEnv *env_;
static jobject instance_;
static jobject latencyCollector_;
static jclass latencyCollectorClass_;
static jmethodID latencyCollectorEstimatedSent_;
static jmethodID latencyCollectorReceivedFirst_;
static jmethodID latencyCollectorReceivedLast_;
static jmethodID latencyCollectorPacketLoss_;
static jmethodID latencyCollectorGetLatency_;
static jmethodID latencyCollectorGetPacketsLostTotal_;
static jmethodID latencyCollectorGetPacketsLostInSecond_;
static jmethodID latencyCollectorResetAll_;

// lock for accessing sendQueue
static pthread_mutex_t pipeMutex = PTHREAD_MUTEX_INITIALIZER;

class PipeLock {
public:
    PipeLock() { pthread_mutex_lock(&pipeMutex); }

    ~PipeLock() { pthread_mutex_unlock(&pipeMutex); }
};

// send buffer
struct SendBuffer {
    char buf[MAX_PACKET_SIZE];
    int len;
};
static std::list<SendBuffer> sendQueue;


static void recordEstimatedSent(uint64_t frameIndex, uint64_t estimetedSentTime){
    env_->CallVoidMethod(latencyCollector_, latencyCollectorEstimatedSent_, frameIndex, estimetedSentTime);
}
static void recordFirstPacketReceived(uint64_t frameIndex){
    env_->CallVoidMethod(latencyCollector_, latencyCollectorReceivedFirst_, frameIndex);
}
static void recordLastPacketReceived(uint64_t frameIndex){
    env_->CallVoidMethod(latencyCollector_, latencyCollectorReceivedLast_, frameIndex);
}
static void recordPacketLoss(uint64_t lost) {
    env_->CallVoidMethod(latencyCollector_, latencyCollectorPacketLoss_, lost);
}
static uint64_t getLatency(uint32_t i, uint32_t j) {
    return env_->CallLongMethod(latencyCollector_, latencyCollectorGetLatency_, i, j);
}
static uint64_t getPacketsLostTotal() {
    return env_->CallLongMethod(latencyCollector_, latencyCollectorGetPacketsLostTotal_);
}
static uint64_t getPacketsLostInSecond() {
    return env_->CallLongMethod(latencyCollector_, latencyCollectorGetPacketsLostInSecond_);
}
static void resetAll() {
    env_->CallVoidMethod(latencyCollector_, latencyCollectorResetAll_);
}

static void sendPacketLossReport(ALVR_LOST_FRAME_TYPE frameType, uint32_t fromPacketCounter, uint32_t toPacketCounter) {
    PacketErrorReport report;
    report.type = ALVR_PACKET_TYPE_PACKET_ERROR_REPORT;
    report.lostFrameType = frameType;
    report.fromPacketCounter = fromPacketCounter;
    report.toPacketCounter = toPacketCounter;
    int ret = sendto(sock, &report, sizeof(report), 0, (sockaddr *) &serverAddr,
           sizeof(serverAddr));
    LOGI("Sent packet loss report. ret=%d errno=%d", ret, errno);
}

static void processVideoSequence(uint32_t sequence) {
    if (prevVideoSequence != 0 && prevVideoSequence + 1 != sequence) {
        uint32_t lost = sequence - (prevVideoSequence + 1);
        recordPacketLoss(lost);

        sendPacketLossReport(processingIDRFrame() ? ALVR_LOST_FRAME_TYPE_IDR : ALVR_LOST_FRAME_TYPE_P
                , prevVideoSequence + 1, sequence - 1);

        LOGE("VideoPacket loss %d (%d -> %d)", lost, prevVideoSequence + 1,
            sequence - 1);
    }
    prevVideoSequence = sequence;
}

static void processSoundSequence(uint32_t sequence) {
    if (prevSoundSequence != 0 && prevSoundSequence + 1 != sequence) {
        uint32_t lost = sequence - (prevSoundSequence + 1);
        recordPacketLoss(lost);

        sendPacketLossReport(ALVR_LOST_FRAME_TYPE_AUDIO
                , prevSoundSequence + 1, sequence - 1);

        LOGE("SoundPacket loss %d (%d -> %d)", lost, prevSoundSequence + 1,
             sequence - 1);
    }
    prevSoundSequence = sequence;
}

static int processRecv(int sock) {
    char buf[MAX_PACKET_SIZE];
    int len = MAX_PACKET_SIZE;
    char str[1000];

    sockaddr_in addr;
    socklen_t socklen = sizeof(addr);
    int ret = recvfrom(sock, (char *) buf, len, 0, (sockaddr *) &addr, &socklen);
    if (ret <= 0) {
        return ret;
    }

    if (connected) {
        if (addr.sin_port != serverAddr.sin_port ||
            addr.sin_addr.s_addr != serverAddr.sin_addr.s_addr) {
            // Invalid source address. Ignore.
            inet_ntop(addr.sin_family, &addr.sin_addr, str, sizeof(str));
            LOGE("Received packet from invalid source address. Address=%s:%d", str,
                htons(addr.sin_port));
            return 1;
        }
        lastReceived = getTimestampUs();

        uint32_t type = *(uint32_t *) buf;
        if (type == ALVR_PACKET_TYPE_VIDEO_FRAME_START) {
            // First packet of a video frame
            VideoFrameStart *header = (VideoFrameStart *)buf;

            processVideoSequence(header->packetCounter);

            lastFrameIndex = header->frameIndex;

            recordFirstPacketReceived(header->frameIndex);
            if((int64_t) header->presentationTime - TimeDiff > getTimestampUs()) {
                recordEstimatedSent(header->frameIndex, 0);
            }else{
                recordEstimatedSent(header->frameIndex, (int64_t) header->presentationTime - TimeDiff - getTimestampUs());
            }

            bool ret2 = processPacket(env_, (char *) buf, ret);
            if (ret2) {
                recordLastPacketReceived(lastFrameIndex);
            }
        } else if (type == ALVR_PACKET_TYPE_VIDEO_FRAME) {
            VideoFrame *header = (VideoFrame *)buf;

            processVideoSequence(header->packetCounter);

            // Following packets of a video frame
            bool ret2 = processPacket(env_, (char *) buf, ret);
            if (ret2) {
                recordLastPacketReceived(lastFrameIndex);
            }
        } else if (type == ALVR_PACKET_TYPE_TIME_SYNC) {
            // Time sync packet
            if (ret < sizeof(TimeSync)) {
                return ret;
            }
            TimeSync *timeSync = (TimeSync *) buf;
            uint64_t Current = getTimestampUs();
            if (timeSync->mode == 1) {
                uint64_t RTT = Current - timeSync->clientTime;
                TimeDiff = ((int64_t) timeSync->serverTime + (int64_t) RTT / 2) - (int64_t) Current;
                LOGI("TimeSync: server - client = %ld us RTT = %lu us", TimeDiff, RTT);

                TimeSync sendBuf = *timeSync;
                sendBuf.mode = 2;
                sendBuf.clientTime = Current;
                sendto(sock, &sendBuf, sizeof(sendBuf), 0, (sockaddr *) &serverAddr,
                       sizeof(serverAddr));
            }
        } else if (type == ALVR_PACKET_TYPE_CHANGE_SETTINGS) {
            // Change settings
            if (ret < sizeof(ChangeSettings)) {
                return ret;
            }
            ChangeSettings *settings = (ChangeSettings *) buf;

            jclass clazz = env_->GetObjectClass(instance_);
            jmethodID method = env_->GetMethodID(clazz, "onChangeSettings", "(II)V");
            env_->CallVoidMethod(instance_, method, settings->enableTestMode, settings->suspend);
            env_->DeleteLocalRef(clazz);
        } else if (type == ALVR_PACKET_TYPE_AUDIO_FRAME_START) {
            // Change settings
            if (ret < sizeof(AudioFrameStart)) {
                return ret;
            }
            auto header = (AudioFrameStart *) buf;

            processSoundSequence(header->packetCounter);

            if(g_soundPlayer) {
                g_soundPlayer->putData((uint8_t *) buf + sizeof(*header), ret - sizeof(*header));
            }

            LOG("Received audio frame start: Counter=%d Size=%d PresentationTime=%lu"
            , header->packetCounter, header->frameByteSize, header->presentationTime);
        } else if (type == ALVR_PACKET_TYPE_AUDIO_FRAME) {
            // Change settings
            if (ret < sizeof(AudioFrame)) {
                return ret;
            }
            auto header = (AudioFrame *) buf;

            processSoundSequence(header->packetCounter);

            if(g_soundPlayer) {
                g_soundPlayer->putData((uint8_t *) buf + sizeof(*header), ret - sizeof(*header));
            }

            LOG("Received audio frame: Counter=%d", header->packetCounter);
        }
    } else {
        uint32_t type = *(uint32_t *) buf;
        if (type == ALVR_PACKET_TYPE_BROADCAST_REQUEST_MESSAGE) {
            inet_ntop(addr.sin_family, &addr.sin_addr, str, sizeof(str));
            LOGI("Received broadcast packet from %s:%d.", str, htons(addr.sin_port));

            // Respond with hello message.
            HelloMessage message = {};
            message.type = ALVR_PACKET_TYPE_HELLO_MESSAGE;
            memcpy(message.deviceName, deviceName.c_str(),
                   std::min(deviceName.length(), sizeof(message.deviceName)));
            sendto(sock, &message, sizeof(message), 0, (sockaddr *) &addr, sizeof(addr));
        } else if (type == ALVR_PACKET_TYPE_CONNECTION_MESSAGE) {
            inet_ntop(addr.sin_family, &addr.sin_addr, str, sizeof(str));
            LOGI("Received connection request packet from %s:%d.", str, htons(addr.sin_port));
            if (ret < sizeof(ConnectionMessage)) {
                return ret;
            }
            ConnectionMessage *connectionMessage = (ConnectionMessage *) buf;

            if(connectionMessage->version != ALVR_PROTOCOL_VERSION) {
                LOGE("Received connection message which has unsupported version. Received Version=%d Our Version=%d", connectionMessage->version, ALVR_PROTOCOL_VERSION);
                return ret;
            }
            // Save video width and height
            g_connectionMessage = *connectionMessage;

            serverAddr = addr;
            connected = true;
            hasServerAddress = true;
            lastReceived = getTimestampUs();
            prevVideoSequence = 0;
            prevSoundSequence = 0;
            TimeDiff = 0;
            resetAll();
            setNalCodec(g_connectionMessage.codec);

            LOGI("Try setting recv buffer size = %d bytes", connectionMessage->bufferSize);
            int val = connectionMessage->bufferSize;
            setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char *) &val, sizeof(val));
            socklen_t socklen = sizeof(val);
            getsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char *) &val, &socklen);
            LOGI("Current socket recv buffer is %d bytes", val);

            jclass clazz = env_->GetObjectClass(instance_);
            jmethodID method = env_->GetMethodID(clazz, "onConnected", "(III)V");
            env_->CallVoidMethod(instance_, method, g_connectionMessage.videoWidth, g_connectionMessage.videoHeight, g_connectionMessage.codec);
            env_->DeleteLocalRef(clazz);

            // Start stream.
            StreamControlMessage message = {};
            message.type = ALVR_PACKET_TYPE_STREAM_CONTROL_MESSAGE;
            message.mode = 1;
            sendto(sock, &message, sizeof(message), 0, (sockaddr *) &serverAddr, sizeof(serverAddr));
        }
    }
    return ret;
}

static void processReadPipe(int pipefd) {
    char buf[2000];
    int len = 1;

    int ret = read(pipefd, buf, len);
    if (ret <= 0) {
        return;
    }

    SendBuffer sendBuffer;
    while (1) {
        {
            PipeLock lock;

            if (sendQueue.empty()) {
                break;
            } else {
                sendBuffer = sendQueue.front();
                sendQueue.pop_front();
            }
        }
        if (stopped) {
            return;
        }

        //LOG("Sending tracking packet %d", sendBuffer.len);
        sendto(sock, sendBuffer.buf, sendBuffer.len, 0, (sockaddr *) &serverAddr,
               sizeof(serverAddr));
    }

    return;
}

static void sendTimeSync() {
    time_t current = time(NULL);
    if (prevSentSync != current && connected) {
        LOGI("Sending timesync.");

        TimeSync timeSync = {};
        timeSync.type = ALVR_PACKET_TYPE_TIME_SYNC;
        timeSync.mode = 0;
        timeSync.clientTime = getTimestampUs();
        timeSync.sequence = ++timeSyncSequence;

        timeSync.packetsLostTotal = (uint32_t) getPacketsLostTotal();
        timeSync.packetsLostInSecond = (uint32_t) getPacketsLostInSecond();

        timeSync.averageTotalLatency = (uint32_t) getLatency(0, 0);
        timeSync.maxTotalLatency = (uint32_t) getLatency(0, 1);
        timeSync.minTotalLatency = (uint32_t) getLatency(0, 2);

        timeSync.averageTransportLatency = (uint32_t) getLatency(1, 0);
        timeSync.maxTransportLatency = (uint32_t) getLatency(1, 1);
        timeSync.minTransportLatency = (uint32_t) getLatency(1, 2);

        timeSync.averageDecodeLatency = (uint32_t) getLatency(2, 0);
        timeSync.maxDecodeLatency = (uint32_t) getLatency(2, 1);
        timeSync.minDecodeLatency = (uint32_t) getLatency(2, 2);

        sendto(sock, &timeSync, sizeof(timeSync), 0, (sockaddr *) &serverAddr,
               sizeof(serverAddr));
    }
    prevSentSync = current;
}

static void sendBroadcast() {
    time_t current = time(NULL);
    if (prevSentBroadcast != current && !connected) {
        LOGI("Sending broadcast hello.");

        HelloMessage helloMessage = {};
        helloMessage.type = 1;
        helloMessage.version = ALVR_PROTOCOL_VERSION;
        memcpy(helloMessage.deviceName, deviceName.c_str(),
               std::min(deviceName.length(), sizeof(helloMessage.deviceName)));
        helloMessage.refreshRate = g_is72Hz ? 72 : 60;

        for(const sockaddr_in &address : broadcastAddrList) {
            sendto(sock, &helloMessage, sizeof(helloMessage), 0, (sockaddr *) &address,
                   sizeof(address));
        }
    }
    prevSentBroadcast = current;
}

static void checkConnection() {
    if(connected) {
        if(lastReceived + CONNECTION_TIMEOUT < getTimestampUs()) {
            // Timeout
            LOGE("Connection timeout.");
            connected = false;
            if(g_soundPlayer){
                g_soundPlayer->Stop();
            }
            memset(&serverAddr, 0, sizeof(serverAddr));
        }
    }
}

static void doPeriodicWork() {
    sendTimeSync();
    sendBroadcast();
    checkConnection();
}

static void recoverConnection(std::string serverAddress, int serverPort) {
    LOGI("Sending recover connection request. server=%s:%d", serverAddress.c_str(), serverPort);
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(serverPort);
    inet_pton(AF_INET, serverAddress.c_str(), &addr.sin_addr);

    RecoverConnection message = {};
    message.type = ALVR_PACKET_TYPE_RECOVER_CONNECTION;

    sendto(sock, &message, sizeof(message), 0, (sockaddr *)&addr, sizeof(addr));
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_polygraphene_alvr_UdpReceiverThread_initializeSocket(JNIEnv *env, jobject instance,
                                                              jint port,
                                                              jstring deviceName_,
                                                              jobjectArray broadcastAddrList_) {
    int ret = 0;
    int val;
    socklen_t len;
    int broadcastCount;

    //
    // Initialize variables
    //

    stopped = false;
    lastReceived = 0;
    prevSentSync = 0;
    prevSentBroadcast = 0;
    prevVideoSequence = 0;
    prevSoundSequence = 0;
    connected = false;
    TimeDiff = 0;

    deviceName = GetStringFromJNIString(env, deviceName_);

    initNAL(env);

    //
    // Sound
    //

    g_soundPlayer = std::make_shared<SoundPlayer>();
    if(g_soundPlayer->initialize() != 0) {
        LOGE("Failed on SoundPlayer initialize.");
        g_soundPlayer.reset();
    }
    LOGI("SoundPlayer successfully initialize.");

    //
    // Socket
    //

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        ret = 1;
        goto end;
    }
    val = 1;
    ioctl(sock, FIONBIO, &val);

    val = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (char *) &val, sizeof(val));

    //
    // Socket recv buffer
    //

    //setMaxSocketBuffer();
    // 30Mbps 50ms buffer
    getsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char *) &val, &len);
    LOGI("Default socket recv buffer is %d bytes", val);

    val = 30 * 1000 * 500 / 8;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char *) &val, sizeof(val));
    len = sizeof(val);
    getsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char *) &val, &len);
    LOGI("Current socket recv buffer is %d bytes", val);

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock, (sockaddr *) &addr, sizeof(addr)) < 0) {
        LOGE("bind error : %d %s", errno, strerror(errno));
    }

    //
    // Parse broadcast address list.
    //

    broadcastAddrList.clear();
    broadcastCount = env->GetArrayLength(broadcastAddrList_);
    for(int i = 0; i < broadcastCount; i++) {
        jstring address = (jstring) env->GetObjectArrayElement(broadcastAddrList_, i);
        auto addressStr = GetStringFromJNIString(env, address);
        env->DeleteLocalRef(address);

        sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(addr.sin_family, addressStr.c_str(), &addr.sin_addr);
        broadcastAddrList.push_back(addr);
    }

    //
    // Pipe used for send buffer notification.
    //

    pthread_mutex_init(&pipeMutex, NULL);

    if (pipe2(notify_pipe, O_NONBLOCK) < 0) {
        ret = 1;
        goto end;
    }

    end:

    LOGI("Udp socket initialized.");

    if (ret != 0) {
        if (sock >= 0) {
            close(sock);
        }
        return ret;
    }
    return 0;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_UdpReceiverThread_closeSocket(JNIEnv *env, jobject instance) {
    if (sock >= 0) {
        close(sock);
    }
    destroyNAL(env);
    sendQueue.clear();
}


extern "C"
JNIEXPORT jint JNICALL
Java_com_polygraphene_alvr_UdpReceiverThread_send(JNIEnv *env, jobject instance,
                                                  jbyteArray buf_, jint length) {
    if (stopped) {
        return 0;
    }
    jbyte *buf = env->GetByteArrayElements(buf_, NULL);

    SendBuffer sendBuffer;

    memcpy(sendBuffer.buf, buf, length);
    sendBuffer.len = length;

    {
        PipeLock lock;
        sendQueue.push_back(sendBuffer);
    }
    // Notify enqueue to loop thread
    write(notify_pipe[1], "", 1);

    env->ReleaseByteArrayElements(buf_, buf, 0);

    return 0;
}


extern "C"
JNIEXPORT jint JNICALL
Java_com_polygraphene_alvr_UdpReceiverThread_getNalListSize(JNIEnv *env, jobject instance) {
    return getNalListSize(env);
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_polygraphene_alvr_UdpReceiverThread_waitNal(JNIEnv *env, jobject instance) {
    return waitNal(env);
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_polygraphene_alvr_UdpReceiverThread_getNal(JNIEnv *env, jobject instance) {
    return getNal(env);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_UdpReceiverThread_recycleNal(JNIEnv *env, jobject instance, jobject nal) {
    recycleNal(env, nal);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_UdpReceiverThread_flushNALList(JNIEnv *env, jobject instance) {
    flushNalList(env);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_UdpReceiverThread_runLoop(JNIEnv *env, jobject instance, jobject latencyCollector
, jstring serverAddress, int serverPort) {
    fd_set fds, fds_org;

    FD_ZERO(&fds_org);
    FD_SET(sock, &fds_org);
    FD_SET(notify_pipe[0], &fds_org);
    int nfds = std::max(sock, notify_pipe[0]) + 1;

    env_ = env;
    instance_ = instance;
    latencyCollector_ = latencyCollector;

    latencyCollectorClass_ = env_->GetObjectClass(latencyCollector_);

    latencyCollectorEstimatedSent_ = env_->GetMethodID(latencyCollectorClass_, "EstimatedSent", "(JJ)V");
    latencyCollectorReceivedFirst_ = env_->GetMethodID(latencyCollectorClass_, "ReceivedFirst", "(J)V");
    latencyCollectorReceivedLast_ = env_->GetMethodID(latencyCollectorClass_, "ReceivedLast", "(J)V");
    latencyCollectorPacketLoss_ = env_->GetMethodID(latencyCollectorClass_, "PacketLoss", "(J)V");
    latencyCollectorGetLatency_ = env_->GetMethodID(latencyCollectorClass_, "GetLatency", "(II)J");
    latencyCollectorGetPacketsLostTotal_ = env_->GetMethodID(latencyCollectorClass_, "GetPacketsLostTotal", "()J");
    latencyCollectorGetPacketsLostInSecond_ = env_->GetMethodID(latencyCollectorClass_, "GetPacketsLostInSecond", "()J");
    latencyCollectorResetAll_ = env_->GetMethodID(latencyCollectorClass_, "ResetAll", "()V");

    if(serverAddress != NULL) {
        recoverConnection(GetStringFromJNIString(env, serverAddress), serverPort);
    }

    while (!stopped) {
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

        if (FD_ISSET(notify_pipe[0], &fds)) {
            //LOG("select pipe");
            processReadPipe(notify_pipe[0]);
        }

        if (FD_ISSET(sock, &fds)) {
            //LOG("select sock");
            while (true) {
                int recv_ret = processRecv(sock);
                if (recv_ret < 0) {
                    break;
                }
            }
        }
        doPeriodicWork();
    }

    LOGI("Exited select loop.");

    if(connected) {
        // Stop stream.
        StreamControlMessage message = {};
        message.type = 7;
        message.mode = 2;
        sendto(sock, &message, sizeof(message), 0, (sockaddr *) &serverAddr, sizeof(serverAddr));
    }

    g_soundPlayer.reset();

    LOGI("Exiting UdpReceiverThread runLoop");

    return;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_UdpReceiverThread_interrupt(JNIEnv *env, jobject instance) {
    stopped = true;

    // Notify stop to loop thread.
    write(notify_pipe[1], "", 1);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_UdpReceiverThread_notifyWaitingThread(JNIEnv *env, jobject instance) {
    // Notify NAL waiting thread
    notifyNALWaitingThread(env);
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_polygraphene_alvr_UdpReceiverThread_isConnected(JNIEnv *env, jobject instance) {
    checkConnection();
    return (uint8_t)connected;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_UdpReceiverThread_set72Hz(JNIEnv *env, jobject instance,
                                                     jboolean is72Hz) {
    g_is72Hz = is72Hz;
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_polygraphene_alvr_UdpReceiverThread_getServerAddress(JNIEnv *env, jobject instance) {
    if(hasServerAddress){
        char serverAddress[100];
        inet_ntop(serverAddr.sin_family, &serverAddr.sin_addr, serverAddress, sizeof(serverAddress));
        return env->NewStringUTF(serverAddress);
    }
    return NULL;
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_polygraphene_alvr_UdpReceiverThread_getServerPort(JNIEnv *env, jobject instance) {
    if(hasServerAddress) {
        return htons(serverAddr.sin_port);
    }
    return 0;
}

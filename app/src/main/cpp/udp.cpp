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

// Maximum UDP packet size
static const int MAX_PACKET_SIZE = 2000;
// Connection has lost when elapsed 3 seconds from last packet.
static const uint64_t CONNECTION_TIMEOUT = 3 * 1000 * 1000;

static int sock = -1;
static sockaddr_in serverAddr;
static sockaddr_in broadcastAddr;
static int notify_pipe[2];
static time_t prevSentSync = 0;
static time_t prevSentBroadcast = 0;
static int64_t TimeDiff;
static uint64_t timeSyncSequence = (uint64_t) -1;
static bool stopped = false;
static bool connected = false;
static uint64_t lastReceived = 0;
static std::string deviceName;
static ConnectionMessage g_connectionMessage;

static JNIEnv *env_;
static jobject instance_;

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


uint64_t lastParsedPresentationTime = 0;
uint32_t prevSequence = 0;

static void processSequence(uint32_t sequence) {
    if (prevSequence + 1 != sequence) {
        LOG("packet loss %d (%d -> %d)", sequence - (prevSequence + 1), prevSequence + 1,
            sequence - 1);
    }
    prevSequence = sequence;
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
            LOG("Received packet from invalid source address. Address=%s:%d", str,
                htons(addr.sin_port));
            return 1;
        }
        lastReceived = getTimestampUs();

        uint32_t type = *(uint32_t *) buf;
        if (type == ALVR_PACKET_TYPE_VIDEO_FRAME_START) {
            // First packet of a video frame
            VideoFrameStart *header = (VideoFrameStart *)buf;

            processSequence(header->packetCounter);
            lastParsedPresentationTime = header->presentationTime;

            LOG("presentationTime NALType=%d frameIndex=%lu delay=%ld us", buf[28] & 0x1F,
                header->frameIndex,
                (int64_t) getTimestampUs() - ((int64_t) header->presentationTime - TimeDiff));
            bool ret2 = processPacket(env_, (char *) buf, ret);
            if (ret2) {
                LOG("presentationTime end delay: %ld us",
                    (int64_t) getTimestampUs() - ((int64_t) lastParsedPresentationTime - TimeDiff));
            }
        } else if (type == ALVR_PACKET_TYPE_VIDEO_FRAME) {
            VideoFrame *header = (VideoFrame *)buf;

            processSequence(header->packetCounter);

            // Following packets of a video frame
            bool ret2 = processPacket(env_, (char *) buf, ret);
            if (ret2) {
                LOG("presentationTime end delay: %ld us",
                    (int64_t) getTimestampUs() - ((int64_t) lastParsedPresentationTime - TimeDiff));
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
                LOG("TimeSync: server - client = %ld us RTT = %lu us", TimeDiff, RTT);

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
        }
    } else {
        uint32_t type = *(uint32_t *) buf;
        if (type == ALVR_PACKET_TYPE_BROADCAST_REQUEST_MESSAGE) {
            inet_ntop(addr.sin_family, &addr.sin_addr, str, sizeof(str));
            LOG("Received broadcast packet from %s:%d.", str, htons(addr.sin_port));

            // Respond with hello message.
            HelloMessage message = {};
            message.type = ALVR_PACKET_TYPE_HELLO_MESSAGE;
            memcpy(message.deviceName, deviceName.c_str(),
                   std::min(deviceName.length(), sizeof(message.deviceName)));
            sendto(sock, &message, sizeof(message), 0, (sockaddr *) &addr, sizeof(addr));
        } else if (type == ALVR_PACKET_TYPE_CONNECTION_MESSAGE) {
            inet_ntop(addr.sin_family, &addr.sin_addr, str, sizeof(str));
            LOG("Received connection request packet from %s:%d.", str, htons(addr.sin_port));
            if (ret < sizeof(ConnectionMessage)) {
                return ret;
            }
            ConnectionMessage *connectionMessage = (ConnectionMessage *) buf;

            // Save video width and height
            g_connectionMessage = *connectionMessage;

            serverAddr = addr;
            connected = true;
            lastReceived = getTimestampUs();

            LOG("Try setting recv buffer size = %d bytes", connectionMessage->bufferSize);
            int val = connectionMessage->bufferSize;
            setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char *) &val, sizeof(val));
            socklen_t socklen = sizeof(val);
            getsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char *) &val, &socklen);
            LOG("Current socket recv buffer is %d bytes", val);

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
        LOG("Sending timesync.");

        TimeSync timeSync = {};
        timeSync.type = 3;
        timeSync.mode = 0;
        timeSync.clientTime = getTimestampUs();
        timeSync.sequence = ++timeSyncSequence;

        sendto(sock, &timeSync, sizeof(timeSync), 0, (sockaddr *) &serverAddr,
               sizeof(serverAddr));
    }
    prevSentSync = current;
}

static void sendBroadcast() {
    time_t current = time(NULL);
    if (prevSentBroadcast != current && !connected) {
        LOG("Sending broadcast hello.");

        HelloMessage helloMessage = {};
        helloMessage.type = 1;
        memcpy(helloMessage.deviceName, deviceName.c_str(),
               std::min(deviceName.length(), sizeof(helloMessage.deviceName)));

        sendto(sock, &helloMessage, sizeof(helloMessage), 0, (sockaddr *) &broadcastAddr,
               sizeof(broadcastAddr));
    }
    prevSentBroadcast = current;
}

static void checkConnection() {
    if(connected) {
        if(lastReceived + CONNECTION_TIMEOUT < getTimestampUs()) {
            // Timeout
            LOG("Connection timeout.");
            connected = false;
            memset(&serverAddr, 0, sizeof(serverAddr));
        }
    }
}

static void doPeriodicWork() {
    sendTimeSync();
    sendBroadcast();
    checkConnection();
}

static void setMaxSocketBuffer(){
    int val2 = -1;
    int val = 0;

    LOG("Finding the maximum socket buffer.");
    while(true) {
        socklen_t len = sizeof(val);
        getsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char *) &val, &len);
        if (val2 == val) {
            LOG("Maximum buffer is %d bytes", val);
            break;
        }
        val2 = val;

        val *= 2;
        LOG("Test buffer size. size=%d bytes.", val);
        setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char *) &val, sizeof(val));
    }
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_polygraphene_alvr_UdpReceiverThread_initializeSocket(JNIEnv *env, jobject instance,
                                                              jint port,
                                                              jstring deviceName_,
                                                              jstring broadcastAddrStr_) {
    int ret = 0;
    int val;
    socklen_t len;

    const char *deviceName_c = env->GetStringUTFChars(deviceName_, 0);
    deviceName = deviceName_c;
    env->ReleaseStringUTFChars(deviceName_, deviceName_c);

    const char *broadcastAddrStr_c = env->GetStringUTFChars(broadcastAddrStr_, 0);
    std::string broadcastAddrStr = broadcastAddrStr_c;
    env->ReleaseStringUTFChars(broadcastAddrStr_, broadcastAddrStr_c);

    stopped = false;
    lastReceived = 0;
    prevSentSync = 0;
    prevSentBroadcast = 0;
    prevSequence = 0;
    connected = false;

    initNAL();

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        ret = 1;
        goto end;
    }
    val = 1;
    ioctl(sock, FIONBIO, &val);

    val = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (char *) &val, sizeof(val));

    // Set socket recv buffer

    //setMaxSocketBuffer();
    // 30Mbps 50ms buffer
    getsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char *) &val, &len);
    LOG("Default socket recv buffer is %d bytes", val);

    val = 30 * 1000 * 500 / 8;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char *) &val, sizeof(val));
    len = sizeof(val);
    getsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char *) &val, &len);
    LOG("Current socket recv buffer is %d bytes", val);

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock, (sockaddr *) &addr, sizeof(addr)) < 0) {
        LOG("bind error : %d %s", errno, strerror(errno));
    }

    memset(&broadcastAddr, 0, sizeof(broadcastAddr));
    broadcastAddr.sin_family = AF_INET;
    broadcastAddr.sin_port = htons(port);
    inet_pton(broadcastAddr.sin_family, broadcastAddrStr.c_str(), &broadcastAddr.sin_addr);

    pthread_mutex_init(&pipeMutex, NULL);

    if (pipe2(notify_pipe, O_NONBLOCK) < 0) {
        ret = 1;
        goto end;
    }

    end:

    LOG("Udp socket initialized.");

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
    return getNalListSize();
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
JNIEXPORT jobject JNICALL
Java_com_polygraphene_alvr_UdpReceiverThread_peekNal(JNIEnv *env, jobject instance) {
    return peekNal(env);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_UdpReceiverThread_flushNALList(JNIEnv *env, jobject instance) {
    flushNalList(env);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_UdpReceiverThread_runLoop(JNIEnv *env, jobject instance) {
    fd_set fds, fds_org;

    FD_ZERO(&fds_org);
    FD_SET(sock, &fds_org);
    FD_SET(notify_pipe[0], &fds_org);
    int nfds = std::max(sock, notify_pipe[0]) + 1;

    env_ = env;
    instance_ = instance;

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

    if(connected) {
        // Stop stream.
        StreamControlMessage message = {};
        message.type = 7;
        message.mode = 2;
        sendto(sock, &message, sizeof(message), 0, (sockaddr *) &serverAddr, sizeof(serverAddr));
    }

    LOG("Exiting UdpReceiverThread runLoop");

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
JNIEXPORT jint JNICALL
Java_com_polygraphene_alvr_UdpReceiverThread_getWidth(JNIEnv *env, jobject instance) {
    return g_connectionMessage.videoWidth;
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_polygraphene_alvr_UdpReceiverThread_getHeight(JNIEnv *env, jobject instance) {
    return g_connectionMessage.videoHeight;
}
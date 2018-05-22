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

// Maximum UDP packet size
static const int MAX_PACKET_SIZE = 2000;

static int sock = -1;
static sockaddr_in serverAddr;
static int notify_pipe[2];
static time_t prevSentSync = 0;
static int64_t TimeDiff;
static uint64_t timeSyncSequence = (uint64_t) -1;

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

#pragma pack(push, 1)
// hello message
struct HelloMessage {
    int type; // 1
    char device_name[32]; // null-terminated
};
// Client >----(mode 0)----> Server
// Client <----(mode 1)----< Server
// Client >----(mode 2)----> Server
struct TimeSync {
    uint32_t type; // 3
    uint32_t mode; // 0,1,2
    uint64_t sequence;
    uint64_t serverTime;
    uint64_t clientTime;
};
struct ChangeSettings {
    uint32_t type; // 4
    uint32_t enableTestMode;
    uint32_t suspend;
};
#pragma pack(pop)
uint64_t lastParsedPresentationTime = 0;
uint32_t prevSequence = 0;

static void processSequence(uint32_t sequence){
    if (prevSequence + 1 != sequence) {
        LOG("packet loss %d (%d -> %d)", sequence - (prevSequence + 1), prevSequence + 1, sequence - 1);
    }
    prevSequence = sequence;
}

static int processRecv(int sock) {
    char buf[MAX_PACKET_SIZE];
    int len = MAX_PACKET_SIZE;

    sockaddr_in addr;
    socklen_t socklen = sizeof(addr);
    int ret = recvfrom(sock, (char *) buf, len, 0, (sockaddr *) &addr, &socklen);
    if(ret <= 0) {
        return ret;
    }
    uint32_t type = *(uint32_t *) buf;
    if (type == 1) {
        // First packet of a video frame
        uint32_t sequence = *(uint32_t *) (buf + 4);
        uint64_t presentationTime = *(uint64_t *)(buf + 8);
        uint64_t frameIndex = *(uint64_t *)(buf + 16);

        processSequence(sequence);
        lastParsedPresentationTime = presentationTime;

        LOG("presentationTime NALType=%d frameIndex=%lu delay=%ld us", buf[28] & 0x1F, frameIndex, (int64_t)getTimestampUs() - ((int64_t)presentationTime - TimeDiff));
        bool ret2 = processPacket(env_, (char *) buf, ret);
        if(ret2){
            LOG("presentationTime end delay: %ld us", (int64_t)getTimestampUs() - ((int64_t)lastParsedPresentationTime - TimeDiff));
        }
    } else if (type == 2) {
        uint32_t sequence = *(uint32_t *) (buf + 4);
        processSequence(sequence);

        // None first packet of a video frame
        bool ret2 = processPacket(env_, (char *) buf, ret);
        if(ret2){
            LOG("presentationTime end delay: %ld us", (int64_t)getTimestampUs() - ((int64_t)lastParsedPresentationTime - TimeDiff));
        }
    } else if (type == 3) {
        // Time sync packet
        if (ret < sizeof(TimeSync)) {
            return ret;
        }
        TimeSync *timeSync = (TimeSync *) buf;
        uint64_t Current = getTimestampUs();
        if(timeSync->mode == 1){
            uint64_t RTT = Current - timeSync->clientTime;
            TimeDiff = ((int64_t)timeSync->serverTime + (int64_t)RTT / 2) - (int64_t)Current;
            LOG("TimeSync: server - client = %ld us RTT = %lu us", TimeDiff, RTT);

            TimeSync sendBuf = *timeSync;
            sendBuf.mode = 2;
            sendBuf.clientTime = Current;
            sendto(sock, &sendBuf, sizeof(sendBuf), 0, (sockaddr *)&serverAddr, sizeof(serverAddr));
        }
    } else if (type == 4) {
        // Change settings
        if(ret < sizeof(ChangeSettings)) {
            return ret;
        }
        ChangeSettings *settings = (ChangeSettings *) buf;

        jclass clazz = env_->GetObjectClass(instance_);
        jmethodID method = env_->GetMethodID(clazz, "onChangeSettings", "(II)V");
        env_->CallVoidMethod(instance_, method, settings->enableTestMode, settings->suspend);
        env_->DeleteLocalRef(clazz);
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

        //LOG("Sending tracking packet %d", sendBuffer.len);
        sendto(sock, sendBuffer.buf, sendBuffer.len, 0, (sockaddr *) &serverAddr,
               sizeof(serverAddr));
    }

    return;
}

static void sendTimeSync(){
    time_t current = time(NULL);
    if(prevSentSync != current){
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


extern "C"
JNIEXPORT jint JNICALL
Java_com_polygraphene_remoteglass_UdpReceiverThread_initializeSocket(JNIEnv *env, jobject instance,
                                                                     jstring host_, jint port,
                                                                     jstring deviceName_) {
    const char *host = env->GetStringUTFChars(host_, 0);
    const char *deviceName = env->GetStringUTFChars(deviceName_, 0);
    int ret = 0;
    int val;
    HelloMessage message = {};

    initNAL();

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        ret = 1;
        goto end;
    }
    val = 1;
    ioctl(sock, FIONBIO, &val);

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    inet_pton(serverAddr.sin_family, host, &serverAddr.sin_addr);

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(33450);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock, (sockaddr *) &addr, sizeof(addr)) < 0) {
        LOG("bind error : %d %s", errno, strerror(errno));
    }

    pthread_mutex_init(&pipeMutex, NULL);

    if (pipe2(notify_pipe, O_NONBLOCK) < 0) {
        ret = 1;
        goto end;
    }

    message.type = 1;
    memcpy(message.device_name, deviceName,
           std::min(strlen(deviceName), sizeof(message.device_name)));
    sendto(sock, &message, sizeof(message), 0, (sockaddr *) &serverAddr, sizeof(serverAddr));

    end:

    LOG("udp socket initialized");

    env->ReleaseStringUTFChars(host_, host);
    env->ReleaseStringUTFChars(deviceName_, deviceName);

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
Java_com_polygraphene_remoteglass_UdpReceiverThread_closeSocket(JNIEnv *env, jobject instance) {
    if (sock >= 0) {
        close(sock);
    }
}


extern "C"
JNIEXPORT jint JNICALL
Java_com_polygraphene_remoteglass_UdpReceiverThread_send(JNIEnv *env, jobject instance,
                                                         jbyteArray buf_, jint length) {
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
Java_com_polygraphene_remoteglass_UdpReceiverThread_getNalListSize(JNIEnv *env, jobject instance) {
    return getNalListSize();
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_polygraphene_remoteglass_UdpReceiverThread_getNal(JNIEnv *env, jobject instance) {
    return getNal(env);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_remoteglass_UdpReceiverThread_flushNALList(JNIEnv *env, jobject instance) {
    flushNalList(env);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_remoteglass_UdpReceiverThread_runLoop(JNIEnv *env, jobject instance) {
    fd_set fds, fds_org;

    FD_ZERO(&fds_org);
    FD_SET(sock, &fds_org);
    FD_SET(notify_pipe[0], &fds_org);
    int nfds = std::max(sock, notify_pipe[0]) + 1;

    env_ = env;
    instance_ = instance;

    jclass clazz = env->GetObjectClass(instance);
    jmethodID id = env->GetMethodID(clazz, "isStopped", "()Z");


    while (1) {
        if (env->CallBooleanMethod(instance, id)) {
            LOG("select loop stopped");
            break;
        }
        timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 10 * 1000;
        memcpy(&fds, &fds_org, sizeof(fds));
        int ret = select(nfds, &fds, NULL, NULL, &timeout);

        if (ret == 0) {
            sendTimeSync();

            // timeout
            continue;
        }

        if (FD_ISSET(notify_pipe[0], &fds)) {
            //LOG("select pipe");
            processReadPipe(notify_pipe[0]);
        }

        if (FD_ISSET(sock, &fds)) {
            //LOG("select sock");
            while(true) {
                int recv_ret = processRecv(sock);
                if(recv_ret < 0) {
                    break;
                }
            }
        }
        sendTimeSync();
    }
}
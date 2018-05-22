/// SrtReceiverThread jni functions using SRT socket
// Send tracking information and lost packet feedback to server.
// And receive screen video stream.
////////////////////////////////////////////////////////////////////

#include <jni.h>
#include <string>
#include <stdlib.h>
#include <srt.h>
#include <udt.h>
#include <list>
#include "nal.h"
#include "utils.h"


static const int MAX_PACKET_SIZE = 2000;

static const char *VIDEO_STREAM_ID = "videocast";

static SRTSOCKET srtsocket = SRT_INVALID_SOCK;
static int notify_pipe[2];
static JNIEnv *env_;

static pthread_mutex_t pipeMutex = PTHREAD_MUTEX_INITIALIZER;

static class PipeLock {
public:
    PipeLock() { pthread_mutex_lock(&pipeMutex); }
    ~PipeLock() { pthread_mutex_unlock(&pipeMutex); }
};


static struct SendBuffer {
    char buf[MAX_PACKET_SIZE];
    int len;
};
static std::list<SendBuffer> sendQueue;


static void process_read_srt(SRTSOCKET srtsocket1) {
    if (srtsocket1 == SRT_INVALID_SOCK) {
        return;
    }

    char buf[2000];
    int len = 2000;

    int ret = srt_recv(srtsocket1, (char *) buf, len);

    processPacket(env_, (char *) buf, ret);

    return;
}

static void process_read_pipe(int pipefd) {
    char buf[2000];
    int len = 1;

    int ret = read(pipefd, buf, len);
    if(ret <= 0){
        return;
    }

    SendBuffer sendBuffer;
    while(1)
    {
        {
            PipeLock lock;

            if(sendQueue.empty()){
                break;
            }else {
                sendBuffer = sendQueue.front();
                sendQueue.pop_front();
            }
        }

        LOG("Sending srt packet %d", sendBuffer.len);
        srt_send(srtsocket, sendBuffer.buf, sendBuffer.len);
    }

    return;
}



extern "C"
JNIEXPORT jint JNICALL
Java_com_polygraphene_alvr_SrtReceiverThread_initializeSocket(JNIEnv *env, jobject instance,
                                                                     jstring host_, jint port) {
    const char *host = env->GetStringUTFChars(host_, 0);
    struct sockaddr_in addr;
    int ret = 0;
    int optval = 1;

    initNAL();

    srt_startup();

    srtsocket = srt_socket(AF_INET, SOCK_DGRAM, 0);
    if (srtsocket == SRT_INVALID_SOCK) {
        ret = 1;
        goto end;
    }

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(addr.sin_family, host, &addr.sin_addr);

    srt_setsockflag(srtsocket, SRTO_STREAMID, VIDEO_STREAM_ID, strlen(VIDEO_STREAM_ID));

    optval = 1;
    srt_setsockflag(srtsocket, SRTO_RCVLATENCY, &optval, sizeof(optval));
    optval = 1;
    srt_setsockflag(srtsocket, SRTO_TSBPDDELAY, &optval, sizeof(optval));

    ret = srt_connect(srtsocket, (struct sockaddr *) &addr, sizeof(addr));

    pthread_mutex_init(&pipeMutex, NULL);

    pipe(notify_pipe);

    end:

    LOG("srt socket init");

    env->ReleaseStringUTFChars(host_, host);

    if (ret != 0) {
        srt_close(srtsocket);
        return ret;
    }
    return 0;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_SrtReceiverThread_closeSocket(JNIEnv *env, jobject instance) {
    if (srtsocket != SRT_INVALID_SOCK) {
        srt_close(srtsocket);
    }
}


extern "C"
JNIEXPORT jint JNICALL
Java_com_polygraphene_alvr_SrtReceiverThread_send(JNIEnv *env, jobject instance,
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
Java_com_polygraphene_alvr_SrtReceiverThread_getNalListSize(JNIEnv *env, jobject instance) {
    return getNalListSize();
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_polygraphene_alvr_UdpReceiverThread_waitNal(JNIEnv *env, jobject instance) {
return waitNal(env);
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_polygraphene_alvr_SrtReceiverThread_getNal(JNIEnv *env, jobject instance) {
    return getNal(env);
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_polygraphene_alvr_UdpReceiverThread_peekNal(JNIEnv *env, jobject instance) {
    return peekNal(env);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_SrtReceiverThread_flushNALList(JNIEnv *env, jobject instance) {
    flushNalList(env);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_SrtReceiverThread_runLoop(JNIEnv *env, jobject instance) {
    int epoll = srt_epoll_create();
    SRTSOCKET read_fds[1];
    SRTSOCKET write_fds[1];
    int read_sysfds[2];
    int write_sysfds[1];
    int read_n = 1;
    int write_n = 1;
    int read_sysn = 2;
    int write_sysn = 1;

    env_ = env;

    jclass clazz = env->GetObjectClass(instance);
    jmethodID id = env->GetMethodID(clazz, "isStopped", "()Z");

    int flags = SRT_EPOLL_IN/* | SRT_EPOLL_OUT | SRT_EPOLL_ERR*/;
    srt_epoll_add_usock(epoll, srtsocket, &flags);
    flags = SRT_EPOLL_IN/* | SRT_EPOLL_OUT | SRT_EPOLL_ERR*/;
    //srt_epoll_add_ssock(epoll, notify_pipe[0], &flags);
    //srt_epoll_add_ssock(epoll, notify_pipe[1], &flags);


    int flags2 = fcntl(notify_pipe[0], F_GETFL, 0);
    fcntl(notify_pipe[0], F_SETFL, flags2 | O_NONBLOCK);
    flags2 = fcntl(notify_pipe[1], F_GETFL, 0);
    fcntl(notify_pipe[1], F_SETFL, flags2 | O_NONBLOCK);

    while (1) {
        if(env->CallBooleanMethod(instance, id)){
            LOG("epoll stop");
            break;
        }
        int ret = srt_epoll_wait(epoll, read_fds, &read_n, write_fds, &write_n, 10, read_sysfds,
                             &read_sysn, write_sysfds, &write_sysn);

        process_read_pipe(notify_pipe[0]);
        if(ret < 0){
            if(srt_getlasterror(NULL) == SRT_ETIMEOUT) {
                continue;
            }
            LOG("epoll error:%d %d %s", ret, srt_getlasterror(NULL), srt_getlasterror_str());
            break;
        }
        SRT_SOCKSTATUS status = srt_getsockstate(srtsocket);
        //LOG("status: %d", status);
        if(ret == 0){
            usleep(10 * 1000);
            continue;
        }
        //LOG("epoll ok %d %d %d %d %d", ret, read_n, read_sysn, write_n, write_sysn);

        if(read_n >= 1){
            process_read_srt(read_fds[0]);
        }
        if(read_sysn >= 1){
            //LOG("pip:%d %d,%d", read_sysfds[0], notify_pipe[0], notify_pipe[1]);
            process_read_pipe(read_sysfds[0]);
        }
    }

    srt_epoll_release(epoll);
}
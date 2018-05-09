#include <jni.h>
#include <string>
#include <stdlib.h>
#include <srt.h>
#include <udt.h>
#include <list>

jfieldID buf_field = NULL;
jfieldID sequence_field = NULL;
int parseState = 0;
int parseSubState = 0;
jbyteArray currentBuf = NULL;
char *cbuf = NULL;
int bufferLength = 0;
int bufferPos = 0;
uint64_t presentationTime;
const int INITIAL_LENGTH = 30000;
JNIEnv *env_ = NULL;

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

class MutexLock {
public:
    MutexLock() { pthread_mutex_lock(&mutex); }

    ~MutexLock() { pthread_mutex_unlock(&mutex); }
};

struct NALBuffer {
    jbyteArray array;
    int len;
    uint64_t presentationTime;
};
std::list<NALBuffer> nalList;

void processPacket(char *buf, int len);

extern "C"
JNIEXPORT jint JNICALL
Java_com_polygraphene_remoteglass_SrtReceiverThread_initializeSocket(JNIEnv *env, jobject instance,
                                                                     jstring host_, jint port,
                                                                     jbyteArray socket_) {
    const char *host = env->GetStringUTFChars(host_, 0);
    jbyte *socket_2 = env->GetByteArrayElements(socket_, NULL);
    struct sockaddr_in addr;
    int ret = 0;

    parseState = 0;

    srt_startup();

    SRTSOCKET socket = srt_socket(AF_INET, SOCK_DGRAM, 0);
    if (socket == SRT_INVALID_SOCK) {
        *(SRTSOCKET *) socket_2 = SRT_INVALID_SOCK;
        ret = 1;
        goto end;
    }

    *(SRTSOCKET *) socket_2 = socket;

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(addr.sin_family, host, &addr.sin_addr);

    ret = srt_connect(socket, (struct sockaddr *) &addr, sizeof(addr));

    pthread_mutex_init(&mutex, NULL);

    end:

    env->ReleaseStringUTFChars(host_, host);
    env->ReleaseByteArrayElements(socket_, socket_2, 0);

    if (ret != 0) {
        srt_close(socket);
        return ret;
    }
    return 0;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_remoteglass_SrtReceiverThread_closeSocket(JNIEnv *env, jobject instance,
                                                                jbyteArray socket_) {
    jbyte *socket = env->GetByteArrayElements(socket_, NULL);

    SRTSOCKET socket_2 = *(SRTSOCKET *) socket;
    if (socket_2 != SRT_INVALID_SOCK) {
        srt_close(socket_2);
    }

    env->ReleaseByteArrayElements(socket_, socket, 0);
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_polygraphene_remoteglass_SrtReceiverThread_recv(JNIEnv *env, jobject instance,
                                                         jbyteArray socket_, jobject packet) {
    env_ = env;
    jbyte *socket = env->GetByteArrayElements(socket_, NULL);
    if (buf_field == NULL) {
        jclass clazz = env->GetObjectClass(packet);
        buf_field = env->GetFieldID(clazz, "buf", "[B");
        sequence_field = env->GetFieldID(clazz, "sequence", "J");
    }
    jbyteArray buf_ = (jbyteArray) env->GetObjectField(packet, buf_field);

    int len = env->GetArrayLength(buf_);
    jbyte *buf = env->GetByteArrayElements(buf_, NULL);

    SRTSOCKET socket_2 = *(SRTSOCKET *) socket;

    if (socket_2 == SRT_INVALID_SOCK) {
        return -1;
    }

    int ret = srt_recv(socket_2, (char *) buf, len);

    processPacket((char *) buf, ret);

    env->SetLongField(packet, sequence_field, *(uint32_t *) buf);

    env->ReleaseByteArrayElements(buf_, buf, 0);
    env->ReleaseByteArrayElements(socket_, socket, 0);

    return ret;
}

void allocateBuffer(int length) {
    if (currentBuf != NULL) {
        env_->ReleaseByteArrayElements(currentBuf, (jbyte *) cbuf, 0);
        env_->DeleteGlobalRef(currentBuf);
    }
    currentBuf = env_->NewByteArray(length);
    currentBuf = (jbyteArray) env_->NewGlobalRef(currentBuf);
    bufferLength = length;
    bufferPos = 0;

    cbuf = (char *) env_->GetByteArrayElements(currentBuf, NULL);
}

void expandBuffer() {
    // Allocate new buffer
    jbyteArray newBuf = env_->NewByteArray(bufferLength * 2);
    newBuf = (jbyteArray) env_->NewGlobalRef(newBuf);
    char *newcBuf = (char *) env_->GetByteArrayElements(newBuf, NULL);

    // Copy
    if (bufferPos != 0) {
        memcpy(newcBuf, cbuf, bufferPos);
    }

    // Delete old buffer
    env_->ReleaseByteArrayElements(currentBuf, (jbyte *) cbuf, 0);
    env_->DeleteGlobalRef(currentBuf);

    // Replace current buffer
    currentBuf = newBuf;
    cbuf = newcBuf;
    bufferLength = bufferLength * 2;
}

void append(char c) {
    if (bufferPos + 1 >= bufferLength) {
        expandBuffer();
    }
    cbuf[bufferPos++] = c;
}

void processPacket(char *buf, int len) {
    uint32_t sequence = *(uint32_t *) buf;
    int pos = sizeof(sequence);

    if (sequence & (1 << 31)) {
        sequence &= ~(1 << 31);
        presentationTime = *(uint64_t *) (buf + pos);
        pos += 8;
    }

    for (; pos < len; pos++) {
        char c = buf[pos];
        switch (parseState) {
            case 0:
                if (c == 0) {
                    parseState = 1;
                    allocateBuffer(INITIAL_LENGTH);
                    append(c);
                } else {
                    // Ignore until valid NAL appeared
                }
                break;
            case 1:
                if (c == 0) {
                    parseState = 2;
                    append(c);
                } else {
                    // Invalid NAL header
                    parseState = 0;
                }
                break;
            case 2:
                if (c == 0) {
                    parseState = 3;
                    append(c);
                } else {
                    // Invalid NAL header
                    parseState = 0;
                }
                break;
            case 3:
                if (c == 1) {
                    parseState = 4;
                    append(c);
                    parseSubState = 0;
                } else {
                    // Invalid NAL header
                    parseState = 0;
                }
                break;
            case 4:
                // NAL body
                if (c == 0) {
                    parseSubState++;
                } else if (c == 1) {
                    if (parseSubState == 2) {
                        // Convert 00 00 01 to 00 00 00 01 for android H.264 decoder
                        append(0);
                    } else if (parseSubState == 3) {
                        // End of NAL
                        NALBuffer buf;
                        buf.len = bufferPos - 3;
                        buf.array = currentBuf;
                        buf.presentationTime = presentationTime;
                        currentBuf = NULL;

                        {
                            MutexLock lock;
                            nalList.push_back(buf);
                        }

                        parseState = 4;
                        allocateBuffer(INITIAL_LENGTH);
                        append(0);
                        append(0);
                        append(0);
                    }
                    parseSubState = 0;
                } else if (c == 2) {
                    // Detect padding
                    if (parseSubState == 3) {
                        // End of NAL
                        NALBuffer buf;
                        buf.len = bufferPos - 3;
                        buf.array = currentBuf;
                        buf.presentationTime = presentationTime;

                        env_->ReleaseByteArrayElements(currentBuf, (jbyte *) cbuf, 0);
                        currentBuf = NULL;
                        {
                            MutexLock lock;
                            nalList.push_back(buf);
                        }

                        parseSubState = 0;
                        parseState = 0;
                        break;
                    }
                    parseSubState = 0;
                } else {
                    parseSubState = 0;
                }
                append(c);
                break;
        }
    }
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_polygraphene_remoteglass_SrtReceiverThread_getNalListSize(JNIEnv *env, jobject instance) {
    MutexLock lock;
    return nalList.size();
}

extern "C"
JNIEXPORT jobject JNICALL
Java_com_polygraphene_remoteglass_SrtReceiverThread_getNal(JNIEnv *env, jobject instance) {
    NALBuffer buf;
    {
        MutexLock lock;
        if (nalList.size() == 0) {
            return NULL;
        }
        buf = nalList.front();
        nalList.pop_front();
    }
    jclass clazz = env->FindClass("com/polygraphene/remoteglass/NAL");
    jmethodID ctor = env->GetMethodID(clazz, "<init>", "()V");

    jobject nal = env->NewObject(clazz, ctor);
    jfieldID len_ = env->GetFieldID(clazz, "len", "I");
    jfieldID presentationTime_ = env->GetFieldID(clazz, "presentationTime", "J");
    jfieldID buf_ = env->GetFieldID(clazz, "buf", "[B");

    env->SetIntField(nal, len_, buf.len);
    env->SetLongField(nal, presentationTime_, buf.presentationTime);
    env->SetObjectField(nal, buf_, buf.array);
    env->DeleteGlobalRef(buf.array);

    return nal;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_remoteglass_SrtReceiverThread_flushNALList(JNIEnv *env, jobject instance) {
    MutexLock lock;
    for (auto it = nalList.begin();
         it != nalList.end(); ++it) {
        env->DeleteGlobalRef(it->array);
    }
    nalList.clear();
}
/// H.264 NAL Parser functions
// Extract NAL Units from packet by UDP/SRT socket.
////////////////////////////////////////////////////////////////////

#include <jni.h>
#include <string>
#include <stdlib.h>
#include <list>
#include <android/log.h>
#include <pthread.h>
#include "nal.h"
#include "utils.h"

bool initialized = false;

// NAL parser status
int parseState = 0;
int parseSubState = 0;

// Initial length of NAL buffer
const int INITIAL_LENGTH = 30000;

// NAL buffer
jbyteArray currentBuf = NULL;
char *cbuf = NULL;
int bufferLength = 0;
int bufferPos = 0;

// Current NAL meta data from packet
uint64_t presentationTime;
uint64_t frameIndex;

static JNIEnv *env_;

// Parsed NAL queue
struct NALBuffer {
    jbyteArray array;
    int len;
    uint64_t presentationTime;
    uint64_t frameIndex;
};
std::list<NALBuffer> nalList;
Mutex nalMutex;

static pthread_cond_t cond_nonzero =  PTHREAD_COND_INITIALIZER;

static void allocateBuffer(int length) {
    if (currentBuf != NULL) {
        env_->ReleaseByteArrayElements(currentBuf, (jbyte *) cbuf, 0);
        env_->DeleteGlobalRef(currentBuf);
    }
    currentBuf = env_->NewByteArray(length);
    jobject tmp = currentBuf;
    currentBuf = (jbyteArray) env_->NewGlobalRef(currentBuf);
    env_->DeleteLocalRef(tmp);
    bufferLength = length;
    bufferPos = 0;

    cbuf = (char *) env_->GetByteArrayElements(currentBuf, NULL);
}

static void expandBuffer() {
    // Allocate new buffer
    jbyteArray newBuf = env_->NewByteArray(bufferLength * 2);
    jobject tmp = newBuf;
    newBuf = (jbyteArray) env_->NewGlobalRef(newBuf);
    env_->DeleteLocalRef(tmp);
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

static void append(char c) {
    if (bufferPos + 1 >= bufferLength) {
        expandBuffer();
    }
    cbuf[bufferPos++] = c;
}

void initNAL() {
    pthread_cond_init(&cond_nonzero, NULL);

    parseState = 0;
    initialized = true;
}

bool processPacket(JNIEnv *env, char *buf, int len) {
    if (!initialized) {
        initNAL();
    }

    bool newNalParsed = false;

    env_ = env;

    uint32_t type = *(uint32_t *) buf;
    int pos = sizeof(uint32_t);
    uint32_t sequence = *(uint32_t *) (buf + pos);
    pos += sizeof(uint32_t);

    if (type == 1){
        presentationTime = *(uint64_t *) (buf + pos);
        pos += sizeof(uint64_t);
        frameIndex = *(uint64_t *) (buf + pos);
        pos += sizeof(uint64_t);
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
                        buf.frameIndex = frameIndex;
                        currentBuf = NULL;
                        newNalParsed = true;

                        {
                            MutexLock lock(nalMutex);

                            nalList.push_back(buf);

                            pthread_cond_broadcast(&cond_nonzero);
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
                        buf.frameIndex = frameIndex;
                        newNalParsed = true;

                        env_->ReleaseByteArrayElements(currentBuf, (jbyte *) cbuf, 0);
                        currentBuf = NULL;
                        {
                            MutexLock lock(nalMutex);

                            nalList.push_back(buf);

                            pthread_cond_broadcast(&cond_nonzero);
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
    return newNalParsed;
}


jobject waitNal(JNIEnv *env) {
    if (!initialized) {
        initNAL();
    }

    NALBuffer buf;

    while(true){
        MutexLock lock(nalMutex);
        if (nalList.size() != 0) {
            buf = nalList.front();
            nalList.pop_front();
            break;
        }
        nalMutex.CondWait(&cond_nonzero);
    }
    jclass clazz = env->FindClass("com/polygraphene/remoteglass/NAL");
    jmethodID ctor = env->GetMethodID(clazz, "<init>", "()V");

    jobject nal = env->NewObject(clazz, ctor);
    jfieldID len_ = env->GetFieldID(clazz, "len", "I");
    jfieldID presentationTime_ = env->GetFieldID(clazz, "presentationTime", "J");
    jfieldID frameIndex_ = env->GetFieldID(clazz, "frameIndex", "J");
    jfieldID buf_ = env->GetFieldID(clazz, "buf", "[B");

    env->SetIntField(nal, len_, buf.len);
    env->SetLongField(nal, presentationTime_, buf.presentationTime);
    env->SetLongField(nal, frameIndex_, buf.frameIndex);
    env->SetObjectField(nal, buf_, env->NewLocalRef(buf.array));
    env->DeleteGlobalRef(buf.array);

    return nal;
}

jobject getNal(JNIEnv *env) {
    if (!initialized) {
        initNAL();
    }

    NALBuffer buf;
    {
        MutexLock lock(nalMutex);
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
    jfieldID frameIndex_ = env->GetFieldID(clazz, "frameIndex", "J");
    jfieldID buf_ = env->GetFieldID(clazz, "buf", "[B");

    env->SetIntField(nal, len_, buf.len);
    env->SetLongField(nal, presentationTime_, buf.presentationTime);
    env->SetLongField(nal, frameIndex_, buf.frameIndex);
    env->SetObjectField(nal, buf_, env->NewLocalRef(buf.array));
    env->DeleteGlobalRef(buf.array);

    return nal;
}

jobject peekNal(JNIEnv *env) {
    if (!initialized) {
        initNAL();
    }

    NALBuffer buf;
    {
        MutexLock lock(nalMutex);
        if (nalList.size() == 0) {
            return NULL;
        }
        buf = nalList.front();
    }
    jclass clazz = env->FindClass("com/polygraphene/remoteglass/NAL");
    jmethodID ctor = env->GetMethodID(clazz, "<init>", "()V");

    jobject nal = env->NewObject(clazz, ctor);
    jfieldID len_ = env->GetFieldID(clazz, "len", "I");
    jfieldID presentationTime_ = env->GetFieldID(clazz, "presentationTime", "J");
    jfieldID frameIndex_ = env->GetFieldID(clazz, "frameIndex", "J");
    jfieldID buf_ = env->GetFieldID(clazz, "buf", "[B");

    env->SetIntField(nal, len_, buf.len);
    env->SetLongField(nal, presentationTime_, buf.presentationTime);
    env->SetLongField(nal, frameIndex_, buf.frameIndex);
    env->SetObjectField(nal, buf_, env->NewLocalRef(buf.array));

    return nal;
}


int getNalListSize() {
    if (!initialized) {
        initNAL();
    }
    MutexLock lock(nalMutex);
    return nalList.size();
}


void flushNalList(JNIEnv *env) {
    if (!initialized) {
        initNAL();
    }
    MutexLock lock(nalMutex);
    for (auto it = nalList.begin();
         it != nalList.end(); ++it) {
        env->DeleteGlobalRef(it->array);
    }
    nalList.clear();
}
#ifndef ALVRCLIENT_NAL_H
#define ALVRCLIENT_NAL_H

#include <jni.h>
#include <list>
#include "utils.h"
#include "fec.h"


class NALParser {
public:
    NALParser(JNIEnv *env, std::function<void(JNIEnv *env, jobject nal)> nalCallback);
    ~NALParser();

    void setCodec(int codec);

    bool processPacket(VideoFrame *packet, int packetSize, bool &fecFailure);

    void recycle(JNIEnv *env, jobject nal);

    bool fecFailure();
private:
    void push(const char *buffer, int length, uint64_t frameIndex);
    void pushNal(jobject nal);
    int findVPSSPS(const char *frameBuffer, int frameByteSize);

    FECQueue m_queue;

    int m_codec = 1;

    JNIEnv *m_env;

    std::function<void(JNIEnv *env, jobject nal)> mNalCallback;

    // Parsed NAL queue
    std::list<jobject> m_nalList;
    std::list<jobject> m_nalRecycleList;
    Mutex m_nalMutex;

    pthread_cond_t m_cond_nonzero = PTHREAD_COND_INITIALIZER;

    jfieldID NAL_length;
    jfieldID NAL_frameIndex;
    jfieldID NAL_buf;
};
#endif //ALVRCLIENT_NAL_H


#include "latency_collector.h"

std::shared_ptr<LatencyCollector> g_latencyCollector;

LatencyCollector::LatencyCollector(JNIEnv *env, jobject latencyCollector){
    latencyCollector_ = latencyCollector;

    latencyCollectorClass_ = env->GetObjectClass(latencyCollector_);

    latencyCollectorEstimatedSent_ = env->GetMethodID(latencyCollectorClass_, "EstimatedSent", "(JJ)V");
    latencyCollectorReceivedFirst_ = env->GetMethodID(latencyCollectorClass_, "ReceivedFirst", "(J)V");
    latencyCollectorReceivedLast_ = env->GetMethodID(latencyCollectorClass_, "ReceivedLast", "(J)V");
    latencyCollectorPacketLoss_ = env->GetMethodID(latencyCollectorClass_, "PacketLoss", "(J)V");
    latencyCollectorFecFailure_ = env->GetMethodID(latencyCollectorClass_, "FecFailure", "()V");
    latencyCollectorGetLatency_ = env->GetMethodID(latencyCollectorClass_, "GetLatency", "(II)J");
    latencyCollectorGetPacketsLostTotal_ = env->GetMethodID(latencyCollectorClass_, "GetPacketsLostTotal", "()J");
    latencyCollectorGetPacketsLostInSecond_ = env->GetMethodID(latencyCollectorClass_, "GetPacketsLostInSecond", "()J");
    latencyCollectorGetFecFailureTotal_ = env->GetMethodID(latencyCollectorClass_, "GetFecFailureTotal", "()J");
    latencyCollectorGetFecFailureInSecond_ = env->GetMethodID(latencyCollectorClass_, "GetFecFailureInSecond", "()J");
    latencyCollectorResetAll_ = env->GetMethodID(latencyCollectorClass_, "ResetAll", "()V");
}

void LatencyCollector::recordEstimatedSent(JNIEnv *env, uint64_t frameIndex, uint64_t estimetedSentTime){
    env->CallVoidMethod(latencyCollector_, latencyCollectorEstimatedSent_, frameIndex, estimetedSentTime);
}
void LatencyCollector::recordFirstPacketReceived(JNIEnv *env, uint64_t frameIndex){
    env->CallVoidMethod(latencyCollector_, latencyCollectorReceivedFirst_, frameIndex);
}
void LatencyCollector::recordLastPacketReceived(JNIEnv *env, uint64_t frameIndex){
    env->CallVoidMethod(latencyCollector_, latencyCollectorReceivedLast_, frameIndex);
}
void LatencyCollector::recordPacketLoss(JNIEnv *env, int64_t lost) {
    env->CallVoidMethod(latencyCollector_, latencyCollectorPacketLoss_, lost);
}
void LatencyCollector::recordFecFailure(JNIEnv *env) {
    env->CallVoidMethod(latencyCollector_, latencyCollectorFecFailure_);
}
uint64_t LatencyCollector::getLatency(JNIEnv *env, uint32_t i, uint32_t j) {
    return env->CallLongMethod(latencyCollector_, latencyCollectorGetLatency_, i, j);
}
uint64_t LatencyCollector::getPacketsLostTotal(JNIEnv *env) {
    return env->CallLongMethod(latencyCollector_, latencyCollectorGetPacketsLostTotal_);
}
uint64_t LatencyCollector::getPacketsLostInSecond(JNIEnv *env) {
    return env->CallLongMethod(latencyCollector_, latencyCollectorGetPacketsLostInSecond_);
}
uint64_t LatencyCollector::getFecFailureTotal(JNIEnv *env) {
    return env->CallLongMethod(latencyCollector_, latencyCollectorGetFecFailureTotal_);
}
uint64_t LatencyCollector::getFecFailureInSecond(JNIEnv *env) {
    return env->CallLongMethod(latencyCollector_, latencyCollectorGetFecFailureInSecond_);
}
void LatencyCollector::resetAll(JNIEnv *env) {
    env->CallVoidMethod(latencyCollector_, latencyCollectorResetAll_);
}
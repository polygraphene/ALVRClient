#include "asset.h"

AAssetManager *g_assetManager = NULL;
jobject g_javaAssetManager = NULL;

void setAssetManager(JNIEnv *env, jobject assetManager) {
    if (g_assetManager == NULL) {
        g_javaAssetManager = env->NewGlobalRef(assetManager);
        g_assetManager = AAssetManager_fromJava(env, g_javaAssetManager);
    }
}

bool loadAsset(const char *path, std::vector<unsigned char> &buffer) {
    AAsset *asset = AAssetManager_open(g_assetManager, path, AASSET_MODE_STREAMING);
    if (asset == NULL) {
        return false;
    }

    int length = AAsset_getLength(asset);
    buffer.resize(length);

    if (AAsset_read(asset, &buffer[0], length) != length) {
        AAsset_close(asset);
        return false;
    }

    AAsset_close(asset);
    return true;
}
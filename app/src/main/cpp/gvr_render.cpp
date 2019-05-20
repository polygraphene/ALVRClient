//
// Created by SxP on 9/15/2018.
//

#include <VrApi_Helpers.h>

#include "asset.h"
#include "gvr_render.h"
#include "latency_collector.h"

GvrRenderer::GvrRenderer() {
}

void GvrRenderer::glInit(JNIEnv* env, int width, int height) {
    GLuint textures[2];
    glGenTextures(2, textures);

    SurfaceTextureID = textures[0];
    loadingTexture = textures[1];

    LOG("GvrRenderer::glInit: Surface=%d Loading=%d", SurfaceTextureID, loadingTexture);

    ovrRenderer_Create(&Renderer, false, width, height, SurfaceTextureID, loadingTexture, 0, false);
    ovrRenderer_CreateScene(&Renderer);
}


void GvrRenderer::renderFrame(ovrMatrix4f mvpMatrix[2], Recti viewport[2], bool loading) {
    mRendererInitialized = true;
    renderEye(0, mvpMatrix, &viewport[0], &Renderer, loading, false);
    renderEye(1, mvpMatrix, &viewport[1], &Renderer, loading, false);
}

GvrRenderer::~GvrRenderer() {
    if (mRendererInitialized) {
        ovrRenderer_Destroy(&Renderer);
    }
    //GLuint textures[2] = {SurfaceTextureID, loadingTexture};
    //glDeleteTextures(2, textures);
}

extern "C"
JNIEXPORT jlong JNICALL
Java_com_polygraphene_alvr_GvrRenderer_createNative(JNIEnv *env, jobject gvrRenderer, jobject assetManager) {
    setAssetManager(env, assetManager);
    return (jlong) new GvrRenderer();
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_GvrRenderer_glInitNative(JNIEnv *env, jobject gvrRenderer, jlong instance, jint width, jint height) {
    ((GvrRenderer *) instance)->glInit(env, width, height);
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_polygraphene_alvr_GvrRenderer_getLoadingTexture(JNIEnv *env, jobject gvrRenderer, jlong instance) {
    return ((GvrRenderer *) instance)->getLoadingTexture();
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_polygraphene_alvr_GvrRenderer_getSurfaceTexture(JNIEnv *env, jobject instance,
                                                         jlong nativeHandle) {
    return ((GvrRenderer *) nativeHandle)->getSurfaceTexture();
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_GvrRenderer_renderNative(JNIEnv *env, jobject gvrRenderer,
                                                    jlong instance, jfloatArray leftMvp,
                                                    jfloatArray rightMvp, jintArray leftViewport,
                                                    jintArray rightViewport, jboolean loading, jlong frameIndex) {
    ovrMatrix4f mvpMatrix[2];

    {
        jfloat *array = env->GetFloatArrayElements(leftMvp, 0);
        memcpy(mvpMatrix[0].M, array, 16 * sizeof(float));
        mvpMatrix[0] = ovrMatrix4f_Transpose(&mvpMatrix[0]);
        env->ReleaseFloatArrayElements(leftMvp, array, 0);

        array = env->GetFloatArrayElements(rightMvp, 0);
        memcpy(mvpMatrix[1].M, array, 16 * sizeof(float));
        mvpMatrix[1] = ovrMatrix4f_Transpose(&mvpMatrix[1]);
        env->ReleaseFloatArrayElements(rightMvp, array, 0);
    }

    Recti viewport[2];

    {
        jint *array = env->GetIntArrayElements(leftViewport, 0);
        memcpy(&viewport[0], array, 4 * sizeof(float));
        env->ReleaseIntArrayElements(leftViewport, array, 0);

        array = env->GetIntArrayElements(rightViewport, 0);
        memcpy(&viewport[1], array, 4 * sizeof(float));
        env->ReleaseIntArrayElements(rightViewport, array, 0);
    }

    LatencyCollector::Instance().rendered1(frameIndex);
    ((GvrRenderer *) instance)->renderFrame(mvpMatrix, viewport, loading);
    LatencyCollector::Instance().rendered2(frameIndex);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_GvrRenderer_destroyNative(JNIEnv *env, jobject gvrRenderer, jlong instance) {
    delete ((GvrRenderer *) instance);
}


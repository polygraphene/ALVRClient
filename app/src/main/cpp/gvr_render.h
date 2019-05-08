// Daydream-specific rendering logic. This calls the code in render.h but doesn't fully initialize
// it.
//

#ifndef ALVRCLIENT_GVR_RENDER_H
#define ALVRCLIENT_GVR_RENDER_H

#include "render.h"

class GvrRenderer {
public:
    GvrRenderer();
    void glInit(JNIEnv* env, int width, int height);
    void renderFrame(ovrMatrix4f mvpMatrix[2], Recti viewports[2], bool loading);
    ~GvrRenderer();

    int getLoadingTexture() {return loadingTexture;};

    int getSurfaceTexture() { return SurfaceTextureID; }

private:
    bool mRendererInitialized = false;
    ovrRenderer Renderer;
    GLuint SurfaceTextureID = 0;
    GLuint loadingTexture = 0;
};

#endif //ALVRCLIENT_GVR_RENDER_H

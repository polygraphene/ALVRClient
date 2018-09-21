// Daydream-specific rendering logic. This calls the code in render.h but doesn't fully initialize
// it.
//

#ifndef ALVRCLIENT_GVR_CONTEXT_H
#define ALVRCLIENT_GVR_CONTEXT_H

class GvrContext {
public:
    void ovrRenderer_Create(ovrRenderer *renderer, const ovrJava *java, const bool useMultiview, int width, int height,
                            int SurfaceTextureID, int LoadingTexture, int CameraTexture, bool ARMode);
    void ovrRenderer_Destroy(ovrRenderer *renderer);

};


#endif //ALVRCLIENT_GVR_CONTEXT_H

#ifndef ALVRCLIENT_RENDER_H
#define ALVRCLIENT_RENDER_H

#include <VrApi.h>
#include <VrApi_Types.h>
#include <VrApi_Helpers.h>
#include <VrApi_SystemUtils.h>
#include <VrApi_Input.h>
#include <GLES3/gl3.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include "gltf_model.h"

// Must use EGLSyncKHR because the VrApi still supports OpenGL ES 2.0
#define EGL_SYNC

struct Render_EGL {
    EGLDisplay Display;
    EGLConfig Config;
    EGLSurface TinySurface;
    EGLSurface MainSurface;
    EGLContext Context;
};
extern Render_EGL egl;

void eglInit();
void eglDestroy();
void EglInitExtensions(bool *multi_view);

//
// ovrFence
//

typedef struct {
#if defined( EGL_SYNC )
    EGLDisplay Display;
    EGLSyncKHR Sync;
#else
    GLsync		Sync;
#endif
} ovrFence;

void ovrFence_Create(ovrFence *fence);
void ovrFence_Destroy(ovrFence *fence);
void ovrFence_Insert(ovrFence *fence);

//
// ovrFramebuffer
//

typedef struct {
    int Width;
    int Height;
    int Multisamples;
    int TextureSwapChainLength;
    int TextureSwapChainIndex;
    bool UseMultiview;
    ovrTextureSwapChain *ColorTextureSwapChain;
    GLuint *DepthBuffers;
    GLuint *FrameBuffers;
} ovrFramebuffer;

void ovrFramebuffer_Clear(ovrFramebuffer *frameBuffer);
bool ovrFramebuffer_Create(ovrFramebuffer *frameBuffer, const bool useMultiview,
                                  const ovrTextureFormat colorFormat, const int width,
                                  const int height, const int multisamples);
void ovrFramebuffer_Destroy(ovrFramebuffer *frameBuffer);
void ovrFramebuffer_SetCurrent(ovrFramebuffer *frameBuffer);
void ovrFramebuffer_SetNone();
void ovrFramebuffer_Resolve(ovrFramebuffer *frameBuffer);
void ovrFramebuffer_Advance(ovrFramebuffer *frameBuffer);

//
// ovrGeometry
//

typedef struct {
    GLuint Index;
    GLint Size;
    GLenum Type;
    GLboolean Normalized;
    GLsizei Stride;
    const GLvoid *Pointer;
} ovrVertexAttribPointer;

static const int MAX_VERTEX_ATTRIB_POINTERS = 5;

typedef struct {
    GLuint VertexBuffer;
    GLuint IndexBuffer;
    GLuint VertexArrayObject;
    GLuint VertexUVBuffer;
    int VertexCount;
    int IndexCount;
    ovrVertexAttribPointer VertexAttribs[MAX_VERTEX_ATTRIB_POINTERS];
} ovrGeometry;

enum VertexAttributeLocation {
    VERTEX_ATTRIBUTE_LOCATION_POSITION,
    VERTEX_ATTRIBUTE_LOCATION_COLOR,
    VERTEX_ATTRIBUTE_LOCATION_UV,
    VERTEX_ATTRIBUTE_LOCATION_TRANSFORM,
    VERTEX_ATTRIBUTE_LOCATION_NORMAL
};

void ovrGeometry_Clear(ovrGeometry *geometry);
void ovrGeometry_CreatePanel(ovrGeometry *geometry);
void ovrGeometry_CreateTestMode(ovrGeometry *geometry);
void ovrGeometry_Destroy(ovrGeometry *geometry);
void ovrGeometry_CreateVAO(ovrGeometry *geometry);
void ovrGeometry_DestroyVAO(ovrGeometry *geometry);

//
// ovrProgram
//

static const int MAX_PROGRAM_UNIFORMS = 8;
static const int MAX_PROGRAM_TEXTURES = 8;

typedef struct {
    GLuint Program;
    GLuint VertexShader;
    GLuint FragmentShader;
    // These will be -1 if not used by the program.
    GLint UniformLocation[MAX_PROGRAM_UNIFORMS];    // ProgramUniforms[].name
    GLint UniformBinding[MAX_PROGRAM_UNIFORMS];    // ProgramUniforms[].name
    GLint Textures[MAX_PROGRAM_TEXTURES];            // Texture%i
} ovrProgram;


bool
ovrProgram_Create(ovrProgram *program, const char *vertexSource, const char *fragmentSource,
                  const bool useMultiview);
void ovrProgram_Destroy(ovrProgram *program);

//
// ovrRenderer
//


typedef struct {
    ovrFramebuffer FrameBuffer[VRAPI_FRAME_LAYER_EYE_MAX];
    int NumBuffers;
    ovrFence *Fence;            // Per-frame completion fence
    int FenceIndex;
    bool UseMultiview;
    bool SceneCreated;
    ovrProgram Program;
    ovrProgram ProgramLoading;
    ovrGeometry Panel;
    ovrGeometry TestMode;
    int SurfaceTextureID;
    int CameraTexture;
    int LoadingTexture;
    bool ARMode;
    GltfModel *loadingScene;
} ovrRenderer;

void ovrRenderer_Clear(ovrRenderer *renderer);
void ovrRenderer_Create(ovrRenderer *renderer, const ovrJava *java, const bool useMultiview, int width, int height,
                        int SurfaceTextureID, int LoadingTexture, int CameraTexture, bool ARMode);
void ovrRenderer_Destroy(ovrRenderer *renderer);
void ovrRenderer_CreateScene(ovrRenderer *renderer);
ovrLayerProjection2 ovrRenderer_RenderFrame(ovrRenderer *renderer, const ovrJava *java,
                                                   const ovrTracking2 *tracking, ovrMobile *ovr,
                                                   unsigned long long *completionFence,
                                                   bool loading, int enableTestMode,
                                            int AROverlayMode);

#endif //ALVRCLIENT_RENDER_H

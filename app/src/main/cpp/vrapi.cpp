#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <jni.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <VrApi.h>
#include <VrApi_Types.h>
#include <VrApi_Helpers.h>
#include <VrApi_SystemUtils.h>
#include "utils.h"
#include "packet_types.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <string>
#include <list>

#define CHECK_GL_ERRORS 1
#if !defined( EGL_OPENGL_ES3_BIT_KHR )
#define EGL_OPENGL_ES3_BIT_KHR		0x0040
#endif

// EXT_texture_border_clamp
#ifndef GL_CLAMP_TO_BORDER
#define GL_CLAMP_TO_BORDER            0x812D
#endif

#ifndef GL_TEXTURE_BORDER_COLOR
#define GL_TEXTURE_BORDER_COLOR        0x1004
#endif

#if !defined( GL_EXT_multisampled_render_to_texture )

typedef void (GL_APIENTRY *PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXTPROC)(GLenum target,
                                                                       GLsizei samples,
                                                                       GLenum internalformat,
                                                                       GLsizei width,
                                                                       GLsizei height);

typedef void (GL_APIENTRY *PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEEXTPROC)(GLenum target,
                                                                        GLenum attachment,
                                                                        GLenum textarget,
                                                                        GLuint texture, GLint level,
                                                                        GLsizei samples);

#endif

#if !defined( GL_OVR_multiview )
static const int GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_NUM_VIEWS_OVR = 0x9630;
static const int GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_BASE_VIEW_INDEX_OVR = 0x9632;
static const int GL_MAX_VIEWS_OVR = 0x9631;

typedef void (GL_APIENTRY *PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC)(GLenum target,
                                                                    GLenum attachment,
                                                                    GLuint texture, GLint level,
                                                                    GLint baseViewIndex,
                                                                    GLsizei numViews);

#endif

#if !defined( GL_OVR_multiview_multisampled_render_to_texture )

typedef void (GL_APIENTRY *PFNGLFRAMEBUFFERTEXTUREMULTISAMPLEMULTIVIEWOVRPROC)(GLenum target,
                                                                               GLenum attachment,
                                                                               GLuint texture,
                                                                               GLint level,
                                                                               GLsizei samples,
                                                                               GLint baseViewIndex,
                                                                               GLsizei numViews);

#endif

// Must use EGLSyncKHR because the VrApi still supports OpenGL ES 2.0
#define EGL_SYNC

#if defined EGL_SYNC
// EGL_KHR_reusable_sync
PFNEGLCREATESYNCKHRPROC eglCreateSyncKHR;
PFNEGLDESTROYSYNCKHRPROC eglDestroySyncKHR;
PFNEGLCLIENTWAITSYNCKHRPROC eglClientWaitSyncKHR;
PFNEGLSIGNALSYNCKHRPROC eglSignalSyncKHR;
PFNEGLGETSYNCATTRIBKHRPROC eglGetSyncAttribKHR;
#endif


#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ALVR Native", __VA_ARGS__)
#define ALOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, "ALVR Native", __VA_ARGS__)

static const int NUM_MULTI_SAMPLES = 4;

ANativeWindow *window = NULL;
ovrMobile *Ovr;
bool UseMultiview = true;
GLuint SurfaceTextureID = 0;
GLuint loadingTexture = 0;
int enableTestMode = 0;
int suspend = 0;
bool Resumed = false;

uint64_t FrameIndex = 0;
uint64_t WantedFrameIndex = 0;


struct TrackingFrame {
    ovrTracking2 tracking;
    uint64_t frameIndex;
    uint64_t fetchTime;
    double displayTime;
};
std::list<std::shared_ptr<TrackingFrame> > trackingFrameList;
Mutex trackingFrameMutex;

static double GetTimeInSeconds() {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec * 1e9 + now.tv_nsec) * 0.000000001;
}

static std::string DumpMatrix(const ovrMatrix4f *matrix) {
    char buf[1000];
    sprintf(buf, "%.5f, %.5f, %.5f, %.5f\n"
                    "%.5f, %.5f, %.5f, %.5f\n"
                    "%.5f, %.5f, %.5f, %.5f\n"
                    "%.5f, %.5f, %.5f, %.5f\n", matrix->M[0][0], matrix->M[0][1], matrix->M[0][2],
            matrix->M[0][3], matrix->M[1][0], matrix->M[1][1], matrix->M[1][2], matrix->M[1][3],
            matrix->M[2][0], matrix->M[2][1], matrix->M[2][2], matrix->M[2][3], matrix->M[3][0],
            matrix->M[3][1], matrix->M[3][2], matrix->M[3][3]
    );
    return std::string(buf);
}

/*
================================================================================

OpenGL-ES Utility Functions

================================================================================
*/

typedef struct {
    bool multi_view;                        // GL_OVR_multiview, GL_OVR_multiview2
    bool EXT_texture_border_clamp;            // GL_EXT_texture_border_clamp, GL_OES_texture_border_clamp
} OpenGLExtensions_t;

OpenGLExtensions_t glExtensions;

static void EglInitExtensions() {
#if defined EGL_SYNC
    eglCreateSyncKHR = (PFNEGLCREATESYNCKHRPROC) eglGetProcAddress("eglCreateSyncKHR");
    eglDestroySyncKHR = (PFNEGLDESTROYSYNCKHRPROC) eglGetProcAddress("eglDestroySyncKHR");
    eglClientWaitSyncKHR = (PFNEGLCLIENTWAITSYNCKHRPROC) eglGetProcAddress("eglClientWaitSyncKHR");
    eglSignalSyncKHR = (PFNEGLSIGNALSYNCKHRPROC) eglGetProcAddress("eglSignalSyncKHR");
    eglGetSyncAttribKHR = (PFNEGLGETSYNCATTRIBKHRPROC) eglGetProcAddress("eglGetSyncAttribKHR");
#endif

    const char *allExtensions = (const char *) glGetString(GL_EXTENSIONS);
    if (allExtensions != NULL) {
        glExtensions.multi_view = strstr(allExtensions, "GL_OVR_multiview2") &&
                                  strstr(allExtensions,
                                         "GL_OVR_multiview_multisampled_render_to_texture");

        glExtensions.EXT_texture_border_clamp =
                strstr(allExtensions, "GL_EXT_texture_border_clamp") ||
                strstr(allExtensions, "GL_OES_texture_border_clamp");
    }
}

static const char *EglErrorString(const EGLint error) {
    switch (error) {
        case EGL_SUCCESS:
            return "EGL_SUCCESS";
        case EGL_NOT_INITIALIZED:
            return "EGL_NOT_INITIALIZED";
        case EGL_BAD_ACCESS:
            return "EGL_BAD_ACCESS";
        case EGL_BAD_ALLOC:
            return "EGL_BAD_ALLOC";
        case EGL_BAD_ATTRIBUTE:
            return "EGL_BAD_ATTRIBUTE";
        case EGL_BAD_CONTEXT:
            return "EGL_BAD_CONTEXT";
        case EGL_BAD_CONFIG:
            return "EGL_BAD_CONFIG";
        case EGL_BAD_CURRENT_SURFACE:
            return "EGL_BAD_CURRENT_SURFACE";
        case EGL_BAD_DISPLAY:
            return "EGL_BAD_DISPLAY";
        case EGL_BAD_SURFACE:
            return "EGL_BAD_SURFACE";
        case EGL_BAD_MATCH:
            return "EGL_BAD_MATCH";
        case EGL_BAD_PARAMETER:
            return "EGL_BAD_PARAMETER";
        case EGL_BAD_NATIVE_PIXMAP:
            return "EGL_BAD_NATIVE_PIXMAP";
        case EGL_BAD_NATIVE_WINDOW:
            return "EGL_BAD_NATIVE_WINDOW";
        case EGL_CONTEXT_LOST:
            return "EGL_CONTEXT_LOST";
        default:
            return "unknown";
    }
}

static const char *GlFrameBufferStatusString(GLenum status) {
    switch (status) {
        case GL_FRAMEBUFFER_UNDEFINED:
            return "GL_FRAMEBUFFER_UNDEFINED";
        case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
            return "GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT";
        case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
            return "GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT";
        case GL_FRAMEBUFFER_UNSUPPORTED:
            return "GL_FRAMEBUFFER_UNSUPPORTED";
        case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:
            return "GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE";
        default:
            return "unknown";
    }
}

#ifdef CHECK_GL_ERRORS

static const char *GlErrorString(GLenum error) {
    switch (error) {
        case GL_NO_ERROR:
            return "GL_NO_ERROR";
        case GL_INVALID_ENUM:
            return "GL_INVALID_ENUM";
        case GL_INVALID_VALUE:
            return "GL_INVALID_VALUE";
        case GL_INVALID_OPERATION:
            return "GL_INVALID_OPERATION";
        case GL_INVALID_FRAMEBUFFER_OPERATION:
            return "GL_INVALID_FRAMEBUFFER_OPERATION";
        case GL_OUT_OF_MEMORY:
            return "GL_OUT_OF_MEMORY";
        default:
            return "unknown";
    }
}

static void GLCheckErrors(int line) {
    for (int i = 0; i < 10; i++) {
        const GLenum error = glGetError();
        if (error == GL_NO_ERROR) {
            break;
        }
        ALOGE("GL error on line %d: %s", line, GlErrorString(error));
    }
}

#define GL(func)        func; GLCheckErrors( __LINE__ );

#else // CHECK_GL_ERRORS

#define GL(func)        func;

#endif // CHECK_GL_ERRORS

static const char VERTEX_SHADER[] =
        "#ifndef DISABLE_MULTIVIEW\n"
                "	#define DISABLE_MULTIVIEW 0\n"
                "#endif\n"
                "#define NUM_VIEWS 2\n"
                "#if defined( GL_OVR_multiview2 ) && ! DISABLE_MULTIVIEW\n"
                "	#extension GL_OVR_multiview2 : enable\n"
                "	layout(num_views=NUM_VIEWS) in;\n"
                "	#define VIEW_ID gl_ViewID_OVR\n"
                "#else\n"
                "	uniform lowp int ViewID;\n"
                "	#define VIEW_ID ViewID\n"
                "#endif\n"
                "in vec3 vertexPosition;\n"
                "in vec4 vertexColor;\n"
                "in mat4 vertexTransform;\n"
                "in vec2 vertexUv;\n"
                "uniform mat4 mvpMatrix[NUM_VIEWS];\n"
                "uniform lowp int EnableTestMode;\n"
                "out vec4 fragmentColor;\n"
                "out vec2 uv;\n"
                "void main()\n"
                "{\n"
                "	gl_Position = mvpMatrix[VIEW_ID] * vec4( vertexPosition, 1.0 );\n"
                "   if(VIEW_ID == uint(0)){\n"
                "      uv = vec2(vertexUv.x, vertexUv.y);\n"
                "   }else{\n"
                "      uv = vec2(vertexUv.x + 0.5, vertexUv.y);\n"
                "   }\n"
                "   fragmentColor = vertexColor;\n"
                "}\n";

static const char FRAGMENT_SHADER[] =
        "#extension GL_OES_EGL_image_external_essl3 : require\n"
                "#extension GL_OES_EGL_image_external : require\n"
                "in lowp vec2 uv;\n"
                "in lowp vec4 fragmentColor;\n"
                "out lowp vec4 outColor;\n"
                "uniform samplerExternalOES sTexture;\n"
                "uniform lowp int EnableTestMode;\n"
                "void main()\n"
                "{\n"
                "   if(EnableTestMode % 2 == 0){\n"
                "	    outColor = texture(sTexture, uv);\n"
                "   } else {\n"
                "       outColor = fragmentColor;\n"
                "   }\n"
                "}\n";

static const char VERTEX_SHADER_LOADING[] =
        "#ifndef DISABLE_MULTIVIEW\n"
                "	#define DISABLE_MULTIVIEW 0\n"
                "#endif\n"
                "#define NUM_VIEWS 2\n"
                "#if defined( GL_OVR_multiview2 ) && ! DISABLE_MULTIVIEW\n"
                "	#extension GL_OVR_multiview2 : enable\n"
                "	layout(num_views=NUM_VIEWS) in;\n"
                "	#define VIEW_ID gl_ViewID_OVR\n"
                "#else\n"
                "	uniform lowp int ViewID;\n"
                "	#define VIEW_ID ViewID\n"
                "#endif\n"
                "in vec3 vertexPosition;\n"
                "in vec4 vertexColor;\n"
                "in mat4 vertexTransform;\n"
                "in vec2 vertexUv;\n"
                "uniform mat4 mvpMatrix[NUM_VIEWS];\n"
                "uniform lowp int EnableTestMode;\n"
                "out vec4 fragmentColor;\n"
                "out vec2 uv;\n"
                "void main()\n"
                "{\n"
                "	gl_Position = mvpMatrix[VIEW_ID] * vec4( vertexPosition, 1.0 );\n"
                "   uv = vec2(vertexUv.x * 2.0, vertexUv.y);\n"
                "   fragmentColor = vertexColor;\n"
                "}\n";

static const char FRAGMENT_SHADER_LOADING[] =
        "in lowp vec2 uv;\n"
                "in lowp vec4 fragmentColor;\n"
                "out lowp vec4 outColor;\n"
                "uniform sampler2D sTexture;\n"
                "uniform lowp int EnableTestMode;\n"
                "void main()\n"
                "{\n"
                "   if(EnableTestMode % 2 == 0){\n"
                "	    outColor = texture(sTexture, uv);\n"
                "   } else {\n"
                "       outColor = fragmentColor;\n"
                "   }\n"
                "}\n";

struct {
    EGLDisplay Display;
    EGLConfig Config;
    EGLSurface TinySurface;
    EGLSurface MainSurface;
    EGLContext Context;
} egl;

void eglInit() {
    EGLint major, minor;

    egl.Display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(egl.Display, &major, &minor);

// Do NOT use eglChooseConfig, because the Android EGL code pushes in multisample
    // flags in eglChooseConfig if the user has selected the "force 4x MSAA" option in
    // settings, and that is completely wasted for our warp target.
    const int MAX_CONFIGS = 1024;
    EGLConfig configs[MAX_CONFIGS];
    EGLint numConfigs = 0;
    if (eglGetConfigs(egl.Display, configs, MAX_CONFIGS, &numConfigs) == EGL_FALSE) {
        ALOGE("        eglGetConfigs() failed: %s", EglErrorString(eglGetError()));
        return;
    }
    const EGLint configAttribs[] =
            {
                    EGL_RED_SIZE, 8,
                    EGL_GREEN_SIZE, 8,
                    EGL_BLUE_SIZE, 8,
                    EGL_ALPHA_SIZE, 8, // need alpha for the multi-pass timewarp compositor
                    EGL_DEPTH_SIZE, 0,
                    EGL_STENCIL_SIZE, 0,
                    EGL_SAMPLES, 0,
                    EGL_NONE
            };
    egl.Config = 0;
    for (int i = 0; i < numConfigs; i++) {
        EGLint value = 0;

        eglGetConfigAttrib(egl.Display, configs[i], EGL_RENDERABLE_TYPE, &value);
        if ((value & EGL_OPENGL_ES3_BIT_KHR) != EGL_OPENGL_ES3_BIT_KHR) {
            continue;
        }

        // The pbuffer config also needs to be compatible with normal window rendering
        // so it can share textures with the window context.
        eglGetConfigAttrib(egl.Display, configs[i], EGL_SURFACE_TYPE, &value);
        if ((value & (EGL_WINDOW_BIT | EGL_PBUFFER_BIT)) != (EGL_WINDOW_BIT | EGL_PBUFFER_BIT)) {
            continue;
        }

        int j = 0;
        for (; configAttribs[j] != EGL_NONE; j += 2) {
            eglGetConfigAttrib(egl.Display, configs[i], configAttribs[j], &value);
            if (value != configAttribs[j + 1]) {
                break;
            }
        }
        if (configAttribs[j] == EGL_NONE) {
            egl.Config = configs[i];
            break;
        }
    }
    if (egl.Config == 0) {
        ALOGE("        eglChooseConfig() failed: %s", EglErrorString(eglGetError()));
        return;
    }
    EGLint contextAttribs[] =
            {
                    EGL_CONTEXT_CLIENT_VERSION, 3,
                    EGL_NONE
            };
    ALOGV("        Context = eglCreateContext( Display, Config, EGL_NO_CONTEXT, contextAttribs )");
    egl.Context = eglCreateContext(egl.Display, egl.Config, EGL_NO_CONTEXT, contextAttribs);
    if (egl.Context == EGL_NO_CONTEXT) {
        ALOGE("        eglCreateContext() failed: %s", EglErrorString(eglGetError()));
        return;
    }
    const EGLint surfaceAttribs[] =
            {
                    EGL_WIDTH, 16,
                    EGL_HEIGHT, 16,
                    EGL_NONE
            };
    ALOGV("        TinySurface = eglCreatePbufferSurface( Display, Config, surfaceAttribs )");
    egl.TinySurface = eglCreatePbufferSurface(egl.Display, egl.Config, surfaceAttribs);
    if (egl.TinySurface == EGL_NO_SURFACE) {
        ALOGE("        eglCreatePbufferSurface() failed: %s", EglErrorString(eglGetError()));
        eglDestroyContext(egl.Display, egl.Context);
        egl.Context = EGL_NO_CONTEXT;
        return;
    }
    ALOGV("        eglMakeCurrent( Display, TinySurface, TinySurface, Context )");
    if (eglMakeCurrent(egl.Display, egl.TinySurface, egl.TinySurface, egl.Context) == EGL_FALSE) {
        ALOGE("        eglMakeCurrent() failed: %s", EglErrorString(eglGetError()));
        eglDestroySurface(egl.Display, egl.TinySurface);
        eglDestroyContext(egl.Display, egl.Context);
        egl.Context = EGL_NO_CONTEXT;
        return;
    }
}

static void eglDestroy()
{
    if ( egl.Display != 0 )
    {
        ALOGE( "        eglMakeCurrent( Display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT )" );
        if ( eglMakeCurrent( egl.Display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT ) == EGL_FALSE )
        {
            ALOGE( "        eglMakeCurrent() failed: %s", EglErrorString( eglGetError() ) );
        }
    }
    if ( egl.Context != EGL_NO_CONTEXT )
    {
        ALOGE( "        eglDestroyContext( Display, Context )" );
        if ( eglDestroyContext( egl.Display, egl.Context ) == EGL_FALSE )
        {
            ALOGE( "        eglDestroyContext() failed: %s", EglErrorString( eglGetError() ) );
        }
        egl.Context = EGL_NO_CONTEXT;
    }
    if ( egl.TinySurface != EGL_NO_SURFACE )
    {
        ALOGE( "        eglDestroySurface( Display, TinySurface )" );
        if ( eglDestroySurface( egl.Display, egl.TinySurface ) == EGL_FALSE )
        {
            ALOGE( "        eglDestroySurface() failed: %s", EglErrorString( eglGetError() ) );
        }
        egl.TinySurface = EGL_NO_SURFACE;
    }
    if ( egl.Display != 0 )
    {
        ALOGE( "        eglTerminate( Display )" );
        if ( eglTerminate( egl.Display ) == EGL_FALSE )
        {
            ALOGE( "        eglTerminate() failed: %s", EglErrorString( eglGetError() ) );
        }
        egl.Display = 0;
    }
}


const int NUM_ROTATIONS = 2;
const int NUM_INSTANCES = 100;

typedef struct {
#if defined( EGL_SYNC )
    EGLDisplay Display;
    EGLSyncKHR Sync;
#else
    GLsync		Sync;
#endif
} ovrFence;

static void ovrFence_Create(ovrFence *fence) {
#if defined( EGL_SYNC )
    fence->Display = 0;
    fence->Sync = EGL_NO_SYNC_KHR;
#else
    fence->Sync = 0;
#endif
}

static void ovrFence_Destroy(ovrFence *fence) {
#if defined( EGL_SYNC )
    if (fence->Sync != EGL_NO_SYNC_KHR) {
        if (eglDestroySyncKHR(fence->Display, fence->Sync) == EGL_FALSE) {
            ALOGE("eglDestroySyncKHR() : EGL_FALSE");
            return;
        }
        fence->Display = 0;
        fence->Sync = EGL_NO_SYNC_KHR;
    }
#else
    if ( fence->Sync != 0 )
    {
        glDeleteSync( fence->Sync );
        fence->Sync = 0;
    }
#endif
}

static void ovrFence_Insert(ovrFence *fence) {
    ovrFence_Destroy(fence);

#if defined( EGL_SYNC )
    fence->Display = eglGetCurrentDisplay();
    fence->Sync = eglCreateSyncKHR(fence->Display, EGL_SYNC_FENCE_KHR, NULL);
    if (fence->Sync == EGL_NO_SYNC_KHR) {
        ALOGE("eglCreateSyncKHR() : EGL_NO_SYNC_KHR");
        return;
    }
    // Force flushing the commands.
    // Note that some drivers will already flush when calling eglCreateSyncKHR.
    if (eglClientWaitSyncKHR(fence->Display, fence->Sync, EGL_SYNC_FLUSH_COMMANDS_BIT_KHR, 0) ==
        EGL_FALSE) {
        ALOGE("eglClientWaitSyncKHR() : EGL_FALSE");
        return;
    }
#else
    // Create and insert a new sync object.
    fence->Sync = glFenceSync( GL_SYNC_GPU_COMMANDS_COMPLETE, 0 );
    // Force flushing the commands.
    // Note that some drivers will already flush when calling glFenceSync.
    glClientWaitSync( fence->Sync, GL_SYNC_FLUSH_COMMANDS_BIT, 0 );
#endif
}


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

static void ovrFramebuffer_Clear(ovrFramebuffer *frameBuffer) {
    frameBuffer->Width = 0;
    frameBuffer->Height = 0;
    frameBuffer->Multisamples = 0;
    frameBuffer->TextureSwapChainLength = 0;
    frameBuffer->TextureSwapChainIndex = 0;
    frameBuffer->UseMultiview = false;
    frameBuffer->ColorTextureSwapChain = NULL;
    frameBuffer->DepthBuffers = NULL;
    frameBuffer->FrameBuffers = NULL;
}

static bool ovrFramebuffer_Create(ovrFramebuffer *frameBuffer, const bool useMultiview,
                                  const ovrTextureFormat colorFormat, const int width,
                                  const int height, const int multisamples) {
    PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXTPROC glRenderbufferStorageMultisampleEXT =
            (PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXTPROC) eglGetProcAddress(
                    "glRenderbufferStorageMultisampleEXT");
    PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEEXTPROC glFramebufferTexture2DMultisampleEXT =
            (PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEEXTPROC) eglGetProcAddress(
                    "glFramebufferTexture2DMultisampleEXT");

    PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC glFramebufferTextureMultiviewOVR =
            (PFNGLFRAMEBUFFERTEXTUREMULTIVIEWOVRPROC) eglGetProcAddress(
                    "glFramebufferTextureMultiviewOVR");
    PFNGLFRAMEBUFFERTEXTUREMULTISAMPLEMULTIVIEWOVRPROC glFramebufferTextureMultisampleMultiviewOVR =
            (PFNGLFRAMEBUFFERTEXTUREMULTISAMPLEMULTIVIEWOVRPROC) eglGetProcAddress(
                    "glFramebufferTextureMultisampleMultiviewOVR");

    frameBuffer->Width = width;
    frameBuffer->Height = height;
    frameBuffer->Multisamples = multisamples;
    frameBuffer->UseMultiview = (useMultiview && (glFramebufferTextureMultiviewOVR != NULL)) ? true
                                                                                             : false;

    frameBuffer->ColorTextureSwapChain = vrapi_CreateTextureSwapChain(
            frameBuffer->UseMultiview ? VRAPI_TEXTURE_TYPE_2D_ARRAY : VRAPI_TEXTURE_TYPE_2D,
            colorFormat, width, height, 1, true);
    frameBuffer->TextureSwapChainLength = vrapi_GetTextureSwapChainLength(
            frameBuffer->ColorTextureSwapChain);
    frameBuffer->DepthBuffers = (GLuint *) malloc(
            frameBuffer->TextureSwapChainLength * sizeof(GLuint));
    frameBuffer->FrameBuffers = (GLuint *) malloc(
            frameBuffer->TextureSwapChainLength * sizeof(GLuint));

    ALOGV("        frameBuffer->UseMultiview = %d", frameBuffer->UseMultiview);

    for (int i = 0; i < frameBuffer->TextureSwapChainLength; i++) {
        // Create the color buffer texture.
        const GLuint colorTexture = vrapi_GetTextureSwapChainHandle(
                frameBuffer->ColorTextureSwapChain, i);
        GLenum colorTextureTarget = frameBuffer->UseMultiview ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;
        GL(glBindTexture(colorTextureTarget, colorTexture));
        if (glExtensions.EXT_texture_border_clamp) {
            GL(glTexParameteri(colorTextureTarget, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER));
            GL(glTexParameteri(colorTextureTarget, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER));
            GLfloat borderColor[] = {0.0f, 0.0f, 0.0f, 0.0f};
            GL(glTexParameterfv(colorTextureTarget, GL_TEXTURE_BORDER_COLOR, borderColor));
        } else {
            // Just clamp to edge. However, this requires manually clearing the border
            // around the layer to clear the edge texels.
            GL(glTexParameteri(colorTextureTarget, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
            GL(glTexParameteri(colorTextureTarget, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
        }
        GL(glTexParameteri(colorTextureTarget, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
        GL(glTexParameteri(colorTextureTarget, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
        GL(glBindTexture(colorTextureTarget, 0));

        if (frameBuffer->UseMultiview) {
            // Create the depth buffer texture.
            GL(glGenTextures(1, &frameBuffer->DepthBuffers[i]));
            GL(glBindTexture(GL_TEXTURE_2D_ARRAY, frameBuffer->DepthBuffers[i]));
            GL(glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_DEPTH_COMPONENT24, width, height, 2));
            GL(glBindTexture(GL_TEXTURE_2D_ARRAY, 0));

            // Create the frame buffer.
            GL(glGenFramebuffers(1, &frameBuffer->FrameBuffers[i]));
            GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, frameBuffer->FrameBuffers[i]));
            if (multisamples > 1 && (glFramebufferTextureMultisampleMultiviewOVR != NULL)) {
                GL(glFramebufferTextureMultisampleMultiviewOVR(GL_DRAW_FRAMEBUFFER,
                                                               GL_DEPTH_ATTACHMENT,
                                                               frameBuffer->DepthBuffers[i],
                                                               0 /* level */,
                                                               multisamples /* samples */,
                                                               0 /* baseViewIndex */,
                                                               2 /* numViews */ ));
                GL(glFramebufferTextureMultisampleMultiviewOVR(GL_DRAW_FRAMEBUFFER,
                                                               GL_COLOR_ATTACHMENT0, colorTexture,
                                                               0 /* level */,
                                                               multisamples /* samples */,
                                                               0 /* baseViewIndex */,
                                                               2 /* numViews */ ));
            } else {
                GL(glFramebufferTextureMultiviewOVR(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                                    frameBuffer->DepthBuffers[i], 0 /* level */,
                                                    0 /* baseViewIndex */, 2 /* numViews */ ));
                GL(glFramebufferTextureMultiviewOVR(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                                    colorTexture, 0 /* level */,
                                                    0 /* baseViewIndex */, 2 /* numViews */ ));
            }

            GL(GLenum renderFramebufferStatus = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER));
            GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0));
            if (renderFramebufferStatus != GL_FRAMEBUFFER_COMPLETE) {
                ALOGE("Incomplete frame buffer object: %s",
                      GlFrameBufferStatusString(renderFramebufferStatus));
                return false;
            }
        } else {
            if (multisamples > 1 && glRenderbufferStorageMultisampleEXT != NULL &&
                glFramebufferTexture2DMultisampleEXT != NULL) {
                // Create multisampled depth buffer.
                GL(glGenRenderbuffers(1, &frameBuffer->DepthBuffers[i]));
                GL(glBindRenderbuffer(GL_RENDERBUFFER, frameBuffer->DepthBuffers[i]));
                GL(glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER, multisamples,
                                                       GL_DEPTH_COMPONENT24, width, height));
                GL(glBindRenderbuffer(GL_RENDERBUFFER, 0));

                // Create the frame buffer.
                // NOTE: glFramebufferTexture2DMultisampleEXT only works with GL_FRAMEBUFFER.
                GL(glGenFramebuffers(1, &frameBuffer->FrameBuffers[i]));
                GL(glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer->FrameBuffers[i]));
                GL(glFramebufferTexture2DMultisampleEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                                        GL_TEXTURE_2D, colorTexture, 0,
                                                        multisamples));
                GL(glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
                                             frameBuffer->DepthBuffers[i]));
                GL(GLenum renderFramebufferStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER));
                GL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
                if (renderFramebufferStatus != GL_FRAMEBUFFER_COMPLETE) {
                    ALOGE("Incomplete frame buffer object: %s",
                          GlFrameBufferStatusString(renderFramebufferStatus));
                    return false;
                }
            } else {
                // Create depth buffer.
                GL(glGenRenderbuffers(1, &frameBuffer->DepthBuffers[i]));
                GL(glBindRenderbuffer(GL_RENDERBUFFER, frameBuffer->DepthBuffers[i]));
                GL(glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height));
                GL(glBindRenderbuffer(GL_RENDERBUFFER, 0));

                // Create the frame buffer.
                GL(glGenFramebuffers(1, &frameBuffer->FrameBuffers[i]));
                GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, frameBuffer->FrameBuffers[i]));
                GL(glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                             GL_RENDERBUFFER, frameBuffer->DepthBuffers[i]));
                GL(glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                          colorTexture, 0));
                GL(GLenum renderFramebufferStatus = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER));
                GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0));
                if (renderFramebufferStatus != GL_FRAMEBUFFER_COMPLETE) {
                    ALOGE("Incomplete frame buffer object: %s",
                          GlFrameBufferStatusString(renderFramebufferStatus));
                    return false;
                }
            }
        }
    }

    return true;
}

static void ovrFramebuffer_Destroy(ovrFramebuffer *frameBuffer) {
    GL(glDeleteFramebuffers(frameBuffer->TextureSwapChainLength, frameBuffer->FrameBuffers));
    if (frameBuffer->UseMultiview) {
        GL(glDeleteTextures(frameBuffer->TextureSwapChainLength, frameBuffer->DepthBuffers));
    } else {
        GL(glDeleteRenderbuffers(frameBuffer->TextureSwapChainLength, frameBuffer->DepthBuffers));
    }
    vrapi_DestroyTextureSwapChain(frameBuffer->ColorTextureSwapChain);

    free(frameBuffer->DepthBuffers);
    free(frameBuffer->FrameBuffers);

    ovrFramebuffer_Clear(frameBuffer);
}

static void ovrFramebuffer_SetCurrent(ovrFramebuffer *frameBuffer) {
    GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER,
                         frameBuffer->FrameBuffers[frameBuffer->TextureSwapChainIndex]));
}

static void ovrFramebuffer_SetNone() {
    GL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0));
}

static void ovrFramebuffer_Resolve(ovrFramebuffer *frameBuffer) {
    // Discard the depth buffer, so the tiler won't need to write it back out to memory.
    const GLenum depthAttachment[1] = {GL_DEPTH_ATTACHMENT};
    glInvalidateFramebuffer(GL_DRAW_FRAMEBUFFER, 1, depthAttachment);

    // Flush this frame worth of commands.
    glFlush();
}

static void ovrFramebuffer_Advance(ovrFramebuffer *frameBuffer) {
    // Advance to the next texture from the set.
    frameBuffer->TextureSwapChainIndex =
            (frameBuffer->TextureSwapChainIndex + 1) % frameBuffer->TextureSwapChainLength;
}


typedef struct {
    GLuint Index;
    GLint Size;
    GLenum Type;
    GLboolean Normalized;
    GLsizei Stride;
    const GLvoid *Pointer;
} ovrVertexAttribPointer;

#define MAX_VERTEX_ATTRIB_POINTERS        4

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
    VERTEX_ATTRIBUTE_LOCATION_TRANSFORM
};

typedef struct {
    enum VertexAttributeLocation location;
    const char *name;
} ovrVertexAttribute;

static ovrVertexAttribute ProgramVertexAttributes[] =
        {
                {VERTEX_ATTRIBUTE_LOCATION_POSITION,  "vertexPosition"},
                {VERTEX_ATTRIBUTE_LOCATION_COLOR,     "vertexColor"},
                {VERTEX_ATTRIBUTE_LOCATION_UV,        "vertexUv"},
                {VERTEX_ATTRIBUTE_LOCATION_TRANSFORM, "vertexTransform"}
        };

static void ovrGeometry_Clear(ovrGeometry *geometry) {
    geometry->VertexBuffer = 0;
    geometry->IndexBuffer = 0;
    geometry->VertexArrayObject = 0;
    geometry->VertexCount = 0;
    geometry->IndexCount = 0;
    for (int i = 0; i < MAX_VERTEX_ATTRIB_POINTERS; i++) {
        memset(&geometry->VertexAttribs[i], 0, sizeof(geometry->VertexAttribs[i]));
        geometry->VertexAttribs[i].Index = -1;
    }
}

static void ovrGeometry_CreatePanel(ovrGeometry *geometry) {
    typedef struct {
        float positions[4][4];
        float uv[4][2];
    } ovrCubeVertices;

    static const ovrCubeVertices cubeVertices =
            {
                    // positions
                    {
                            {-1, -1, 0, 1}, {1,   1, 0, 1}, {1,   -1, 0, 1}, {-1, 1, 0, 1}
                    },
                    // uv
                    {       {0,  1},        {0.5, 0},       {0.5, 1},        {0,  0}}
            };

    static const unsigned short cubeIndices[6] =
            {
                    0, 2, 1, 0, 1, 3,
            };


    geometry->VertexCount = 4;
    geometry->IndexCount = 6;

    geometry->VertexAttribs[0].Index = VERTEX_ATTRIBUTE_LOCATION_POSITION;
    geometry->VertexAttribs[0].Size = 4;
    geometry->VertexAttribs[0].Type = GL_FLOAT;
    geometry->VertexAttribs[0].Normalized = true;
    geometry->VertexAttribs[0].Stride = sizeof(cubeVertices.positions[0]);
    geometry->VertexAttribs[0].Pointer = (const GLvoid *) offsetof(ovrCubeVertices, positions);

    geometry->VertexAttribs[1].Index = VERTEX_ATTRIBUTE_LOCATION_UV;
    geometry->VertexAttribs[1].Size = 2;
    geometry->VertexAttribs[1].Type = GL_FLOAT;
    geometry->VertexAttribs[1].Normalized = true;
    geometry->VertexAttribs[1].Stride = 8;
    geometry->VertexAttribs[1].Pointer = (const GLvoid *) offsetof(ovrCubeVertices, uv);

    GL(glGenBuffers(1, &geometry->VertexBuffer));
    GL(glBindBuffer(GL_ARRAY_BUFFER, geometry->VertexBuffer));
    GL(glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), &cubeVertices, GL_STATIC_DRAW));
    GL(glBindBuffer(GL_ARRAY_BUFFER, 0));

    GL(glGenBuffers(1, &geometry->IndexBuffer));
    GL(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, geometry->IndexBuffer));
    GL(glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(cubeIndices), cubeIndices, GL_STATIC_DRAW));
    GL(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
}

static void ovrGeometry_CreateTestMode(ovrGeometry *geometry) {
    typedef struct {
        float positions[8][4];
        unsigned char colors[8][4];
        float uv[8][2];
    } ovrCubeVertices;

    static const ovrCubeVertices cubeVertices =
            {
                    // positions
                    {
                            {-0.5, +0.5, -0.5, +0.5}, {+0.5, +0.5, -0.5, +0.5}, {+0.5, +0.5, +0.5, +0.5}, {-0.5, +0.5, +0.5, +0.5},    // top
                            {-0.5, -0.5, -0.5, +0.5}, {-0.5, -0.5, +0.5, +0.5}, {+0.5, -0.5, +0.5, +0.5}, {+0.5, -0.5, -0.5, +0.5}    // bottom
                    },
                    // colors
                    {
                            {255,  0,    255,  255},  {0,    255,  0,    255},  {0,    0,    255,  255},  {255,  0,    0,    255},
                            {0,    0,    255,  255},  {0,    255,  0,    255},  {255,  0,    255,  255},  {255,  0,    0,    255}
                    },
                    // uv
                    {
                            {0,    1},                {0.5,  0},                {0.5,  1},                {0,    0},
                            {0,    1},                {0.5,  0},                {0.5,  1},                {0,    0},
                    }
            };

    static const unsigned short cubeIndices[36] =
            {
                    0, 2, 1, 2, 0, 3,    // top
                    4, 6, 5, 6, 4, 7,    // bottom
                    2, 6, 7, 7, 1, 2,    // right
                    0, 4, 5, 5, 3, 0,    // left
                    3, 5, 6, 6, 2, 3,    // front
                    0, 1, 7, 7, 4, 0    // back
            };

    geometry->VertexCount = 8;
    geometry->IndexCount = 36;

    geometry->VertexAttribs[0].Index = VERTEX_ATTRIBUTE_LOCATION_POSITION;
    geometry->VertexAttribs[0].Size = 4;
    geometry->VertexAttribs[0].Type = GL_FLOAT;
    geometry->VertexAttribs[0].Normalized = true;
    geometry->VertexAttribs[0].Stride = sizeof(cubeVertices.positions[0]);
    geometry->VertexAttribs[0].Pointer = (const GLvoid *) offsetof(ovrCubeVertices, positions);

    geometry->VertexAttribs[1].Index = VERTEX_ATTRIBUTE_LOCATION_COLOR;
    geometry->VertexAttribs[1].Size = 4;
    geometry->VertexAttribs[1].Type = GL_UNSIGNED_BYTE;
    geometry->VertexAttribs[1].Normalized = true;
    geometry->VertexAttribs[1].Stride = sizeof(cubeVertices.colors[0]);
    geometry->VertexAttribs[1].Pointer = (const GLvoid *) offsetof(ovrCubeVertices, colors);

    geometry->VertexAttribs[2].Index = VERTEX_ATTRIBUTE_LOCATION_UV;
    geometry->VertexAttribs[2].Size = 2;
    geometry->VertexAttribs[2].Type = GL_FLOAT;
    geometry->VertexAttribs[2].Normalized = true;
    geometry->VertexAttribs[2].Stride = 8;
    geometry->VertexAttribs[2].Pointer = (const GLvoid *) offsetof(ovrCubeVertices, uv);

    GL(glGenBuffers(1, &geometry->VertexBuffer));
    GL(glBindBuffer(GL_ARRAY_BUFFER, geometry->VertexBuffer));
    GL(glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), &cubeVertices, GL_STATIC_DRAW));
    GL(glBindBuffer(GL_ARRAY_BUFFER, 0));

    GL(glGenBuffers(1, &geometry->IndexBuffer));
    GL(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, geometry->IndexBuffer));
    GL(glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(cubeIndices), cubeIndices, GL_STATIC_DRAW));
    GL(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
}

static void ovrGeometry_Destroy(ovrGeometry *geometry) {
    GL(glDeleteBuffers(1, &geometry->IndexBuffer));
    GL(glDeleteBuffers(1, &geometry->VertexBuffer));

    ovrGeometry_Clear(geometry);
}

static void ovrGeometry_CreateVAO(ovrGeometry *geometry) {
    GL(glGenVertexArrays(1, &geometry->VertexArrayObject));
    GL(glBindVertexArray(geometry->VertexArrayObject));

    GL(glBindBuffer(GL_ARRAY_BUFFER, geometry->VertexBuffer));

    for (int i = 0; i < MAX_VERTEX_ATTRIB_POINTERS; i++) {
        if (geometry->VertexAttribs[i].Index != -1) {
            GL(glEnableVertexAttribArray(geometry->VertexAttribs[i].Index));
            GL(glVertexAttribPointer(geometry->VertexAttribs[i].Index,
                                     geometry->VertexAttribs[i].Size,
                                     geometry->VertexAttribs[i].Type,
                                     geometry->VertexAttribs[i].Normalized,
                                     geometry->VertexAttribs[i].Stride,
                                     geometry->VertexAttribs[i].Pointer));
        }
    }

    GL(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, geometry->IndexBuffer));

    GL(glBindVertexArray(0));
}

static void ovrGeometry_DestroyVAO(ovrGeometry *geometry) {
    GL(glDeleteVertexArrays(1, &geometry->VertexArrayObject));
}

/*
================================================================================

ovrProgram

================================================================================
*/

#define MAX_PROGRAM_UNIFORMS    8
#define MAX_PROGRAM_TEXTURES    8

typedef struct {
    GLuint Program;
    GLuint VertexShader;
    GLuint FragmentShader;
    // These will be -1 if not used by the program.
    GLint UniformLocation[MAX_PROGRAM_UNIFORMS];    // ProgramUniforms[].name
    GLint UniformBinding[MAX_PROGRAM_UNIFORMS];    // ProgramUniforms[].name
    GLint Textures[MAX_PROGRAM_TEXTURES];            // Texture%i
} ovrProgram;

enum E1test {
    UNIFORM_VIEW_ID,
    UNIFORM_MVP_MATRIX,
    UNIFORM_ENABLE_TEST_MODE,
};
enum E2test {
    UNIFORM_TYPE_VECTOR4,
    UNIFORM_TYPE_MATRIX4X4,
    UNIFORM_TYPE_INT,
    UNIFORM_TYPE_BUFFER,
};
typedef struct {
    E1test index;
    E2test type;
    const char *name;
} ovrUniform;

static ovrUniform ProgramUniforms[] =
        {
                {UNIFORM_VIEW_ID,          UNIFORM_TYPE_INT,       "ViewID"},
                {UNIFORM_MVP_MATRIX,       UNIFORM_TYPE_MATRIX4X4, "mvpMatrix"},
                {UNIFORM_ENABLE_TEST_MODE, UNIFORM_TYPE_INT,       "EnableTestMode"},
        };

static const char *programVersion = "#version 300 es\n";

static bool
ovrProgram_Create(ovrProgram *program, const char *vertexSource, const char *fragmentSource,
                  const bool useMultiview) {
    GLint r;

    GL(program->VertexShader = glCreateShader(GL_VERTEX_SHADER));
    if (program->VertexShader == 0) {
        ALOGE("glCreateShader error: %d", glGetError());
        return false;
    }

    const char *vertexSources[3] = {programVersion,
                                    (useMultiview) ? "#define DISABLE_MULTIVIEW 0\n"
                                                   : "#define DISABLE_MULTIVIEW 1\n",
                                    vertexSource
    };
    GL(glShaderSource(program->VertexShader, 3, vertexSources, 0));
    GL(glCompileShader(program->VertexShader));
    GL(glGetShaderiv(program->VertexShader, GL_COMPILE_STATUS, &r));
    if (r == GL_FALSE) {
        GLchar msg[4096];
        GL(glGetShaderInfoLog(program->VertexShader, sizeof(msg), 0, msg));
        ALOGE("%s\n%s\n", vertexSource, msg);
        return false;
    }

    const char *fragmentSources[2] = {programVersion, fragmentSource};
    GL(program->FragmentShader = glCreateShader(GL_FRAGMENT_SHADER));
    GL(glShaderSource(program->FragmentShader, 2, fragmentSources, 0));
    GL(glCompileShader(program->FragmentShader));
    GL(glGetShaderiv(program->FragmentShader, GL_COMPILE_STATUS, &r));
    if (r == GL_FALSE) {
        GLchar msg[4096];
        GL(glGetShaderInfoLog(program->FragmentShader, sizeof(msg), 0, msg));
        ALOGE("%s\n%s\n", fragmentSource, msg);
        return false;
    }

    GL(program->Program = glCreateProgram());
    GL(glAttachShader(program->Program, program->VertexShader));
    GL(glAttachShader(program->Program, program->FragmentShader));

    // Bind the vertex attribute locations.
    for (int i = 0; i < sizeof(ProgramVertexAttributes) / sizeof(ProgramVertexAttributes[0]); i++) {
        GL(glBindAttribLocation(program->Program, ProgramVertexAttributes[i].location,
                                ProgramVertexAttributes[i].name));
    }

    GL(glLinkProgram(program->Program));
    GL(glGetProgramiv(program->Program, GL_LINK_STATUS, &r));
    if (r == GL_FALSE) {
        GLchar msg[4096];
        GL(glGetProgramInfoLog(program->Program, sizeof(msg), 0, msg));
        ALOGE("Linking program failed: %s\n", msg);
        return false;
    }

    int numBufferBindings = 0;

    // Get the uniform locations.
    memset(program->UniformLocation, -1, sizeof(program->UniformLocation));
    for (int i = 0; i < sizeof(ProgramUniforms) / sizeof(ProgramUniforms[0]); i++) {
        const int uniformIndex = ProgramUniforms[i].index;
        if (ProgramUniforms[i].type == UNIFORM_TYPE_BUFFER) {
            GL(program->UniformLocation[uniformIndex] = glGetUniformBlockIndex(program->Program,
                                                                               ProgramUniforms[i].name));
            program->UniformBinding[uniformIndex] = numBufferBindings++;
            GL(glUniformBlockBinding(program->Program, program->UniformLocation[uniformIndex],
                                     program->UniformBinding[uniformIndex]));
        } else {
            GL(program->UniformLocation[uniformIndex] = glGetUniformLocation(program->Program,
                                                                             ProgramUniforms[i].name));
            program->UniformBinding[uniformIndex] = program->UniformLocation[uniformIndex];
        }
    }

    GL(glUseProgram(program->Program));

    // Get the texture locations.
    for (int i = 0; i < MAX_PROGRAM_TEXTURES; i++) {
        char name[32];
        sprintf(name, "Texture%i", i);
        program->Textures[i] = glGetUniformLocation(program->Program, name);
        if (program->Textures[i] != -1) {
            GL(glUniform1i(program->Textures[i], i));
        }
    }

    GL(glUseProgram(0));

    return true;
}

static void ovrProgram_Destroy(ovrProgram *program) {
    if (program->Program != 0) {
        GL(glDeleteProgram(program->Program));
        program->Program = 0;
    }
    if (program->VertexShader != 0) {
        GL(glDeleteShader(program->VertexShader));
        program->VertexShader = 0;
    }
    if (program->FragmentShader != 0) {
        GL(glDeleteShader(program->FragmentShader));
        program->FragmentShader = 0;
    }
}


/*
================================================================================

ovrRenderer

================================================================================
*/



ovrJava java;
int SwapInterval = 1;
bool CreatedScene = false;
ovrProgram Program;
ovrProgram ProgramLoading;
ovrGeometry Panel;
ovrGeometry TestMode;


static int MAX_FENCES = 4;

typedef struct {
    ovrFramebuffer FrameBuffer[VRAPI_FRAME_LAYER_EYE_MAX];
    int NumBuffers;
    ovrFence *Fence;            // Per-frame completion fence
    int FenceIndex;
} ovrRenderer;

static void ovrRenderer_Clear(ovrRenderer *renderer) {
    for (int eye = 0; eye < VRAPI_FRAME_LAYER_EYE_MAX; eye++) {
        ovrFramebuffer_Clear(&renderer->FrameBuffer[eye]);
    }
    renderer->NumBuffers = VRAPI_FRAME_LAYER_EYE_MAX;

    renderer->FenceIndex = 0;
}

static void
ovrRenderer_Create(ovrRenderer *renderer, const ovrJava *java, const bool useMultiview) {
    renderer->NumBuffers = useMultiview ? 1 : VRAPI_FRAME_LAYER_EYE_MAX;

    // Create the frame buffers.
    for (int eye = 0; eye < renderer->NumBuffers; eye++) {
        ovrFramebuffer_Create(&renderer->FrameBuffer[eye], useMultiview,
                              VRAPI_TEXTURE_FORMAT_8888,
                              vrapi_GetSystemPropertyInt(java,
                                                         VRAPI_SYS_PROP_SUGGESTED_EYE_TEXTURE_WIDTH),
                              vrapi_GetSystemPropertyInt(java,
                                                         VRAPI_SYS_PROP_SUGGESTED_EYE_TEXTURE_HEIGHT),
                              NUM_MULTI_SAMPLES);

    }

    renderer->Fence = (ovrFence *) malloc(MAX_FENCES * sizeof(ovrFence));
    for (int i = 0; i < MAX_FENCES; i++) {
        ovrFence_Create(&renderer->Fence[i]);
    }
}

static void ovrRenderer_Destroy(ovrRenderer *renderer) {
    for (int eye = 0; eye < renderer->NumBuffers; eye++) {
        ovrFramebuffer_Destroy(&renderer->FrameBuffer[eye]);
    }

    for (int i = 0; i < MAX_FENCES; i++) {
        ovrFence_Destroy(&renderer->Fence[i]);
    }
    free(renderer->Fence);
}

static ovrLayerProjection2 ovrRenderer_RenderFrame(ovrRenderer *renderer, const ovrJava *java,
                                                   const ovrTracking2 *tracking, ovrMobile *ovr,
                                                   unsigned long long *completionFence,
                                                   bool loading) {
    ovrTracking2 updatedTracking = *tracking;

    ovrLayerProjection2 layer = vrapi_DefaultLayerProjection2();
    layer.HeadPose = updatedTracking.HeadPose;
    for (int eye = 0; eye < VRAPI_FRAME_LAYER_EYE_MAX; eye++) {
        ovrFramebuffer *frameBuffer = &renderer->FrameBuffer[renderer->NumBuffers == 1 ? 0 : eye];
        layer.Textures[eye].ColorSwapChain = frameBuffer->ColorTextureSwapChain;
        layer.Textures[eye].SwapChainIndex = frameBuffer->TextureSwapChainIndex;
        layer.Textures[eye].TexCoordsFromTanAngles = ovrMatrix4f_TanAngleMatrixFromProjection(
                &updatedTracking.Eye[eye].ProjectionMatrix);
    }
    layer.Header.Flags |= VRAPI_FRAME_LAYER_FLAG_CHROMATIC_ABERRATION_CORRECTION;


    ovrFramebuffer *frameBuffer = &renderer->FrameBuffer[0];
    ovrFramebuffer_SetCurrent(frameBuffer);

    // Render the eye images.
    for (int eye = 0; eye < renderer->NumBuffers; eye++) {
        // NOTE: In the non-mv case, latency can be further reduced by updating the sensor prediction
        // for each eye (updates orientation, not position)
        ovrFramebuffer *frameBuffer = &renderer->FrameBuffer[eye];
        ovrFramebuffer_SetCurrent(frameBuffer);

        if (loading) {
            GL(glUseProgram(ProgramLoading.Program));
        } else {
            GL(glUseProgram(Program.Program));
        }
        if (Program.UniformLocation[UNIFORM_VIEW_ID] >=
            0)  // NOTE: will not be present when multiview path is enabled.
        {
            GL(glUniform1i(Program.UniformLocation[UNIFORM_VIEW_ID], eye));
        }
        GL(glEnable(GL_SCISSOR_TEST));
        GL(glDepthMask(GL_TRUE));
        GL(glEnable(GL_DEPTH_TEST));
        GL(glDepthFunc(GL_LEQUAL));
        GL(glEnable(GL_CULL_FACE));
        GL(glCullFace(GL_BACK));
        GL(glViewport(0, 0, frameBuffer->Width, frameBuffer->Height));
        GL(glScissor(0, 0, frameBuffer->Width, frameBuffer->Height));
        GL(glClearColor(0.0f, 0.0f, 0.0f, 1.0f));
        GL(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));

        //enableTestMode = 0;
        GL(glUniform1i(Program.UniformLocation[UNIFORM_ENABLE_TEST_MODE], enableTestMode));
        if ((enableTestMode & 1) != 0) {
            int N = 10;
            for (int i = 0; i < N; i++) {
                ovrMatrix4f TestModeMatrix[2];
                TestModeMatrix[0] = ovrMatrix4f_CreateIdentity();
                if (i < 0) {
                    ovrMatrix4f scale = ovrMatrix4f_CreateScale(10, 10, 10);
                    TestModeMatrix[0] = ovrMatrix4f_Multiply(&scale, &TestModeMatrix[0]);
                }
                double theta = 2.0 * M_PI * i / (1.0 * N);
                ovrMatrix4f translation = ovrMatrix4f_CreateTranslation(float(5.0 * cos(theta)), 0,
                                                                        float(5 * sin(theta)));
                TestModeMatrix[0] = ovrMatrix4f_Multiply(&translation, &TestModeMatrix[0]);

                TestModeMatrix[1] = TestModeMatrix[0];

                TestModeMatrix[0] = ovrMatrix4f_Multiply(&tracking->Eye[0].ViewMatrix,
                                                         &TestModeMatrix[0]);
                TestModeMatrix[1] = ovrMatrix4f_Multiply(&tracking->Eye[1].ViewMatrix,
                                                         &TestModeMatrix[1]);
                TestModeMatrix[0] = ovrMatrix4f_Multiply(&tracking->Eye[0].ProjectionMatrix,
                                                         &TestModeMatrix[0]);
                TestModeMatrix[1] = ovrMatrix4f_Multiply(&tracking->Eye[1].ProjectionMatrix,
                                                         &TestModeMatrix[1]);

                if (i == 0) {
                    LOG("theta:%f", theta);
                    LOG("rotate:%f %f %f %f", tracking->HeadPose.Pose.Orientation.x,
                        tracking->HeadPose.Pose.Orientation.y,
                        tracking->HeadPose.Pose.Orientation.z, tracking->HeadPose.Pose.Orientation.w
                    );
                    LOG("tran:\n%s", DumpMatrix(&translation).c_str());
                    LOG("view:\n%s", DumpMatrix(&tracking->Eye[0].ViewMatrix).c_str());
                    LOG("proj:\n%s", DumpMatrix(&tracking->Eye[0].ProjectionMatrix).c_str());
                    LOG("mm:\n%s", DumpMatrix(&TestModeMatrix[0]).c_str());
                }

                GL(glUniformMatrix4fv(Program.UniformLocation[UNIFORM_MVP_MATRIX], 2, true,
                                      (float *) TestModeMatrix));
                GL(glBindVertexArray(TestMode.VertexArrayObject));

                if (i < 2) {
                    GL(glActiveTexture(GL_TEXTURE0));
                    GL(glBindTexture(GL_TEXTURE_EXTERNAL_OES, SurfaceTextureID));
                } else {
                    GL(glActiveTexture(GL_TEXTURE0));
                    GL(glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0));
                }

                GL(glDrawElements(GL_TRIANGLES, TestMode.IndexCount, GL_UNSIGNED_SHORT, NULL));
            }
        } else if (loading) {
            GL(glBindVertexArray(Panel.VertexArrayObject));
            GL(glActiveTexture(GL_TEXTURE0));
            GL(glBindTexture(GL_TEXTURE_2D, loadingTexture));

            // Display information message on front and back of user.
            for (int i = 0; i < 2; i++) {
                ovrMatrix4f mvpMatrix[2];
                mvpMatrix[0] = ovrMatrix4f_CreateIdentity();
                mvpMatrix[1] = ovrMatrix4f_CreateIdentity();

                if (i == 0) {
                    ovrMatrix4f translation = ovrMatrix4f_CreateTranslation(0, 0, -3);
                    mvpMatrix[0] = ovrMatrix4f_Multiply(&translation, &mvpMatrix[0]);
                } else {
                    ovrMatrix4f rotation = ovrMatrix4f_CreateRotation(0, (float)M_PI, 0);
                    mvpMatrix[0] = ovrMatrix4f_Multiply(&rotation, &mvpMatrix[0]);

                    ovrMatrix4f translation = ovrMatrix4f_CreateTranslation(0, 0, 3);
                    mvpMatrix[0] = ovrMatrix4f_Multiply(&translation, &mvpMatrix[0]);
                }

                mvpMatrix[1] = mvpMatrix[0];

                mvpMatrix[0] = ovrMatrix4f_Multiply(&tracking->Eye[0].ViewMatrix,
                                                    &mvpMatrix[0]);
                mvpMatrix[1] = ovrMatrix4f_Multiply(&tracking->Eye[1].ViewMatrix,
                                                    &mvpMatrix[1]);
                mvpMatrix[0] = ovrMatrix4f_Multiply(&tracking->Eye[0].ProjectionMatrix,
                                                    &mvpMatrix[0]);
                mvpMatrix[1] = ovrMatrix4f_Multiply(&tracking->Eye[1].ProjectionMatrix,
                                                    &mvpMatrix[1]);

                GL(glUniformMatrix4fv(Program.UniformLocation[UNIFORM_MVP_MATRIX], 2, true,
                                      (float *) mvpMatrix));

                GL(glDrawElements(GL_TRIANGLES, Panel.IndexCount, GL_UNSIGNED_SHORT, NULL));
            }
        } else {
            ovrMatrix4f mvpMatrix[2];
            mvpMatrix[0] = ovrMatrix4f_CreateIdentity();
            mvpMatrix[1] = ovrMatrix4f_CreateIdentity();

            GL(glBindVertexArray(Panel.VertexArrayObject));

            GL(glActiveTexture(GL_TEXTURE0));
            GL(glBindTexture(GL_TEXTURE_EXTERNAL_OES, SurfaceTextureID));

            GL(glUniformMatrix4fv(Program.UniformLocation[UNIFORM_MVP_MATRIX], 2, true,
                                  (float *) mvpMatrix));

            GL(glDrawElements(GL_TRIANGLES, Panel.IndexCount, GL_UNSIGNED_SHORT, NULL));
        }

        GL(glBindVertexArray(0));
        if (loading) {
            GL(glBindTexture(GL_TEXTURE_2D, 0));
        } else {
            GL(glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0));
        }
        GL(glUseProgram(0));

        // Explicitly clear the border texels to black when GL_CLAMP_TO_BORDER is not available.
        if (glExtensions.EXT_texture_border_clamp == false) {
            // Clear to fully opaque black.
            GL(glClearColor(0.0f, 0.0f, 0.0f, 1.0f));
            // bottom
            GL(glScissor(0, 0, frameBuffer->Width, 1));
            GL(glClear(GL_COLOR_BUFFER_BIT));
            // top
            GL(glScissor(0, frameBuffer->Height - 1, frameBuffer->Width, 1));
            GL(glClear(GL_COLOR_BUFFER_BIT));
            // left
            GL(glScissor(0, 0, 1, frameBuffer->Height));
            GL(glClear(GL_COLOR_BUFFER_BIT));
            // right
            GL(glScissor(frameBuffer->Width - 1, 0, 1, frameBuffer->Height));
            GL(glClear(GL_COLOR_BUFFER_BIT));
        }

        ovrFramebuffer_Resolve(frameBuffer);
        ovrFramebuffer_Advance(frameBuffer);
    }

    ovrFramebuffer_SetNone();

    // Use a single fence to indicate the frame is ready to be displayed.
    ovrFence *fence = &renderer->Fence[renderer->FenceIndex];
    ovrFence_Insert(fence);
    renderer->FenceIndex = (renderer->FenceIndex + 1) % MAX_FENCES;

    *completionFence = (size_t) fence->Sync;

    return layer;
}


ovrRenderer Renderer;

void onVrModeChange(JNIEnv *env) {
    if (Resumed && window != NULL) {
        if (Ovr == NULL) {
            ALOGV("Entering VR mode.");
            ovrModeParms parms = vrapi_DefaultModeParms(&java);

            parms.Flags |= VRAPI_MODE_FLAG_RESET_WINDOW_FULLSCREEN;

            parms.Flags |= VRAPI_MODE_FLAG_NATIVE_WINDOW;
            parms.Display = (size_t) egl.Display;
            parms.WindowSurface = (size_t) window;
            parms.ShareContext = (size_t) egl.Context;

            Ovr = vrapi_EnterVrMode(&parms);

            if (Ovr == NULL) {
                ALOGE("Invalid ANativeWindow");
                return;
            }

            int CpuLevel = 2;
            int GpuLevel = 3;
            vrapi_SetClockLevels(Ovr, CpuLevel, GpuLevel);

            ALOGV("		vrapi_SetClockLevels( %d, %d )", CpuLevel, GpuLevel);

            vrapi_SetPerfThread(Ovr, VRAPI_PERF_THREAD_TYPE_MAIN, gettid());

            ALOGV("		vrapi_SetPerfThread( MAIN, %d )", gettid());

            //vrapi_SetTrackingTransform( Ovr, vrapi_GetTrackingTransform( Ovr, VRAPI_TRACKING_TRANSFORM_SYSTEM_CENTER_FLOOR_LEVEL ) );
        }
    } else {
        if (Ovr != NULL) {
            ALOGV("Leaving VR mode.");
            vrapi_LeaveVrMode(Ovr);
            Ovr = NULL;
        }
    }
}

void renderLoadingScene() {
    double DisplayTime = GetTimeInSeconds();

    // Show a loading icon.
    FrameIndex++;

    double displayTime = vrapi_GetPredictedDisplayTime(Ovr, FrameIndex);
    ovrTracking2 tracking = vrapi_GetPredictedTracking2(Ovr, displayTime);

    unsigned long long completionFence = 0;

    const ovrLayerProjection2 worldLayer = ovrRenderer_RenderFrame(&Renderer, &java,
                                                                   &tracking,
                                                                   Ovr, &completionFence, true);

    const ovrLayerHeader2 *layers[] =
            {
                    &worldLayer.Header
            };


    ovrSubmitFrameDescription2 frameDesc = {};
    frameDesc.Flags = 0;
    frameDesc.SwapInterval = 1;
    frameDesc.FrameIndex = FrameIndex;
    frameDesc.DisplayTime = DisplayTime;
    frameDesc.LayerCount = 1;
    frameDesc.Layers = layers;

    vrapi_SubmitFrame2(Ovr, &frameDesc);

    if (!CreatedScene) {
        ovrProgram_Create(&Program, VERTEX_SHADER, FRAGMENT_SHADER, UseMultiview);
        ovrProgram_Create(&ProgramLoading, VERTEX_SHADER_LOADING, FRAGMENT_SHADER_LOADING,
                          UseMultiview);
        ovrGeometry_CreatePanel(&Panel);
        ovrGeometry_CreateVAO(&Panel);
        ovrGeometry_CreateTestMode(&TestMode);
        ovrGeometry_CreateVAO(&TestMode);

        CreatedScene = true;
    }
}


extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_VrAPI_initialize(JNIEnv *env, jobject instance, jobject activity) {
    eglInit();

    EglInitExtensions();

    java.Env = env;
    env->GetJavaVM(&java.Vm);
    java.ActivityObject = env->NewGlobalRef(activity);

    const ovrInitParms initParms = vrapi_DefaultInitParms(&java);
    int32_t initResult = vrapi_Initialize(&initParms);
    if (initResult != VRAPI_INITIALIZE_SUCCESS) {
        // If initialization failed, vrapi_* function calls will not be available.
        ALOGE("vrapi_Initialize failed");
        return;
    }

    UseMultiview &= (glExtensions.multi_view &&
                     vrapi_GetSystemPropertyInt(&java, VRAPI_SYS_PROP_MULTIVIEW_AVAILABLE));
    ALOGV("UseMultiview:%d", UseMultiview);

    ovrRenderer_Create(&Renderer, &java, UseMultiview);

    //
    // Generate texture for SurfaceTexture which is output of MediaCodec.
    //
    GLuint textures[2];
    glGenTextures(2, textures);

    SurfaceTextureID = textures[0];
    loadingTexture = textures[1];

    glBindTexture(GL_TEXTURE_EXTERNAL_OES, SurfaceTextureID);

    glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER,
                    GL_NEAREST);
    glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER,
                    GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S,
                    GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T,
                    GL_CLAMP_TO_EDGE);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_VrAPI_destroy(JNIEnv *env, jobject instance) {
    ovrRenderer_Destroy(&Renderer);

    ovrProgram_Destroy(&Program);
    ovrProgram_Destroy(&ProgramLoading);
    ovrGeometry_DestroyVAO(&Panel);
    ovrGeometry_Destroy(&Panel);
    ovrGeometry_DestroyVAO(&TestMode);
    ovrGeometry_Destroy(&TestMode);

    GLuint textures[2] = {SurfaceTextureID, loadingTexture};
    glDeleteTextures(2, textures);

    CreatedScene = false;
    eglDestroy();

    vrapi_Shutdown();
}


extern "C"
JNIEXPORT jint JNICALL
Java_com_polygraphene_alvr_VrAPI_createLoadingTexture(JNIEnv *env, jobject instance) {
    return loadingTexture;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_VrAPI_render(JNIEnv *env, jobject instance, jobject callback) {
    if (!CreatedScene) {
        // Show a loading message.
        renderLoadingScene();
    }

    double currentTime = GetTimeInSeconds();

    unsigned long long completionFence = 0;

    jclass clazz = env->GetObjectClass(callback);
    jmethodID waitFrame = env->GetMethodID(clazz, "waitFrame", "()J");
    int64_t renderedFrameIndex = 0;
    for (int i = 0; i < 10; i++) {
        renderedFrameIndex = env->CallLongMethod(callback, waitFrame);
        if (renderedFrameIndex == -1) {
            // Activity has Paused or connection becomes idle
            return;
        }
        ALOGV("Got frame for render. wanted FrameIndex=%lu got=%lu waiting=%.3f ms delay=%lu",
              WantedFrameIndex, renderedFrameIndex,
              (GetTimeInSeconds() - currentTime) * 1000,
              WantedFrameIndex - renderedFrameIndex);
        if (WantedFrameIndex <= renderedFrameIndex) {
            break;
        }
        if ((enableTestMode & 4) != 0) {
            break;
        }
    }

    std::shared_ptr<TrackingFrame> frame;
    {
        MutexLock lock(trackingFrameMutex);
        int i = 0;
        for (std::list<std::shared_ptr<TrackingFrame> >::iterator it = trackingFrameList.begin();
             it != trackingFrameList.end(); ++it, i++) {
            if ((*it)->frameIndex == renderedFrameIndex) {
                frame = *it;
                break;
            }
        }
    }
    if (!frame) {
        // No matching tracking info. Too old frame.
        LOG("Too old frame has arrived. ignore. FrameIndex=%lu WantedFrameIndex=%lu",
            renderedFrameIndex, WantedFrameIndex);
        return;
    }
    LOG("Frame latency is %lu us. FrameIndex=%lu", getTimestampUs() - frame->fetchTime,
        frame->frameIndex);

// Render eye images and setup the primary layer using ovrTracking2.
    const ovrLayerProjection2 worldLayer = ovrRenderer_RenderFrame(&Renderer, &java,
                                                                   &frame->tracking,
                                                                   Ovr, &completionFence, false);

    const ovrLayerHeader2 *layers2[] =
            {
                    &worldLayer.Header
            };

    // TODO
    double DisplayTime = 0.0;

    ovrSubmitFrameDescription2 frameDesc = {};
    frameDesc.
            Flags = 0;
    frameDesc.
            SwapInterval = SwapInterval;
    frameDesc.
            FrameIndex = renderedFrameIndex;
    frameDesc.
            CompletionFence = completionFence;
    frameDesc.
            DisplayTime = DisplayTime;
    frameDesc.
            LayerCount = 1;
    frameDesc.
            Layers = layers2;

    WantedFrameIndex = renderedFrameIndex + 1;

// Hand over the eye images to the time warp.
    ovrResult res = vrapi_SubmitFrame2(Ovr, &frameDesc);

    ALOGV("vrapi_SubmitFrame2 return=%d rendered FrameIndex=%lu Orientation=(%f, %f, %f, %f)", res,
          renderedFrameIndex, frame->tracking.HeadPose.Pose.Orientation.x,
          frame->tracking.HeadPose.Pose.Orientation.y, frame->tracking.HeadPose.Pose.Orientation.z,
          frame->tracking.HeadPose.Pose.Orientation.w
    );
    if (suspend) {
        ALOGV("submit enter suspend");
        while (suspend) {
            usleep(1000 * 10);
        }
        ALOGV("submit leave suspend");
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_VrAPI_renderLoading(JNIEnv *env, jobject instance) {
    renderLoadingScene();
}

void sendTrackingInfo(JNIEnv *env, jobject callback, double displayTime, ovrTracking2 *tracking) {
    jbyteArray array = env->NewByteArray(sizeof(TrackingInfo));
    TrackingInfo *packet = (TrackingInfo *) env->GetByteArrayElements(array, 0);

    uint64_t clientTime = getTimestampUs();

    packet->type = ALVR_PACKET_TYPE_TRACKING_INFO;
    packet->clientTime = clientTime;
    packet->FrameIndex = FrameIndex;
    packet->predictedDisplayTime = displayTime;

    memcpy(&packet->HeadPose_Pose_Orientation, &tracking->HeadPose.Pose.Orientation, sizeof(ovrQuatf));
    memcpy(&packet->HeadPose_Pose_Position, &tracking->HeadPose.Pose.Position, sizeof(ovrVector3f));

    memcpy(&packet->HeadPose_AngularVelocity, &tracking->HeadPose.AngularVelocity, sizeof(ovrVector3f));
    memcpy(&packet->HeadPose_LinearVelocity, &tracking->HeadPose.LinearVelocity, sizeof(ovrVector3f));
    memcpy(&packet->HeadPose_AngularAcceleration, &tracking->HeadPose.AngularAcceleration, sizeof(ovrVector3f));
    memcpy(&packet->HeadPose_LinearAcceleration, &tracking->HeadPose.LinearAcceleration, sizeof(ovrVector3f));

    memcpy(&packet->Eye[0].ProjectionMatrix, &tracking->Eye[0].ProjectionMatrix, sizeof(ovrMatrix4f));
    memcpy(&packet->Eye[0].ViewMatrix, &tracking->Eye[0].ViewMatrix, sizeof(ovrMatrix4f));
    memcpy(&packet->Eye[1].ProjectionMatrix, &tracking->Eye[1].ProjectionMatrix, sizeof(ovrMatrix4f));
    memcpy(&packet->Eye[1].ViewMatrix, &tracking->Eye[1].ViewMatrix, sizeof(ovrMatrix4f));

    env->ReleaseByteArrayElements(array, (jbyte *) packet, 0);

    ALOGV("Sending tracking info. FrameIndex=%lu", FrameIndex);

    jclass clazz = env->GetObjectClass(callback);
    jmethodID sendTracking = env->GetMethodID(clazz, "onSendTracking", "([BIJ)V");

    env->CallVoidMethod(callback, sendTracking, array, sizeof(TrackingInfo), FrameIndex);
}

// Called from TrackingThread
extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_VrAPI_fetchTrackingInfo(JNIEnv *env, jobject instance,
                                                   jobject callback) {
    std::shared_ptr<TrackingFrame> frame(new TrackingFrame());

    FrameIndex++;

    frame->frameIndex = FrameIndex;
    frame->fetchTime = getTimestampUs();

    frame->displayTime = vrapi_GetPredictedDisplayTime(Ovr, FrameIndex);
    frame->tracking = vrapi_GetPredictedTracking2(Ovr, frame->displayTime);

    {
        MutexLock lock(trackingFrameMutex);
        trackingFrameList.push_back(frame);
        if (trackingFrameList.size() > 100) {
            trackingFrameList.pop_front();
        }
    }

    sendTrackingInfo(env, callback, frame->displayTime, &frame->tracking);
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_polygraphene_alvr_VrAPI_getSurfaceTextureID(JNIEnv *env, jobject instance) {
    return SurfaceTextureID;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_VrAPI_onChangeSettings(JNIEnv *env, jobject instance,
                                                  jint EnableTestMode, jint Suspend) {
    enableTestMode = EnableTestMode;
    suspend = Suspend;
}

//
// Life cycle management.
//

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_VrAPI_onSurfaceCreated(JNIEnv *env, jobject instance,
                                                  jobject surface) {
    window = ANativeWindow_fromSurface(env, surface);

    onVrModeChange(env);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_VrAPI_onSurfaceDestroyed(JNIEnv *env, jobject instance) {
    if (window != NULL) {
        ANativeWindow_release(window);
    }
    window = NULL;

    onVrModeChange(env);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_VrAPI_onSurfaceChanged(JNIEnv *env, jobject instance, jobject surface) {
    ANativeWindow *newWindow = ANativeWindow_fromSurface(env, surface);
    if (newWindow != window) {
        ANativeWindow_release(window);
        window = NULL;
        onVrModeChange(env);

        window = newWindow;
        if (window != NULL) {
            onVrModeChange(env);
        }
    } else if (newWindow != NULL) {
        ANativeWindow_release(newWindow);
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_VrAPI_onResume(JNIEnv *env, jobject instance) {
    Resumed = true;
    onVrModeChange(env);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_alvr_VrAPI_onPause(JNIEnv *env, jobject instance) {
    Resumed = false;
    onVrModeChange(env);
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_polygraphene_alvr_VrAPI_isVrMode(JNIEnv *env, jobject instance) {
    return Ovr != NULL;
}

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

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>
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


#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, "RemoteGlass Native", __VA_ARGS__)
#define ALOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, "RemoteGlass Native", __VA_ARGS__)

static const int NUM_MULTI_SAMPLES = 4;

ANativeWindow *window = NULL;
ovrMobile *Ovr;
bool UseMultiview = true;

static double GetTimeInSeconds() {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec * 1e9 + now.tv_nsec) * 0.000000001;
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

static const char * GlErrorString( GLenum error )
{
    switch ( error )
    {
        case GL_NO_ERROR:						return "GL_NO_ERROR";
        case GL_INVALID_ENUM:					return "GL_INVALID_ENUM";
        case GL_INVALID_VALUE:					return "GL_INVALID_VALUE";
        case GL_INVALID_OPERATION:				return "GL_INVALID_OPERATION";
        case GL_INVALID_FRAMEBUFFER_OPERATION:	return "GL_INVALID_FRAMEBUFFER_OPERATION";
        case GL_OUT_OF_MEMORY:					return "GL_OUT_OF_MEMORY";
        default: return "unknown";
    }
}

static void GLCheckErrors( int line )
{
    for ( int i = 0; i < 10; i++ )
    {
        const GLenum error = glGetError();
        if ( error == GL_NO_ERROR )
        {
            break;
        }
        ALOGE( "GL error on line %d: %s", line, GlErrorString( error ) );
    }
}

#define GL( func )		func; GLCheckErrors( __LINE__ );

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
                "uniform SceneMatrices\n"
                "{\n"
                "	uniform mat4 ViewMatrix[NUM_VIEWS];\n"
                "	uniform mat4 ProjectionMatrix[NUM_VIEWS];\n"
                "} sm;\n"
                "out vec4 fragmentColor;\n"
                "void main()\n"
                "{\n"
                "	gl_Position = sm.ProjectionMatrix[VIEW_ID] * ( sm.ViewMatrix[VIEW_ID] * ( vertexTransform * vec4( vertexPosition, 1.0 ) ) );\n"
                "	fragmentColor = vertexColor;\n"
                "}\n";

static const char FRAGMENT_SHADER[] =
        "in lowp vec4 fragmentColor;\n"
                "out lowp vec4 outColor;\n"
                "void main()\n"
                "{\n"
                "	outColor = fragmentColor;\n"
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

#define MAX_VERTEX_ATTRIB_POINTERS        3

typedef struct {
    GLuint VertexBuffer;
    GLuint IndexBuffer;
    GLuint VertexArrayObject;
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

static void ovrGeometry_CreateCube(ovrGeometry *geometry) {
    typedef struct {
        signed char positions[8][4];
        unsigned char colors[8][4];
    } ovrCubeVertices;

    static const ovrCubeVertices cubeVertices =
            {
                    // positions
                    {
                            {-126, +127, -126, +127}, {+127, +127, -126, +127}, {+127, +127, +127, +127}, {-126, +127, +127, +127},    // top
                            {-126, -126, -126, +127}, {-126, -126, +127, +127}, {+127, -126, +127, +127}, {+127, -126, -126, +127}    // bottom
                    },
                    // colors
                    {
                            {255,  0,    255,  255},  {0,    255,  0,    255},  {0,    0,    255,  255},  {255,  0,    0,    255},
                            {0,    0,    255,  255},  {0,    255,  0,    255},  {255,  0,    255,  255},  {255,  0,    0,    255}
                    },
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
    geometry->VertexAttribs[0].Type = GL_BYTE;
    geometry->VertexAttribs[0].Normalized = true;
    geometry->VertexAttribs[0].Stride = sizeof(cubeVertices.positions[0]);
    geometry->VertexAttribs[0].Pointer = (const GLvoid *) offsetof(ovrCubeVertices, positions);

    geometry->VertexAttribs[1].Index = VERTEX_ATTRIBUTE_LOCATION_COLOR;
    geometry->VertexAttribs[1].Size = 4;
    geometry->VertexAttribs[1].Type = GL_UNSIGNED_BYTE;
    geometry->VertexAttribs[1].Normalized = true;
    geometry->VertexAttribs[1].Stride = sizeof(cubeVertices.colors[0]);
    geometry->VertexAttribs[1].Pointer = (const GLvoid *) offsetof(ovrCubeVertices, colors);

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
    UNIFORM_MODEL_MATRIX,
    UNIFORM_VIEW_ID,
    UNIFORM_SCENE_MATRICES,
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
                {UNIFORM_MODEL_MATRIX,   UNIFORM_TYPE_MATRIX4X4, "ModelMatrix"},
                {UNIFORM_VIEW_ID,        UNIFORM_TYPE_INT,       "ViewID"},
                {UNIFORM_SCENE_MATRICES, UNIFORM_TYPE_BUFFER,    "SceneMatrices"},
        };

static void ovrProgram_Clear(ovrProgram *program) {
    program->Program = 0;
    program->VertexShader = 0;
    program->FragmentShader = 0;
    memset(program->UniformLocation, 0, sizeof(program->UniformLocation));
    memset(program->UniformBinding, 0, sizeof(program->UniformBinding));
    memset(program->Textures, 0, sizeof(program->Textures));
}

static const char *programVersion = "#version 300 es\n";

static bool
ovrProgram_Create(ovrProgram *program, const char *vertexSource, const char *fragmentSource,
                  const bool useMultiview) {
    GLint r;

    GL(program->VertexShader = glCreateShader(GL_VERTEX_SHADER));
    if(program->VertexShader == 0){
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

#define NUM_INSTANCES        1500
#define NUM_ROTATIONS        16

typedef struct {
    bool CreatedScene;
    bool CreatedVAOs;
    unsigned int Random;
    ovrProgram Program;
    ovrGeometry Cube;
    GLuint SceneMatrices;
    GLuint InstanceTransformBuffer;
    ovrVector3f Rotations[NUM_ROTATIONS];
    ovrVector3f CubePositions[NUM_INSTANCES];
    int CubeRotations[NUM_INSTANCES];
} ovrScene;

static void ovrScene_Clear(ovrScene *scene) {
    scene->CreatedScene = false;
    scene->CreatedVAOs = false;
    scene->Random = 2;
    scene->SceneMatrices = 0;
    scene->InstanceTransformBuffer = 0;

    ovrProgram_Clear(&scene->Program);
    ovrGeometry_Clear(&scene->Cube);
}

static bool ovrScene_IsCreated(ovrScene *scene) {
    return scene->CreatedScene;
}

static void ovrScene_CreateVAOs(ovrScene *scene) {
    if (!scene->CreatedVAOs) {
        ovrGeometry_CreateVAO(&scene->Cube);

        // Modify the VAO to use the instance transform attributes.
        GL(glBindVertexArray(scene->Cube.VertexArrayObject));
        GL(glBindBuffer(GL_ARRAY_BUFFER, scene->InstanceTransformBuffer));
        for (int i = 0; i < 4; i++) {
            GL(glEnableVertexAttribArray(VERTEX_ATTRIBUTE_LOCATION_TRANSFORM + i));
            GL(glVertexAttribPointer(VERTEX_ATTRIBUTE_LOCATION_TRANSFORM + i, 4, GL_FLOAT,
                                     false, 4 * 4 * sizeof(float),
                                     (void *) (i * 4 * sizeof(float))));
            GL(glVertexAttribDivisor(VERTEX_ATTRIBUTE_LOCATION_TRANSFORM + i, 1));
        }
        GL(glBindVertexArray(0));

        scene->CreatedVAOs = true;
    }
}

static void ovrScene_DestroyVAOs(ovrScene *scene) {
    if (scene->CreatedVAOs) {
        ovrGeometry_DestroyVAO(&scene->Cube);

        scene->CreatedVAOs = false;
    }
}

// Returns a random float in the range [0, 1].
static float ovrScene_RandomFloat(ovrScene *scene) {
    scene->Random = 1664525L * scene->Random + 1013904223L;
    unsigned int rf = 0x3F800000 | (scene->Random & 0x007FFFFF);
    return (*(float *) &rf) - 1.0f;
}

static void ovrScene_Create(ovrScene *scene, bool useMultiview) {
    ovrProgram_Create(&scene->Program, VERTEX_SHADER, FRAGMENT_SHADER, useMultiview);
    ovrGeometry_CreateCube(&scene->Cube);

    // Create the instance transform attribute buffer.
    GL(glGenBuffers(1, &scene->InstanceTransformBuffer));
    GL(glBindBuffer(GL_ARRAY_BUFFER, scene->InstanceTransformBuffer));
    GL(glBufferData(GL_ARRAY_BUFFER, NUM_INSTANCES * 4 * 4 * sizeof(float), NULL, GL_DYNAMIC_DRAW));
    GL(glBindBuffer(GL_ARRAY_BUFFER, 0));

    // Setup the scene matrices.
    GL(glGenBuffers(1, &scene->SceneMatrices));
    GL(glBindBuffer(GL_UNIFORM_BUFFER, scene->SceneMatrices));
    GL(glBufferData(GL_UNIFORM_BUFFER, 2 * sizeof(ovrMatrix4f) /* 2 view matrices */ +
                                       2 * sizeof(ovrMatrix4f) /* 2 projection matrices */,
                    NULL, GL_STATIC_DRAW));
    GL(glBindBuffer(GL_UNIFORM_BUFFER, 0));

    // Setup random rotations.
    for (int i = 0; i < NUM_ROTATIONS; i++) {
        scene->Rotations[i].x = ovrScene_RandomFloat(scene);
        scene->Rotations[i].y = ovrScene_RandomFloat(scene);
        scene->Rotations[i].z = ovrScene_RandomFloat(scene);
    }

    // Setup random cube positions and rotations.
    for (int i = 0; i < NUM_INSTANCES; i++) {
        // Using volatile keeps the compiler from optimizing away multiple calls to ovrScene_RandomFloat().
        volatile float rx, ry, rz;
        for (;;) {
            rx = (ovrScene_RandomFloat(scene) - 0.5f) * (50.0f + sqrt(NUM_INSTANCES));
            ry = (ovrScene_RandomFloat(scene) - 0.5f) * (50.0f + sqrt(NUM_INSTANCES));
            rz = (ovrScene_RandomFloat(scene) - 0.5f) * (50.0f + sqrt(NUM_INSTANCES));
            // If too close to 0,0,0
            if (fabsf(rx) < 4.0f && fabsf(ry) < 4.0f && fabsf(rz) < 4.0f) {
                continue;
            }
            // Test for overlap with any of the existing cubes.
            bool overlap = false;
            for (int j = 0; j < i; j++) {
                if (fabsf(rx - scene->CubePositions[j].x) < 4.0f &&
                    fabsf(ry - scene->CubePositions[j].y) < 4.0f &&
                    fabsf(rz - scene->CubePositions[j].z) < 4.0f) {
                    overlap = true;
                    break;
                }
            }
            if (!overlap) {
                break;
            }
        }

        // Insert into list sorted based on distance.
        int insert = 0;
        const float distSqr = rx * rx + ry * ry + rz * rz;
        for (int j = i; j > 0; j--) {
            const ovrVector3f *otherPos = &scene->CubePositions[j - 1];
            const float otherDistSqr = otherPos->x * otherPos->x + otherPos->y * otherPos->y +
                                       otherPos->z * otherPos->z;
            if (distSqr > otherDistSqr) {
                insert = j;
                break;
            }
            scene->CubePositions[j] = scene->CubePositions[j - 1];
            scene->CubeRotations[j] = scene->CubeRotations[j - 1];
        }

        scene->CubePositions[insert].x = rx;
        scene->CubePositions[insert].y = ry;
        scene->CubePositions[insert].z = rz;

        scene->CubeRotations[insert] = (int) (ovrScene_RandomFloat(scene) * (NUM_ROTATIONS - 0.1f));
    }

    scene->CreatedScene = true;

#if !MULTI_THREADED
    ovrScene_CreateVAOs(scene);
#endif
}

static void ovrScene_Destroy(ovrScene *scene) {
#if !MULTI_THREADED
    ovrScene_DestroyVAOs(scene);
#endif

    ovrProgram_Destroy(&scene->Program);
    ovrGeometry_Destroy(&scene->Cube);
    GL(glDeleteBuffers(1, &scene->InstanceTransformBuffer));
    GL(glDeleteBuffers(1, &scene->SceneMatrices));
    scene->CreatedScene = false;
}

/*
================================================================================

ovrSimulation

================================================================================
*/

typedef struct {
    ovrVector3f CurrentRotation;
} ovrSimulation;

static void ovrSimulation_Clear(ovrSimulation *simulation) {
    simulation->CurrentRotation.x = 0.0f;
    simulation->CurrentRotation.y = 0.0f;
    simulation->CurrentRotation.z = 0.0f;
}

static void ovrSimulation_Advance(ovrSimulation *simulation, double elapsedDisplayTime) {
    // Update rotation.
    simulation->CurrentRotation.x = (float) (elapsedDisplayTime);
    simulation->CurrentRotation.y = (float) (elapsedDisplayTime);
    simulation->CurrentRotation.z = (float) (elapsedDisplayTime);
}

/*
================================================================================

ovrRenderer

================================================================================
*/

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
                                                   const ovrScene *scene,
                                                   const ovrSimulation *simulation,
                                                   const ovrTracking2 *tracking, ovrMobile *ovr,
                                                   unsigned long long *completionFence) {
    ovrMatrix4f rotationMatrices[NUM_ROTATIONS];
    for (int i = 0; i < NUM_ROTATIONS; i++) {
        rotationMatrices[i] = ovrMatrix4f_CreateRotation(
                scene->Rotations[i].x * simulation->CurrentRotation.x,
                scene->Rotations[i].y * simulation->CurrentRotation.y,
                scene->Rotations[i].z * simulation->CurrentRotation.z);
    }

    // Update the instance transform attributes.
    GL(glBindBuffer(GL_ARRAY_BUFFER, scene->InstanceTransformBuffer));
    GL(ovrMatrix4f *cubeTransforms = (ovrMatrix4f *) glMapBufferRange(GL_ARRAY_BUFFER, 0,
                                                                      NUM_INSTANCES *
                                                                      sizeof(ovrMatrix4f),
                                                                      GL_MAP_WRITE_BIT |
               GL_MAP_INVALIDATE_BUFFER_BIT ));
    for (int i = 0; i < NUM_INSTANCES; i++) {
        const int index = scene->CubeRotations[i];

        // Write in order in case the mapped buffer lives on write-combined memory.
        cubeTransforms[i].M[0][0] = rotationMatrices[index].M[0][0];
        cubeTransforms[i].M[0][1] = rotationMatrices[index].M[0][1];
        cubeTransforms[i].M[0][2] = rotationMatrices[index].M[0][2];
        cubeTransforms[i].M[0][3] = rotationMatrices[index].M[0][3];

        cubeTransforms[i].M[1][0] = rotationMatrices[index].M[1][0];
        cubeTransforms[i].M[1][1] = rotationMatrices[index].M[1][1];
        cubeTransforms[i].M[1][2] = rotationMatrices[index].M[1][2];
        cubeTransforms[i].M[1][3] = rotationMatrices[index].M[1][3];

        cubeTransforms[i].M[2][0] = rotationMatrices[index].M[2][0];
        cubeTransforms[i].M[2][1] = rotationMatrices[index].M[2][1];
        cubeTransforms[i].M[2][2] = rotationMatrices[index].M[2][2];
        cubeTransforms[i].M[2][3] = rotationMatrices[index].M[2][3];

        cubeTransforms[i].M[3][0] = scene->CubePositions[i].x;
        cubeTransforms[i].M[3][1] = scene->CubePositions[i].y;
        cubeTransforms[i].M[3][2] = scene->CubePositions[i].z;
        cubeTransforms[i].M[3][3] = 1.0f;
    }
    GL(glUnmapBuffer(GL_ARRAY_BUFFER));
    GL(glBindBuffer(GL_ARRAY_BUFFER, 0));

    ovrTracking2 updatedTracking = *tracking;

    ovrMatrix4f eyeViewMatrixTransposed[2];
    eyeViewMatrixTransposed[0] = ovrMatrix4f_Transpose(&updatedTracking.Eye[0].ViewMatrix);
    eyeViewMatrixTransposed[1] = ovrMatrix4f_Transpose(&updatedTracking.Eye[1].ViewMatrix);

    ovrMatrix4f projectionMatrixTransposed[2];
    projectionMatrixTransposed[0] = ovrMatrix4f_Transpose(&updatedTracking.Eye[0].ProjectionMatrix);
    projectionMatrixTransposed[1] = ovrMatrix4f_Transpose(&updatedTracking.Eye[1].ProjectionMatrix);

    // Update the scene matrices.
    GL(glBindBuffer(GL_UNIFORM_BUFFER, scene->SceneMatrices));
    GL(ovrMatrix4f *sceneMatrices = (ovrMatrix4f *) glMapBufferRange(GL_UNIFORM_BUFFER, 0,
                                                                     2 *
                                                                     sizeof(ovrMatrix4f) /* 2 view matrices */ +
                                                                     2 *
                                                                     sizeof(ovrMatrix4f) /* 2 projection matrices */,
               GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT ));

    if (sceneMatrices != NULL) {
        memcpy((char *) sceneMatrices, &eyeViewMatrixTransposed, 2 * sizeof(ovrMatrix4f));
        memcpy((char *) sceneMatrices + 2 * sizeof(ovrMatrix4f), &projectionMatrixTransposed,
               2 * sizeof(ovrMatrix4f));
    }

    GL(glUnmapBuffer(GL_UNIFORM_BUFFER));
    GL(glBindBuffer(GL_UNIFORM_BUFFER, 0));

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

    // Render the eye images.
    for (int eye = 0; eye < renderer->NumBuffers; eye++) {
        // NOTE: In the non-mv case, latency can be further reduced by updating the sensor prediction
        // for each eye (updates orientation, not position)
        ovrFramebuffer *frameBuffer = &renderer->FrameBuffer[eye];
        ovrFramebuffer_SetCurrent(frameBuffer);

        GL(glUseProgram(scene->Program.Program));
        GL(glBindBufferBase(GL_UNIFORM_BUFFER,
                            scene->Program.UniformBinding[UNIFORM_SCENE_MATRICES],
                            scene->SceneMatrices));
        if (scene->Program.UniformLocation[UNIFORM_VIEW_ID] >=
            0)  // NOTE: will not be present when multiview path is enabled.
        {
            GL(glUniform1i(scene->Program.UniformLocation[UNIFORM_VIEW_ID], eye));
        }
        GL(glEnable(GL_SCISSOR_TEST));
        GL(glDepthMask(GL_TRUE));
        GL(glEnable(GL_DEPTH_TEST));
        GL(glDepthFunc(GL_LEQUAL));
        GL(glEnable(GL_CULL_FACE));
        GL(glCullFace(GL_BACK));
        GL(glViewport(0, 0, frameBuffer->Width, frameBuffer->Height));
        GL(glScissor(0, 0, frameBuffer->Width, frameBuffer->Height));
        GL(glClearColor(0.125f, 0.0f, 0.125f, 1.0f));
        GL(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
        GL(glBindVertexArray(scene->Cube.VertexArrayObject));
        GL(glDrawElementsInstanced(GL_TRIANGLES, scene->Cube.IndexCount, GL_UNSIGNED_SHORT, NULL,
                                   NUM_INSTANCES));
        GL(glBindVertexArray(0));
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
ovrScene Scene;
ovrSimulation Simulation;
ovrJava java;
int SwapInterval = 1;

void onVrModeChange(JNIEnv *env) {

    eglInit();


    EglInitExtensions();

    java.Env = env;
    env->GetJavaVM(&java.Vm);
    ovrModeParms parms = vrapi_DefaultModeParms(&java);


    const ovrInitParms initParms = vrapi_DefaultInitParms(&java);
    int32_t initResult = vrapi_Initialize(&initParms);
    if (initResult != VRAPI_INITIALIZE_SUCCESS) {
        // If intialization failed, vrapi_* function calls will not be available.
        ALOGE("vrapi_Initialize failed");
        return;
    }

    UseMultiview &= (glExtensions.multi_view &&
                     vrapi_GetSystemPropertyInt(&java, VRAPI_SYS_PROP_MULTIVIEW_AVAILABLE));
    ALOGV("UseMultiview:%d", UseMultiview);

    ovrRenderer_Create(&Renderer, &java, UseMultiview);


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


}


extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_remoteglass_VrAPI_init(JNIEnv *env, jobject instance) {
    //eglInit();
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_remoteglass_VrAPI_onSurfaceCreated(JNIEnv *env, jobject instance,
                                                         jobject surface, jobject activity) {
    if (window != NULL) {
        ANativeWindow_release(window);
    }
    window = ANativeWindow_fromSurface(env, surface);

    java.ActivityObject = env->NewGlobalRef(activity);
    onVrModeChange(env);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_remoteglass_VrAPI_onSurfaceDestroyed(JNIEnv *env, jobject instance) {

#if MULTI_THREADED
    ovrRenderThread_Destroy( &appState.RenderThread );
#else
    ovrRenderer_Destroy(&Renderer);
#endif

    ovrScene_Destroy(&Scene);
    //ovrEgl_DestroyContext( &appState.Egl );

    vrapi_Shutdown();

    //(*java.Vm)->DetachCurrentThread( java.Vm );

    if (window != NULL) {
        ANativeWindow_release(window);
    }
}


double startTime;
long long FrameIndex = 0;
extern "C"
JNIEXPORT void JNICALL
Java_com_polygraphene_remoteglass_VrAPI_render(JNIEnv *env, jobject instance) {

    double DisplayTime = 0.0;

    if (!ovrScene_IsCreated(&Scene)) {
        // Show a loading icon.
        int frameFlags = 0;
        frameFlags |= VRAPI_FRAME_FLAG_FLUSH;

        ovrLayerProjection2 blackLayer = vrapi_DefaultLayerBlackProjection2();
        blackLayer.Header.Flags |= VRAPI_FRAME_LAYER_FLAG_INHIBIT_SRGB_FRAMEBUFFER;

        ovrLayerLoadingIcon2 iconLayer = vrapi_DefaultLayerLoadingIcon2();
        iconLayer.Header.Flags |= VRAPI_FRAME_LAYER_FLAG_INHIBIT_SRGB_FRAMEBUFFER;

        const ovrLayerHeader2 *layers[] =
                {
                        &blackLayer.Header,
                        &iconLayer.Header,
                };


        ovrSubmitFrameDescription2 frameDesc = {};
        frameDesc.Flags = frameFlags;
        frameDesc.SwapInterval = 1;
        frameDesc.FrameIndex = FrameIndex;
        frameDesc.DisplayTime = DisplayTime;
        frameDesc.LayerCount = 2;
        frameDesc.Layers = layers;

        ovrResult res = vrapi_SubmitFrame2(Ovr, &frameDesc);
        ALOGV("vrapi_SubmitFrame2: %d", res);

        // Create the scene.
        ovrScene_Create(&Scene, UseMultiview);

        startTime = GetTimeInSeconds();

    }
    FrameIndex++;


// Get the HMD pose, predicted for the middle of the time period during which
// the new eye images will be displayed. The number of frames predicted ahead
// depends on the pipeline depth of the engine and the synthesis rate.
// The better the prediction, the less black will be pulled in at the edges.
    const double predictedDisplayTime = vrapi_GetPredictedDisplayTime(Ovr, FrameIndex);
    const ovrTracking2 tracking = vrapi_GetPredictedTracking2(Ovr, predictedDisplayTime);

    DisplayTime = predictedDisplayTime;

// Advance the simulation based on the elapsed time since start of loop till predicted display time.
    ovrSimulation_Advance(&Simulation, predictedDisplayTime
                                       - startTime);


    unsigned long long completionFence = 0;

// Render eye images and setup the primary layer using ovrTracking2.
    const ovrLayerProjection2 worldLayer = ovrRenderer_RenderFrame(&Renderer, &java,
                                                                   &Scene, &Simulation, &tracking,
                                                                   Ovr, &completionFence);

    const ovrLayerHeader2 *layers2[] =
            {
                    &worldLayer.Header
            };

    ovrSubmitFrameDescription2 frameDesc = {};
    frameDesc.
            Flags = 0;
    frameDesc.
            SwapInterval = SwapInterval;
    frameDesc.
            FrameIndex = FrameIndex;
    frameDesc.
            CompletionFence = completionFence;
    frameDesc.
            DisplayTime = DisplayTime;
    frameDesc.
            LayerCount = 1;
    frameDesc.
            Layers = layers2;

// Hand over the eye images to the time warp.
    ovrResult res = vrapi_SubmitFrame2(Ovr, &frameDesc
    );
    ALOGV("vrapi_SubmitFrame2 a %d", res);
}
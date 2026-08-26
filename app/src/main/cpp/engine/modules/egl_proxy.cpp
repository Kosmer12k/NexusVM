/*
 * NexusVM — Module 6: EGL/Vulkan GPU Proxy Implementation
 * 
 * This module provides the critical GPU passthrough that enables
 * near-native gaming performance. The guest's OpenGL ES / Vulkan calls
 * are forwarded directly to the host's GPU driver.
 * 
 * Architecture:
 *   [Guest App/Game]
 *       |  (OpenGL ES / Vulkan calls)
 *   [libegl_proxy.so in guest]  (LD_PRELOAD wrapper)
 *       |  (shared memory / Unix socket)
 *   [EGLProxy module in engine]
 *       |  (real EGL/GLES calls)
 *   [Host GPU Driver (Adreno/Mali/PowerVR)]
 *       |
 *   [SurfaceView on screen]
 */

#include "egl_proxy.h"
#include "nexus_engine.h"
#include "utils/logger.h"

#ifdef __ANDROID__
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <android/native_window.h>
#else
// Host Mock EGL / GLES Types & Functions
typedef int EGLint;
typedef void* EGLNativeWindowType;
#define EGL_DEFAULT_DISPLAY nullptr
#define EGL_NO_DISPLAY      nullptr
#define EGL_NO_CONTEXT      nullptr
#define EGL_NO_SURFACE      nullptr
#define EGL_NONE            0
#define EGL_SUCCESS         0x3000
#define EGL_BAD_SURFACE     0x300D
#define EGL_OPENGL_ES3_BIT_KHR 0x00000040
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_SURFACE_TYPE    0x3033
#define EGL_WINDOW_BIT      0x0004
#define EGL_RED_SIZE        0x3024
#define EGL_GREEN_SIZE      0x3023
#define EGL_BLUE_SIZE       0x3022
#define EGL_ALPHA_SIZE      0x3021
#define EGL_DEPTH_SIZE      0x3025
#define EGL_STENCIL_SIZE    0x3026
#define EGL_SAMPLE_BUFFERS  0x3032
#define EGL_CONTEXT_MAJOR_VERSION 0x3098
#define EGL_CONTEXT_MINOR_VERSION 0x30FB
#define EGL_WIDTH           0x3057
#define EGL_HEIGHT          0x3056
#define GL_VENDOR           0x1F00
#define GL_RENDERER         0x1F01
#define GL_VERSION          0x1F02
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_DEPTH_BUFFER_BIT 0x00000100

static inline EGLDisplay eglGetDisplay(void*) { return (EGLDisplay)1; }
static inline bool eglInitialize(EGLDisplay, EGLint* maj, EGLint* min) { if (maj) *maj = 1; if (min) *min = 5; return true; }
static inline EGLint eglGetError() { return EGL_SUCCESS; }
static inline bool eglChooseConfig(EGLDisplay, const EGLint*, EGLConfig* cfg, EGLint, EGLint* num) { if (cfg) *cfg = (EGLConfig)1; if (num) *num = 1; return true; }
static inline EGLContext eglCreateContext(EGLDisplay, EGLConfig, EGLContext, const EGLint*) { return (EGLContext)1; }
static inline EGLSurface eglCreateWindowSurface(EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint*) { return (EGLSurface)1; }
static inline bool eglQuerySurface(EGLDisplay, EGLSurface, EGLint, int* val) { if (val) *val = 1080; return true; }
static inline bool eglMakeCurrent(EGLDisplay, EGLSurface, EGLSurface, EGLContext) { return true; }
static inline bool eglDestroySurface(EGLDisplay, EGLSurface) { return true; }
static inline bool eglDestroyContext(EGLDisplay, EGLContext) { return true; }
static inline bool eglTerminate(EGLDisplay) { return true; }
static inline bool eglSwapBuffers(EGLDisplay, EGLSurface) { return true; }
static inline const unsigned char* glGetString(unsigned int) { return (const unsigned char*)"Host-Software-Mock"; }
static inline void glViewport(int, int, int, int) {}
static inline void glClearColor(float, float, float, float) {}
static inline void glClear(unsigned int) {}
#endif
#include <ctime>
#include <cstring>

#define MOD_TAG "EGLProxy"

namespace nexusvm {

EGLProxy::EGLProxy(NexusEngine& engine) : m_engine(engine) {
    MLOG_I(MOD_TAG, "Created");
}

EGLProxy::~EGLProxy() {
    shutdown();
}

int EGLProxy::init() {
    MLOG_I(MOD_TAG, "Initialized");
    return 0;
}

void EGLProxy::shutdown() {
    teardown();
    MLOG_I(MOD_TAG, "Shutdown");
}

// ============================================================================
// EGL Initialization
// ============================================================================

int EGLProxy::initEGL() {
    MLOG_I(MOD_TAG, "Initializing EGL...");
    
    // Get the default display
    m_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (m_display == EGL_NO_DISPLAY) {
        MLOG_E(MOD_TAG, "eglGetDisplay failed");
        return -1;
    }
    
    // Initialize EGL
    EGLint major, minor;
    if (!eglInitialize(m_display, &major, &minor)) {
        MLOG_E(MOD_TAG, "eglInitialize failed: 0x%x", eglGetError());
        return -2;
    }
    MLOG_I(MOD_TAG, "EGL version: %d.%d", major, minor);
    
    // Choose EGL config
    EGLint config_attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_ALPHA_SIZE,      8,
        EGL_DEPTH_SIZE,      24,
        EGL_STENCIL_SIZE,    8,
        EGL_SAMPLE_BUFFERS,  0,
        EGL_NONE
    };
    
    EGLint num_configs;
    if (!eglChooseConfig(m_display, config_attribs, &m_egl_config, 1, &num_configs) || num_configs == 0) {
        MLOG_E(MOD_TAG, "eglChooseConfig failed: 0x%x", eglGetError());
        return -3;
    }
    
    // Create OpenGL ES 3.2 context (highest available)
    EGLint context_attribs[] = {
        EGL_CONTEXT_MAJOR_VERSION, 3,
        EGL_CONTEXT_MINOR_VERSION, 2,
        EGL_NONE
    };
    
    m_context = eglCreateContext(m_display, m_egl_config, EGL_NO_CONTEXT, context_attribs);
    if (m_context == EGL_NO_CONTEXT) {
        // Fall back to OpenGL ES 3.0
        context_attribs[3] = 0;
        m_context = eglCreateContext(m_display, m_egl_config, EGL_NO_CONTEXT, context_attribs);
        if (m_context == EGL_NO_CONTEXT) {
            MLOG_E(MOD_TAG, "eglCreateContext failed: 0x%x", eglGetError());
            return -4;
        }
        MLOG_I(MOD_TAG, "Created OpenGL ES 3.0 context");
    } else {
        MLOG_I(MOD_TAG, "Created OpenGL ES 3.2 context");
    }
    
    return 0;
}

int EGLProxy::createSurface() {
    if (!m_native_window) {
        MLOG_E(MOD_TAG, "No native window set");
        return -1;
    }
    
    // Create window surface from the SurfaceView's native window
    EGLint surface_attribs[] = {
        EGL_NONE
    };
    
    m_surface = eglCreateWindowSurface(m_display, m_egl_config,
                                        static_cast<EGLNativeWindowType>(m_native_window),
                                        surface_attribs);
    if (m_surface == EGL_NO_SURFACE) {
        MLOG_E(MOD_TAG, "eglCreateWindowSurface failed: 0x%x", eglGetError());
        return -2;
    }
    
    // Query surface dimensions
    eglQuerySurface(m_display, m_surface, EGL_WIDTH, &m_fb_width);
    eglQuerySurface(m_display, m_surface, EGL_HEIGHT, &m_fb_height);
    m_fb_stride = m_fb_width * 4; // RGBA
    
    MLOG_I(MOD_TAG, "EGL surface created: %dx%d", m_fb_width, m_fb_height);
    return 0;
}

// ============================================================================
// Passthrough Setup
// ============================================================================

int EGLProxy::setupPassthrough(void* native_window) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    MLOG_I(MOD_TAG, "Setting up GPU passthrough");
    
    m_native_window = native_window;
    
    // Initialize EGL
    int ret = initEGL();
    if (ret != 0) {
        MLOG_E(MOD_TAG, "EGL init failed: %d", ret);
        return ret;
    }
    
    // Create surface
    ret = createSurface();
    if (ret != 0) {
        MLOG_E(MOD_TAG, "Surface creation failed: %d", ret);
        return ret;
    }
    
    // Make context current
    ret = makeCurrent();
    if (ret != 0) {
        MLOG_E(MOD_TAG, "Make current failed: %d", ret);
        return ret;
    }
    
    // Log GPU info
    const char* vendor   = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const char* version  = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    
    MLOG_I(MOD_TAG, "GPU Vendor:   %s", vendor ? vendor : "unknown");
    MLOG_I(MOD_TAG, "GPU Renderer: %s", renderer ? renderer : "unknown");
    MLOG_I(MOD_TAG, "GL Version:   %s", version ? version : "unknown");
    
    // Set initial viewport
    glViewport(0, 0, m_fb_width, m_fb_height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    eglSwapBuffers(m_display, m_surface);
    
    m_initialized = true;
    
    // Reset FPS counter
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    m_last_fps_time = static_cast<uint64_t>(ts.tv_sec) * 1000 + 
                      static_cast<uint64_t>(ts.tv_nsec) / 1000000;
    m_fps_frame_count = 0;
    
    MLOG_I(MOD_TAG, "GPU passthrough ready (native %dx%d)", m_fb_width, m_fb_height);
    return 0;
}

int EGLProxy::teardown() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (!m_initialized) return 0;
    
    MLOG_I(MOD_TAG, "Tearing down GPU passthrough");
    
    if (m_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        
        if (m_surface != EGL_NO_SURFACE) {
            eglDestroySurface(m_display, m_surface);
            m_surface = EGL_NO_SURFACE;
        }
        
        if (m_context != EGL_NO_CONTEXT) {
            eglDestroyContext(m_display, m_context);
            m_context = EGL_NO_CONTEXT;
        }
        
        eglTerminate(m_display);
        m_display = EGL_NO_DISPLAY;
    }
    
    m_initialized = false;
    return 0;
}

void EGLProxy::setNativeWindow(void* native_window) {
    m_native_window = native_window;
}

// ============================================================================
// Rendering
// ============================================================================

int EGLProxy::makeCurrent() {
    if (!eglMakeCurrent(m_display, m_surface, m_surface, m_context)) {
        MLOG_E(MOD_TAG, "eglMakeCurrent failed: 0x%x", eglGetError());
        return -1;
    }
    return 0;
}

int EGLProxy::swapBuffers() {
    if (!m_initialized) return -1;
    
    if (!eglSwapBuffers(m_display, m_surface)) {
        EGLint error = eglGetError();
        if (error == EGL_BAD_SURFACE) {
            // Surface lost — need to recreate
            MLOG_W(MOD_TAG, "EGL surface lost, recreating...");
            eglDestroySurface(m_display, m_surface);
            m_surface = EGL_NO_SURFACE;
            createSurface();
            makeCurrent();
            return -2;
        }
        MLOG_E(MOD_TAG, "eglSwapBuffers failed: 0x%x", error);
        return -1;
    }
    
    m_frame_count++;
    updateFps();
    
    return 0;
}

void EGLProxy::updateFps() {
    m_fps_frame_count++;
    
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now = static_cast<uint64_t>(ts.tv_sec) * 1000 + 
                   static_cast<uint64_t>(ts.tv_nsec) / 1000000;
    
    uint64_t elapsed = now - m_last_fps_time;
    if (elapsed >= 1000) { // Update every second
        m_fps = static_cast<float>(m_fps_frame_count) * 1000.0f / static_cast<float>(elapsed);
        m_fps_frame_count = 0;
        m_last_fps_time = now;
    }
}

} // namespace nexusvm

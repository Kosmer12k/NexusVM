/*
 * NexusVM — Module 6: EGL/Vulkan GPU Proxy
 * 
 * Provides GPU passthrough from guest to host.
 * Guest rendering calls are forwarded to the host's real GPU.
 */

#pragma once

#include <string>
#include <atomic>
#include <mutex>
#include <cstdint>

// Forward declare EGL types to avoid including full headers in header
typedef void* EGLDisplay;
typedef void* EGLSurface;
typedef void* EGLContext;
typedef void* EGLConfig;

namespace nexusvm {

class NexusEngine;

class EGLProxy {
public:
    explicit EGLProxy(NexusEngine& engine);
    ~EGLProxy();
    
    int init();
    void shutdown();
    
    /** Setup GPU passthrough with the given host native window */
    int setupPassthrough(void* native_window);
    
    /** Tear down GPU passthrough */
    int teardown();
    
    /** Set the native window for rendering */
    void setNativeWindow(void* native_window);
    
    /** Swap buffers (present a frame) */
    int swapBuffers();
    
    /** Make the proxy context current */
    int makeCurrent();
    
    /** Get the shared memory framebuffer address */
    void* getFramebuffer() const { return m_framebuffer; }
    int getFramebufferWidth() const { return m_fb_width; }
    int getFramebufferHeight() const { return m_fb_height; }
    
    /** Performance stats */
    float getFps() const { return m_fps; }
    uint64_t getFrameCount() const { return m_frame_count; }

private:
    NexusEngine& m_engine;
    
    // EGL state
    EGLDisplay  m_display  = nullptr;
    EGLSurface  m_surface  = nullptr;
    EGLContext  m_context  = nullptr;
    EGLConfig   m_egl_config = nullptr;
    void*       m_native_window = nullptr;
    
    // Framebuffer
    void*   m_framebuffer = nullptr;
    int     m_fb_width = 0;
    int     m_fb_height = 0;
    int     m_fb_stride = 0;
    
    // Stats
    std::atomic<float>    m_fps{0.0f};
    std::atomic<uint64_t> m_frame_count{0};
    uint64_t m_last_fps_time = 0;
    int      m_fps_frame_count = 0;
    
    mutable std::mutex m_mutex;
    bool m_initialized = false;
    
    /** Initialize EGL display and context */
    int initEGL();
    
    /** Create a pbuffer or window surface */
    int createSurface();
    
    /** Update FPS counter */
    void updateFps();
};

} // namespace nexusvm

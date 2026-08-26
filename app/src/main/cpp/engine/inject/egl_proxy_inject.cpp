/*
 * NexusVM — EGL Proxy Inject Library
 * 
 * This library is injected into guest processes that use OpenGL ES.
 * It redirects EGL/GLES calls to the host engine's EGL context
 * via shared memory, achieving near-native GPU performance.
 * 
 * For the initial version, this is a stub that will be expanded
 * as the GPU passthrough architecture is refined.
 */

#include "egl_proxy_inject.h"
#include "utils/logger.h"
#include <dlfcn.h>

#define TAG "NexusVM-EGLProxy"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

// ============================================================================
// EGL function pointers (resolved to host's real EGL implementation)
// ============================================================================

static void* g_real_egl_lib = nullptr;
static void* g_real_gles_lib = nullptr;

void nexusvm_egl_proxy_init() {
    LOGI("EGL Proxy inject library loaded");
    
    // In a full implementation, this would:
    // 1. Connect to the engine's EGL proxy Unix socket
    // 2. Set up shared memory for command buffers
    // 3. Redirect all EGL/GLES calls through the shared memory channel
    
    // For now, we passthrough directly to the host's EGL
    // This works because on ARM64, the GPU driver is the same
    g_real_egl_lib = dlopen("libEGL.so", RTLD_LAZY);
    g_real_gles_lib = dlopen("libGLESv2.so", RTLD_LAZY);
    
    if (g_real_egl_lib) {
        LOGI("Host EGL library loaded");
    }
    if (g_real_gles_lib) {
        LOGI("Host GLESv2 library loaded");
    }
}

void nexusvm_egl_proxy_cleanup() {
    LOGI("EGL Proxy inject library unloaded");
    
    if (g_real_egl_lib) {
        dlclose(g_real_egl_lib);
        g_real_egl_lib = nullptr;
    }
    if (g_real_gles_lib) {
        dlclose(g_real_gles_lib);
        g_real_gles_lib = nullptr;
    }
}

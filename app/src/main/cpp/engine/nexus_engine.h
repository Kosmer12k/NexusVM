/*
 * NexusVM Engine — Core Engine Header
 * 
 * This is the central coordinator of the entire container engine.
 * It manages the lifecycle of all modules and provides the main API
 * called from Java via JNI.
 *
 * Copyright (c) 2024 NexusVM Project
 */

#pragma once

#include <string>
#include <memory>
#include <atomic>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <vector>

// Forward declarations — each module is its own class
namespace nexusvm {

class ContainerLoader;
class SyscallInterceptor;
class FakeRoot;
class VirtualBinder;
class PropertySpoof;
class EGLProxy;
class ProcessManager;
class InputBridge;

// ============================================================================
// Engine State Machine
// ============================================================================
enum class EngineState : int {
    UNINITIALIZED = 0,    // Engine not yet created
    INITIALIZED   = 1,    // Engine created, modules loaded
    ROM_LOADED    = 2,    // GSI ROM extracted and rootfs ready
    STARTING      = 3,    // Guest OS is booting
    RUNNING       = 4,    // Guest OS fully running
    PAUSED        = 5,    // Guest OS suspended
    STOPPING      = 6,    // Guest OS shutting down
    ERROR         = -1    // Fatal error occurred
};

// ============================================================================
// Engine Configuration (passed from Java)
// ============================================================================
struct EngineConfig {
    std::string rom_path;              // Path to system.img
    std::string rootfs_path;           // Path to extracted rootfs (inside app data)
    std::string data_path;             // App's internal data directory
    
    // Device spoofing
    std::string device_model;          // e.g. "Pixel 7 Pro"
    std::string device_brand;          // e.g. "Google"
    std::string device_manufacturer;   // e.g. "Google"
    std::string device_fingerprint;    // Build fingerprint
    std::string android_version;       // e.g. "12"
    int         sdk_version = 31;      // API level
    
    // Display
    int display_width  = 1080;
    int display_height = 2400;
    int display_dpi    = 420;
    
    // Performance
    int max_cpu_cores  = 4;
    int max_memory_mb  = 2048;         // RAM limit for guest
    bool gpu_passthrough = true;       // Enable GPU direct access
    
    // Debug
    bool debug_logging  = false;
    bool enable_adb     = false;       // Guest ADB over TCP
    int  adb_port       = 5555;
};

// ============================================================================
// Callback Interface (Engine → Java)
// ============================================================================
struct EngineCallbacks {
    std::function<void(EngineState state)>           on_state_changed;
    std::function<void(int level, const char* msg)>  on_log_message;
    std::function<void(int progress, const char* step)> on_boot_progress;
    std::function<void(const char* error)>           on_error;
    std::function<void(void* pixels, int w, int h)>  on_frame_ready;
};

// ============================================================================
// NexusEngine — Main Engine Class
// ============================================================================
class NexusEngine {
public:
    // Singleton access (one engine per process)
    static NexusEngine& getInstance();
    
    // Delete copy/move
    NexusEngine(const NexusEngine&) = delete;
    NexusEngine& operator=(const NexusEngine&) = delete;
    NexusEngine(NexusEngine&&) = delete;
    NexusEngine& operator=(NexusEngine&&) = delete;

    // ========================================================================
    // Lifecycle API (called from JNI)
    // ========================================================================
    
    /**
     * Initialize the engine with the given configuration.
     * Sets up all modules but does not start the guest OS.
     * @return 0 on success, negative error code on failure
     */
    int initialize(const EngineConfig& config);
    
    /**
     * Load and extract a GSI ROM image.
     * Extracts system.img to rootfs_path and prepares the guest filesystem.
     * @return 0 on success, negative error code on failure
     */
    int loadRom(const std::string& rom_path);
    
    /**
     * Start the guest Android OS.
     * Boots init → zygote → system_server.
     * @return 0 on success, negative error code on failure
     */
    int start();
    
    /**
     * Pause the guest OS (freeze all guest processes).
     * @return 0 on success, negative error code on failure
     */
    int pause();
    
    /**
     * Resume a paused guest OS.
     * @return 0 on success, negative error code on failure
     */
    int resume();
    
    /**
     * Stop the guest OS and clean up all resources.
     * @return 0 on success, negative error code on failure
     */
    int stop();
    
    /**
     * Destroy the engine and free all memory.
     */
    void destroy();
    
    // ========================================================================
    // Runtime API
    // ========================================================================
    
    /** Get current engine state */
    EngineState getState() const;
    
    /** Set a system property in the guest */
    int setGuestProperty(const std::string& key, const std::string& value);
    
    /** Get a system property from the guest */
    std::string getGuestProperty(const std::string& key) const;
    
    /** Send touch event to guest */
    void sendTouchEvent(int action, float x, float y, int pointer_id);
    
    /** Send key event to guest */
    void sendKeyEvent(int action, int keycode);
    
    /** Set the native window for rendering (from SurfaceView) */
    void setNativeWindow(void* native_window);
    
    /** Set callback interface */
    void setCallbacks(const EngineCallbacks& callbacks);
    
    // ========================================================================
    // Module Access (for internal use)
    // ========================================================================
    
    ContainerLoader*     getContainerLoader()     { return m_container_loader.get(); }
    SyscallInterceptor*  getSyscallInterceptor()  { return m_syscall_interceptor.get(); }
    FakeRoot*            getFakeRoot()             { return m_fake_root.get(); }
    VirtualBinder*       getVirtualBinder()        { return m_virtual_binder.get(); }
    PropertySpoof*       getPropertySpoof()        { return m_property_spoof.get(); }
    EGLProxy*            getEGLProxy()             { return m_egl_proxy.get(); }
    ProcessManager*      getProcessManager()       { return m_process_manager.get(); }
    InputBridge*         getInputBridge()           { return m_input_bridge.get(); }
    
    const EngineConfig&  getConfig() const         { return m_config; }

private:
    NexusEngine();
    ~NexusEngine();
    
    // State management
    void setState(EngineState state);
    
    // Module initialization (called during initialize())
    int initModules();
    void destroyModules();
    
    // Members
    EngineConfig                        m_config;
    EngineCallbacks                     m_callbacks;
    std::atomic<EngineState>            m_state{EngineState::UNINITIALIZED};
    mutable std::mutex                  m_mutex;
    
    // Modules (unique_ptr for automatic cleanup)
    std::unique_ptr<ContainerLoader>    m_container_loader;
    std::unique_ptr<SyscallInterceptor> m_syscall_interceptor;
    std::unique_ptr<FakeRoot>           m_fake_root;
    std::unique_ptr<VirtualBinder>      m_virtual_binder;
    std::unique_ptr<PropertySpoof>      m_property_spoof;
    std::unique_ptr<EGLProxy>           m_egl_proxy;
    std::unique_ptr<ProcessManager>     m_process_manager;
    std::unique_ptr<InputBridge>        m_input_bridge;
    
    // Native window handle (from SurfaceView)
    void*                               m_native_window = nullptr;
};

} // namespace nexusvm

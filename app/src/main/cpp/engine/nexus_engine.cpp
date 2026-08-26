/*
 * NexusVM Engine — Core Engine Implementation
 */

#include "nexus_engine.h"
#include "utils/logger.h"
#include "modules/container_loader.h"
#include "modules/syscall_interceptor.h"
#include "modules/fake_root.h"
#include "modules/virtual_binder.h"
#include "modules/property_spoof.h"
#include "modules/egl_proxy.h"
#include "modules/process_manager.h"
#include "modules/input_bridge.h"

#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace nexusvm {

// ============================================================================
// Singleton
// ============================================================================

NexusEngine& NexusEngine::getInstance() {
    static NexusEngine instance;
    return instance;
}

NexusEngine::NexusEngine() {
    LOG_I("NexusVM Engine v1.0.0 created");
}

NexusEngine::~NexusEngine() {
    destroy();
}

// ============================================================================
// State Management
// ============================================================================

void NexusEngine::setState(EngineState state) {
    EngineState old_state = m_state.exchange(state);
    if (old_state != state) {
        LOG_I("Engine state: %d -> %d", static_cast<int>(old_state), static_cast<int>(state));
        if (m_callbacks.on_state_changed) {
            m_callbacks.on_state_changed(state);
        }
    }
}

EngineState NexusEngine::getState() const {
    return m_state.load();
}

// ============================================================================
// Lifecycle
// ============================================================================

int NexusEngine::initialize(const EngineConfig& config) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_state != EngineState::UNINITIALIZED) {
        LOG_W("Engine already initialized, current state: %d", static_cast<int>(m_state.load()));
        return -1;
    }
    
    LOG_I("Initializing NexusVM Engine...");
    LOG_I("  ROM path:     %s", config.rom_path.c_str());
    LOG_I("  RootFS path:  %s", config.rootfs_path.c_str());
    LOG_I("  Data path:    %s", config.data_path.c_str());
    LOG_I("  Display:      %dx%d @%ddpi", config.display_width, config.display_height, config.display_dpi);
    LOG_I("  GPU pass:     %s", config.gpu_passthrough ? "enabled" : "disabled");
    LOG_I("  Device:       %s %s", config.device_brand.c_str(), config.device_model.c_str());
    
    m_config = config;
    
    // Create rootfs directory if it doesn't exist
    if (mkdir(config.rootfs_path.c_str(), 0755) != 0 && errno != EEXIST) {
        LOG_E("Failed to create rootfs directory: %s (%s)", config.rootfs_path.c_str(), strerror(errno));
        setState(EngineState::ERROR);
        return -2;
    }
    
    // Initialize all modules
    int ret = initModules();
    if (ret != 0) {
        LOG_E("Failed to initialize modules: %d", ret);
        setState(EngineState::ERROR);
        return ret;
    }
    
    setState(EngineState::INITIALIZED);
    LOG_I("NexusVM Engine initialized successfully");
    return 0;
}

int NexusEngine::initModules() {
    LOG_I("Initializing engine modules...");
    
    // Create modules in dependency order
    m_container_loader    = std::make_unique<ContainerLoader>(*this);
    m_fake_root           = std::make_unique<FakeRoot>(*this);
    m_syscall_interceptor = std::make_unique<SyscallInterceptor>(*this);
    m_property_spoof      = std::make_unique<PropertySpoof>(*this);
    m_process_manager     = std::make_unique<ProcessManager>(*this);
    m_virtual_binder      = std::make_unique<VirtualBinder>(*this);
    m_egl_proxy           = std::make_unique<EGLProxy>(*this);
    m_input_bridge        = std::make_unique<InputBridge>(*this);
    
    // Initialize each module
    int ret;
    
    ret = m_container_loader->init();
    if (ret != 0) { LOG_E("ContainerLoader init failed: %d", ret); return ret; }
    
    ret = m_fake_root->init();
    if (ret != 0) { LOG_E("FakeRoot init failed: %d", ret); return ret; }
    
    ret = m_syscall_interceptor->init();
    if (ret != 0) { LOG_E("SyscallInterceptor init failed: %d", ret); return ret; }
    
    ret = m_property_spoof->init();
    if (ret != 0) { LOG_E("PropertySpoof init failed: %d", ret); return ret; }
    
    ret = m_process_manager->init();
    if (ret != 0) { LOG_E("ProcessManager init failed: %d", ret); return ret; }
    
    ret = m_virtual_binder->init();
    if (ret != 0) { LOG_E("VirtualBinder init failed: %d", ret); return ret; }
    
    ret = m_egl_proxy->init();
    if (ret != 0) { LOG_E("EGLProxy init failed: %d", ret); return ret; }
    
    ret = m_input_bridge->init();
    if (ret != 0) { LOG_E("InputBridge init failed: %d", ret); return ret; }
    
    LOG_I("All %d modules initialized", 8);
    return 0;
}

void NexusEngine::destroyModules() {
    LOG_I("Destroying engine modules...");
    
    // Destroy in reverse order
    if (m_input_bridge)        { m_input_bridge->shutdown();        m_input_bridge.reset(); }
    if (m_egl_proxy)           { m_egl_proxy->shutdown();           m_egl_proxy.reset(); }
    if (m_virtual_binder)      { m_virtual_binder->shutdown();      m_virtual_binder.reset(); }
    if (m_process_manager)     { m_process_manager->shutdown();     m_process_manager.reset(); }
    if (m_property_spoof)      { m_property_spoof->shutdown();      m_property_spoof.reset(); }
    if (m_syscall_interceptor) { m_syscall_interceptor->shutdown();  m_syscall_interceptor.reset(); }
    if (m_fake_root)           { m_fake_root->shutdown();           m_fake_root.reset(); }
    if (m_container_loader)    { m_container_loader->shutdown();    m_container_loader.reset(); }
    
    LOG_I("All modules destroyed");
}

int NexusEngine::loadRom(const std::string& rom_path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_state != EngineState::INITIALIZED) {
        LOG_E("Cannot load ROM in state %d", static_cast<int>(m_state.load()));
        return -1;
    }
    
    LOG_I("Loading ROM: %s", rom_path.c_str());
    m_config.rom_path = rom_path;
    
    // Use ContainerLoader to extract and prepare the rootfs
    int ret = m_container_loader->extractRom(rom_path, m_config.rootfs_path);
    if (ret != 0) {
        LOG_E("ROM extraction failed: %d", ret);
        setState(EngineState::ERROR);
        if (m_callbacks.on_error) {
            m_callbacks.on_error("Failed to extract ROM image");
        }
        return ret;
    }
    
    // Setup the container filesystem (fake /proc, /sys, /dev, etc.)
    ret = m_container_loader->setupFilesystem(m_config.rootfs_path);
    if (ret != 0) {
        LOG_E("Filesystem setup failed: %d", ret);
        setState(EngineState::ERROR);
        return ret;
    }
    
    // Apply property spoofing to build.prop
    ret = m_property_spoof->patchBuildProp(m_config.rootfs_path + "/system/build.prop");
    if (ret != 0) {
        LOG_W("build.prop patching failed (non-fatal): %d", ret);
    }
    
    setState(EngineState::ROM_LOADED);
    LOG_I("ROM loaded successfully");
    return 0;
}

int NexusEngine::start() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_state != EngineState::ROM_LOADED) {
        LOG_E("Cannot start in state %d (must be ROM_LOADED)", static_cast<int>(m_state.load()));
        return -1;
    }
    
    LOG_I("Starting guest Android OS...");
    setState(EngineState::STARTING);
    
    if (m_callbacks.on_boot_progress) {
        m_callbacks.on_boot_progress(0, "Preparing container...");
    }
    
    // Step 1: Install syscall hooks for guest processes
    int ret = m_syscall_interceptor->installHooks(m_config.rootfs_path);
    if (ret != 0) {
        LOG_E("Syscall hook installation failed: %d", ret);
        setState(EngineState::ERROR);
        return ret;
    }
    
    if (m_callbacks.on_boot_progress) {
        m_callbacks.on_boot_progress(10, "Syscall hooks installed");
    }
    
    // Step 2: Start Virtual Binder daemon
    ret = m_virtual_binder->startDaemon();
    if (ret != 0) {
        LOG_E("Virtual Binder start failed: %d", ret);
        setState(EngineState::ERROR);
        return ret;
    }
    
    if (m_callbacks.on_boot_progress) {
        m_callbacks.on_boot_progress(20, "Virtual Binder ready");
    }
    
    // Step 3: Setup EGL proxy (if GPU passthrough enabled)
    if (m_config.gpu_passthrough && m_native_window) {
        ret = m_egl_proxy->setupPassthrough(m_native_window);
        if (ret != 0) {
            LOG_W("EGL proxy setup failed (falling back to software): %d", ret);
            // Non-fatal — can fall back to software rendering
        }
    }
    
    if (m_callbacks.on_boot_progress) {
        m_callbacks.on_boot_progress(30, "GPU proxy ready");
    }
    
    // Step 4: Setup input bridge
    ret = m_input_bridge->startBridge(m_config.display_width, m_config.display_height);
    if (ret != 0) {
        LOG_W("Input bridge setup failed (non-fatal): %d", ret);
    }
    
    if (m_callbacks.on_boot_progress) {
        m_callbacks.on_boot_progress(40, "Input bridge ready");
    }
    
    // Step 5: Launch guest init process
    ret = m_process_manager->launchInit(m_config.rootfs_path);
    if (ret != 0) {
        LOG_E("Guest init launch failed: %d", ret);
        setState(EngineState::ERROR);
        return ret;
    }
    
    if (m_callbacks.on_boot_progress) {
        m_callbacks.on_boot_progress(60, "Guest init started");
    }
    
    // Process manager will update state to RUNNING once system_server is up
    LOG_I("Guest OS boot sequence initiated");
    return 0;
}

int NexusEngine::pause() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_state != EngineState::RUNNING) {
        return -1;
    }
    
    LOG_I("Pausing guest OS...");
    int ret = m_process_manager->freezeAll();
    if (ret == 0) {
        setState(EngineState::PAUSED);
    }
    return ret;
}

int NexusEngine::resume() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_state != EngineState::PAUSED) {
        return -1;
    }
    
    LOG_I("Resuming guest OS...");
    int ret = m_process_manager->unfreezeAll();
    if (ret == 0) {
        setState(EngineState::RUNNING);
    }
    return ret;
}

int NexusEngine::stop() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_state != EngineState::RUNNING && m_state != EngineState::PAUSED && 
        m_state != EngineState::STARTING) {
        return -1;
    }
    
    LOG_I("Stopping guest OS...");
    setState(EngineState::STOPPING);
    
    // Stop in reverse boot order
    m_input_bridge->stopBridge();
    m_egl_proxy->teardown();
    m_process_manager->killAll();
    m_virtual_binder->stopDaemon();
    m_syscall_interceptor->removeHooks();
    
    setState(EngineState::INITIALIZED);
    LOG_I("Guest OS stopped");
    return 0;
}

void NexusEngine::destroy() {
    if (m_state == EngineState::RUNNING || m_state == EngineState::PAUSED) {
        stop();
    }
    
    destroyModules();
    setState(EngineState::UNINITIALIZED);
    LOG_I("NexusVM Engine destroyed");
}

// ============================================================================
// Runtime API
// ============================================================================

int NexusEngine::setGuestProperty(const std::string& key, const std::string& value) {
    if (!m_property_spoof) return -1;
    return m_property_spoof->setProperty(key, value);
}

std::string NexusEngine::getGuestProperty(const std::string& key) const {
    if (!m_property_spoof) return "";
    return m_property_spoof->getProperty(key);
}

void NexusEngine::sendTouchEvent(int action, float x, float y, int pointer_id) {
    if (m_input_bridge && m_state == EngineState::RUNNING) {
        m_input_bridge->injectTouch(action, x, y, pointer_id);
    }
}

void NexusEngine::sendKeyEvent(int action, int keycode) {
    if (m_input_bridge && m_state == EngineState::RUNNING) {
        m_input_bridge->injectKey(action, keycode);
    }
}

void NexusEngine::setNativeWindow(void* native_window) {
    m_native_window = native_window;
    if (m_egl_proxy) {
        m_egl_proxy->setNativeWindow(native_window);
    }
}

void NexusEngine::setCallbacks(const EngineCallbacks& callbacks) {
    m_callbacks = callbacks;
}

} // namespace nexusvm

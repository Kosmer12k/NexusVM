/*
 * NexusVM — Module 8: Input Bridge
 * Forwards touch and key events from host SurfaceView to guest /dev/input.
 */

#pragma once

#include <cstdint>
#include <thread>
#include <atomic>
#include <mutex>

namespace nexusvm {

class NexusEngine;

class InputBridge {
public:
    explicit InputBridge(NexusEngine& engine);
    ~InputBridge();
    
    int init();
    void shutdown();
    
    /** Start the input bridge with the given display dimensions */
    int startBridge(int display_width, int display_height);
    
    /** Stop the bridge */
    int stopBridge();
    
    /** Inject a touch event from the host */
    void injectTouch(int action, float x, float y, int pointer_id);
    
    /** Inject a key event from the host */
    void injectKey(int action, int keycode);
    
    /** Set guest display dimensions (for coordinate translation) */
    void setGuestDimensions(int width, int height);

private:
    NexusEngine& m_engine;
    
    int m_host_width = 1080;
    int m_host_height = 2400;
    int m_guest_width = 1080;
    int m_guest_height = 2400;
    
    // Virtual input device (Unix socket for guest to read)
    int m_touch_fd = -1;
    int m_key_fd = -1;
    std::string m_socket_path;
    
    std::atomic<bool> m_running{false};
    mutable std::mutex m_mutex;
    
    /** Create virtual input device files */
    int createVirtualDevices();
    
    /** Translate host coordinates to guest coordinates */
    void translateCoordinates(float host_x, float host_y, int& guest_x, int& guest_y);
};

} // namespace nexusvm

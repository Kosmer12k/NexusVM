/*
 * NexusVM — Module 8: Input Bridge Implementation
 */

#include "input_bridge.h"
#include "nexus_engine.h"
#include "utils/logger.h"
#include "utils/file_utils.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <linux/input.h>
#include <unistd.h>
#include <cstring>
#include <ctime>

#define MOD_TAG "InputBridge"

namespace nexusvm {

InputBridge::InputBridge(NexusEngine& engine) : m_engine(engine) {
    MLOG_I(MOD_TAG, "Created");
}

InputBridge::~InputBridge() {
    shutdown();
}

int InputBridge::init() {
    MLOG_I(MOD_TAG, "Initialized");
    return 0;
}

void InputBridge::shutdown() {
    stopBridge();
    MLOG_I(MOD_TAG, "Shutdown");
}

int InputBridge::startBridge(int display_width, int display_height) {
    MLOG_I(MOD_TAG, "Starting input bridge (%dx%d)", display_width, display_height);
    
    m_host_width = display_width;
    m_host_height = display_height;
    m_guest_width = display_width;
    m_guest_height = display_height;
    
    // Create virtual input devices
    int ret = createVirtualDevices();
    if (ret != 0) {
        MLOG_W(MOD_TAG, "Virtual device creation failed: %d", ret);
    }
    
    m_running = true;
    MLOG_I(MOD_TAG, "Input bridge started");
    return 0;
}

int InputBridge::stopBridge() {
    if (!m_running) return 0;
    
    m_running = false;
    
    if (m_touch_fd >= 0) {
        close(m_touch_fd);
        m_touch_fd = -1;
    }
    if (m_key_fd >= 0) {
        close(m_key_fd);
        m_key_fd = -1;
    }
    
    MLOG_I(MOD_TAG, "Input bridge stopped");
    return 0;
}

int InputBridge::createVirtualDevices() {
    const auto& config = m_engine.getConfig();
    m_socket_path = config.data_path + "/nexusvm_input.sock";
    
    // Create Unix socket for input events
    // Guest processes will connect to this socket to receive input
    
    unlink(m_socket_path.c_str());
    
    m_touch_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (m_touch_fd < 0) {
        MLOG_E(MOD_TAG, "socket() failed: %s", strerror(errno));
        return -1;
    }
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, m_socket_path.c_str(), sizeof(addr.sun_path) - 1);
    
    if (bind(m_touch_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        MLOG_E(MOD_TAG, "bind() failed: %s", strerror(errno));
        close(m_touch_fd);
        m_touch_fd = -1;
        return -2;
    }
    
    MLOG_I(MOD_TAG, "Virtual input device created: %s", m_socket_path.c_str());
    return 0;
}

// ============================================================================
// Input Injection
// ============================================================================

void InputBridge::translateCoordinates(float host_x, float host_y,
                                        int& guest_x, int& guest_y) {
    // Scale from host display coordinates to guest display coordinates
    guest_x = static_cast<int>(host_x * static_cast<float>(m_guest_width) / 
                               static_cast<float>(m_host_width));
    guest_y = static_cast<int>(host_y * static_cast<float>(m_guest_height) / 
                               static_cast<float>(m_host_height));
    
    // Clamp
    if (guest_x < 0) guest_x = 0;
    if (guest_y < 0) guest_y = 0;
    if (guest_x >= m_guest_width) guest_x = m_guest_width - 1;
    if (guest_y >= m_guest_height) guest_y = m_guest_height - 1;
}

void InputBridge::injectTouch(int action, float x, float y, int pointer_id) {
    if (!m_running || m_touch_fd < 0) return;
    
    int guest_x, guest_y;
    translateCoordinates(x, y, guest_x, guest_y);
    
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    
    // Build Linux input_event structs (same format as /dev/input/eventX)
    struct input_event events[6];
    int event_count = 0;
    
    // EV_ABS: ABS_MT_SLOT
    events[event_count].time.tv_sec = ts.tv_sec;
    events[event_count].time.tv_usec = ts.tv_nsec / 1000;
    events[event_count].type = EV_ABS;
    events[event_count].code = ABS_MT_SLOT;
    events[event_count].value = pointer_id;
    event_count++;
    
    // EV_ABS: ABS_MT_TRACKING_ID
    events[event_count].time.tv_sec = ts.tv_sec;
    events[event_count].time.tv_usec = ts.tv_nsec / 1000;
    events[event_count].type = EV_ABS;
    events[event_count].code = ABS_MT_TRACKING_ID;
    events[event_count].value = (action == 1) ? -1 : pointer_id; // -1 = finger up
    event_count++;
    
    if (action != 1) { // Not finger up
        // EV_ABS: ABS_MT_POSITION_X
        events[event_count].time.tv_sec = ts.tv_sec;
        events[event_count].time.tv_usec = ts.tv_nsec / 1000;
        events[event_count].type = EV_ABS;
        events[event_count].code = ABS_MT_POSITION_X;
        events[event_count].value = guest_x;
        event_count++;
        
        // EV_ABS: ABS_MT_POSITION_Y
        events[event_count].time.tv_sec = ts.tv_sec;
        events[event_count].time.tv_usec = ts.tv_nsec / 1000;
        events[event_count].type = EV_ABS;
        events[event_count].code = ABS_MT_POSITION_Y;
        events[event_count].value = guest_y;
        event_count++;
        
        // EV_ABS: ABS_MT_PRESSURE
        events[event_count].time.tv_sec = ts.tv_sec;
        events[event_count].time.tv_usec = ts.tv_nsec / 1000;
        events[event_count].type = EV_ABS;
        events[event_count].code = ABS_MT_PRESSURE;
        events[event_count].value = 100;
        event_count++;
    }
    
    // EV_SYN: SYN_REPORT
    events[event_count].time.tv_sec = ts.tv_sec;
    events[event_count].time.tv_usec = ts.tv_nsec / 1000;
    events[event_count].type = EV_SYN;
    events[event_count].code = SYN_REPORT;
    events[event_count].value = 0;
    event_count++;
    
    // Send events over the socket
    send(m_touch_fd, events, sizeof(struct input_event) * event_count, MSG_DONTWAIT);
}

void InputBridge::injectKey(int action, int keycode) {
    if (!m_running || m_touch_fd < 0) return;
    
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    
    struct input_event events[2];
    
    // EV_KEY
    events[0].time.tv_sec = ts.tv_sec;
    events[0].time.tv_usec = ts.tv_nsec / 1000;
    events[0].type = EV_KEY;
    events[0].code = static_cast<uint16_t>(keycode);
    events[0].value = action; // 0=up, 1=down, 2=repeat
    
    // EV_SYN
    events[1].time.tv_sec = ts.tv_sec;
    events[1].time.tv_usec = ts.tv_nsec / 1000;
    events[1].type = EV_SYN;
    events[1].code = SYN_REPORT;
    events[1].value = 0;
    
    send(m_touch_fd, events, sizeof(events), MSG_DONTWAIT);
}

void InputBridge::setGuestDimensions(int width, int height) {
    m_guest_width = width;
    m_guest_height = height;
    MLOG_I(MOD_TAG, "Guest dimensions set: %dx%d", width, height);
}

} // namespace nexusvm

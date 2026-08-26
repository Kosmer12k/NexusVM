/*
 * NexusVM — Module 4: Virtual Binder Implementation
 * 
 * This is one of the most complex modules. Android's Binder IPC is the
 * backbone of all inter-process communication. Without a working Binder,
 * system_server cannot communicate with any app or system service.
 * 
 * Our approach:
 * 1. Create a Unix Domain Socket at a known path
 * 2. Guest processes connect to this socket instead of /dev/binder
 * 3. We relay Binder transactions between processes via shared memory
 * 4. We implement a virtual ServiceManager that handles service registration
 */

#include "virtual_binder.h"
#include "nexus_engine.h"
#include "utils/logger.h"
#include "utils/file_utils.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>

#define MOD_TAG "VirtualBinder"

// Binder protocol constants (from kernel binder.h)
static constexpr uint32_t BR_NOOP = 12;
static constexpr uint32_t BR_TRANSACTION_COMPLETE = 6;
static constexpr uint32_t BR_REPLY = 2;
static constexpr uint32_t BR_ERROR = 0;

static constexpr uint32_t BC_TRANSACTION = 0;
static constexpr uint32_t BC_REPLY = 1;
static constexpr uint32_t BC_REGISTER_LOOPER = 11;
static constexpr uint32_t BC_ENTER_LOOPER = 12;

// ServiceManager transaction codes
static constexpr uint32_t SVC_MGR_GET_SERVICE = 1;
static constexpr uint32_t SVC_MGR_CHECK_SERVICE = 2;
static constexpr uint32_t SVC_MGR_ADD_SERVICE = 3;
static constexpr uint32_t SVC_MGR_LIST_SERVICES = 4;

namespace nexusvm {

VirtualBinder::VirtualBinder(NexusEngine& engine) : m_engine(engine) {
    MLOG_I(MOD_TAG, "Created");
}

VirtualBinder::~VirtualBinder() {
    shutdown();
}

int VirtualBinder::init() {
    // Set socket path inside the rootfs
    const auto& config = m_engine.getConfig();
    m_socket_path = config.data_path + "/nexusvm_binder.sock";
    
    MLOG_I(MOD_TAG, "Initialized (socket: %s)", m_socket_path.c_str());
    return 0;
}

void VirtualBinder::shutdown() {
    stopDaemon();
    MLOG_I(MOD_TAG, "Shutdown");
}

// ============================================================================
// Daemon Lifecycle
// ============================================================================

int VirtualBinder::startDaemon() {
    if (m_running) {
        MLOG_W(MOD_TAG, "Daemon already running");
        return 0;
    }
    
    // Remove old socket
    unlink(m_socket_path.c_str());
    
    // Create Unix Domain Socket
    m_server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_server_fd < 0) {
        MLOG_E(MOD_TAG, "socket() failed: %s", strerror(errno));
        return -1;
    }
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, m_socket_path.c_str(), sizeof(addr.sun_path) - 1);
    
    if (bind(m_server_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        MLOG_E(MOD_TAG, "bind() failed: %s", strerror(errno));
        close(m_server_fd);
        m_server_fd = -1;
        return -2;
    }
    
    if (listen(m_server_fd, 64) < 0) {
        MLOG_E(MOD_TAG, "listen() failed: %s", strerror(errno));
        close(m_server_fd);
        m_server_fd = -1;
        return -3;
    }
    
    // Register default system services
    registerService("activity", 0, -1, 0);
    registerService("package", 0, -1, 0);
    registerService("window", 0, -1, 0);
    registerService("content", 0, -1, 0);
    registerService("alarm", 0, -1, 0);
    registerService("power", 0, -1, 0);
    registerService("display", 0, -1, 0);
    registerService("input", 0, -1, 0);
    registerService("SurfaceFlinger", 0, -1, 0);
    
    // Start daemon thread
    m_running = true;
    m_daemon_thread = std::thread(&VirtualBinder::daemonLoop, this);
    
    MLOG_I(MOD_TAG, "Virtual Binder daemon started on %s", m_socket_path.c_str());
    return 0;
}

int VirtualBinder::stopDaemon() {
    if (!m_running) return 0;
    
    m_running = false;
    
    if (m_server_fd >= 0) {
        ::shutdown(m_server_fd, SHUT_RDWR);
        close(m_server_fd);
        m_server_fd = -1;
    }
    
    if (m_daemon_thread.joinable()) {
        m_daemon_thread.join();
    }
    
    unlink(m_socket_path.c_str());
    
    MLOG_I(MOD_TAG, "Virtual Binder daemon stopped");
    return 0;
}

// ============================================================================
// Daemon Loop
// ============================================================================

void VirtualBinder::daemonLoop() {
    MLOG_I(MOD_TAG, "Daemon loop started");
    
    // Use epoll for efficient I/O multiplexing
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        MLOG_E(MOD_TAG, "epoll_create failed: %s", strerror(errno));
        return;
    }
    
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = m_server_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, m_server_fd, &ev);
    
    struct epoll_event events[64];
    
    while (m_running) {
        int nfds = epoll_wait(epoll_fd, events, 64, 1000); // 1 second timeout
        
        if (nfds < 0) {
            if (errno == EINTR) continue;
            MLOG_E(MOD_TAG, "epoll_wait error: %s", strerror(errno));
            break;
        }
        
        for (int i = 0; i < nfds; ++i) {
            if (events[i].data.fd == m_server_fd) {
                // New connection
                int client_fd = accept(m_server_fd, nullptr, nullptr);
                if (client_fd >= 0) {
                    MLOG_D(MOD_TAG, "New binder client: fd=%d", client_fd);
                    
                    // Add client to epoll
                    ev.events = EPOLLIN;
                    ev.data.fd = client_fd;
                    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev);
                }
            } else {
                // Client data ready
                handleClient(events[i].data.fd);
            }
        }
    }
    
    close(epoll_fd);
    MLOG_I(MOD_TAG, "Daemon loop ended");
}

void VirtualBinder::handleClient(int client_fd) {
    // Read the binder command from the client
    uint8_t buf[4096];
    ssize_t n = recv(client_fd, buf, sizeof(buf), 0);
    
    if (n <= 0) {
        MLOG_D(MOD_TAG, "Client fd=%d disconnected", client_fd);
        close(client_fd);
        return;
    }
    
    // Parse the command
    if (n < 4) return;
    
    uint32_t cmd;
    memcpy(&cmd, buf, sizeof(cmd));
    
    switch (cmd) {
        case BC_TRANSACTION: {
            // Parse transaction
            if (static_cast<size_t>(n) < sizeof(uint32_t) * 4) break;
            
            BinderTransaction txn;
            memcpy(&txn.target_handle, buf + 4, sizeof(uint32_t));
            memcpy(&txn.code, buf + 8, sizeof(uint32_t));
            memcpy(&txn.flags, buf + 12, sizeof(uint32_t));
            
            size_t data_offset = 16;
            if (static_cast<size_t>(n) > data_offset) {
                txn.data.assign(buf + data_offset, buf + n);
            }
            
            MLOG_D(MOD_TAG, "Transaction: handle=%u code=%u flags=0x%x data_size=%zu",
                   txn.target_handle, txn.code, txn.flags, txn.data.size());
            
            // Handle ServiceManager transactions (handle 0)
            if (txn.target_handle == 0) {
                BinderTransaction reply;
                processTransaction(txn, reply);
                
                // Send reply
                uint32_t reply_cmd = BR_REPLY;
                send(client_fd, &reply_cmd, sizeof(reply_cmd), 0);
                if (!reply.data.empty()) {
                    send(client_fd, reply.data.data(), reply.data.size(), 0);
                }
            } else {
                // Forward to target service
                uint32_t complete_cmd = BR_TRANSACTION_COMPLETE;
                send(client_fd, &complete_cmd, sizeof(complete_cmd), 0);
            }
            break;
        }
        
        case BC_REGISTER_LOOPER:
        case BC_ENTER_LOOPER: {
            uint32_t noop_cmd = BR_NOOP;
            send(client_fd, &noop_cmd, sizeof(noop_cmd), 0);
            break;
        }
        
        default:
            MLOG_D(MOD_TAG, "Unknown binder command: %u", cmd);
            break;
    }
}

// ============================================================================
// Service Management
// ============================================================================

int VirtualBinder::registerService(const std::string& name, uint32_t handle,
                                    int owner_fd, pid_t owner_pid) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    BinderService svc;
    svc.name = name;
    svc.handle = (handle == 0) ? m_next_handle++ : handle;
    svc.owner_fd = owner_fd;
    svc.owner_pid = owner_pid;
    
    m_services[name] = svc;
    MLOG_I(MOD_TAG, "Service registered: %s (handle=%u)", name.c_str(), svc.handle);
    return 0;
}

const BinderService* VirtualBinder::lookupService(const std::string& name) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_services.find(name);
    if (it != m_services.end()) {
        return &it->second;
    }
    return nullptr;
}

int VirtualBinder::processTransaction(const BinderTransaction& txn, BinderTransaction& reply) {
    // Handle ServiceManager commands
    switch (txn.code) {
        case SVC_MGR_GET_SERVICE:
        case SVC_MGR_CHECK_SERVICE: {
            // Extract service name from transaction data
            if (txn.data.size() < 4) {
                reply.data = {0, 0, 0, 0}; // NULL handle
                return 0;
            }
            
            // Service name is encoded as a Parcel string
            // For simplicity, we try to find it
            std::string svc_name(txn.data.begin(), txn.data.end());
            // Strip null bytes and Parcel overhead
            svc_name.erase(std::remove(svc_name.begin(), svc_name.end(), '\0'), svc_name.end());
            
            auto* svc = lookupService(svc_name);
            if (svc) {
                uint32_t handle = svc->handle;
                reply.data.resize(sizeof(handle));
                memcpy(reply.data.data(), &handle, sizeof(handle));
                MLOG_D(MOD_TAG, "GetService: %s -> handle %u", svc_name.c_str(), handle);
            } else {
                reply.data = {0, 0, 0, 0};
                MLOG_D(MOD_TAG, "GetService: %s -> not found", svc_name.c_str());
            }
            break;
        }
        
        case SVC_MGR_ADD_SERVICE: {
            // Register a new service
            MLOG_D(MOD_TAG, "AddService request (data_size=%zu)", txn.data.size());
            
            if (txn.data.size() < 4) return -1;
            
            std::string svc_name(txn.data.begin(), txn.data.end());
            svc_name.erase(std::remove(svc_name.begin(), svc_name.end(), '\0'), svc_name.end());
            
            registerService(svc_name, 0, -1, txn.sender_pid);
            
            // Reply success
            uint32_t status = 0;
            reply.data.resize(sizeof(status));
            memcpy(reply.data.data(), &status, sizeof(status));
            break;
        }
        
        case SVC_MGR_LIST_SERVICES: {
            std::lock_guard<std::mutex> lock(m_mutex);
            
            // Build a list of service names
            std::string list;
            for (const auto& [name, svc] : m_services) {
                list += name + "\n";
            }
            reply.data.assign(list.begin(), list.end());
            break;
        }
        
        default:
            MLOG_W(MOD_TAG, "Unknown ServiceManager transaction code: %u", txn.code);
            break;
    }
    
    return 0;
}

int VirtualBinder::processBWR(int client_fd, const uint8_t* write_buf, size_t write_size,
                               uint8_t* read_buf, size_t read_size, size_t* read_consumed) {
    // BINDER_WRITE_READ ioctl emulation
    // This processes the write buffer (commands from the client)
    // and fills the read buffer (responses to the client)
    
    (void)client_fd;
    (void)write_buf;
    (void)write_size;
    (void)read_buf;
    (void)read_size;
    
    if (read_consumed) *read_consumed = 0;
    
    // Full BWR implementation would parse each command in write_buf
    // and produce appropriate responses in read_buf.
    // This is simplified for the initial version.
    
    return 0;
}

} // namespace nexusvm

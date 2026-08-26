/*
 * NexusVM — Module 4: Virtual Binder
 * 
 * Provides a userspace Binder IPC mechanism for the guest OS.
 * Replaces /dev/binder, /dev/hwbinder, /dev/vndbinder.
 */

#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <cstdint>

namespace nexusvm {

class NexusEngine;

/** Binder transaction data */
struct BinderTransaction {
    uint32_t target_handle;
    uint32_t code;
    uint32_t flags;
    std::vector<uint8_t> data;
    std::vector<uint8_t> offsets;
    pid_t sender_pid;
    uid_t sender_uid;
};

/** Registered Binder service */
struct BinderService {
    std::string name;
    uint32_t handle;
    int owner_fd;     // Unix socket fd of the owning process
    pid_t owner_pid;
};

class VirtualBinder {
public:
    explicit VirtualBinder(NexusEngine& engine);
    ~VirtualBinder();
    
    int init();
    void shutdown();
    
    /** Start the virtual Binder daemon (listens on Unix socket) */
    int startDaemon();
    
    /** Stop the daemon */
    int stopDaemon();
    
    /** Register a service in the virtual ServiceManager */
    int registerService(const std::string& name, uint32_t handle, int owner_fd, pid_t owner_pid);
    
    /** Look up a service by name */
    const BinderService* lookupService(const std::string& name) const;
    
    /** Process a Binder transaction */
    int processTransaction(const BinderTransaction& txn, BinderTransaction& reply);
    
    /** Get the Unix socket path for guest processes to connect to */
    std::string getSocketPath() const { return m_socket_path; }

private:
    NexusEngine& m_engine;
    std::string m_socket_path;
    int m_server_fd = -1;
    std::atomic<bool> m_running{false};
    std::thread m_daemon_thread;
    mutable std::mutex m_mutex;
    
    std::unordered_map<std::string, BinderService> m_services;
    uint32_t m_next_handle = 1;
    
    /** Daemon main loop — accepts connections and processes Binder IPC */
    void daemonLoop();
    
    /** Handle a single client connection */
    void handleClient(int client_fd);
    
    /** Process BINDER_WRITE_READ ioctl emulation */
    int processBWR(int client_fd, const uint8_t* write_buf, size_t write_size,
                   uint8_t* read_buf, size_t read_size, size_t* read_consumed);
};

} // namespace nexusvm

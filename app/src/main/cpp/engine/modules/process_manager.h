/*
 * NexusVM — Module 7: Process Manager
 * Manages guest process lifecycle: init, Zygote, system_server.
 */

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <mutex>
#include <sys/types.h>

namespace nexusvm {

class NexusEngine;

/** Represents a guest process */
struct GuestProcess {
    pid_t       pid;
    pid_t       ppid;
    std::string name;
    std::string executable;
    int         status;      // 0=running, 1=stopped, -1=crashed
    uint64_t    start_time;
    bool        is_critical; // If true, crash = restart
};

class ProcessManager {
public:
    explicit ProcessManager(NexusEngine& engine);
    ~ProcessManager();
    
    int init();
    void shutdown();
    
    /** Launch the guest init process */
    int launchInit(const std::string& rootfs_path);
    
    /** Freeze all guest processes (SIGSTOP) */
    int freezeAll();
    
    /** Unfreeze all guest processes (SIGCONT) */
    int unfreezeAll();
    
    /** Kill all guest processes */
    int killAll();
    
    /** Get list of running guest processes */
    std::vector<GuestProcess> getProcessList() const;
    
    /** Check if a specific process is running */
    bool isRunning(pid_t pid) const;
    
    /** Get the init PID */
    pid_t getInitPid() const { return m_init_pid; }

private:
    NexusEngine& m_engine;
    
    pid_t m_init_pid = -1;
    std::atomic<bool> m_running{false};
    std::thread m_monitor_thread;
    mutable std::mutex m_mutex;
    
    std::unordered_map<pid_t, GuestProcess> m_processes;
    
    /** Setup environment variables for the guest init */
    std::vector<std::string> buildEnvironment(const std::string& rootfs_path);
    
    /** Process monitoring thread */
    void monitorLoop();
    
    /** Handle child process death */
    void handleChildDeath(pid_t pid, int status);
    
    /** Register a new guest process */
    void registerProcess(pid_t pid, const std::string& name, const std::string& exe, bool critical);
};

} // namespace nexusvm

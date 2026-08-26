/*
 * NexusVM — Module 7: Process Manager Implementation
 * 
 * Manages the lifecycle of guest Android processes.
 * The boot sequence is:
 *   1. Engine forks a child process
 *   2. Child becomes guest "init" (PID 1 from guest's perspective)
 *   3. Init starts Zygote (app_process64)
 *   4. Zygote forks system_server
 *   5. system_server starts all Android framework services
 *   6. Apps can then be launched
 * 
 * Key challenge: Without root, we can't use real PID namespaces.
 * We track guest processes by their host PIDs and use the
 * FakeRoot module to make them think they have appropriate UIDs.
 */

#include "process_manager.h"
#include "nexus_engine.h"
#include "utils/logger.h"
#include "utils/file_utils.h"

#include <sys/wait.h>
#include <sys/prctl.h>
#include <signal.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <ctime>

#define MOD_TAG "ProcessManager"

namespace nexusvm {

ProcessManager::ProcessManager(NexusEngine& engine) : m_engine(engine) {
    MLOG_I(MOD_TAG, "Created");
}

ProcessManager::~ProcessManager() {
    shutdown();
}

int ProcessManager::init() {
    MLOG_I(MOD_TAG, "Initialized");
    return 0;
}

void ProcessManager::shutdown() {
    killAll();
    MLOG_I(MOD_TAG, "Shutdown");
}

// ============================================================================
// Environment Setup
// ============================================================================

std::vector<std::string> ProcessManager::buildEnvironment(const std::string& rootfs_path) {
    std::vector<std::string> env;
    const auto& config = m_engine.getConfig();
    
    // Android system paths
    env.push_back("ANDROID_ROOT=" + rootfs_path + "/system");
    env.push_back("ANDROID_DATA=" + rootfs_path + "/data");
    env.push_back("ANDROID_STORAGE=" + rootfs_path + "/storage");
    env.push_back("EXTERNAL_STORAGE=" + rootfs_path + "/storage/emulated/0");
    env.push_back("TMPDIR=" + rootfs_path + "/tmp");
    env.push_back("HOME=" + rootfs_path + "/data");
    
    // PATH for binaries
    env.push_back("PATH=" + rootfs_path + "/system/bin:" + 
                  rootfs_path + "/system/xbin:/usr/bin:/bin");
    
    // Library search path
    env.push_back("LD_LIBRARY_PATH=" + rootfs_path + "/system/lib64:" +
                  rootfs_path + "/vendor/lib64:" +
                  rootfs_path + "/system/lib64/hw");
    
    // BOOTCLASSPATH for ART/Dalvik
    std::string bootcp = rootfs_path + "/system/framework/framework.jar";
    bootcp += ":" + rootfs_path + "/system/framework/ext.jar";
    bootcp += ":" + rootfs_path + "/system/framework/services.jar";
    bootcp += ":" + rootfs_path + "/system/framework/apache-xml.jar";
    bootcp += ":" + rootfs_path + "/system/framework/bouncycastle.jar";
    env.push_back("BOOTCLASSPATH=" + bootcp);
    
    // System server classpath
    env.push_back("SYSTEMSERVERCLASSPATH=" + 
                  rootfs_path + "/system/framework/services.jar:" +
                  rootfs_path + "/system/framework/ethernet-service.jar:" +
                  rootfs_path + "/system/framework/wifi-service.jar");
    
    // ART settings
    env.push_back("ANDROID_ART_ROOT=" + rootfs_path + "/apex/com.android.art");
    env.push_back("DEX2OATBOOTCLASSPATH=" + bootcp);
    
    // Display
    env.push_back("ANDROID_LCD_DENSITY=" + std::to_string(config.display_dpi));
    
    // NexusVM specific
    env.push_back("NEXUSVM_ROOTFS=" + rootfs_path);
    env.push_back("NEXUSVM_BINDER_SOCKET=" + config.data_path + "/nexusvm_binder.sock");
    
    // LD_PRELOAD for hooking
    std::string preload = rootfs_path + "/system/lib64/libnexusvm_preload.so";
    if (FileUtils::exists(preload)) {
        env.push_back("LD_PRELOAD=" + preload);
    }
    
    return env;
}

// ============================================================================
// Process Launch
// ============================================================================

int ProcessManager::launchInit(const std::string& rootfs_path) {
    MLOG_I(MOD_TAG, "Launching guest init from: %s", rootfs_path.c_str());
    
    // Verify essential binaries exist
    std::string init_bin = rootfs_path + "/system/bin/sh";
    std::string app_process = rootfs_path + "/system/bin/app_process64";
    
    bool has_init = FileUtils::exists(init_bin);
    bool has_app_process = FileUtils::exists(app_process);
    
    MLOG_I(MOD_TAG, "  sh:            %s", has_init ? "found" : "MISSING");
    MLOG_I(MOD_TAG, "  app_process64: %s", has_app_process ? "found" : "MISSING");
    
    if (!has_init) {
        MLOG_E(MOD_TAG, "Shell binary not found — rootfs may not be properly extracted");
        return -1;
    }
    
    // Build environment
    auto env_vec = buildEnvironment(rootfs_path);
    
    // Convert to char** for execve
    std::vector<const char*> envp;
    for (const auto& e : env_vec) {
        envp.push_back(e.c_str());
    }
    envp.push_back(nullptr);
    
    // Fork the init process
    pid_t pid = fork();
    
    if (pid < 0) {
        MLOG_E(MOD_TAG, "fork() failed: %s", strerror(errno));
        return -2;
    }
    
    if (pid == 0) {
        // ===== CHILD PROCESS (Guest Init) =====
        
        // Set process name
        prctl(PR_SET_NAME, "nexusvm-init", 0, 0, 0);
        
        // Set process death signal (if engine dies, init dies too)
        prctl(PR_SET_PDEATHSIG, SIGKILL);
        
        // Change working directory to rootfs
        if (chdir(rootfs_path.c_str()) != 0) {
            _exit(100);
        }
        
        // Setup environment
        for (const auto& e : env_vec) {
            putenv(const_cast<char*>(e.c_str()));
        }
        
        // If app_process exists, start Zygote directly
        // Otherwise, start a shell for debugging
        if (has_app_process) {
            // Start Zygote (app_process64)
            // This is what Android's init normally does:
            //   /system/bin/app_process64 -Xzygote /system/bin --zygote --start-system-server
            
            const char* argv[] = {
                app_process.c_str(),
                "-Xzygote",
                "/system/bin",
                "--zygote",
                "--start-system-server",
                nullptr
            };
            
            execve(app_process.c_str(), const_cast<char**>(argv), 
                   const_cast<char**>(envp.data()));
            
            // If execve failed, try shell as fallback
            MLOG_E(MOD_TAG, "execve(app_process64) failed: %s", strerror(errno));
        }
        
        // Fallback: start shell
        const char* sh_argv[] = { init_bin.c_str(), nullptr };
        execve(init_bin.c_str(), const_cast<char**>(sh_argv), 
               const_cast<char**>(envp.data()));
        
        // If we get here, execve failed
        _exit(101);
    }
    
    // ===== PARENT PROCESS (Engine) =====
    
    m_init_pid = pid;
    registerProcess(pid, "init", init_bin, true);
    
    MLOG_I(MOD_TAG, "Guest init launched: PID=%d", pid);
    
    // Start the process monitor thread
    m_running = true;
    m_monitor_thread = std::thread(&ProcessManager::monitorLoop, this);
    
    return 0;
}

// ============================================================================
// Process Control
// ============================================================================

int ProcessManager::freezeAll() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    int frozen = 0;
    for (auto& [pid, proc] : m_processes) {
        if (proc.status == 0) {
            if (kill(pid, SIGSTOP) == 0) {
                proc.status = 1;
                frozen++;
            }
        }
    }
    
    MLOG_I(MOD_TAG, "Frozen %d processes", frozen);
    return 0;
}

int ProcessManager::unfreezeAll() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    int resumed = 0;
    for (auto& [pid, proc] : m_processes) {
        if (proc.status == 1) {
            if (kill(pid, SIGCONT) == 0) {
                proc.status = 0;
                resumed++;
            }
        }
    }
    
    MLOG_I(MOD_TAG, "Resumed %d processes", resumed);
    return 0;
}

int ProcessManager::killAll() {
    MLOG_I(MOD_TAG, "Killing all guest processes");
    
    m_running = false;
    
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        // First SIGTERM for graceful shutdown
        for (auto& [pid, proc] : m_processes) {
            kill(pid, SIGTERM);
        }
    }
    
    // Wait briefly
    usleep(500000); // 500ms
    
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        // Then SIGKILL for anything still running
        for (auto& [pid, proc] : m_processes) {
            kill(pid, SIGKILL);
            waitpid(pid, nullptr, WNOHANG);
        }
        
        m_processes.clear();
    }
    
    if (m_monitor_thread.joinable()) {
        m_monitor_thread.join();
    }
    
    m_init_pid = -1;
    MLOG_I(MOD_TAG, "All guest processes killed");
    return 0;
}

// ============================================================================
// Process Monitoring
// ============================================================================

void ProcessManager::monitorLoop() {
    MLOG_I(MOD_TAG, "Process monitor started");
    
    while (m_running) {
        // Wait for any child to change state
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);
        
        if (pid > 0) {
            handleChildDeath(pid, status);
        } else if (pid < 0 && errno != ECHILD) {
            MLOG_E(MOD_TAG, "waitpid error: %s", strerror(errno));
        }
        
        // Sleep briefly to avoid busy-waiting
        usleep(100000); // 100ms
    }
    
    MLOG_I(MOD_TAG, "Process monitor stopped");
}

void ProcessManager::handleChildDeath(pid_t pid, int status) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_processes.find(pid);
    if (it == m_processes.end()) return;
    
    GuestProcess& proc = it->second;
    
    if (WIFEXITED(status)) {
        MLOG_I(MOD_TAG, "Process %s (PID %d) exited with code %d",
               proc.name.c_str(), pid, WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        MLOG_W(MOD_TAG, "Process %s (PID %d) killed by signal %d",
               proc.name.c_str(), pid, WTERMSIG(status));
    }
    
    proc.status = -1;
    
    // If it's a critical process (init, zygote), notify engine
    if (proc.is_critical) {
        MLOG_E(MOD_TAG, "Critical process %s died!", proc.name.c_str());
        // Could attempt restart here
    }
    
    m_processes.erase(it);
}

// ============================================================================
// Process Registry
// ============================================================================

void ProcessManager::registerProcess(pid_t pid, const std::string& name,
                                      const std::string& exe, bool critical) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    GuestProcess proc;
    proc.pid = pid;
    proc.ppid = getpid();
    proc.name = name;
    proc.executable = exe;
    proc.status = 0;
    proc.is_critical = critical;
    
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    proc.start_time = static_cast<uint64_t>(ts.tv_sec);
    
    m_processes[pid] = proc;
    MLOG_D(MOD_TAG, "Registered process: %s (PID %d, critical=%d)", 
           name.c_str(), pid, critical);
}

std::vector<GuestProcess> ProcessManager::getProcessList() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::vector<GuestProcess> list;
    list.reserve(m_processes.size());
    for (const auto& [pid, proc] : m_processes) {
        list.push_back(proc);
    }
    return list;
}

bool ProcessManager::isRunning(pid_t pid) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_processes.find(pid);
    return it != m_processes.end() && it->second.status == 0;
}

} // namespace nexusvm

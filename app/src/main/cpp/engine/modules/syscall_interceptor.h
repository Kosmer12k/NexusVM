/*
 * NexusVM — Module 2: Syscall Interceptor
 * 
 * Intercepts and redirects system calls from guest processes.
 * Uses seccomp-bpf for trapping and PLT hooking for redirection.
 */

#pragma once

#include <string>
#include <unordered_map>
#include <functional>
#include <cstdint>

namespace nexusvm {

class NexusEngine;

class SyscallInterceptor {
public:
    explicit SyscallInterceptor(NexusEngine& engine);
    ~SyscallInterceptor();
    
    int init();
    void shutdown();
    
    /**
     * Install syscall hooks into guest rootfs binaries
     * Prepares LD_PRELOAD libraries that will be loaded into guest processes
     */
    int installHooks(const std::string& rootfs_path);
    
    /**
     * Remove installed hooks
     */
    int removeHooks();
    
    /**
     * Install seccomp-bpf filter for the current process
     * Traps specified syscalls and redirects them to our handler
     */
    int installSeccompFilter();
    
    /**
     * Register a custom handler for a specific syscall number
     */
    using SyscallHandler = std::function<long(long a0, long a1, long a2, long a3, long a4, long a5)>;
    void registerHandler(int syscall_nr, SyscallHandler handler);
    
    /**
     * Setup file path redirection
     * All file access from guest will be redirected: /system/... -> rootfs/system/...
     */
    int setupPathRedirection(const std::string& rootfs_path);

private:
    NexusEngine& m_engine;
    std::string  m_rootfs_path;
    std::unordered_map<int, SyscallHandler> m_handlers;
    bool m_hooks_installed = false;
    
    /** Create the LD_PRELOAD library for guest injection */
    int createPreloadLibrary(const std::string& output_path);
    
    /** Setup environment variables for path redirection */
    int setupEnvironment(const std::string& rootfs_path);
};

} // namespace nexusvm

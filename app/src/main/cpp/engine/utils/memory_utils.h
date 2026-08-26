/*
 * NexusVM — Memory Utilities
 * 
 * Provides memory mapping, protection, and manipulation functions
 * used by the hooking framework and syscall interceptor.
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace nexusvm {

/**
 * Represents a mapped memory region (parsed from /proc/self/maps)
 */
struct MemoryRegion {
    uintptr_t   start;
    uintptr_t   end;
    int         perms;       // PROT_READ | PROT_WRITE | PROT_EXEC
    size_t      offset;
    std::string pathname;
    
    size_t size() const { return end - start; }
    bool isReadable() const;
    bool isWritable() const;
    bool isExecutable() const;
};

class MemoryUtils {
public:
    /**
     * Parse /proc/<pid>/maps and return all memory regions
     */
    static std::vector<MemoryRegion> getMemoryMaps(pid_t pid = -1);
    
    /**
     * Find memory regions for a specific library
     */
    static std::vector<MemoryRegion> findLibraryRegions(const std::string& lib_name, pid_t pid = -1);
    
    /**
     * Get the base address of a loaded library
     */
    static uintptr_t getLibraryBase(const std::string& lib_name, pid_t pid = -1);
    
    /**
     * Make a memory region writable (for patching)
     * @return Original protection flags (for restoration)
     */
    static int makeWritable(void* addr, size_t size);
    
    /**
     * Restore memory protection
     */
    static int restoreProtection(void* addr, size_t size, int original_prot);
    
    /**
     * Safely read memory (checks if readable first)
     */
    static bool safeRead(const void* addr, void* buffer, size_t size);
    
    /**
     * Safely write memory (temporarily makes writable)
     */
    static bool safeWrite(void* addr, const void* data, size_t size);
    
    /**
     * Allocate executable memory (for hook trampolines)
     */
    static void* allocateExecutable(size_t size);
    
    /**
     * Free executable memory
     */
    static void freeExecutable(void* addr, size_t size);
    
    /**
     * Flush instruction cache (required after code patching on ARM64)
     */
    static void flushInstructionCache(void* addr, size_t size);
    
    /**
     * Get the page-aligned address for a given address
     */
    static uintptr_t pageAlign(uintptr_t addr);
    
    /**
     * Get the system page size
     */
    static size_t getPageSize();
};

} // namespace nexusvm

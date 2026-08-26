/*
 * NexusVM — Memory Utilities Implementation
 */

#include "memory_utils.h"
#include "logger.h"

#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <cstring>
#include <cerrno>

namespace nexusvm {

// ============================================================================
// MemoryRegion
// ============================================================================

bool MemoryRegion::isReadable() const   { return (perms & PROT_READ) != 0; }
bool MemoryRegion::isWritable() const   { return (perms & PROT_WRITE) != 0; }
bool MemoryRegion::isExecutable() const { return (perms & PROT_EXEC) != 0; }

// ============================================================================
// Memory Maps Parsing
// ============================================================================

std::vector<MemoryRegion> MemoryUtils::getMemoryMaps(pid_t pid) {
    std::vector<MemoryRegion> regions;
    
    char maps_path[64];
    if (pid <= 0) {
        snprintf(maps_path, sizeof(maps_path), "/proc/self/maps");
    } else {
        snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", pid);
    }
    
    FILE* fp = fopen(maps_path, "r");
    if (!fp) {
        LOG_E("Failed to open %s: %s", maps_path, strerror(errno));
        return regions;
    }
    
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        MemoryRegion region{};
        char perms_str[5] = {};
        char pathname[256] = {};
        unsigned long start, end;
        unsigned long offset;
        int dev_major, dev_minor;
        unsigned long inode;
        
        int scanned = sscanf(line, "%lx-%lx %4s %lx %x:%x %lu %255s",
                             &start, &end, perms_str, &offset,
                             &dev_major, &dev_minor, &inode, pathname);
        
        if (scanned < 7) continue;
        
        region.start = static_cast<uintptr_t>(start);
        region.end   = static_cast<uintptr_t>(end);
        region.offset = static_cast<size_t>(offset);
        
        region.perms = 0;
        if (perms_str[0] == 'r') region.perms |= PROT_READ;
        if (perms_str[1] == 'w') region.perms |= PROT_WRITE;
        if (perms_str[2] == 'x') region.perms |= PROT_EXEC;
        
        if (scanned >= 8) {
            region.pathname = pathname;
        }
        
        regions.push_back(region);
    }
    
    fclose(fp);
    return regions;
}

std::vector<MemoryRegion> MemoryUtils::findLibraryRegions(const std::string& lib_name, pid_t pid) {
    std::vector<MemoryRegion> result;
    auto all_regions = getMemoryMaps(pid);
    
    for (const auto& region : all_regions) {
        if (region.pathname.find(lib_name) != std::string::npos) {
            result.push_back(region);
        }
    }
    
    return result;
}

uintptr_t MemoryUtils::getLibraryBase(const std::string& lib_name, pid_t pid) {
    auto regions = findLibraryRegions(lib_name, pid);
    if (regions.empty()) {
        return 0;
    }
    
    // Return the lowest address (base of the first mapping)
    uintptr_t base = regions[0].start;
    for (const auto& r : regions) {
        if (r.start < base) base = r.start;
    }
    return base;
}

// ============================================================================
// Memory Protection
// ============================================================================

int MemoryUtils::makeWritable(void* addr, size_t size) {
    uintptr_t page_addr = pageAlign(reinterpret_cast<uintptr_t>(addr));
    size_t page_size = getPageSize();
    size_t aligned_size = (reinterpret_cast<uintptr_t>(addr) + size - page_addr + page_size - 1) & ~(page_size - 1);
    
    // First, find current protection
    auto regions = getMemoryMaps();
    int old_prot = PROT_READ;
    for (const auto& r : regions) {
        if (r.start <= reinterpret_cast<uintptr_t>(addr) && 
            reinterpret_cast<uintptr_t>(addr) < r.end) {
            old_prot = r.perms;
            break;
        }
    }
    
    if (mprotect(reinterpret_cast<void*>(page_addr), aligned_size, 
                 PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        LOG_E("mprotect failed for %p size %zu: %s", addr, size, strerror(errno));
        return -1;
    }
    
    return old_prot;
}

int MemoryUtils::restoreProtection(void* addr, size_t size, int original_prot) {
    uintptr_t page_addr = pageAlign(reinterpret_cast<uintptr_t>(addr));
    size_t page_size = getPageSize();
    size_t aligned_size = (reinterpret_cast<uintptr_t>(addr) + size - page_addr + page_size - 1) & ~(page_size - 1);
    
    return mprotect(reinterpret_cast<void*>(page_addr), aligned_size, original_prot);
}

// ============================================================================
// Safe Memory Operations
// ============================================================================

bool MemoryUtils::safeRead(const void* addr, void* buffer, size_t size) {
    // Use process_vm_readv for safe reading (won't segfault)
    // Fallback: just check if address is in a readable region
    auto regions = getMemoryMaps();
    for (const auto& r : regions) {
        if (r.start <= reinterpret_cast<uintptr_t>(addr) &&
            reinterpret_cast<uintptr_t>(addr) + size <= r.end &&
            r.isReadable()) {
            memcpy(buffer, addr, size);
            return true;
        }
    }
    return false;
}

bool MemoryUtils::safeWrite(void* addr, const void* data, size_t size) {
    int old_prot = makeWritable(addr, size);
    if (old_prot < 0) return false;
    
    memcpy(addr, data, size);
    
    // Flush instruction cache if the region was executable
    if (old_prot & PROT_EXEC) {
        flushInstructionCache(addr, size);
    }
    
    restoreProtection(addr, size, old_prot);
    return true;
}

// ============================================================================
// Executable Memory Allocation
// ============================================================================

void* MemoryUtils::allocateExecutable(size_t size) {
    size_t aligned_size = (size + getPageSize() - 1) & ~(getPageSize() - 1);
    
    void* mem = mmap(nullptr, aligned_size,
                     PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    
    if (mem == MAP_FAILED) {
        LOG_E("Failed to allocate executable memory: %s", strerror(errno));
        return nullptr;
    }
    
    return mem;
}

void MemoryUtils::freeExecutable(void* addr, size_t size) {
    if (addr) {
        size_t aligned_size = (size + getPageSize() - 1) & ~(getPageSize() - 1);
        munmap(addr, aligned_size);
    }
}

// ============================================================================
// Cache & Page Utilities
// ============================================================================

void MemoryUtils::flushInstructionCache(void* addr, size_t size) {
    // ARM64: use __builtin___clear_cache
    __builtin___clear_cache(
        static_cast<char*>(addr),
        static_cast<char*>(addr) + size
    );
}

uintptr_t MemoryUtils::pageAlign(uintptr_t addr) {
    size_t page_size = getPageSize();
    return addr & ~(page_size - 1);
}

size_t MemoryUtils::getPageSize() {
    static size_t page_size = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    return page_size;
}

} // namespace nexusvm

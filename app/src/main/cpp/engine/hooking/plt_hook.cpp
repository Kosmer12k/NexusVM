/*
 * NexusVM — PLT Hook Implementation
 */

#include "plt_hook.h"
#include "utils/logger.h"
#include "utils/memory_utils.h"
#include "utils/elf_parser.h"

#include <dlfcn.h>
#include <link.h>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

namespace nexusvm {

PltHook::~PltHook() {
    unhookAll();
}

// ============================================================================
// GOT Entry Discovery
// ============================================================================

std::vector<uintptr_t> PltHook::findGotEntries(const std::string& library,
                                                 const std::string& symbol) {
    std::vector<uintptr_t> entries;
    
    // Get the base address of the target library
    uintptr_t lib_base = MemoryUtils::getLibraryBase(library);
    if (lib_base == 0) {
        LOG_E("Library not found in memory: %s", library.c_str());
        return entries;
    }
    
    // Parse the ELF at the base address
    ElfParser parser;
    // Use memory regions to determine size
    auto regions = MemoryUtils::findLibraryRegions(library);
    if (regions.empty()) {
        LOG_E("No memory regions found for: %s", library.c_str());
        return entries;
    }
    
    // Find the total mapped size
    uintptr_t max_end = 0;
    for (const auto& r : regions) {
        if (r.end > max_end) max_end = r.end;
    }
    size_t total_size = max_end - lib_base;
    
    int ret = parser.parse(lib_base, total_size);
    if (ret != 0) {
        LOG_E("Failed to parse ELF for %s: %d", library.c_str(), ret);
        return entries;
    }
    
    // Search PLT relocations (R_AARCH64_JUMP_SLOT)
    for (const auto& rel : parser.getPltRelocations()) {
        if (rel.sym_name == symbol) {
            entries.push_back(lib_base + rel.offset);
        }
    }
    
    // Also search GOT relocations (R_AARCH64_GLOB_DAT)
    for (const auto& rel : parser.getGotRelocations()) {
        if (rel.sym_name == symbol) {
            entries.push_back(lib_base + rel.offset);
        }
    }
    
    return entries;
}

// ============================================================================
// GOT Entry Overwrite
// ============================================================================

int PltHook::overwriteGotEntry(uintptr_t got_addr, uintptr_t new_func, uintptr_t* old_func) {
    // Save original value
    if (!MemoryUtils::safeRead(reinterpret_cast<const void*>(got_addr), old_func, sizeof(uintptr_t))) {
        LOG_E("Cannot read GOT entry at %p", reinterpret_cast<void*>(got_addr));
        return -1;
    }
    
    // Write new value
    if (!MemoryUtils::safeWrite(reinterpret_cast<void*>(got_addr), &new_func, sizeof(uintptr_t))) {
        LOG_E("Cannot write GOT entry at %p", reinterpret_cast<void*>(got_addr));
        return -2;
    }
    
    LOG_D("GOT overwrite: [%p] %p -> %p", 
          reinterpret_cast<void*>(got_addr),
          reinterpret_cast<void*>(*old_func),
          reinterpret_cast<void*>(new_func));
    
    return 0;
}

// ============================================================================
// Hook API
// ============================================================================

int PltHook::hookFunction(const std::string& library, const std::string& symbol,
                           void* hook_func, void** orig_func) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    LOG_I("Hooking %s:%s", library.c_str(), symbol.c_str());
    
    // Check if already hooked
    if (isHooked(library, symbol)) {
        LOG_W("Already hooked: %s:%s", library.c_str(), symbol.c_str());
        return -1;
    }
    
    // Find GOT entries
    auto got_entries = findGotEntries(library, symbol);
    if (got_entries.empty()) {
        LOG_E("No GOT entries found for %s in %s", symbol.c_str(), library.c_str());
        return -2;
    }
    
    // Hook each GOT entry
    for (uintptr_t got_addr : got_entries) {
        PltHookEntry entry;
        entry.library    = library;
        entry.symbol     = symbol;
        entry.got_addr   = got_addr;
        entry.hook_func  = reinterpret_cast<uintptr_t>(hook_func);
        entry.active     = false;
        
        uintptr_t original = 0;
        int ret = overwriteGotEntry(got_addr, reinterpret_cast<uintptr_t>(hook_func), &original);
        if (ret != 0) {
            LOG_E("Failed to overwrite GOT entry at %p", reinterpret_cast<void*>(got_addr));
            continue;
        }
        
        entry.original_func = original;
        entry.active = true;
        
        // Return the original function pointer (first one found)
        if (orig_func && *orig_func == nullptr) {
            *orig_func = reinterpret_cast<void*>(original);
        }
        
        m_hooks.push_back(entry);
    }
    
    LOG_I("Hooked %s:%s (%zu GOT entries)", library.c_str(), symbol.c_str(), got_entries.size());
    return 0;
}

int PltHook::hookFunctionGlobal(const std::string& symbol,
                                 void* hook_func, void** orig_func) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    LOG_I("Global hooking: %s", symbol.c_str());
    
    int hook_count = 0;
    
    // Iterate all loaded libraries
    auto regions = MemoryUtils::getMemoryMaps();
    
    // Collect unique library paths
    std::vector<std::string> libraries;
    std::string last_path;
    for (const auto& r : regions) {
        if (!r.pathname.empty() && r.pathname[0] == '/' && 
            r.pathname != last_path &&
            r.pathname.find(".so") != std::string::npos) {
            libraries.push_back(r.pathname);
            last_path = r.pathname;
        }
    }
    
    // Hook in each library
    for (const auto& lib : libraries) {
        // Extract basename for matching
        std::string basename = lib;
        auto pos = basename.rfind('/');
        if (pos != std::string::npos) {
            basename = basename.substr(pos + 1);
        }
        
        auto got_entries = findGotEntries(basename, symbol);
        for (uintptr_t got_addr : got_entries) {
            PltHookEntry entry;
            entry.library    = basename;
            entry.symbol     = symbol;
            entry.got_addr   = got_addr;
            entry.hook_func  = reinterpret_cast<uintptr_t>(hook_func);
            
            uintptr_t original = 0;
            int ret = overwriteGotEntry(got_addr, reinterpret_cast<uintptr_t>(hook_func), &original);
            if (ret == 0) {
                entry.original_func = original;
                entry.active = true;
                
                if (orig_func && *orig_func == nullptr) {
                    *orig_func = reinterpret_cast<void*>(original);
                }
                
                m_hooks.push_back(entry);
                hook_count++;
            }
        }
    }
    
    LOG_I("Global hook %s: %d GOT entries patched", symbol.c_str(), hook_count);
    return hook_count;
}

int PltHook::unhookFunction(const std::string& library, const std::string& symbol) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    int restored = 0;
    for (auto& entry : m_hooks) {
        if (entry.library == library && entry.symbol == symbol && entry.active) {
            uintptr_t dummy;
            int ret = overwriteGotEntry(entry.got_addr, entry.original_func, &dummy);
            if (ret == 0) {
                entry.active = false;
                restored++;
            }
        }
    }
    
    LOG_I("Unhooked %s:%s (%d entries restored)", library.c_str(), symbol.c_str(), restored);
    return restored > 0 ? 0 : -1;
}

void PltHook::unhookAll() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    for (auto& entry : m_hooks) {
        if (entry.active) {
            uintptr_t dummy;
            overwriteGotEntry(entry.got_addr, entry.original_func, &dummy);
            entry.active = false;
        }
    }
    
    LOG_I("All %zu hooks removed", m_hooks.size());
    m_hooks.clear();
}

bool PltHook::isHooked(const std::string& library, const std::string& symbol) const {
    for (const auto& entry : m_hooks) {
        if (entry.library == library && entry.symbol == symbol && entry.active) {
            return true;
        }
    }
    return false;
}

} // namespace nexusvm

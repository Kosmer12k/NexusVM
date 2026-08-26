/*
 * NexusVM — PLT Hook (GOT Overwrite)
 * 
 * Hooks function calls by overwriting GOT (Global Offset Table) entries.
 * This is the primary hooking mechanism — it's fast, reliable, and doesn't
 * modify executable code (no instruction cache issues).
 * 
 * How it works:
 * 1. Parse ELF to find the GOT entry for the target function
 * 2. Save the original pointer
 * 3. Overwrite the GOT entry with our hook function pointer
 * 4. When the target function is called, it jumps to our hook instead
 * 5. Our hook can call the original function via the saved pointer
 */

#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <mutex>

namespace nexusvm {

/**
 * Represents a single PLT hook
 */
struct PltHookEntry {
    std::string library;       // Library name (e.g., "libc.so")
    std::string symbol;        // Symbol name (e.g., "getuid")
    uintptr_t   got_addr;      // Address of the GOT entry
    uintptr_t   original_func; // Original function pointer (before hook)
    uintptr_t   hook_func;     // Our hook function pointer
    bool        active;        // Whether the hook is currently installed
};

/**
 * PLT Hooking Engine
 * 
 * Usage:
 *   PltHook hook;
 *   hook.hookFunction("libc.so", "getuid", (void*)my_getuid, (void**)&orig_getuid);
 */
class PltHook {
public:
    PltHook() = default;
    ~PltHook();
    
    /**
     * Hook a function in a loaded library by overwriting its GOT entry
     * 
     * @param library    Name of the library to hook in (e.g. "libc.so")
     * @param symbol     Symbol name to hook (e.g. "getuid")
     * @param hook_func  Pointer to the hook function
     * @param orig_func  [out] Receives pointer to the original function
     * @return 0 on success
     */
    int hookFunction(const std::string& library, const std::string& symbol,
                     void* hook_func, void** orig_func);
    
    /**
     * Hook a function in ALL loaded libraries that import it
     * Useful for hooking libc functions that are used everywhere
     * 
     * @param symbol     Symbol name to hook
     * @param hook_func  Pointer to the hook function
     * @param orig_func  [out] Receives pointer to the original function
     * @return Number of GOT entries hooked
     */
    int hookFunctionGlobal(const std::string& symbol,
                           void* hook_func, void** orig_func);
    
    /**
     * Unhook a previously hooked function (restore original GOT entry)
     */
    int unhookFunction(const std::string& library, const std::string& symbol);
    
    /**
     * Unhook all hooks
     */
    void unhookAll();
    
    /**
     * Get list of all active hooks
     */
    const std::vector<PltHookEntry>& getHooks() const { return m_hooks; }
    
    /**
     * Check if a function is currently hooked
     */
    bool isHooked(const std::string& library, const std::string& symbol) const;

private:
    /**
     * Find GOT entries for a symbol in a specific library
     * Returns list of GOT addresses
     */
    std::vector<uintptr_t> findGotEntries(const std::string& library, 
                                           const std::string& symbol);
    
    /**
     * Overwrite a GOT entry with a new address
     */
    int overwriteGotEntry(uintptr_t got_addr, uintptr_t new_func, uintptr_t* old_func);
    
    std::vector<PltHookEntry>  m_hooks;
    mutable std::mutex         m_mutex;
};

} // namespace nexusvm

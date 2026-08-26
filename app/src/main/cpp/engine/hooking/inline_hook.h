/*
 * NexusVM — Inline Hook (ARM64)
 * 
 * Hooks functions by patching their first instructions with a branch
 * to our hook function. A trampoline is created to execute the original
 * instructions and jump back.
 * 
 * ARM64 inline hook layout:
 * 
 * [Original Function]          [Trampoline]               [Hook Function]
 * ┌─────────────────┐         ┌──────────────────┐       ┌──────────────┐
 * │ LDR X16, #8     │ ─jump─→│ (saved original  │       │ (our hook    │
 * │ BR  X16         │         │  instructions)   │       │  code runs)  │
 * │ <hook_func_addr>│         │ LDR X16, #8      │       │              │
 * │ ...             │         │ BR  X16           │       │ call orig()  │──→ trampoline
 * └─────────────────┘         │ <continue_addr>  │       └──────────────┘
 *                             └──────────────────┘
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <mutex>

namespace nexusvm {

/**
 * Represents an inline hook with its trampoline
 */
struct InlineHookEntry {
    uintptr_t target_func;     // Function being hooked
    uintptr_t hook_func;       // Our hook function
    uintptr_t trampoline;      // Trampoline (calls original + jumps back)
    size_t    trampoline_size;  // Size of trampoline allocation
    uint8_t   saved_bytes[16]; // Original bytes overwritten
    size_t    patch_size;      // Number of bytes we patched (12 for ARM64 absolute branch)
    bool      active;
};

/**
 * ARM64 Inline Hooking Engine
 */
class InlineHook {
public:
    InlineHook() = default;
    ~InlineHook();
    
    /**
     * Hook a function by patching its prologue
     * 
     * @param target_func  Address of the function to hook
     * @param hook_func    Address of our hook function
     * @param orig_func    [out] Receives trampoline address (call this for original behavior)
     * @return 0 on success
     */
    int hook(void* target_func, void* hook_func, void** orig_func);
    
    /**
     * Remove a hook and restore original instructions
     */
    int unhook(void* target_func);
    
    /**
     * Remove all hooks
     */
    void unhookAll();

private:
    /**
     * Build the trampoline:
     * 1. Execute the overwritten instructions
     * 2. Jump back to target_func + patch_size
     */
    uintptr_t buildTrampoline(uintptr_t target, size_t patch_size);
    
    /**
     * Generate ARM64 absolute branch (LDR X16, #8; BR X16; .quad addr)
     * Total: 12 bytes
     */
    static void generateAbsoluteBranch(uint8_t* buf, uintptr_t dest);
    
    /**
     * Check if an ARM64 instruction is PC-relative (needs fixup in trampoline)
     */
    static bool isPcRelativeInstruction(uint32_t instruction);
    
    /**
     * Fixup a PC-relative instruction for a new location
     */
    static uint32_t fixupPcRelative(uint32_t instruction, uintptr_t old_pc, uintptr_t new_pc);
    
    std::vector<InlineHookEntry>  m_hooks;
    mutable std::mutex            m_mutex;
};

} // namespace nexusvm

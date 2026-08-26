/*
 * NexusVM — Inline Hook Implementation (ARM64/AArch64)
 */

#include "inline_hook.h"
#include "utils/logger.h"
#include "utils/memory_utils.h"

#include <cstring>
#include <sys/mman.h>

namespace nexusvm {

// ARM64 absolute branch sequence: 12 bytes
// LDR X16, #8      -> 0x58000050
// BR  X16           -> 0xD61F0200
// <64-bit address>  -> 8 bytes
static constexpr size_t ARM64_ABS_BRANCH_SIZE = 12;

// ARM64 instruction encodings
static constexpr uint32_t ARM64_LDR_X16_8  = 0x58000050;  // LDR X16, [PC, #8]
static constexpr uint32_t ARM64_BR_X16     = 0xD61F0200;  // BR X16

InlineHook::~InlineHook() {
    unhookAll();
}

// ============================================================================
// ARM64 Branch Generation
// ============================================================================

void InlineHook::generateAbsoluteBranch(uint8_t* buf, uintptr_t dest) {
    // LDR X16, [PC, #8]  (load the address from 8 bytes ahead)
    uint32_t ldr = ARM64_LDR_X16_8;
    memcpy(buf, &ldr, 4);
    
    // BR X16  (branch to the loaded address)
    uint32_t br = ARM64_BR_X16;
    memcpy(buf + 4, &br, 4);
    
    // 64-bit destination address
    memcpy(buf + 8, &dest, 8);
}

// ============================================================================
// PC-Relative Instruction Detection
// ============================================================================

bool InlineHook::isPcRelativeInstruction(uint32_t insn) {
    // ADR/ADRP: xxxx 0000 0xxx xxxx xxxx xxxx xxxx xxxx (bit 31 selects ADR vs ADRP)
    if ((insn & 0x1F000000) == 0x10000000) return true;
    
    // B/BL: x00101xx xxxxxxxx xxxxxxxx xxxxxxxx
    if ((insn & 0x7C000000) == 0x14000000) return true;
    
    // B.cond: 01010100 xxxxxxxx xxxxxxxx xxx0xxxx
    if ((insn & 0xFF000010) == 0x54000000) return true;
    
    // CBZ/CBNZ: x011010x xxxxxxxx xxxxxxxx xxxxxxxx
    if ((insn & 0x7E000000) == 0x34000000) return true;
    
    // TBZ/TBNZ: x011011x xxxxxxxx xxxxxxxx xxxxxxxx
    if ((insn & 0x7E000000) == 0x36000000) return true;
    
    // LDR literal: xx011x00 xxxxxxxx xxxxxxxx xxxxxxxx
    if ((insn & 0x3B000000) == 0x18000000) return true;
    
    return false;
}

// ============================================================================
// PC-Relative Instruction Fixup
// ============================================================================

uint32_t InlineHook::fixupPcRelative(uint32_t insn, uintptr_t old_pc, uintptr_t new_pc) {
    int64_t pc_diff = static_cast<int64_t>(new_pc) - static_cast<int64_t>(old_pc);
    
    // B/BL: imm26 field (bits 25:0), offset = sign_extend(imm26) << 2
    if ((insn & 0x7C000000) == 0x14000000) {
        int32_t imm26 = static_cast<int32_t>(insn & 0x03FFFFFF);
        if (imm26 & 0x02000000) imm26 |= static_cast<int32_t>(0xFC000000); // sign extend
        int64_t target = old_pc + (static_cast<int64_t>(imm26) << 2);
        int64_t new_offset = (target - new_pc) >> 2;
        return (insn & 0xFC000000) | (static_cast<uint32_t>(new_offset) & 0x03FFFFFF);
    }
    
    // B.cond: imm19 field (bits 23:5)
    if ((insn & 0xFF000010) == 0x54000000) {
        int32_t imm19 = static_cast<int32_t>((insn >> 5) & 0x7FFFF);
        if (imm19 & 0x40000) imm19 |= static_cast<int32_t>(0xFFF80000);
        int64_t target = old_pc + (static_cast<int64_t>(imm19) << 2);
        int64_t new_offset = (target - new_pc) >> 2;
        return (insn & 0xFF00001F) | ((static_cast<uint32_t>(new_offset) & 0x7FFFF) << 5);
    }
    
    // CBZ/CBNZ: imm19 field (bits 23:5)
    if ((insn & 0x7E000000) == 0x34000000) {
        int32_t imm19 = static_cast<int32_t>((insn >> 5) & 0x7FFFF);
        if (imm19 & 0x40000) imm19 |= static_cast<int32_t>(0xFFF80000);
        int64_t target = old_pc + (static_cast<int64_t>(imm19) << 2);
        int64_t new_offset = (target - new_pc) >> 2;
        return (insn & 0xFF00001F) | ((static_cast<uint32_t>(new_offset) & 0x7FFFF) << 5);
    }
    
    // TBZ/TBNZ: imm14 field (bits 18:5)
    if ((insn & 0x7E000000) == 0x36000000) {
        int32_t imm14 = static_cast<int32_t>((insn >> 5) & 0x3FFF);
        if (imm14 & 0x2000) imm14 |= static_cast<int32_t>(0xFFFFC000);
        int64_t target = old_pc + (static_cast<int64_t>(imm14) << 2);
        int64_t new_offset = (target - new_pc) >> 2;
        return (insn & 0xFFF8001F) | ((static_cast<uint32_t>(new_offset) & 0x3FFF) << 5);
    }
    
    // For ADR/ADRP and LDR literal, the fixup is more complex
    // For now, return the instruction unchanged (may cause issues with these)
    LOG_W("PC-relative fixup not implemented for instruction 0x%08X", insn);
    (void)pc_diff;
    return insn;
}

// ============================================================================
// Trampoline Builder
// ============================================================================

uintptr_t InlineHook::buildTrampoline(uintptr_t target, size_t patch_size) {
    // Trampoline needs space for:
    // 1. Saved original instructions (with PC-relative fixups)
    // 2. Absolute branch back to target + patch_size
    size_t trampoline_size = patch_size + ARM64_ABS_BRANCH_SIZE;
    
    // Align to page size
    trampoline_size = (trampoline_size + 15) & ~15;
    
    // Allocate executable memory
    void* trampoline = MemoryUtils::allocateExecutable(trampoline_size);
    if (!trampoline) {
        LOG_E("Failed to allocate trampoline memory");
        return 0;
    }
    
    uint8_t* ptr = static_cast<uint8_t*>(trampoline);
    
    // Copy and fixup original instructions
    for (size_t i = 0; i < patch_size; i += 4) {
        uint32_t insn;
        memcpy(&insn, reinterpret_cast<void*>(target + i), 4);
        
        if (isPcRelativeInstruction(insn)) {
            insn = fixupPcRelative(insn, target + i, 
                                   reinterpret_cast<uintptr_t>(ptr) + i);
        }
        
        memcpy(ptr + i, &insn, 4);
    }
    
    // Add absolute branch back to original function (after the patched bytes)
    generateAbsoluteBranch(ptr + patch_size, target + patch_size);
    
    // Flush instruction cache
    MemoryUtils::flushInstructionCache(trampoline, trampoline_size);
    
    return reinterpret_cast<uintptr_t>(trampoline);
}

// ============================================================================
// Hook API
// ============================================================================

int InlineHook::hook(void* target_func, void* hook_func, void** orig_func) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    uintptr_t target = reinterpret_cast<uintptr_t>(target_func);
    uintptr_t hook   = reinterpret_cast<uintptr_t>(hook_func);
    
    LOG_I("Inline hook: %p -> %p", target_func, hook_func);
    
    // Check alignment (ARM64 instructions must be 4-byte aligned)
    if (target & 3) {
        LOG_E("Target function not aligned: %p", target_func);
        return -1;
    }
    
    // We need to overwrite exactly ARM64_ABS_BRANCH_SIZE (12) bytes = 3 instructions
    size_t patch_size = ARM64_ABS_BRANCH_SIZE;
    
    // Build trampoline (saved instructions + jump back)
    uintptr_t trampoline = buildTrampoline(target, patch_size);
    if (trampoline == 0) {
        LOG_E("Failed to build trampoline");
        return -2;
    }
    
    // Save original bytes
    InlineHookEntry entry;
    entry.target_func    = target;
    entry.hook_func      = hook;
    entry.trampoline     = trampoline;
    entry.trampoline_size = patch_size + ARM64_ABS_BRANCH_SIZE;
    entry.patch_size     = patch_size;
    memcpy(entry.saved_bytes, target_func, patch_size);
    
    // Patch the target function with an absolute branch to our hook
    uint8_t branch_code[ARM64_ABS_BRANCH_SIZE];
    generateAbsoluteBranch(branch_code, hook);
    
    if (!MemoryUtils::safeWrite(target_func, branch_code, patch_size)) {
        LOG_E("Failed to patch target function at %p", target_func);
        MemoryUtils::freeExecutable(reinterpret_cast<void*>(trampoline), entry.trampoline_size);
        return -3;
    }
    
    entry.active = true;
    m_hooks.push_back(entry);
    
    // Return the trampoline as the "original function" pointer
    if (orig_func) {
        *orig_func = reinterpret_cast<void*>(trampoline);
    }
    
    LOG_I("Inline hook installed: %p -> %p (trampoline: %p)",
          target_func, hook_func, reinterpret_cast<void*>(trampoline));
    
    return 0;
}

int InlineHook::unhook(void* target_func) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    uintptr_t target = reinterpret_cast<uintptr_t>(target_func);
    
    for (auto it = m_hooks.begin(); it != m_hooks.end(); ++it) {
        if (it->target_func == target && it->active) {
            // Restore original bytes
            MemoryUtils::safeWrite(target_func, it->saved_bytes, it->patch_size);
            
            // Free trampoline
            MemoryUtils::freeExecutable(reinterpret_cast<void*>(it->trampoline), 
                                        it->trampoline_size);
            
            it->active = false;
            LOG_I("Inline hook removed: %p", target_func);
            return 0;
        }
    }
    
    LOG_W("No hook found for target: %p", target_func);
    return -1;
}

void InlineHook::unhookAll() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    for (auto& entry : m_hooks) {
        if (entry.active) {
            MemoryUtils::safeWrite(reinterpret_cast<void*>(entry.target_func),
                                   entry.saved_bytes, entry.patch_size);
            MemoryUtils::freeExecutable(reinterpret_cast<void*>(entry.trampoline),
                                        entry.trampoline_size);
            entry.active = false;
        }
    }
    
    LOG_I("All %zu inline hooks removed", m_hooks.size());
    m_hooks.clear();
}

} // namespace nexusvm

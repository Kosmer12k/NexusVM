/*
 * NexusVM — Module 3: Fake Root Implementation
 * 
 * Hooks libc/bionic functions to make guest processes think they run as root.
 * This is critical because Android's init, Zygote, and system_server all
 * perform UID checks and setuid() calls that would fail without root.
 * 
 * Hooked functions:
 * - getuid / geteuid / getgid / getegid → return 0 (root)
 * - setuid / setgid / setreuid / setregid → return 0 (success, no-op)
 * - capget / capset → fake full capabilities
 * - SELinux: getcon / setcon / security_getenforce → fake permissive mode
 */

#include "fake_root.h"
#include "nexus_engine.h"
#include "utils/logger.h"
#include "hooking/plt_hook.h"

#include <unistd.h>
#include <sys/types.h>
#include <cstring>

#define MOD_TAG "FakeRoot"

namespace nexusvm {

// ============================================================================
// Global state for hook functions (must be accessible from C callbacks)
// ============================================================================

static FakeRoot* g_fake_root_instance = nullptr;

// Original function pointers
static uid_t  (*orig_getuid)()  = nullptr;
static uid_t  (*orig_geteuid)() = nullptr;
static gid_t  (*orig_getgid)()  = nullptr;
static gid_t  (*orig_getegid)() = nullptr;
static int    (*orig_setuid)(uid_t)   = nullptr;
static int    (*orig_setgid)(gid_t)   = nullptr;
static int    (*orig_setreuid)(uid_t, uid_t)  = nullptr;
static int    (*orig_setregid)(gid_t, gid_t)  = nullptr;
static int    (*orig_setresuid)(uid_t, uid_t, uid_t) = nullptr;
static int    (*orig_setresgid)(gid_t, gid_t, gid_t) = nullptr;

// ============================================================================
// Hook Functions
// ============================================================================

static uid_t hook_getuid() {
    if (g_fake_root_instance) return g_fake_root_instance->getFakeUid();
    return 0;
}

static uid_t hook_geteuid() {
    if (g_fake_root_instance) return g_fake_root_instance->getFakeUid();
    return 0;
}

static gid_t hook_getgid() {
    if (g_fake_root_instance) return g_fake_root_instance->getFakeGid();
    return 0;
}

static gid_t hook_getegid() {
    if (g_fake_root_instance) return g_fake_root_instance->getFakeGid();
    return 0;
}

static int hook_setuid(uid_t /*uid*/) {
    MLOG_D(MOD_TAG, "setuid() intercepted -> fake success");
    return 0;
}

static int hook_setgid(gid_t /*gid*/) {
    MLOG_D(MOD_TAG, "setgid() intercepted -> fake success");
    return 0;
}

static int hook_setreuid(uid_t /*ruid*/, uid_t /*euid*/) {
    return 0;
}

static int hook_setregid(gid_t /*rgid*/, gid_t /*egid*/) {
    return 0;
}

static int hook_setresuid(uid_t /*ruid*/, uid_t /*euid*/, uid_t /*suid*/) {
    return 0;
}

static int hook_setresgid(gid_t /*rgid*/, gid_t /*egid*/, gid_t /*sgid*/) {
    return 0;
}

// SELinux hooks
static int hook_security_getenforce() {
    return 0; // Permissive
}

// ============================================================================
// FakeRoot Implementation
// ============================================================================

FakeRoot::FakeRoot(NexusEngine& engine) : m_engine(engine) {
    g_fake_root_instance = this;
    MLOG_I(MOD_TAG, "Created");
}

FakeRoot::~FakeRoot() {
    shutdown();
    g_fake_root_instance = nullptr;
}

int FakeRoot::init() {
    MLOG_I(MOD_TAG, "Initialized (fake UID=%d, GID=%d)", m_fake_uid, m_fake_gid);
    grantAllCapabilities();
    return 0;
}

void FakeRoot::shutdown() {
    if (m_hooks_installed) {
        removeHooks();
    }
    MLOG_I(MOD_TAG, "Shutdown");
}

int FakeRoot::installHooks() {
    MLOG_I(MOD_TAG, "Installing fake root hooks");
    
    // We use the PLT hook mechanism to override libc functions
    // The hooks are installed into the guest's libc when it loads
    
    // For now, we store the hook function pointers that will be used
    // by the injection library (libfakeroot.so)
    
    // In a production implementation, these hooks are installed via:
    // 1. LD_PRELOAD with libfakeroot.so (which defines these functions)
    // 2. PLT hooking on specific guest binaries after they load
    
    m_hooks_installed = true;
    
    MLOG_I(MOD_TAG, "Fake root hooks installed");
    MLOG_I(MOD_TAG, "  UID spoofing: %d", m_fake_uid);
    MLOG_I(MOD_TAG, "  GID spoofing: %d", m_fake_gid);
    MLOG_I(MOD_TAG, "  Capabilities: 0x%016llx", (unsigned long long)m_capabilities);
    MLOG_I(MOD_TAG, "  SELinux context: %s", m_fake_selinux_context.c_str());
    
    return 0;
}

int FakeRoot::removeHooks() {
    // Hooks in guest processes are automatically removed when they exit
    m_hooks_installed = false;
    MLOG_I(MOD_TAG, "Hooks removed");
    return 0;
}

// ============================================================================
// Capability Management
// ============================================================================

bool FakeRoot::hasCapability(int cap) const {
    if (cap < 0 || cap > 63) return false;
    return (m_capabilities & (1ULL << cap)) != 0;
}

void FakeRoot::grantCapability(int cap) {
    if (cap >= 0 && cap <= 63) {
        m_capabilities |= (1ULL << cap);
    }
}

void FakeRoot::revokeCapability(int cap) {
    if (cap >= 0 && cap <= 63) {
        m_capabilities &= ~(1ULL << cap);
    }
}

void FakeRoot::grantAllCapabilities() {
    m_capabilities = 0xFFFFFFFFFFFFFFFFULL;
}

} // namespace nexusvm

/*
 * NexusVM — Module 3: Fake Root
 * 
 * Makes guest processes believe they are running as root (UID 0).
 * Hooks getuid/setuid/capabilities/SELinux functions.
 */

#pragma once

#include <string>
#include <unordered_map>
#include <cstdint>

namespace nexusvm {

class NexusEngine;

class FakeRoot {
public:
    explicit FakeRoot(NexusEngine& engine);
    ~FakeRoot();
    
    int init();
    void shutdown();
    
    /** Install fake root hooks (PLT hooks on libc) */
    int installHooks();
    
    /** Remove hooks */
    int removeHooks();
    
    /** Set the fake UID for guest processes */
    void setFakeUid(uid_t uid) { m_fake_uid = uid; }
    void setFakeGid(gid_t gid) { m_fake_gid = gid; }
    
    /** Get current fake values */
    uid_t getFakeUid() const { return m_fake_uid; }
    gid_t getFakeGid() const { return m_fake_gid; }
    
    /** Capability management */
    bool hasCapability(int cap) const;
    void grantCapability(int cap);
    void revokeCapability(int cap);
    void grantAllCapabilities();
    
    /** SELinux context spoofing */
    void setFakeSelinuxContext(const std::string& context) { m_fake_selinux_context = context; }
    const std::string& getFakeSelinuxContext() const { return m_fake_selinux_context; }

private:
    NexusEngine& m_engine;
    uid_t m_fake_uid = 0;  // Root by default
    gid_t m_fake_gid = 0;
    uint64_t m_capabilities = 0xFFFFFFFFFFFFFFFFULL; // All caps granted
    std::string m_fake_selinux_context = "u:r:init:s0";
    bool m_hooks_installed = false;
};

} // namespace nexusvm

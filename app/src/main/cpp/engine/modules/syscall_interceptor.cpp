/*
 * NexusVM — Module 2: Syscall Interceptor Implementation
 * 
 * Core syscall interception engine using:
 * 1. seccomp-bpf for trapping privileged syscalls
 * 2. PLT hooking for libc function redirection
 * 3. LD_PRELOAD injection for guest processes
 * 4. File path translation (/system → /data/data/.../rootfs/system)
 */

#include "syscall_interceptor.h"
#include "nexus_engine.h"
#include "utils/logger.h"
#include "utils/file_utils.h"

#include <linux/seccomp.h>
#include <linux/filter.h>
#include <linux/audit.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <signal.h>
#include <unistd.h>
#include <cstring>

#define MOD_TAG "SyscallInterceptor"

namespace nexusvm {

SyscallInterceptor::SyscallInterceptor(NexusEngine& engine) : m_engine(engine) {
    MLOG_I(MOD_TAG, "Created");
}

SyscallInterceptor::~SyscallInterceptor() {
    shutdown();
}

int SyscallInterceptor::init() {
    MLOG_I(MOD_TAG, "Initialized");
    return 0;
}

void SyscallInterceptor::shutdown() {
    if (m_hooks_installed) {
        removeHooks();
    }
    MLOG_I(MOD_TAG, "Shutdown");
}

// ============================================================================
// Hook Installation
// ============================================================================

int SyscallInterceptor::installHooks(const std::string& rootfs_path) {
    MLOG_I(MOD_TAG, "Installing syscall hooks for rootfs: %s", rootfs_path.c_str());
    m_rootfs_path = rootfs_path;
    
    // Setup file path redirection rules
    int ret = setupPathRedirection(rootfs_path);
    if (ret != 0) {
        MLOG_E(MOD_TAG, "Path redirection setup failed: %d", ret);
        return ret;
    }
    
    // Setup environment
    ret = setupEnvironment(rootfs_path);
    if (ret != 0) {
        MLOG_E(MOD_TAG, "Environment setup failed: %d", ret);
        return ret;
    }
    
    // Create the LD_PRELOAD library that will be injected into guest processes
    std::string preload_path = rootfs_path + "/system/lib64/libnexusvm_preload.so";
    ret = createPreloadLibrary(preload_path);
    if (ret != 0) {
        MLOG_W(MOD_TAG, "Preload library creation failed (will use direct hooking): %d", ret);
    }
    
    // Register default syscall handlers for privileged operations
    
    // mount() - fake success (guest tries to mount filesystems)
    registerHandler(__NR_mount, [](long, long, long, long, long, long) -> long {
        MLOG_D(MOD_TAG, "Intercepted mount() -> fake success");
        return 0;
    });
    
    // umount2() - fake success
    registerHandler(__NR_umount2, [](long, long, long, long, long, long) -> long {
        return 0;
    });
    
    // pivot_root() - fake success
    registerHandler(__NR_pivot_root, [](long, long, long, long, long, long) -> long {
        return 0;
    });
    
    // chroot() - fake success (guest init tries to chroot)
    registerHandler(__NR_chroot, [](long, long, long, long, long, long) -> long {
        MLOG_D(MOD_TAG, "Intercepted chroot() -> fake success");
        return 0;
    });
    
    // setuid/setgid - handled by FakeRoot module, but we intercept here too
    registerHandler(__NR_setuid, [](long, long, long, long, long, long) -> long {
        return 0;
    });
    registerHandler(__NR_setgid, [](long, long, long, long, long, long) -> long {
        return 0;
    });
    registerHandler(__NR_setreuid, [](long, long, long, long, long, long) -> long {
        return 0;
    });
    registerHandler(__NR_setregid, [](long, long, long, long, long, long) -> long {
        return 0;
    });
    registerHandler(__NR_setresuid, [](long, long, long, long, long, long) -> long {
        return 0;
    });
    registerHandler(__NR_setresgid, [](long, long, long, long, long, long) -> long {
        return 0;
    });
    registerHandler(__NR_setgroups, [](long, long, long, long, long, long) -> long {
        return 0;
    });
    
    // mknod() - fake success for device nodes
    registerHandler(__NR_mknod, [](long, long, long, long, long, long) -> long {
        return 0;
    });
    registerHandler(__NR_mknodat, [](long, long, long, long, long, long) -> long {
        return 0;
    });
    
    // reboot() - intercept to prevent actual reboot
    registerHandler(__NR_reboot, [](long, long, long, long, long, long) -> long {
        MLOG_I(MOD_TAG, "Intercepted reboot() -> ignored");
        return 0;
    });
    
    // sethostname/setdomainname - fake success
    registerHandler(__NR_sethostname, [](long, long, long, long, long, long) -> long {
        return 0;
    });
    registerHandler(__NR_setdomainname, [](long, long, long, long, long, long) -> long {
        return 0;
    });
    
    m_hooks_installed = true;
    MLOG_I(MOD_TAG, "Syscall hooks installed (%zu handlers registered)", m_handlers.size());
    return 0;
}

int SyscallInterceptor::removeHooks() {
    m_handlers.clear();
    m_hooks_installed = false;
    MLOG_I(MOD_TAG, "Syscall hooks removed");
    return 0;
}

// ============================================================================
// seccomp-bpf Filter
// ============================================================================

int SyscallInterceptor::installSeccompFilter() {
    MLOG_I(MOD_TAG, "Installing seccomp-bpf filter");
    
    // Enable seccomp for this process
    // First, we need to allow ourselves to install filters
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        MLOG_E(MOD_TAG, "prctl(NO_NEW_PRIVS) failed: %s", strerror(errno));
        return -1;
    }
    
    // Build BPF program that traps specific syscalls
    // For trapped syscalls, we return SECCOMP_RET_TRAP which sends SIGSYS
    // Our SIGSYS handler then calls the registered handler
    
    struct sock_filter filter[] = {
        // Load architecture
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)),
        
        // Verify architecture is AArch64 (0xC00000B7)
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_AARCH64, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
        
        // Load syscall number
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
        
        // Trap specific syscalls
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_mount, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
        
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_umount2, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
        
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_chroot, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
        
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_pivot_root, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
        
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_reboot, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
        
        // Allow everything else
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
    };
    
    struct sock_fprog prog = {
        .len = sizeof(filter) / sizeof(filter[0]),
        .filter = filter,
    };
    
    if (syscall(__NR_seccomp, SECCOMP_SET_MODE_FILTER, 0, &prog) != 0) {
        MLOG_E(MOD_TAG, "seccomp filter install failed: %s", strerror(errno));
        return -2;
    }
    
    MLOG_I(MOD_TAG, "seccomp-bpf filter installed");
    return 0;
}

// ============================================================================
// Handler Registration
// ============================================================================

void SyscallInterceptor::registerHandler(int syscall_nr, SyscallHandler handler) {
    m_handlers[syscall_nr] = std::move(handler);
}

// ============================================================================
// Path Redirection
// ============================================================================

int SyscallInterceptor::setupPathRedirection(const std::string& rootfs_path) {
    MLOG_I(MOD_TAG, "Setting up path redirection -> %s", rootfs_path.c_str());
    
    // The path redirection works by hooking open/openat/access/stat etc.
    // in the LD_PRELOAD library that gets injected into guest processes.
    //
    // When guest calls open("/system/bin/sh"), our hook translates it to
    // open("/data/data/com.nexusvm.engine/rootfs/system/bin/sh")
    //
    // Path translation rules:
    //   /system/*     -> rootfs/system/*
    //   /vendor/*     -> rootfs/vendor/*
    //   /data/*       -> rootfs/data/*
    //   /proc/*       -> rootfs/proc/*  (then redirected to fake proc)
    //   /sys/*        -> rootfs/sys/*   (then redirected to fake sys)
    //   /dev/binder   -> Virtual Binder socket
    //   /dev/input/*  -> Input Bridge socket
    
    // Write redirection config for the preload library
    std::string config_path = rootfs_path + "/.nexusvm_paths";
    std::string config = 
        "# NexusVM Path Redirection Config\n"
        "# Format: <guest_path_prefix>=<host_path_prefix>\n"
        "rootfs=" + rootfs_path + "\n"
        "/system=" + rootfs_path + "/system\n"
        "/vendor=" + rootfs_path + "/vendor\n"
        "/data=" + rootfs_path + "/data\n"
        "/cache=" + rootfs_path + "/cache\n"
        "/proc=" + rootfs_path + "/proc\n"
        "/sys=" + rootfs_path + "/sys\n"
        "/dev=" + rootfs_path + "/dev\n"
        "/apex=" + rootfs_path + "/apex\n"
        "/linkerconfig=" + rootfs_path + "/linkerconfig\n"
        "/mnt=" + rootfs_path + "/mnt\n"
        "/storage=" + rootfs_path + "/storage\n"
        "/tmp=" + rootfs_path + "/tmp\n";
    
    return FileUtils::writeFile(config_path, config);
}

int SyscallInterceptor::setupEnvironment(const std::string& rootfs_path) {
    // These environment variables will be set for guest processes
    // They tell the Android runtime where to find things
    
    std::string env_path = rootfs_path + "/.nexusvm_env";
    std::string env = 
        "ANDROID_ROOT=" + rootfs_path + "/system\n"
        "ANDROID_DATA=" + rootfs_path + "/data\n"
        "ANDROID_STORAGE=" + rootfs_path + "/storage\n"
        "ANDROID_ART_ROOT=" + rootfs_path + "/apex/com.android.art\n"
        "ANDROID_I18N_ROOT=" + rootfs_path + "/apex/com.android.i18n\n"
        "ANDROID_TZDATA_ROOT=" + rootfs_path + "/apex/com.android.tzdata\n"
        "EXTERNAL_STORAGE=" + rootfs_path + "/storage/emulated/0\n"
        "PATH=" + rootfs_path + "/system/bin:" + rootfs_path + "/system/xbin\n"
        "LD_LIBRARY_PATH=" + rootfs_path + "/system/lib64:" + rootfs_path + "/vendor/lib64\n"
        "BOOTCLASSPATH=" + rootfs_path + "/system/framework/framework.jar\n"
        "CLASSPATH=" + rootfs_path + "/system/framework/services.jar\n"
        "TMPDIR=" + rootfs_path + "/tmp\n"
        "HOME=" + rootfs_path + "/data\n";
    
    return FileUtils::writeFile(env_path, env);
}

int SyscallInterceptor::createPreloadLibrary(const std::string& output_path) {
    // The preload library is a .so that gets loaded via LD_PRELOAD
    // into every guest process. It hooks libc functions for path redirection.
    //
    // In production, this library is pre-compiled and bundled as an asset.
    // Here we copy it from the app's native lib directory.
    
    MLOG_I(MOD_TAG, "Preload library target: %s", output_path.c_str());
    
    // The actual preload library is compiled separately as libfakeroot.so
    // It will be copied from the APK's lib/ directory during setup
    // For now, we just ensure the directory exists
    
    std::string dir = output_path.substr(0, output_path.rfind('/'));
    FileUtils::mkdirRecursive(dir);
    
    return 0;
}

} // namespace nexusvm

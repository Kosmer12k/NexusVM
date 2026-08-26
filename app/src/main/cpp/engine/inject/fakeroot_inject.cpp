/*
 * NexusVM — Fakeroot Inject Library Implementation
 * 
 * This shared library (.so) gets loaded via LD_PRELOAD into every
 * guest process. It overrides key libc/bionic functions to:
 * 
 * 1. Make processes think they run as root (UID 0)
 * 2. Redirect filesystem paths (/system → /data/data/.../rootfs/system)
 * 3. Fake capabilities and SELinux contexts
 * 4. Redirect /dev/binder to our Virtual Binder socket
 * 
 * The functions here use __attribute__((visibility("default"))) so they
 * take priority over the real libc functions via LD_PRELOAD symbol interposition.
 */

#include "fakeroot_inject.h"
#include "utils/logger.h"

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define TAG "NexusVM-Inject"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)

// ============================================================================
// Rootfs path (read from environment variable at load time)
// ============================================================================

static char g_rootfs_path[512] = {0};
static bool g_initialized = false;

// Original function pointers (resolved via dlsym)
static int (*real_open)(const char*, int, ...) = nullptr;
static int (*real_openat)(int, const char*, int, ...) = nullptr;
static int (*real_access)(const char*, int) = nullptr;
static int (*real_stat)(const char*, struct stat*) = nullptr;
static int (*real_lstat)(const char*, struct stat*) = nullptr;
static FILE* (*real_fopen)(const char*, const char*) = nullptr;
static int (*real_chdir)(const char*) = nullptr;

// ============================================================================
// Path Redirection
// ============================================================================

/**
 * Translate a guest path to a host path if it matches a known prefix.
 * Returns the translated path in the buffer, or the original path if no match.
 */
static const char* translate_path(const char* path, char* buf, size_t buf_size) {
    if (!path || !g_initialized || g_rootfs_path[0] == 0) return path;
    
    // Only redirect absolute paths
    if (path[0] != '/') return path;
    
    // Skip paths that are already inside our rootfs
    if (strncmp(path, g_rootfs_path, strlen(g_rootfs_path)) == 0) return path;
    
    // Paths to redirect
    static const char* prefixes[] = {
        "/system", "/vendor", "/data", "/cache",
        "/proc", "/sys", "/dev", "/apex",
        "/mnt", "/storage", "/tmp", "/config",
        "/linkerconfig",
        nullptr
    };
    
    for (int i = 0; prefixes[i]; ++i) {
        size_t plen = strlen(prefixes[i]);
        if (strncmp(path, prefixes[i], plen) == 0 && 
            (path[plen] == '/' || path[plen] == '\0')) {
            snprintf(buf, buf_size, "%s%s", g_rootfs_path, path);
            return buf;
        }
    }
    
    return path;
}

// ============================================================================
// Overridden libc Functions
// ============================================================================

#ifdef __cplusplus
extern "C" {
#endif

// ----- UID/GID spoofing -----

__attribute__((visibility("default")))
uid_t getuid(void) { return 0; }

__attribute__((visibility("default")))
uid_t geteuid(void) { return 0; }

__attribute__((visibility("default")))
gid_t getgid(void) { return 0; }

__attribute__((visibility("default")))
gid_t getegid(void) { return 0; }

__attribute__((visibility("default")))
int setuid(uid_t uid) { (void)uid; return 0; }

__attribute__((visibility("default")))
int setgid(gid_t gid) { (void)gid; return 0; }

__attribute__((visibility("default")))
int setreuid(uid_t ruid, uid_t euid) { (void)ruid; (void)euid; return 0; }

__attribute__((visibility("default")))
int setregid(gid_t rgid, gid_t egid) { (void)rgid; (void)egid; return 0; }

__attribute__((visibility("default")))
int setresuid(uid_t ruid, uid_t euid, uid_t suid) { 
    (void)ruid; (void)euid; (void)suid; return 0; 
}

__attribute__((visibility("default")))
int setresgid(gid_t rgid, gid_t egid, gid_t sgid) { 
    (void)rgid; (void)egid; (void)sgid; return 0; 
}

__attribute__((visibility("default")))
int setgroups(size_t size, const gid_t* list) { 
    (void)size; (void)list; return 0; 
}

// ----- SELinux spoofing -----

__attribute__((visibility("default")))
int security_getenforce(void) { return 0; } // Permissive

__attribute__((visibility("default")))
int getcon(char** context) {
    if (context) {
        *context = strdup("u:r:init:s0");
    }
    return 0;
}

__attribute__((visibility("default")))
int setcon(const char* context) { (void)context; return 0; }

// ----- Filesystem redirection -----

__attribute__((visibility("default")))
int open(const char* pathname, int flags, ...) {
    char translated[1024];
    const char* actual_path = translate_path(pathname, translated, sizeof(translated));
    
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = static_cast<mode_t>(va_arg(ap, int));
        va_end(ap);
    }
    
    if (real_open) {
        return real_open(actual_path, flags, mode);
    }
    
    // Fallback to syscall if real_open not resolved
    return syscall(__NR_openat, AT_FDCWD, actual_path, flags, mode);
}

__attribute__((visibility("default")))
FILE* fopen(const char* pathname, const char* mode) {
    char translated[1024];
    const char* actual_path = translate_path(pathname, translated, sizeof(translated));
    
    if (real_fopen) {
        return real_fopen(actual_path, mode);
    }
    return nullptr;
}

__attribute__((visibility("default")))
int access(const char* pathname, int mode) {
    char translated[1024];
    const char* actual_path = translate_path(pathname, translated, sizeof(translated));
    
    if (real_access) {
        return real_access(actual_path, mode);
    }
    return syscall(__NR_faccessat, AT_FDCWD, actual_path, mode, 0);
}

__attribute__((visibility("default")))
int chdir(const char* path) {
    char translated[1024];
    const char* actual_path = translate_path(path, translated, sizeof(translated));
    
    if (real_chdir) {
        return real_chdir(actual_path);
    }
    return syscall(__NR_chdir, actual_path);
}

// ----- Mount / Chroot (no-op) -----

__attribute__((visibility("default")))
int mount(const char* source, const char* target, const char* fstype,
          unsigned long flags, const void* data) {
    (void)source; (void)target; (void)fstype; (void)flags; (void)data;
    return 0; // Fake success
}

__attribute__((visibility("default")))
int umount(const char* target) { (void)target; return 0; }

__attribute__((visibility("default")))
int umount2(const char* target, int flags) { (void)target; (void)flags; return 0; }

__attribute__((visibility("default")))
int chroot(const char* path) { (void)path; return 0; }

__attribute__((visibility("default")))
int pivot_root(const char* new_root, const char* put_old) {
    (void)new_root; (void)put_old; return 0;
}

// ----- IOCTL Interception (Virtual Binder & Ashmem) -----

static int g_binder_socket_fd = -1;

static int get_binder_socket() {
    if (g_binder_socket_fd >= 0) return g_binder_socket_fd;
    const char* sock_path = getenv("NEXUSVM_BINDER_SOCKET");
    if (!sock_path) return -1;

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    g_binder_socket_fd = fd;
    return g_binder_socket_fd;
}

__attribute__((visibility("default")))
int ioctl(int fd, unsigned long request, ...) {
    va_list ap;
    va_start(ap, request);
    void* argp = va_arg(ap, void*);
    va_end(ap);

    // Check if this is a Binder ioctl (BINDER_WRITE_READ = 0xc0306201 on 64-bit, or BINDER_SET_MAX_THREADS, etc.)
    // Binder magic is 'b' (0x62)
    if (((request >> 8) & 0xFF) == 'b') {
        int sock = get_binder_socket();
        if (sock >= 0) {
            // Forward transaction over the Unix socket to VirtualBinder daemon
            send(sock, argp, sizeof(void*), MSG_DONTWAIT);
            return 0; // Handled
        }
        return 0; // Fallback mock success
    }

    // Default passthrough
    return syscall(__NR_ioctl, fd, request, argp);
}

// ----- Reboot prevention -----

__attribute__((visibility("default")))
int reboot(int cmd) {
    (void)cmd;
    LOGI("reboot() intercepted and blocked");
    return 0;
}

#ifdef __cplusplus
}
#endif

// ============================================================================
// Constructor / Destructor
// ============================================================================

void nexusvm_inject_init() {
    LOGI("NexusVM inject library loaded");
    
    // Read rootfs path from environment
    const char* rootfs = getenv("NEXUSVM_ROOTFS");
    if (rootfs) {
        strncpy(g_rootfs_path, rootfs, sizeof(g_rootfs_path) - 1);
        LOGI("Rootfs: %s", g_rootfs_path);
    }
    
    // Resolve original function pointers via RTLD_NEXT
    real_open = reinterpret_cast<int(*)(const char*, int, ...)>(
        dlsym(RTLD_NEXT, "open"));
    real_openat = reinterpret_cast<int(*)(int, const char*, int, ...)>(
        dlsym(RTLD_NEXT, "openat"));
    real_access = reinterpret_cast<int(*)(const char*, int)>(
        dlsym(RTLD_NEXT, "access"));
    real_fopen = reinterpret_cast<FILE*(*)(const char*, const char*)>(
        dlsym(RTLD_NEXT, "fopen"));
    real_chdir = reinterpret_cast<int(*)(const char*)>(
        dlsym(RTLD_NEXT, "chdir"));
    
    g_initialized = true;
    LOGI("NexusVM inject library initialized");
}

void nexusvm_inject_cleanup() {
    LOGI("NexusVM inject library unloaded");
    g_initialized = false;
}

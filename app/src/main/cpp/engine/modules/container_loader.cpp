/*
 * NexusVM — Module 1: Container Loader Implementation
 * 
 * This module handles the critical first step: extracting a GSI ROM image
 * and preparing a usable rootfs inside the app's private data directory.
 * 
 * Key challenge: We cannot use mount() without root.
 * Solution: Extract the EXT4 filesystem contents using userspace tools.
 */

#include "container_loader.h"
#include "nexus_engine.h"
#include "utils/logger.h"
#include "utils/file_utils.h"

#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <sstream>
#include <algorithm>

#define MOD_TAG "ContainerLoader"

namespace nexusvm {

ContainerLoader::ContainerLoader(NexusEngine& engine) : m_engine(engine) {
    MLOG_I(MOD_TAG, "Created");
}

ContainerLoader::~ContainerLoader() {
    shutdown();
}

int ContainerLoader::init() {
    MLOG_I(MOD_TAG, "Initialized");
    return 0;
}

void ContainerLoader::shutdown() {
    MLOG_I(MOD_TAG, "Shutdown");
}

// ============================================================================
// ROM Extraction
// ============================================================================

int ContainerLoader::extractRom(const std::string& rom_path, const std::string& output_dir) {
    MLOG_I(MOD_TAG, "Extracting ROM: %s -> %s", rom_path.c_str(), output_dir.c_str());
    
    // Check if ROM file exists
    if (!FileUtils::exists(rom_path)) {
        MLOG_E(MOD_TAG, "ROM file not found: %s", rom_path.c_str());
        return -1;
    }
    
    int64_t rom_size = FileUtils::getFileSize(rom_path);
    MLOG_I(MOD_TAG, "ROM size: %lld bytes (%.1f MB)", 
           (long long)rom_size, rom_size / (1024.0 * 1024.0));
    
    // Create output directory
    FileUtils::mkdirRecursive(output_dir);
    
    // Check if it is a tar.gz / tar rootfs archive
    if (rom_path.size() >= 7 && (rom_path.substr(rom_path.size() - 7) == ".tar.gz" || rom_path.substr(rom_path.size() - 4) == ".tgz")) {
        MLOG_I(MOD_TAG, "Detected .tar.gz RootFS package");
        int ret = FileUtils::extractTarGz(rom_path, output_dir);
        if (ret != 0) {
            MLOG_E(MOD_TAG, "Tar.gz extraction failed: %d", ret);
            return ret;
        }
        MLOG_I(MOD_TAG, "RootFS archive extracted successfully");
        return 0;
    }
    
    // Detect image type and extract
    if (FileUtils::isSparseImage(rom_path)) {
        MLOG_I(MOD_TAG, "Detected Android Sparse Image");
        
        // First convert sparse -> raw
        std::string raw_path = output_dir + "/../system_raw.img";
        int ret = FileUtils::sparseToRaw(rom_path, raw_path);
        if (ret != 0) {
            MLOG_E(MOD_TAG, "Sparse to raw conversion failed: %d", ret);
            return ret;
        }
        
        // Then extract raw EXT4
        ret = FileUtils::extractExt4Image(raw_path, output_dir);
        if (ret != 0) {
            MLOG_E(MOD_TAG, "EXT4 extraction failed: %d", ret);
            return ret;
        }
        
        // Clean up raw image to save space
        unlink(raw_path.c_str());
        
    } else if (FileUtils::isErofsImage(rom_path)) {
        MLOG_I(MOD_TAG, "Detected EROFS Image");
        // EROFS requires bundled erofs-utils (fsck.erofs --extract)
        // For now, return error with instruction
        MLOG_E(MOD_TAG, "EROFS extraction requires erofs-utils binary");
        return -100;
        
    } else {
        MLOG_I(MOD_TAG, "Assumed raw EXT4 Image");
        int ret = FileUtils::extractExt4Image(rom_path, output_dir);
        if (ret != 0) {
            MLOG_E(MOD_TAG, "EXT4 extraction failed: %d", ret);
            return ret;
        }
    }
    
    MLOG_I(MOD_TAG, "ROM extraction complete");
    return 0;
}

// ============================================================================
// Filesystem Setup
// ============================================================================

int ContainerLoader::setupFilesystem(const std::string& rootfs_path) {
    MLOG_I(MOD_TAG, "Setting up container filesystem at: %s", rootfs_path.c_str());
    
    // Create essential directories
    const char* dirs[] = {
        "/system", "/system/bin", "/system/lib64", "/system/framework",
        "/system/etc", "/system/app", "/system/priv-app",
        "/data", "/data/data", "/data/app", "/data/local", "/data/local/tmp",
        "/data/dalvik-cache", "/data/dalvik-cache/arm64",
        "/data/system", "/data/misc",
        "/cache", "/cache/dalvik-cache",
        "/proc", "/sys", "/dev",
        "/dev/input", "/dev/socket", "/dev/pts",
        "/mnt", "/mnt/sdcard",
        "/storage", "/storage/emulated", "/storage/emulated/0",
        "/vendor", "/vendor/lib64",
        "/apex",
        "/tmp",
        "/config",
        "/linkerconfig",
        nullptr
    };
    
    for (int i = 0; dirs[i] != nullptr; ++i) {
        std::string dir = rootfs_path + dirs[i];
        FileUtils::mkdirRecursive(dir);
    }
    
    // Create fake proc filesystem
    int ret = createFakeProc(rootfs_path);
    if (ret != 0) MLOG_W(MOD_TAG, "Fake /proc creation partially failed");
    
    // Create fake sys filesystem
    ret = createFakeSys(rootfs_path);
    if (ret != 0) MLOG_W(MOD_TAG, "Fake /sys creation partially failed");
    
    // Create fake dev entries
    ret = createFakeDev(rootfs_path);
    if (ret != 0) MLOG_W(MOD_TAG, "Fake /dev creation partially failed");
    
    // Setup data partition
    ret = setupDataPartition(rootfs_path);
    if (ret != 0) MLOG_W(MOD_TAG, "Data partition setup partially failed");
    
    // Patch init.rc if it exists
    std::string init_rc = rootfs_path + "/system/etc/init/hw/init.rc";
    if (!FileUtils::exists(init_rc)) {
        init_rc = rootfs_path + "/init.rc";
    }
    if (FileUtils::exists(init_rc)) {
        patchInitRc(init_rc);
    }
    
    // Create default.prop for guest
    std::string default_prop = rootfs_path + "/default.prop";
    std::string prop_content = 
        "ro.secure=0\n"
        "ro.debuggable=1\n"
        "ro.adb.secure=0\n"
        "persist.sys.usb.config=adb\n"
        "ro.allow.mock.location=1\n"
        "ro.hardware.chipname=nexusvm\n"
        "persist.sys.dalvik.vm.lib.2=libart.so\n"
        "dalvik.vm.heapsize=512m\n"
        "dalvik.vm.heapgrowthlimit=256m\n";
    FileUtils::writeFile(default_prop, prop_content);
    
    MLOG_I(MOD_TAG, "Container filesystem setup complete");
    return 0;
}

// ============================================================================
// Fake /proc
// ============================================================================

int ContainerLoader::createFakeProc(const std::string& rootfs_path) {
    std::string proc_dir = rootfs_path + "/proc";
    
    // /proc/cpuinfo — fake CPU information
    const auto& config = m_engine.getConfig();
    std::ostringstream cpuinfo;
    for (int i = 0; i < config.max_cpu_cores; ++i) {
        cpuinfo << "processor\t: " << i << "\n"
                << "BogoMIPS\t: 48.00\n"
                << "Features\t: fp asimd evtstrm aes pmull sha1 sha2 crc32 atomics\n"
                << "CPU implementer\t: 0x41\n"
                << "CPU architecture: 8\n"
                << "CPU variant\t: 0x1\n"
                << "CPU part\t: 0xd05\n"
                << "CPU revision\t: 0\n\n";
    }
    FileUtils::writeFile(proc_dir + "/cpuinfo", cpuinfo.str());
    
    // /proc/meminfo
    int mem_mb = config.max_memory_mb;
    long mem_kb = static_cast<long>(mem_mb) * 1024;
    std::ostringstream meminfo;
    meminfo << "MemTotal:       " << mem_kb << " kB\n"
            << "MemFree:        " << (mem_kb * 3 / 4) << " kB\n"
            << "MemAvailable:   " << (mem_kb * 7 / 8) << " kB\n"
            << "Buffers:        " << (mem_kb / 16) << " kB\n"
            << "Cached:         " << (mem_kb / 8) << " kB\n"
            << "SwapTotal:      0 kB\n"
            << "SwapFree:       0 kB\n";
    FileUtils::writeFile(proc_dir + "/meminfo", meminfo.str());
    
    // /proc/version
    FileUtils::writeFile(proc_dir + "/version",
        "Linux version 5.10.0-nexusvm (nexusvm@build) "
        "(Android clang 14.0.0) #1 SMP PREEMPT\n");
    
    // /proc/stat
    std::ostringstream stat_content;
    stat_content << "cpu  1000 200 300 5000 100 0 0 0 0 0\n";
    for (int i = 0; i < config.max_cpu_cores; ++i) {
        stat_content << "cpu" << i << " " << (1000/config.max_cpu_cores)
                     << " 50 75 1250 25 0 0 0 0 0\n";
    }
    stat_content << "intr 0\n"
                 << "ctxt 0\n"
                 << "btime 1700000000\n"
                 << "processes 1\n"
                 << "procs_running 1\n"
                 << "procs_blocked 0\n";
    FileUtils::writeFile(proc_dir + "/stat", stat_content.str());
    
    // /proc/filesystems
    FileUtils::writeFile(proc_dir + "/filesystems",
        "nodev\tsysfs\n"
        "nodev\ttmpfs\n"
        "nodev\tproc\n"
        "nodev\tdevpts\n"
        "\text4\n"
        "nodev\tfuse\n");
    
    // /proc/mounts
    FileUtils::writeFile(proc_dir + "/mounts",
        "rootfs / rootfs rw 0 0\n"
        "tmpfs /dev tmpfs rw,nosuid,mode=755 0 0\n"
        "proc /proc proc rw 0 0\n"
        "sysfs /sys sysfs rw 0 0\n"
        "/dev/block/system /system ext4 ro,noatime 0 0\n"
        "/dev/block/data /data ext4 rw,nosuid,nodev,noatime 0 0\n");
    
    // /proc/self directory structure
    FileUtils::mkdirRecursive(proc_dir + "/self");
    FileUtils::writeFile(proc_dir + "/self/status",
        "Name:\tinit\n"
        "State:\tS (sleeping)\n"
        "Pid:\t1\n"
        "PPid:\t0\n"
        "Uid:\t0\t0\t0\t0\n"
        "Gid:\t0\t0\t0\t0\n");
    
    return 0;
}

// ============================================================================
// Fake /sys
// ============================================================================

int ContainerLoader::createFakeSys(const std::string& rootfs_path) {
    std::string sys_dir = rootfs_path + "/sys";
    
    // Basic /sys structure
    FileUtils::mkdirRecursive(sys_dir + "/class/power_supply/battery");
    FileUtils::writeFile(sys_dir + "/class/power_supply/battery/status", "Charging");
    FileUtils::writeFile(sys_dir + "/class/power_supply/battery/level", "100");
    FileUtils::writeFile(sys_dir + "/class/power_supply/battery/capacity", "100");
    
    FileUtils::mkdirRecursive(sys_dir + "/devices/system/cpu");
    const auto& config = m_engine.getConfig();
    for (int i = 0; i < config.max_cpu_cores; ++i) {
        std::string cpu_dir = sys_dir + "/devices/system/cpu/cpu" + std::to_string(i);
        FileUtils::mkdirRecursive(cpu_dir + "/cpufreq");
        FileUtils::writeFile(cpu_dir + "/online", "1");
        FileUtils::writeFile(cpu_dir + "/cpufreq/scaling_cur_freq", "1800000");
        FileUtils::writeFile(cpu_dir + "/cpufreq/scaling_max_freq", "2400000");
        FileUtils::writeFile(cpu_dir + "/cpufreq/scaling_min_freq", "300000");
    }
    
    // Block devices
    FileUtils::mkdirRecursive(sys_dir + "/block");
    
    return 0;
}

// ============================================================================
// Fake /dev
// ============================================================================

int ContainerLoader::createFakeDev(const std::string& rootfs_path) {
    std::string dev_dir = rootfs_path + "/dev";
    
    // Create device node placeholder files
    // (We can't create actual device nodes without root, but we create
    //  placeholder files that our engine will intercept when accessed)
    
    FileUtils::writeFile(dev_dir + "/null", "");
    FileUtils::writeFile(dev_dir + "/zero", "");
    FileUtils::writeFile(dev_dir + "/random", "");
    FileUtils::writeFile(dev_dir + "/urandom", "");
    
    // Binder devices (will be intercepted by Virtual Binder module)
    FileUtils::writeFile(dev_dir + "/binder", "");
    FileUtils::writeFile(dev_dir + "/hwbinder", "");
    FileUtils::writeFile(dev_dir + "/vndbinder", "");
    
    // Input devices (will be intercepted by Input Bridge module)
    FileUtils::writeFile(dev_dir + "/input/event0", "");
    FileUtils::writeFile(dev_dir + "/input/event1", "");
    
    // Socket directory for system services
    FileUtils::mkdirRecursive(dev_dir + "/socket");
    
    // Properties
    FileUtils::writeFile(dev_dir + "/__properties__", "");
    
    // Ashmem (handled by engine)
    FileUtils::writeFile(dev_dir + "/ashmem", "");
    
    return 0;
}

// ============================================================================
// Data Partition
// ============================================================================

int ContainerLoader::setupDataPartition(const std::string& rootfs_path) {
    std::string data_dir = rootfs_path + "/data";
    
    // Essential data directories
    const char* data_dirs[] = {
        "/data/system/packages.xml",
        "/data/system/users/0",
        "/data/misc/wifi",
        "/data/misc/keystore",
        "/data/misc/keychain",
        "/data/local/tmp",
        "/data/dalvik-cache/arm64",
        "/data/app",
        "/data/data",
        "/data/user/0",
        "/data/user_de/0",
        nullptr
    };
    
    for (int i = 0; data_dirs[i] != nullptr; ++i) {
        FileUtils::mkdirRecursive(rootfs_path + data_dirs[i]);
    }
    
    return 0;
}

// ============================================================================
// Init.rc Patching
// ============================================================================

int ContainerLoader::patchInitRc(const std::string& init_rc_path) {
    MLOG_I(MOD_TAG, "Patching init.rc: %s", init_rc_path.c_str());
    
    std::string content = FileUtils::readFile(init_rc_path);
    if (content.empty()) {
        MLOG_W(MOD_TAG, "init.rc is empty or unreadable");
        return -1;
    }
    
    std::string patched;
    int ret = stripHardwareServices(content, patched);
    if (ret != 0) return ret;
    
    return FileUtils::writeFile(init_rc_path, patched);
}

int ContainerLoader::stripHardwareServices(const std::string& content, std::string& output) {
    // Services that require real hardware and will crash in container
    const char* services_to_remove[] = {
        "vold", "netd", "wificond", "hostapd",
        "installd", "lmkd", "logd",
        "healthd", "thermalservice",
        "gatekeeperd", "fingerprintd",
        "audioserver", "cameraserver", "mediaserver",
        "mediaextractor", "mediametrics",
        "incidentd", "tombstoned", "traced",
        "statsd", "storaged",
        "gpu_service",
        nullptr
    };
    
    std::istringstream stream(content);
    std::string line;
    bool skip_service = false;
    int removed_count = 0;
    
    while (std::getline(stream, line)) {
        // Check if this line starts a service we want to remove
        bool should_remove = false;
        if (line.find("service ") == 0 || line.find("service\t") == 0) {
            for (int i = 0; services_to_remove[i] != nullptr; ++i) {
                if (line.find(services_to_remove[i]) != std::string::npos) {
                    should_remove = true;
                    break;
                }
            }
        }
        
        if (should_remove) {
            skip_service = true;
            removed_count++;
            output += "# [NexusVM removed] " + line + "\n";
            continue;
        }
        
        // If we're in a removed service block, skip until next service/section
        if (skip_service) {
            if (!line.empty() && line[0] != ' ' && line[0] != '\t' && line[0] != '#') {
                // New section started, stop skipping
                skip_service = false;
            } else {
                output += "# [NexusVM removed] " + line + "\n";
                continue;
            }
        }
        
        output += line + "\n";
    }
    
    MLOG_I(MOD_TAG, "Removed %d hardware-dependent services from init.rc", removed_count);
    return 0;
}

bool ContainerLoader::isRootfsReady(const std::string& rootfs_path) const {
    // Check for essential files/directories
    return FileUtils::exists(rootfs_path + "/system/bin") &&
           FileUtils::exists(rootfs_path + "/system/lib64") &&
           FileUtils::exists(rootfs_path + "/proc/cpuinfo") &&
           FileUtils::exists(rootfs_path + "/dev/binder");
}

int ContainerLoader::cleanRootfs(const std::string& rootfs_path) {
    MLOG_I(MOD_TAG, "Cleaning rootfs: %s", rootfs_path.c_str());
    return FileUtils::removeRecursive(rootfs_path);
}

} // namespace nexusvm

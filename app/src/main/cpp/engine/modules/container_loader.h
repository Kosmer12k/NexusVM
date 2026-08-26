/*
 * NexusVM — Module 1: Container Loader
 * 
 * Handles extracting and preparing the GSI ROM filesystem.
 * Unpacks system.img, creates fake /proc, /sys, /dev.
 */

#pragma once

#include <string>

namespace nexusvm {

class NexusEngine; // Forward declaration

class ContainerLoader {
public:
    explicit ContainerLoader(NexusEngine& engine);
    ~ContainerLoader();
    
    /** Initialize the loader */
    int init();
    
    /** Shutdown and cleanup */
    void shutdown();
    
    /**
     * Extract a ROM image to the output directory
     * Handles: Raw EXT4, Android Sparse, EROFS
     */
    int extractRom(const std::string& rom_path, const std::string& output_dir);
    
    /**
     * Setup the container filesystem
     * Creates fake /proc, /sys, /dev, /data, /cache
     * Configures init.rc, default.prop, etc.
     */
    int setupFilesystem(const std::string& rootfs_path);
    
    /**
     * Patch init.rc to remove hardware-dependent services
     */
    int patchInitRc(const std::string& init_rc_path);
    
    /**
     * Check if the rootfs is already prepared
     */
    bool isRootfsReady(const std::string& rootfs_path) const;
    
    /**
     * Clean the rootfs directory
     */
    int cleanRootfs(const std::string& rootfs_path);

private:
    NexusEngine& m_engine;
    
    /** Create fake /proc entries (cpuinfo, meminfo, etc.) */
    int createFakeProc(const std::string& rootfs_path);
    
    /** Create fake /sys entries */
    int createFakeSys(const std::string& rootfs_path);
    
    /** Create fake /dev entries */
    int createFakeDev(const std::string& rootfs_path);
    
    /** Setup /data partition structure */
    int setupDataPartition(const std::string& rootfs_path);
    
    /** Remove services that need real hardware from init.rc */
    int stripHardwareServices(const std::string& init_rc_content, std::string& output);
};

} // namespace nexusvm

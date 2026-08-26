/*
 * NexusVM — File Utilities
 * 
 * Filesystem operations used by the container loader and other modules.
 */

#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace nexusvm {

class FileUtils {
public:
    /**
     * Check if a file or directory exists
     */
    static bool exists(const std::string& path);
    
    /**
     * Check if path is a directory
     */
    static bool isDirectory(const std::string& path);
    
    /**
     * Create directory recursively (like mkdir -p)
     */
    static int mkdirRecursive(const std::string& path, int mode = 0755);
    
    /**
     * Remove directory and all contents recursively
     */
    static int removeRecursive(const std::string& path);
    
    /**
     * Copy a file from src to dst
     */
    static int copyFile(const std::string& src, const std::string& dst);
    
    /**
     * Read entire file into a string
     */
    static std::string readFile(const std::string& path);
    
    /**
     * Write string to a file (creates or overwrites)
     */
    static int writeFile(const std::string& path, const std::string& content);
    
    /**
     * Get file size in bytes
     */
    static int64_t getFileSize(const std::string& path);
    
    /**
     * List files in a directory
     */
    static std::vector<std::string> listDir(const std::string& path);
    
    /**
     * Create a symbolic link
     */
    static int createSymlink(const std::string& target, const std::string& link_path);
    
    /**
     * Set file permissions (chmod)
     */
    static int setPermissions(const std::string& path, int mode);
    
    /**
     * Extract a .tar or .tar.gz archive directly into a directory
     * Works completely in userspace without root
     */
    static int extractTarGz(const std::string& archive_path, const std::string& output_dir);
    
    /**
     * Extract a raw EXT4 image to a directory using basic file operations
     * (Does NOT use mount — works without root)
     */
    static int extractExt4Image(const std::string& image_path, const std::string& output_dir);
    
    /**
     * Convert Android sparse image to raw image
     * (simg2img equivalent in C++)
     */
    static int sparseToRaw(const std::string& sparse_path, const std::string& raw_path);
    
    /**
     * Detect if a file is an Android sparse image
     */
    static bool isSparseImage(const std::string& path);
    
    /**
     * Detect if a file is an EROFS image
     */
    static bool isErofsImage(const std::string& path);
};

} // namespace nexusvm

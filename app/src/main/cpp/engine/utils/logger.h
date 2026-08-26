/*
 * NexusVM — Logger Utility
 * 
 * Centralized logging with Android logcat integration.
 * All modules use these macros for consistent log output.
 */

#pragma once

#ifdef __ANDROID__
#include <android/log.h>
#else
#include <cstdio>
#include <cstdarg>
#define ANDROID_LOG_VERBOSE 2
#define ANDROID_LOG_DEBUG   3
#define ANDROID_LOG_INFO    4
#define ANDROID_LOG_WARN    5
#define ANDROID_LOG_ERROR   6
#define ANDROID_LOG_FATAL   7
#define __android_log_print(level, tag, fmt, ...) printf("[%s] " fmt "\n", tag, ##__VA_ARGS__)
#endif

#define NEXUSVM_TAG "NexusVM"

// Log macros — use these throughout the engine
#define LOG_V(fmt, ...) __android_log_print(ANDROID_LOG_VERBOSE, NEXUSVM_TAG, fmt, ##__VA_ARGS__)
#define LOG_D(fmt, ...) __android_log_print(ANDROID_LOG_DEBUG,   NEXUSVM_TAG, fmt, ##__VA_ARGS__)
#define LOG_I(fmt, ...) __android_log_print(ANDROID_LOG_INFO,    NEXUSVM_TAG, fmt, ##__VA_ARGS__)
#define LOG_W(fmt, ...) __android_log_print(ANDROID_LOG_WARN,    NEXUSVM_TAG, fmt, ##__VA_ARGS__)
#define LOG_E(fmt, ...) __android_log_print(ANDROID_LOG_ERROR,   NEXUSVM_TAG, fmt, ##__VA_ARGS__)
#define LOG_F(fmt, ...) __android_log_print(ANDROID_LOG_FATAL,   NEXUSVM_TAG, fmt, ##__VA_ARGS__)

// Module-specific log macros (prepend module name)
#define MLOG_I(module, fmt, ...) LOG_I("[%s] " fmt, module, ##__VA_ARGS__)
#define MLOG_D(module, fmt, ...) LOG_D("[%s] " fmt, module, ##__VA_ARGS__)
#define MLOG_W(module, fmt, ...) LOG_W("[%s] " fmt, module, ##__VA_ARGS__)
#define MLOG_E(module, fmt, ...) LOG_E("[%s] " fmt, module, ##__VA_ARGS__)

namespace nexusvm {

/**
 * Logger class for file-based logging (optional, in addition to logcat)
 */
class Logger {
public:
    static Logger& getInstance();
    
    /** Initialize file logging (write to a log file in app data) */
    int initFileLog(const char* log_path);
    
    /** Write a log entry to the file */
    void writeToFile(int level, const char* tag, const char* msg);
    
    /** Flush and close the log file */
    void close();
    
    /** Set minimum log level */
    void setMinLevel(int level) { m_min_level = level; }

private:
    Logger() = default;
    ~Logger();
    
    int   m_log_fd = -1;
    int   m_min_level = ANDROID_LOG_DEBUG;
};

} // namespace nexusvm

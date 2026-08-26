/*
 * NexusVM — Logger Implementation
 */

#include "logger.h"

#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <cerrno>
#include <sys/time.h>

namespace nexusvm {

Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

Logger::~Logger() {
    close();
}

int Logger::initFileLog(const char* log_path) {
    if (m_log_fd >= 0) {
        ::close(m_log_fd);
    }
    
    m_log_fd = open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (m_log_fd < 0) {
        LOG_E("Failed to open log file: %s (%s)", log_path, strerror(errno));
        return -1;
    }
    
    LOG_I("File logging initialized: %s", log_path);
    return 0;
}

void Logger::writeToFile(int level, const char* tag, const char* msg) {
    if (m_log_fd < 0 || level < m_min_level) return;
    
    // Get timestamp
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    struct tm tm_info;
    localtime_r(&tv.tv_sec, &tm_info);
    
    // Level character
    char level_char;
    switch (level) {
        case ANDROID_LOG_VERBOSE: level_char = 'V'; break;
        case ANDROID_LOG_DEBUG:   level_char = 'D'; break;
        case ANDROID_LOG_INFO:    level_char = 'I'; break;
        case ANDROID_LOG_WARN:    level_char = 'W'; break;
        case ANDROID_LOG_ERROR:   level_char = 'E'; break;
        case ANDROID_LOG_FATAL:   level_char = 'F'; break;
        default:                  level_char = '?'; break;
    }
    
    // Format: "2024-01-01 12:00:00.123 I/NexusVM: message\n"
    char buf[2048];
    int len = snprintf(buf, sizeof(buf),
        "%04d-%02d-%02d %02d:%02d:%02d.%03ld %c/%s: %s\n",
        tm_info.tm_year + 1900, tm_info.tm_mon + 1, tm_info.tm_mday,
        tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec,
        tv.tv_usec / 1000,
        level_char, tag, msg);
    
    if (len > 0) {
        write(m_log_fd, buf, static_cast<size_t>(len));
    }
}

void Logger::close() {
    if (m_log_fd >= 0) {
        ::close(m_log_fd);
        m_log_fd = -1;
    }
}

} // namespace nexusvm

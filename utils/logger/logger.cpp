#include "logger.h"
#include <iostream>

LogLevel Logger::currentLevel = LOG_INFO; // Default level
std::mutex Logger::logMutex;
extern bool isDebugMode;

void Logger::SetLevel(LogLevel level) {
    currentLevel = level;
}

void Logger::Log(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> guard(logMutex);
    // Check if the message level is debug and if debug mode is enable
    if (level == LOG_DEBUG && !isDebugMode) {
        return;
    }
    if (level <= currentLevel) {
        std::cerr << "[" << LogLevelToString(level) << "] " << message << std::endl;
    }
}

std::string Logger::LogLevelToString(LogLevel level) {
    switch (level) {
        case LOG_ERROR: return "ERROR";
        case LOG_WARNING: return "WARNING";
        case LOG_INFO: return "INFO";
        case LOG_DEBUG: return "DEBUG";
        default: return "UNKNOWN";
    }
}

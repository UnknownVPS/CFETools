#include "logger.h"
#include <iostream>
#include <chrono>

LogLevel Logger::currentLevel = LOG_INFO;
bool Logger::isDebugMode = false;
std::unordered_map<std::string, std::chrono::steady_clock::time_point> Logger::timers;
std::mutex Logger::timerMutex;

void Logger::SetLevel(LogLevel level) {
    currentLevel = level;
    isDebugMode = (level == LOG_DEBUG);
}

void Logger::Log(LogLevel level, const std::string& message) {
    if (level == LOG_DEBUG && !isDebugMode) return;
    if (level <= currentLevel) {
        std::cerr << "[" << LogLevelToString(level) << "] " << message << std::endl;
    }
}

std::string Logger::LogLevelToString(LogLevel level) {
    switch(level) {
        case LOG_ERROR: return "ERROR";
        case LOG_WARNING: return "WARNING";
        case LOG_INFO: return "INFO";
        case LOG_DEBUG: return "DEBUG";
        default: return "UNKNOWN";
    }
}

// ------------------- Timer functions -------------------

void Logger::StartTimer(const std::string& task) {
    std::lock_guard<std::mutex> lock(timerMutex);
    timers[task] = std::chrono::steady_clock::now();
}

void Logger::EndTimer(const std::string& task, LogLevel level) {
    if (level > currentLevel) return;

    std::lock_guard<std::mutex> lock(timerMutex);
    auto it = timers.find(task);
    if (it != timers.end()) {
        auto endTime = std::chrono::steady_clock::now();
        std::chrono::duration<double, std::milli> elapsed = endTime - it->second;
        Log(level, task + " took " + std::to_string(elapsed.count()) + " ms");
        timers.erase(it); // remove timer after logging
    } else {
        Log(LOG_WARNING, "Timer for task '" + task + "' was not started!");
    }
}
#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <chrono>
#include <mutex>
#include <unordered_map>

enum LogLevel {
    LOG_ERROR = 0,
    LOG_WARNING = 1,
    LOG_INFO = 2,
    LOG_DEBUG = 3
};

class Logger {
public:
    static void SetLevel(LogLevel level);
    static void Log(LogLevel level, const std::string& message);
    static std::string LogLevelToString(LogLevel level);

    // Timer functions
    static void StartTimer(const std::string& task);
    static void EndTimer(const std::string& task, LogLevel level);

private:
    static LogLevel currentLevel;
    static bool isDebugMode;

    static std::unordered_map<std::string, std::chrono::steady_clock::time_point> timers;
    static std::mutex timerMutex;
};

#endif // LOGGER_H
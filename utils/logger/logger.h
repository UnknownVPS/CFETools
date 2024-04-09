#ifndef LOGGER_H
#define LOGGER_H

#include <string>

enum LogLevel {
    LOG_ERROR,
    LOG_WARNING,
    LOG_INFO,
    LOG_DEBUG
};

class Logger {
public:
    static void SetLevel(LogLevel level);
    static void Log(LogLevel level, const std::string& message);

private:
    static LogLevel currentLevel;
    static std::string LogLevelToString(LogLevel level);
};
extern bool isDebugMode;
#endif // LOGGER_H

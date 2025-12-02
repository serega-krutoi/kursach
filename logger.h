#include <string>

#pragma once

enum class LogLevel {
    Info,
    Warning,
    Error,
    Debug
};

void logMessage(LogLevel level, const std::string& msg);

void logInfo(const std::string& msg);
void logWarning(const std::string& msg);
void logError(const std::string& msg);
void logDebug(const std::string& msg);

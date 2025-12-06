#include "logger.h"
#include <fstream>
#include <iostream>
#include <chrono>
#include <ctime>
#include <mutex>

namespace {
    std::ofstream logFile;
    std::mutex logMutex;
    bool initialized = false;

    std::string levelToString(LogLevel level) {
        switch (level) {
            case LogLevel::Info:    return "INFO";
            case LogLevel::Warning: return "WARN";
            case LogLevel::Error:   return "ERROR";
            case LogLevel::Debug:   return "DEBUG";
        }
        return "UNKNOWN";
    }

    std::string currentTimeString() {
        using namespace std::chrono;
        auto now = system_clock::now();
        std::time_t t = system_clock::to_time_t(now);
        std::tm tm{};
    #if defined(_WIN32) || defined(_WIN64)
        localtime_s(&tm, &t); 

    #else
        localtime_r(&t, &tm);   

    #endif
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
        return std::string(buf);
    }

    void ensureInitialized() {
        if (!initialized) {
            logFile.open("log.txt", std::ios::out | std::ios::app);
            initialized = true;
        }
    }
}

void logMessage(LogLevel level, const std::string& msg) {
    std::lock_guard<std::mutex> lock(logMutex);
    ensureInitialized();

    std::string timeStr  = currentTimeString();
    std::string levelStr = levelToString(level);

    std::string full = "[" + timeStr + "][" + levelStr + "] " + msg + "\n";

    if (logFile.is_open()) {
        logFile << full;
        logFile.flush();
    }

    // ВСЁ: в консоль только через stderr, stdout не трогаем
    std::cerr << full;
}

void logInfo(const std::string& msg)    { logMessage(LogLevel::Info, msg); }
void logWarning(const std::string& msg) { logMessage(LogLevel::Warning, msg); }
void logError(const std::string& msg)   { logMessage(LogLevel::Error, msg); }
void logDebug(const std::string& msg)   { logMessage(LogLevel::Debug, msg); }

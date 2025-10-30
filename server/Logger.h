#ifndef LOGGER_H
#define LOGGER_H

#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>
#include <ctime>

class Logger {
public:
    enum Level {
        DEBUG,
        INFO,
        WARNING,
        ERROR
    };

    static void setLevel(Level level) { currentLevel = level; }
    static Level getLevel() { return currentLevel; }

    static void debug(const std::string& message) {
        log(DEBUG, message);
    }

    static void info(const std::string& message) {
        log(INFO, message);
    }

    static void warning(const std::string& message) {
        log(WARNING, message);
    }

    static void error(const std::string& message) {
        log(ERROR, message);
    }

private:
    static Level currentLevel;

    static void log(Level level, const std::string& message) {
        if (level < currentLevel) return;

        std::time_t now = std::time(nullptr);
        std::tm* timeInfo = std::localtime(&now);

        std::ostringstream timestamp;
        timestamp << std::put_time(timeInfo, "%Y-%m-%d %H:%M:%S");

        std::string levelStr;
        switch (level) {
            case DEBUG: levelStr = "DEBUG"; break;
            case INFO: levelStr = "INFO"; break;
            case WARNING: levelStr = "WARNING"; break;
            case ERROR: levelStr = "ERROR"; break;
        }

        std::cout << "[" << timestamp.str() << "] "
                  << "[" << levelStr << "] "
                  << message << std::endl;
    }
};

#endif

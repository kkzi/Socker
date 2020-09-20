#pragma once

#ifdef DISABLE_LOGGER

#include <string>
class __declspec(dllexport) Logger
{
private:
    Logger() = default;
    ~Logger() = default;

    Logger(const Logger&) = default;
    Logger& operator=(const Logger&) = default;

public:
    static void init(const std::string& name, const std::string& level = "debug", int logFileMode = 1) {};
    static void setLevel(const std::string& level) {};
    static void drop() {};
};

#define LOG_RESULT(...)void(0);
#define LOG_TRACE(...) void(0);
#define LOG_DEBUG(...) void(0);
#define LOG_INFO(...)  void(0);
#define LOG_WARN(...)  void(0);
#define LOG_ERROR(...) void(0);
#define LOG_FATAL(...) void(0);
#else

#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG


#if defined BUILD_STATIC || defined(__clang__)
#  define LOGGER_API
#else
#  if defined(__GNUC__) && defined(__unix__)
#    define LOGGER_API __attribute__ ((__visibility__("default")))
#  elif defined (WIN32)
#    if defined(LOGGER_EXPORTS)
#        define LOGGER_API __declspec(dllexport)
#    else
#        define LOGGER_API __declspec(dllimport)
#    endif
#  endif
#endif


#include "spdlog/spdlog.h"
#include <string>
#include <memory>

class LOGGER_API Logger
{
private:
    Logger() = default;
    ~Logger() = default;

    Logger(const Logger&) = default;
    Logger& operator=(const Logger&) = default;

public:
    static std::shared_ptr<spdlog::logger> logger();

public:
    /**
     * @param name: log name, and file name prefix if log file enabled
     * @param level: log level, "trace", "debug", "info", "warning", "error"
     * @param logFileMode: log file mode, 
     *                  0: enable log to console, default.
     *                  1: enable log file and enable rotate.
     *                  2: enable log file but disable rotate.
     *                  4: disable log to console.
     */
    static void init(const std::string& name, const std::string& level = "debug", int logFileMode = 1);
    static void setLevel(const std::string& level);
    static void drop();
};


#define LOG_RESULT(ok, ...) {\
    if (Logger::logger()) {\
        auto level = ok ? spdlog::level::info : spdlog::level::err; \
        SPDLOG_LOGGER_CALL(Logger::logger(), level, __VA_ARGS__); \
    }; \
}

#define LOG_TRACE(...) if(Logger::logger())SPDLOG_LOGGER_TRACE   (Logger::logger(), __VA_ARGS__)
#define LOG_DEBUG(...) if(Logger::logger())SPDLOG_LOGGER_DEBUG   (Logger::logger(), __VA_ARGS__)
#define LOG_INFO(...)  if(Logger::logger())SPDLOG_LOGGER_INFO    (Logger::logger(), __VA_ARGS__)
#define LOG_WARN(...)  if(Logger::logger())SPDLOG_LOGGER_WARN    (Logger::logger(), __VA_ARGS__
#define LOG_FATAL(...) if(Logger::logger())SPDLOG_LOGGER_CRITICAL(Logger::logger(), __VA_ARGS__)

#endif

#pragma once

#include <memory>  // For std::shared_ptr
#define _SILENCE_ALL_MS_EXT_DEPRECATION_WARNINGS

// --- Configuration ---

#ifdef _WIN32
  #pragma warning(push)
  #pragma warning(disable : 4081)
  #pragma warning(disable : 4706)
  #pragma warning(disable : 4996)

  // system header includes
  #pragma warning(pop)
#endif
// Define log levels (consistent with spdlog)
#define LOG_LEVEL_PUBCXX_TRACE 0
#define LOG_LEVEL_PUBCXX_DEBUG 1
#define LOG_LEVEL_PUBCXX_INFO 2
#define LOG_LEVEL_PUBCXX_WARN 3
#define LOG_LEVEL_PUBCXX_ERROR 4
#define LOG_LEVEL_PUBCXX_CRITICAL 5
#define LOG_LEVEL_PUBCXX_OFF 6

// Set via build system (e.g., -DACTIVE_LOG_LEVEL=LOG_LEVEL_PUBCXX_INFO) or
// define here.
#ifndef ACTIVE_LOG_LEVEL
  // Default to INFO level for both debug and release builds
  #define ACTIVE_LOG_LEVEL LOG_LEVEL_PUBCXX_INFO
#endif

// --- spdlog Integration ---

// The ACTIVE_LOG_LEVEL and SPDLOG_ACTIVE_LEVEL should be set via the build
// system. For header-only spdlog, we map our custom log levels to spdlog's
// compile-time levels.
#if ACTIVE_LOG_LEVEL == LOG_LEVEL_PUBCXX_TRACE
  #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#elif ACTIVE_LOG_LEVEL == LOG_LEVEL_PUBCXX_DEBUG
  #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#elif ACTIVE_LOG_LEVEL == LOG_LEVEL_PUBCXX_INFO
  #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_INFO
#elif ACTIVE_LOG_LEVEL == LOG_LEVEL_PUBCXX_WARN
  #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_WARN
#elif ACTIVE_LOG_LEVEL == LOG_LEVEL_PUBCXX_ERROR
  #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_ERROR
#elif ACTIVE_LOG_LEVEL == LOG_LEVEL_PUBCXX_CRITICAL
  #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_CRITICAL
#elif ACTIVE_LOG_LEVEL == LOG_LEVEL_PUBCXX_OFF
  #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_OFF
#endif

#include <spdlog/spdlog.h>

// --- Logger Access Declaration ---

// Declare the function that retrieves the logger instance.
// The definition will be in logging.cpp
std::shared_ptr<spdlog::logger>& get_logger();

// --- Logging Macros ---

// clang-format off
#if ACTIVE_LOG_LEVEL <= LOG_LEVEL_PUBCXX_TRACE
    #define LOG_TRACE(fmt, ...) SPDLOG_LOGGER_TRACE(get_logger(), fmt, ##__VA_ARGS__)
#else
    #define LOG_TRACE(fmt, ...) (void)0
#endif

#if ACTIVE_LOG_LEVEL <= LOG_LEVEL_PUBCXX_DEBUG
    #define LOG_DEBUG(fmt, ...) SPDLOG_LOGGER_DEBUG(get_logger(), fmt, ##__VA_ARGS__)
#else
    #define LOG_DEBUG(fmt, ...) (void)0
#endif

#if ACTIVE_LOG_LEVEL <= LOG_LEVEL_PUBCXX_INFO
    #define LOG_INFO(fmt, ...) SPDLOG_LOGGER_INFO(get_logger(), fmt, ##__VA_ARGS__)
#else
    #define LOG_INFO(fmt, ...) (void)0
#endif

#if ACTIVE_LOG_LEVEL <= LOG_LEVEL_PUBCXX_WARN
    #define LOG_WARN(fmt, ...) SPDLOG_LOGGER_WARN(get_logger(), fmt, ##__VA_ARGS__)
#else
    #define LOG_WARN(fmt, ...) (void)0
#endif

#if ACTIVE_LOG_LEVEL <= LOG_LEVEL_PUBCXX_ERROR
    #define LOG_ERROR(fmt, ...) SPDLOG_LOGGER_ERROR(get_logger(), fmt, ##__VA_ARGS__)
#else
    #define LOG_ERROR(fmt, ...) (void)0
#endif

#if ACTIVE_LOG_LEVEL <= LOG_LEVEL_PUBCXX_CRITICAL
    #define LOG_CRITICAL(fmt, ...) SPDLOG_LOGGER_CRITICAL(get_logger(), fmt, ##__VA_ARGS__)
#else
    #define LOG_CRITICAL(fmt, ...) (void)0
#endif
// clang-format on

// --- Assertion Macro Example (Optional) ---
#ifndef NDEBUG
  #define APP_ASSERT(condition, fmt, ...) \
    do { \
      if (!(condition)) { \
        /* Use the logger macro directly - initialization will be triggered if \
         * needed */ \
        LOG_CRITICAL( \
            "Assertion Failed: ({}) " fmt, #condition, ##__VA_ARGS__); \
        /* Ensure log is flushed before aborting */ \
        if (auto& logger = get_logger()) { \
          logger->flush(); \
        } \
        std::abort(); \
      } \
    } while (0)
#else
  #define APP_ASSERT(condition, fmt, ...) (void)0
#endif

#ifndef LOGGING_CONTROL_HPP
#define LOGGING_CONTROL_HPP

#include "fmtlog.h"
#include <string>

/**
 * @brief PROD-004: Logging control for RuleForge
 *
 * Provides runtime control over log levels without requiring recompilation.
 * Default behavior:
 * - Debug builds: All logs enabled (DBG level)
 * - Release builds: Only warnings and errors (WRN level, set via FMTLOG_ACTIVE_LEVEL=2)
 *
 * Environment variable override:
 * Set FMTLOG_LEVEL environment variable before running:
 *   - DBG or 0: Debug (most verbose)
 *   - INF or 1: Info
 *   - WRN or 2: Warning (default for production)
 *   - ERR or 3: Error only
 *   - OFF or 4: No logging
 *
 * Example:
 *   export FMTLOG_LEVEL=WRN  # or set FMTLOG_LEVEL=2 on Windows
 */

namespace ruleforge {

/**
 * @brief Log levels matching fmtlog levels
 */
enum class LogLevel {
    Debug = 0,   // DBG - Verbose debug information
    Info = 1,    // INF - General information
    Warning = 2, // WRN - Warnings (default for production)
    Error = 3,   // ERR - Errors only
    Off = 4      // OFF - No logging
};

/**
 * @brief Initialize logging system.
 *
 * Call this at application startup to configure logging.
 * Reads FMTLOG_LEVEL environment variable if set.
 *
 * @param default_level Default log level if env var not set
 */
inline void init_logging(LogLevel default_level = LogLevel::Warning) {
    // Check environment variable
    char const* env_level = std::getenv("FMTLOG_LEVEL");
    if (env_level) {
        std::string level_str(env_level);
        if (level_str == "DBG" || level_str == "0") {
            fmtlog::setLogLevel(fmtlog::DBG);
        } else if (level_str == "INF" || level_str == "1") {
            fmtlog::setLogLevel(fmtlog::INF);
        } else if (level_str == "WRN" || level_str == "2") {
            fmtlog::setLogLevel(fmtlog::WRN);
        } else if (level_str == "ERR" || level_str == "3") {
            fmtlog::setLogLevel(fmtlog::ERR);
        } else if (level_str == "OFF" || level_str == "4") {
            fmtlog::setLogLevel(fmtlog::OFF);
        }
    } else {
        fmtlog::setLogLevel(static_cast<fmtlog::LogLevel>(static_cast<int>(default_level)));
    }
}

/**
 * @brief Set log level at runtime
 *
 * @param level The desired log level
 */
inline void set_log_level(LogLevel level) {
    fmtlog::setLogLevel(static_cast<fmtlog::LogLevel>(static_cast<int>(level)));
}

/**
 * @brief Get current log level
 *
 * @return Current log level
 */
inline LogLevel get_log_level() {
    return static_cast<LogLevel>(static_cast<int>(fmtlog::getLogLevel()));
}

/**
 * @brief Check if a log level is enabled
 *
 * Useful for avoiding expensive string formatting when logging is disabled.
 *
 * @param level The log level to check
 * @return true if logging at this level is enabled
 */
inline bool is_log_level_enabled(LogLevel level) {
    return static_cast<int>(level) >= static_cast<int>(get_log_level());
}

/**
 * @brief Log level names for display
 */
inline char const* log_level_name(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:   return "DEBUG";
        case LogLevel::Info:    return "INFO";
        case LogLevel::Warning: return "WARNING";
        case LogLevel::Error:   return "ERROR";
        case LogLevel::Off:     return "OFF";
    }
    return "UNKNOWN";
}

} // namespace ruleforge

#endif // LOGGING_CONTROL_HPP

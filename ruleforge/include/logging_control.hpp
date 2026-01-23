#ifndef LOGGING_CONTROL_HPP
#define LOGGING_CONTROL_HPP

#include "tlog.h"
#include <cstdlib>
#include <string>

/**
 * @brief PROD-004: Logging control for RuleForge
 *
 * Provides runtime control over log levels without requiring recompilation.
 * Uses TurboNet tlog for high-performance async logging.
 *
 * Default behavior:
 * - Debug builds: All logs enabled (DEBUG level)
 * - Release builds: Only warnings and errors (WARN level, set via TLOG_ACTIVE_LEVEL=2)
 *
 * Environment variable override:
 * Set TLOG_LEVEL environment variable before running:
 *   - DEBUG or 0: Debug (most verbose)
 *   - INFO or 1: Info
 *   - WARN or 2: Warning (default for production)
 *   - ERROR or 3: Error only
 *   - FATAL or 4: Fatal only
 */

namespace ruleforge {

// Global logger instance for RuleForge
inline tlog_t* g_ruleforge_logger = nullptr;

/**
 * @brief Initialize logging system.
 *
 * Call this at application startup to configure logging.
 * Reads TLOG_LEVEL environment variable if set.
 *
 * @param default_level Default log level if env var not set
 */
inline void init_logging(turbo_log_level_t default_level = TURBO_LOG_LEVEL_WARN) {
    if (g_ruleforge_logger) return; // Already initialized
    
    tlog_config_t config = {
        .min_level = default_level,
        .async_mode = 1,
        .buffer_size = 64 * 1024
    };
    
    // Check environment variable
    char const* env_level = std::getenv("TLOG_LEVEL");
    if (env_level) {
        std::string level_str(env_level);
        if (level_str == "DEBUG" || level_str == "0") {
            config.min_level = TURBO_LOG_LEVEL_DEBUG;
        } else if (level_str == "INFO" || level_str == "1") {
            config.min_level = TURBO_LOG_LEVEL_INFO;
        } else if (level_str == "WARN" || level_str == "2") {
            config.min_level = TURBO_LOG_LEVEL_WARN;
        } else if (level_str == "ERROR" || level_str == "3") {
            config.min_level = TURBO_LOG_LEVEL_ERROR;
        } else if (level_str == "FATAL" || level_str == "4") {
            config.min_level = TURBO_LOG_LEVEL_FATAL;
        }
    }
    
    g_ruleforge_logger = tlog_create(&config);
    
    // Add console sink with colors
    turbo_console_sink_opts_t console_opts = {
        .output = stderr,
        .use_colors = 1,
        .pattern = "[{time}] [{level}] {message}"
    };
    tlog_add_sink(g_ruleforge_logger, turbo_sink_console_create(&console_opts));
    
    tlog_set_default(g_ruleforge_logger);
}

/**
 * @brief Cleanup logging system.
 * Call at application shutdown.
 */
inline void cleanup_logging() {
    if (g_ruleforge_logger) {
        tlog_flush(g_ruleforge_logger);
        tlog_destroy(g_ruleforge_logger);
        g_ruleforge_logger = nullptr;
    }
}

/**
 * @brief Set log level at runtime
 *
 * @param level The desired log level
 */
inline void set_log_level(turbo_log_level_t level) {
    if (g_ruleforge_logger) {
        tlog_set_level(g_ruleforge_logger, level);
    }
}

/**
 * @brief Get current log level
 *
 * @return Current log level
 */
inline turbo_log_level_t get_log_level() {
    if (g_ruleforge_logger) {
        return tlog_get_level(g_ruleforge_logger);
    }
    return TURBO_LOG_LEVEL_WARN;
}

/**
 * @brief Check if a log level is enabled
 *
 * Useful for avoiding expensive string formatting when logging is disabled.
 *
 * @param level The log level to check
 * @return true if logging at this level is enabled
 */
inline bool is_log_level_enabled(turbo_log_level_t level) {
    return level >= get_log_level();
}

/**
 * @brief Log level names for display
 */
inline char const* log_level_name(turbo_log_level_t level) {
    return turbo_log_level_name(level);
}

} // namespace ruleforge

// Convenience macros mapping to tlog - use these in RuleForge code
#define logd(...) TLOG_DEBUG(__VA_ARGS__)
#define logi(...) TLOG_INFO(__VA_ARGS__)
#define logw(...) TLOG_WARN(__VA_ARGS__)
#define loge(...) TLOG_ERROR(__VA_ARGS__)

#endif // LOGGING_CONTROL_HPP

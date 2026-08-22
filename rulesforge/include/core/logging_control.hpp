#ifndef LOGGING_CONTROL_HPP
#define LOGGING_CONTROL_HPP

#include "tlog.h"

#ifdef __cplusplus
#include <sstream>
#include <string>
#include <utility>

namespace rulesforge::logging_detail {

template <typename T>
std::string to_log_string(T&& value) {
    std::ostringstream stream;
    stream << std::forward<T>(value);
    return stream.str();
}

inline void replace_next_placeholder(std::string&) {}

template <typename T, typename... Rest>
void replace_next_placeholder(std::string& message, T&& value, Rest&&... rest) {
    std::string replacement = to_log_string(std::forward<T>(value));
    std::size_t const pos = message.find("{}");
    if (pos == std::string::npos) {
        message += " ";
        message += replacement;
    } else {
        message.replace(pos, 2, replacement);
    }
    if constexpr (sizeof...(Rest) > 0) {
        replace_next_placeholder(message, std::forward<Rest>(rest)...);
    }
}

template <typename... Args>
std::string format_message(char const* format, Args&&... args) {
    std::string message = format != nullptr ? format : "";
    if constexpr (sizeof...(Args) > 0) {
        replace_next_placeholder(message, std::forward<Args>(args)...);
    }
    return message;
}

template <typename... Args>
void debug(char const* format, Args&&... args) {
    std::string message = format_message(format, std::forward<Args>(args)...);
    TLOG_DEBUG(message.c_str());
}

template <typename... Args>
void info(char const* format, Args&&... args) {
    std::string message = format_message(format, std::forward<Args>(args)...);
    TLOG_INFO(message.c_str());
}

template <typename... Args>
void warn(char const* format, Args&&... args) {
    std::string message = format_message(format, std::forward<Args>(args)...);
    TLOG_WARN(message.c_str());
}

template <typename... Args>
void error(char const* format, Args&&... args) {
    std::string message = format_message(format, std::forward<Args>(args)...);
    TLOG_ERROR(message.c_str());
}

} // namespace rulesforge::logging_detail

// Short aliases for internal use
#define logd(...) ::rulesforge::logging_detail::debug(__VA_ARGS__)
#define logi(...) ::rulesforge::logging_detail::info(__VA_ARGS__)
#define logw(...) ::rulesforge::logging_detail::warn(__VA_ARGS__)
#define loge(...) ::rulesforge::logging_detail::error(__VA_ARGS__)
#else
#define logd(...) TLOG_DEBUG(__VA_ARGS__)
#define logi(...) TLOG_INFO(__VA_ARGS__)
#define logw(...) TLOG_WARN(__VA_ARGS__)
#define loge(...) TLOG_ERROR(__VA_ARGS__)
#endif

#endif // LOGGING_CONTROL_HPP

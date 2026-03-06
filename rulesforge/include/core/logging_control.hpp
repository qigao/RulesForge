#ifndef LOGGING_CONTROL_HPP
#define LOGGING_CONTROL_HPP

#include "tlog.h"

// Short aliases for internal use
#define logd(...) TLOG_DEBUG(__VA_ARGS__)
#define logi(...) TLOG_INFO(__VA_ARGS__)
#define logw(...) TLOG_WARN(__VA_ARGS__)
#define loge(...) TLOG_ERROR(__VA_ARGS__)

#endif // LOGGING_CONTROL_HPP

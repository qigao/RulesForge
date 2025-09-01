#include "pubcxx/logger.hpp"

// Define the get_logger function
std::shared_ptr<spdlog::logger>& get_logger() {
    static std::shared_ptr<spdlog::logger> logger = spdlog::default_logger();
    return logger;
}

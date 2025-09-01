#include "pubcxx/logger.hpp"   // Include our logging header

#include <string>
#include <thread>   // Include for threading example
#include <vector>

void some_function() {
    LOG_DEBUG("Entering some_function");
    // ... function logic ...
    LOG_TRACE("Performing detailed step");
    // ...
    LOG_DEBUG("Exiting some_function");
}

void thread_func(int id) {
    LOG_INFO("Thread {} starting.", id);
    LOG_DEBUG("Some debug work in thread {}.", id);
    LOG_INFO("Thread {} finished.", id);
}

int main() {

    LOG_INFO("Application starting...");   // First log call triggers initialization

    int value = 42;
    std::string name = "World";
    std::vector<int> data = {1, 2, 3};

    LOG_TRACE("This is a trace message. Value = {}", value);
    LOG_DEBUG("Debugging info: Name = '{}'", name);
    LOG_INFO("Informational message. Data size = {}", data.size());
    LOG_WARN("This is a warning message. Something might be wrong.");
    LOG_ERROR("An error occurred! Error code = {}", -1);
    LOG_CRITICAL("Critical failure! User = {}", name);

    some_function();

    // Test thread safety of initialization
    std::vector<std::thread> threads;
    for (int i = 0; i < 5; ++i) { threads.emplace_back(thread_func, i + 1); }
    for (auto& t : threads) { t.join(); }

    APP_ASSERT(value > 0, "Value must be positive, but got {}", value);

    LOG_INFO("Application finished.");

    return 0;
}

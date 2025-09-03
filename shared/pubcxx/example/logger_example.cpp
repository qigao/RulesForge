#include "pubcxx/logger.hpp"   // Include our logging header

#include <string>
#include <thread>   // Include for threading example
#include <vector>

void some_function() {
    logd("Entering some_function");
    // ... function logic ...
    LOG_TRACE("Performing detailed step");
    // ...
    logd("Exiting some_function");
}

void thread_func(int id) {
    logi("Thread {} starting.", id);
    logd("Some debug work in thread {}.", id);
    logi("Thread {} finished.", id);
}

int main() {

    logi("Application starting...");   // First log call triggers initialization

    int value = 42;
    std::string name = "World";
    std::vector<int> data = {1, 2, 3};

    LOG_TRACE("This is a trace message. Value = {}", value);
    logd("Debugging info: Name = '{}'", name);
    logi("Informational message. Data size = {}", data.size());
    logw("This is a warning message. Something might be wrong.");
    loge("An error occurred! Error code = {}", -1);
    LOG_CRITICAL("Critical failure! User = {}", name);

    some_function();

    // Test thread safety of initialization
    std::vector<std::thread> threads;
    for (int i = 0; i < 5; ++i) { threads.emplace_back(thread_func, i + 1); }
    for (auto& t : threads) { t.join(); }

    APP_ASSERT(value > 0, "Value must be positive, but got {}", value);

    logi("Application finished.");

    return 0;
}

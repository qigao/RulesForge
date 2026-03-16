#include "engine/priority_disruptor.hpp"
#include "engine/priority_types.hpp"

#include <array>
#include <iostream>

int main() {
    std::cout << "Testing PriorityDisruptor..." << std::endl;

    // Check PriorityEntry size
    std::cout << "PriorityEntry size: " << sizeof(PriorityEntry) << " bytes" << std::endl;

    // Create disruptor
    std::cout << "Creating PriorityDisruptor..." << std::endl;
    PriorityDisruptor queue;
    std::cout << "PriorityDisruptor created successfully" << std::endl;

    // Try to enqueue
    std::cout << "Trying to enqueue..." << std::endl;
    bool result = queue.try_enqueue(Priority::LOW, 100);
    std::cout << "try_enqueue result: " << (result ? "SUCCESS" : "FAILED") << std::endl;

    if (result) {
        std::cout << "Trying to dequeue..." << std::endl;
        auto hash = queue.try_dequeue();
        if (hash) {
            std::cout << "Dequeued successfully: hash=" << *hash << std::endl;
        } else {
            std::cout << "Dequeue failed" << std::endl;
        }
    }
    std::cout << "Test completed" << std::endl;

    return 0;
}

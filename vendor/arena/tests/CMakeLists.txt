# Memory module tests

# Test for simple memory pool
add_executable(test_memory_pool
    test_memory_pool.c
)

target_link_libraries(test_memory_pool PRIVATE
    TurboNet::Memory
)

add_test(NAME MemoryPool COMMAND test_memory_pool)

set_target_properties(test_memory_pool PROPERTIES
    FOLDER "memory/tests"
)

# Test for arena buffer
add_executable(test_arena_buffer
    test_arena_buffer.c
)

target_link_libraries(test_arena_buffer PRIVATE
    TurboNet::Memory
    unity
)

add_test(NAME ArenaBuffer COMMAND test_arena_buffer)

set_target_properties(test_arena_buffer PROPERTIES
    FOLDER "memory/tests"
)

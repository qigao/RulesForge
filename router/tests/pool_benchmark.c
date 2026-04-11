/**
 * @file pool_benchmark.c
 * @brief Object pool performance benchmark
 */

#include "../session_pool.h"
#include "../buffer_pool.h"
#include "rule_forge.h"
#include "turbo_thread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define usleep(us) Sleep((us) / 1000)
#endif

// Get current time in nanoseconds
static uint64_t get_time_ns() {
#ifdef _WIN32
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (counter.QuadPart * 1000000000ULL) / freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000ULL + ts.tv_nsec;
#endif
}

// Benchmark object pool performance
void benchmark_object_pool() {
    printf("=== Object Pool Benchmark ===\n\n");

    // 1. Test Session Pool
    printf("1. Session Pool Performance\n");

    ruleforge_knowledge_base_t kb;
    ruleforge_kb_create(&kb);

    session_pool_t* sp = session_pool_create(kb, 16);

    int iterations = 10000;

    // Without pool: create + destroy
    uint64_t start = get_time_ns();
    for (int i = 0; i < iterations; i++) {
        ruleforge_stateful_session_t session;
        ruleforge_session_create(kb, &session);
        ruleforge_session_destroy(session);
    }
    uint64_t no_pool_ns = get_time_ns() - start;

    printf("  Without pool: %llu us (%llu ns/op)\n",
           (unsigned long long)(no_pool_ns / 1000),
           (unsigned long long)(no_pool_ns / iterations));

    // With pool: acquire + release
    start = get_time_ns();
    for (int i = 0; i < iterations; i++) {
        ruleforge_stateful_session_t session = session_pool_acquire(sp, -1);
        session_pool_release(sp, session);
    }
    uint64_t with_pool_ns = get_time_ns() - start;

    printf("  With pool:    %llu us (%llu ns/op)\n",
           (unsigned long long)(with_pool_ns / 1000),
           (unsigned long long)(with_pool_ns / iterations));
    printf("  Speedup:      %.2fx\n\n",
           (double)no_pool_ns / with_pool_ns);

    session_pool_destroy(sp);
    ruleforge_kb_destroy(kb);

    // 2. Test Buffer Pool
    printf("2. Buffer Pool Performance\n");

    buffer_pool_t* bp = buffer_pool_create(1024 * 1024, 16);

    // Without pool: malloc + free
    start = get_time_ns();
    for (int i = 0; i < iterations; i++) {
        uint8_t* buffer = malloc(1024 * 1024);
        free(buffer);
    }
    no_pool_ns = get_time_ns() - start;

    printf("  Without pool: %llu us (%llu ns/op)\n",
           (unsigned long long)(no_pool_ns / 1000),
           (unsigned long long)(no_pool_ns / iterations));

    // With pool: acquire + release
    start = get_time_ns();
    for (int i = 0; i < iterations; i++) {
        buffer_t* buffer = buffer_pool_acquire(bp, -1);
        buffer_pool_release(bp, buffer);
    }
    with_pool_ns = get_time_ns() - start;

    printf("  With pool:    %llu us (%llu ns/op)\n",
           (unsigned long long)(with_pool_ns / 1000),
           (unsigned long long)(with_pool_ns / iterations));
    printf("  Speedup:      %.2fx\n\n",
           (double)no_pool_ns / with_pool_ns);

    buffer_pool_destroy(bp);
}

// Multi-threaded stress test
typedef struct {
    session_pool_t* pool;
    int iterations;
    int thread_id;
} thread_args_t;

void thread_worker(void* arg) {
    thread_args_t* args = (thread_args_t*)arg;

    for (int i = 0; i < args->iterations; i++) {
        ruleforge_stateful_session_t session = session_pool_acquire(args->pool, -1);
        turbo_sleep_ms(0); // yield
        session_pool_release(args->pool, session);
    }
}

void benchmark_concurrent_access() {
    printf("=== Concurrent Access Benchmark ===\n\n");

    ruleforge_knowledge_base_t kb;
    ruleforge_kb_create(&kb);

    session_pool_t* sp = session_pool_create(kb, 16);

    int num_threads = 8;
    int iterations_per_thread = 1000;

    turbo_thread_t threads[8];
    thread_args_t args[8];

    uint64_t start = get_time_ns();

    for (int i = 0; i < num_threads; i++) {
        args[i].pool = sp;
        args[i].iterations = iterations_per_thread;
        args[i].thread_id = i;
        turbo_thread_create(&threads[i], thread_worker, &args[i]);
    }

    for (int i = 0; i < num_threads; i++) {
        turbo_thread_join(&threads[i]);
    }

    uint64_t elapsed_ns = get_time_ns() - start;
    int total_ops = num_threads * iterations_per_thread;

    printf("  Threads:      %d\n", num_threads);
    printf("  Total ops:    %d\n", total_ops);
    printf("  Elapsed:      %llu us\n", (unsigned long long)(elapsed_ns / 1000));
    printf("  Throughput:   %.2f ops/s\n",
           (double)total_ops / elapsed_ns * 1000000000.0);

    pool_stats_t stats;
    session_pool_get_stats(sp, &stats);

    printf("\n  Pool stats:\n");
    printf("    Total acquires:   %llu\n", (unsigned long long)stats.total_acquires);
    printf("    Total releases:   %llu\n", (unsigned long long)stats.total_releases);
    printf("    Acquire failures: %llu\n", (unsigned long long)stats.acquire_failures);

    session_pool_destroy(sp);
    ruleforge_kb_destroy(kb);
}

int main() {
    printf("\n");
    printf("╔════════════════════════════════════════╗\n");
    printf("║   RulesForge Object Pool Benchmark    ║\n");
    printf("╚════════════════════════════════════════╝\n");
    printf("\n");

    benchmark_object_pool();
    benchmark_concurrent_access();

    printf("Benchmark completed.\n\n");

    return 0;
}

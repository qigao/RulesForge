#include "engine/knowledge_base.hpp"
#include "engine/stateful_session.hpp"
#include "parser/rfl_parser.hpp"
#include "tinytest.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
std::vector<std::string> g_captured_args;
int g_invoke_count = 0;
int g_metric_count = 0;

int capture_native(void* ctx, int argc, const char** argv, char** out_result) {
    (void)ctx;
    g_captured_args.clear();
    for (int i = 0; i < argc; ++i) {
        g_captured_args.emplace_back(argv[i] ? argv[i] : "");
    }
    g_invoke_count++;
    if (out_result) {
        *out_result = nullptr;
    }
    return 0;
}

int metric_native(void* ctx, int argc, const char** argv, char** out_result) {
    (void)ctx;
    g_metric_count++;
    if (argc > 0 && argv && argv[0]) {
        size_t n = std::strlen(argv[0]);
        auto* p = static_cast<char*>(std::malloc(n + 1));
        if (p) {
            std::memcpy(p, argv[0], n + 1);
            *out_result = p;
            return 0;
        }
    }
    if (out_result) *out_result = nullptr;
    return 0;
}
}  // namespace

suite("RHS Invoke") {
    it("invokes registered native function from RHS and uses return value in assignment") {
        g_captured_args.clear();
        g_invoke_count = 0;
        g_metric_count = 0;

        ParsingResult result;
        auto kb = build_knowledge_base(R"(
            declare Sensor
                id: String
                value: double
            end
            declare Alert
                score: double
            end

            rule "Invoke Native"
            when
                $s: Sensor(value > 10)
            then
                invoke capture($s.value, "hot", true)
                insert Alert { score = metric($s.value) }
            end
        )", result);

        check(result.success);
        check(kb != nullptr);
        kb->register_native_function("capture", capture_native, nullptr);
        kb->register_native_function("metric", metric_native, nullptr);

        auto session = kb->create_session();
        check(session != nullptr);

        auto sensor = std::make_shared<Fact>();
        sensor->type = "Sensor";
        sensor->fields["id"] = std::string("s-1");
        sensor->fields["value"] = 12.5;
        session->add_fact(sensor);

        int fired = session->fire_all_rules();
        check(fired == 1);
        check(g_invoke_count == 1);
        check(g_metric_count == 1);
        check(g_captured_args.size() == 3);
        check(g_captured_args[0] == "12.500000");
        check(g_captured_args[1] == "\"hot\"");
        check(g_captured_args[2] == "1");
    }
}

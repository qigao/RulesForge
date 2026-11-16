#include "runtime/session_pool.hpp"
#include "rfl_parser.hpp"
#include "tinytest.hpp"
#include "rete/compiler/compiled_network.hpp"
#include <stdexcept>

namespace {
std::shared_ptr<KnowledgeBase> pool_kb() {
    ParsingResult result;
    auto kb = build_knowledge_base("declare Message\n id: int\nend\n", result);
    if (!result.success) throw std::runtime_error("Invalid pool test schema");
    return kb;
}
// Creation fails at the actual network-priming boundary, without changing the
// production factory or replacing allocation/exception behavior.
struct PrimeProbe final : ReteNode {
    int calls = 0, fail_on = 0;
    std::function<void()> on_prime;
    PrimeProbe() : ReteNode(NodeKind::Eval) {}
    void left_activate(StatefulSession&, Token const&) override {
        if (++calls == fail_on) throw std::runtime_error("Injected network priming failure");
        if (on_prime) on_prime();
    }
    void right_activate(StatefulSession&, Fact*, PropagationType) override {}
    void print_node(std::ostream& out) const override { out << "PrimeProbe"; }
};
struct ProbeScope {
    CompiledNetwork& network;
    ProbeScope(KnowledgeBase& kb, PrimeProbe& probe)
        : network(const_cast<CompiledNetwork&>(kb.network())) { network.beta_root_nodes.push_back(&probe); }
    ~ProbeScope() { network.beta_root_nodes.pop_back(); }
};
} // namespace

suite("Session Pool") {
    it("primes replacements without holding the pool mutex") {
        auto kb = pool_kb();
        PrimeProbe probe;
        ProbeScope scope(*kb, probe);
        rulesforge::SessionPool pool(kb, 1);
        auto* lease = pool.acquire();
        size_t observed = 99;
        probe.on_prime = [&] { observed = pool.available_count(); };
        pool.release(lease);
        check_equal(observed, 0u);
        check_equal(pool.available_count(), 1u);
    }
    it("rejects a missing knowledge base or a zero-capacity pool") {
        check_throws_as(rulesforge::SessionPool(nullptr, 1), std::invalid_argument);
        check_throws_as(rulesforge::SessionPool(pool_kb(), 0), std::invalid_argument);
    }
    it("propagates partial construction failure instead of shrinking the pool") {
        auto kb = pool_kb();
        PrimeProbe probe;
        ProbeScope scope(*kb, probe);
        probe.fail_on = 2;
        check_throws_with(rulesforge::SessionPool(kb, 4), "Injected network priming failure");
        check_equal(probe.calls, 2);
    }
    it("does not recycle a lease when replacement creation fails") {
        auto kb = pool_kb();
        PrimeProbe probe;
        ProbeScope scope(*kb, probe);
        rulesforge::SessionPool pool(kb, 1);
        auto* lease = pool.acquire();
        probe.fail_on = 2;
        check_throws_with(pool.release(lease), "Injected network priming failure");
        check_equal(pool.available_count(), 0u);
        check_equal(pool.in_use_count(), 1u);
        check_equal(pool.acquire(false), static_cast<StatefulSession*>(nullptr));
        probe.fail_on = 0;
        pool.release(lease);
        check_equal(pool.available_count(), 1u);
        check_equal(pool.in_use_count(), 0u);
    }
    it("rejects null foreign and already-returned leases without changing capacity") {
        auto kb = pool_kb();
        rulesforge::SessionPool pool(kb, 1);
        auto other = kb->create_session();
        check_throws_as(pool.release(nullptr), std::invalid_argument);
        check_throws_as(pool.release(other.get()), std::invalid_argument);
        auto* lease = pool.acquire();
        pool.release(lease);
        check_throws_as(pool.release(lease), std::invalid_argument);
        check_equal(pool.total_size(), 1u);
        check_equal(pool.available_count(), 1u);
    }
    group("Isolation") {
        it("returns a clean session after release") {
            std::string drl = R"(
                declare Message
                    id: int
                end
            )";

            ParsingResult result;
            auto kb = build_knowledge_base(drl, result);
            check(result.success);
            check(kb != nullptr);

            rulesforge::SessionPool pool(kb, 1);

            {
                rulesforge::SessionGuard guard(pool);
                check(static_cast<bool>(guard));

                Fact* fact = guard->create_fact("Message");
                fact->fields["id"] = static_cast<int64_t>(1);
                guard->add_fact(fact);
                check(guard->get_fact_count() == 1);
            }

            {
                rulesforge::SessionGuard guard(pool);
                check(static_cast<bool>(guard));
                check(guard->get_fact_count() == 0);
                check(guard->fire_all_rules() == 0);
            }
        }
    }
}

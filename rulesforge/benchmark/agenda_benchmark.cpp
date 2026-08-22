#include "engine/agenda.hpp"
#include "core/parsed_rule.hpp"

#include "tinytest.hpp"

#include <vector>

namespace {

constexpr size_t operation_count = 10000;

std::vector<ParsedRule> make_mock_rules(size_t count) {
    std::vector<ParsedRule> rules(count);
    for (size_t i = 0; i < count; ++i) {
        rules[i].salience = static_cast<int>(i % 2000) - 1000;
        rules[i].duration = 0;
        rules[i].activation_group = std::nullopt;
        rules[i].agenda_group = std::nullopt;
        rules[i].lock_on_active = false;
    }
    return rules;
}

Activation make_activation(size_t hash, ParsedRule const& rule) {
    Activation activation{};
    activation.hash_value = hash;
    activation.rule = &rule;
    return activation;
}

void fill_agenda(Agenda& agenda, std::vector<ParsedRule> const& rules) {
    for (size_t i = 0; i < rules.size(); ++i) {
        agenda.add(make_activation(i, rules[i]));
    }
}

} // namespace

suite("Agenda Benchmarks") {
    bench("Agenda") {
        auto add_rules = make_mock_rules(operation_count);
        Agenda agenda;
        benchmark_ops("add 10k operations", 1, operation_count) {
            fill_agenda(agenda, add_rules);
        }
        check_equal(agenda.size(), operation_count);

        auto pop_rules = make_mock_rules(operation_count);
        Agenda pop_agenda;
        fill_agenda(pop_agenda, pop_rules);
        size_t popped = 0;
        benchmark_ops("pop 10k operations", 1, operation_count) {
            while (pop_agenda.pop_next()) {
                ++popped;
            }
        }
        check_equal(popped, operation_count);
        check_equal(pop_agenda.size(), 0);

        auto mixed_rules = make_mock_rules(operation_count);
        Agenda mixed_agenda;
        constexpr size_t mixed_operations = 15000;
        benchmark_ops("mixed 10k add + 5k pop", 1, mixed_operations) {
            for (size_t j = 0; j < 1000; ++j) {
                for (size_t i = 0; i < 10; ++i) {
                    size_t hash = j * 10 + i;
                    mixed_agenda.add(make_activation(hash, mixed_rules[hash]));
                }
                for (size_t i = 0; i < 5; ++i) {
                    mixed_agenda.pop_next();
                }
            }
        }
        check_equal(mixed_agenda.size(), 5000);

        auto batch_rules = make_mock_rules(operation_count);
        Agenda batch_agenda;
        fill_agenda(batch_agenda, batch_rules);
        size_t batch_popped = 0;
        benchmark_ops("batch pop 10k operations", 1, operation_count) {
            for (size_t i = 0; i < 100; ++i) {
                batch_popped += batch_agenda.pop_next_batch_activations(100).size();
            }
        }
        check_equal(batch_popped, operation_count);
        check_equal(batch_agenda.size(), 0);
    }
}

#include "engine/agenda.hpp"
#include "core/parsed_rule.hpp"

#include "tinytest.h"

#include <array>
#include <vector>

namespace {

ParsedRule make_mock_rule(int salience) {
    ParsedRule rule;
    rule.salience = salience;
    rule.duration = 0;
    rule.activation_group = std::nullopt;
    rule.agenda_group = std::nullopt;
    rule.lock_on_active = false;
    return rule;
}

Activation make_activation(size_t hash, ParsedRule const& rule) {
    Activation activation{};
    activation.hash_value = hash;
    activation.rule = &rule;
    return activation;
}

} // namespace

suite("Agenda") {
    it("adds and pops every activation") {
        constexpr size_t activation_count = 10000;
        std::vector<ParsedRule> rules(activation_count);
        Agenda agenda;

        for (size_t i = 0; i < activation_count; ++i) {
            rules[i] = make_mock_rule(static_cast<int>(i % 2000) - 1000);
            agenda.add(make_activation(i, rules[i]));
        }

        size_t popped = 0;
        while (agenda.pop_next()) {
            ++popped;
        }

        check_size_eq(popped, activation_count);
        check_size_eq(agenda.size(), 0);
    }

    it("uses exact salience ordering") {
        std::array<ParsedRule, 3> rules = {
            make_mock_rule(-1), make_mock_rule(1), make_mock_rule(0)};
        Agenda agenda;
        agenda.add(make_activation(1, rules[0]));
        agenda.add(make_activation(2, rules[1]));
        agenda.add(make_activation(3, rules[2]));

        auto first = agenda.pop_next();
        auto second = agenda.pop_next();
        auto third = agenda.pop_next();

        check_true(first.has_value());
        check_true(second.has_value());
        check_true(third.has_value());
        if (first && second && third) {
            check_size_eq(first->hash_value, 2);
            check_size_eq(second->hash_value, 3);
            check_size_eq(third->hash_value, 1);
        }
    }
}

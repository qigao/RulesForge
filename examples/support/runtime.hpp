#pragma once

#include "rules_forge.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <span>
#include <stdexcept>
#include <string_view>

namespace scenario_support {

inline constexpr std::size_t max_decisions = 64;
inline constexpr std::size_t max_input_records = 128;
inline constexpr std::size_t text_capacity = 96;

inline void require(ruleforge_status_t status) {
    if (status != RULES_FORGE_OK)
        throw std::runtime_error(ruleforge_get_last_error_message());
}
inline void release(ruleforge_status_t status) noexcept {
    if (status != RULES_FORGE_OK) {
        std::fprintf(stderr, "Scenario cleanup failed: %s\n", ruleforge_get_last_error_message());
        std::terminate();
    }
}

struct Text {
    std::array<char, text_capacity> bytes{};
    std::size_t length = 0;
    Text() = default;
    explicit Text(std::string_view source) {
        if (source.size() >= bytes.size() || source.find('\0') != std::string_view::npos)
            throw std::length_error("Mock API text exceeds its contract");
        std::copy(source.begin(), source.end(), bytes.begin());
        length = source.size();
    }
    std::string_view view() const noexcept { return {bytes.data(), length}; }
    char const* c_str() const noexcept { return bytes.data(); }
    bool operator==(Text const& other) const noexcept { return view() == other.view(); }
};

struct Decision {
    Text id, action, reason, target;
    double metric = 0;
    bool operator==(Decision const&) const = default;
};
struct DecisionBatch {
    std::array<Decision, max_decisions> values{};
    std::size_t count = 0;
    std::span<Decision const> rows() const noexcept { return {values.data(), count}; }
};

// The mock input API borrows immutable, NUL-terminated records. It never retains
// engine handles, and insertion copies/binds each payload synchronously.
struct FactInput {
    char const* type;
    char const* json;
    bool checkpoint = false;
};
class MockInputApi {
    std::span<FactInput const> records_;
public:
    bool fail_fetch = false;
    explicit MockInputApi(std::span<FactInput const> records) : records_(records) {
        if (records.size() > max_input_records)
            throw std::length_error("Mock input record capacity exceeded");
    }
    std::span<FactInput const> fetch() const {
        if (fail_fetch) throw std::runtime_error("Mock input API failed");
        return records_;
    }
};

struct Program {
    ruleforge_program_t handle = nullptr;
    explicit Program(char const* bundle) { require(ruleforge_program_create(bundle, &handle)); }
    Program(Program const&) = delete;
    Program& operator=(Program const&) = delete;
    ~Program() {
        release(ruleforge_program_close(handle));
        release(ruleforge_program_destroy(handle));
    }
};
struct Query {
    ruleforge_query_result_t handle = nullptr;
    Query(ruleforge_stateful_session_t session, char const* name) {
        require(ruleforge_session_query(session, name, &handle));
    }
    Query(Query const&) = delete;
    Query& operator=(Query const&) = delete;
    ~Query() { release(ruleforge_query_result_destroy(handle)); }
    int size() const {
        int count = ruleforge_query_result_get_size(handle);
        if (count < 0) throw std::runtime_error(ruleforge_get_last_error_message());
        return count;
    }
    ruleforge_fact_t row(int index) const {
        ruleforge_fact_t fact = nullptr;
        require(ruleforge_query_result_get_fact_at_index(handle, index, "row", &fact));
        return fact;
    }
};
inline Text text(ruleforge_fact_t fact, char const* field) {
    Text result;
    require(ruleforge_fact_get_field_as_string(fact, field, result.bytes.data(),
                                               result.bytes.size(), &result.length));
    return result;
}
inline double number(ruleforge_fact_t fact, char const* field) {
    double value = 0;
    require(ruleforge_fact_get_field_as_double(fact, field, &value));
    return value;
}

struct Session {
    ruleforge_stateful_session_t handle = nullptr;
    explicit Session(Program& program) { require(ruleforge_program_create_session(program.handle, &handle)); }
    Session(Session const&) = delete;
    Session& operator=(Session const&) = delete;
    ~Session() { release(ruleforge_session_destroy(handle)); }
    void insert(char const* type, char const* json) {
        require(ruleforge_session_add_fact_json(handle, type, json, nullptr));
    }
    void fire() {
        int fired = 0;
        require(ruleforge_session_fire_all_rules(handle, 1000, &fired));
    }
    void load(MockInputApi const& source) {
        for (auto const& record : source.fetch()) {
            insert(record.type, record.json);
            if (record.checkpoint) fire();
        }
        fire();
    }
    DecisionBatch decisions() const {
        Query query(handle, "Decisions");
        int count = query.size();
        if (count == 0) throw std::runtime_error("Scenario produced no decision");
        if (static_cast<std::size_t>(count) > max_decisions)
            throw std::length_error("Scenario decision batch capacity exceeded");
        DecisionBatch batch;
        for (int index = 0; index < count; ++index) {
            auto fact = query.row(index);
            batch.values[batch.count++] = {text(fact, "id"), text(fact, "action"),
                text(fact, "reason"), text(fact, "target"), number(fact, "metric")};
        }
        return batch;
    }
};

// Single-threaded, fixed-capacity state. A commit stages the entire table before
// publishing it; failed commits cannot leave partial decisions or acknowledgements.
class MockDecisionApi {
public:
    struct Entry { Decision decision; bool delivered = false; };
private:
    std::array<Entry, max_decisions> entries_{};
    std::size_t count_ = 0;
public:
    bool fail_commit = false;
    std::size_t size() const noexcept { return count_; }
    std::span<Entry const> rows() const noexcept { return {entries_.data(), count_}; }
    Decision const& get(std::string_view id) const {
        for (auto const& entry : rows())
            if (entry.decision.id.view() == id) return entry.decision;
        throw std::out_of_range("Mock decision ID was not found");
    }
    void commit(std::span<Decision const> batch) {
        if (fail_commit) throw std::runtime_error("Mock persistence API failed");
        auto staged = entries_;
        auto staged_count = count_;
        for (auto const& decision : batch) {
            if (decision.id.view().empty() || !std::isfinite(decision.metric))
                throw std::invalid_argument("Invalid persisted decision");
            std::size_t index = 0;
            while (index < staged_count && staged[index].decision.id != decision.id) ++index;
            if (index < staged_count) {
                if (!(staged[index].decision == decision))
                    throw std::runtime_error("Conflicting replay of a mock decision ID");
                continue;
            }
            if (staged_count == staged.size())
                throw std::length_error("Mock persistence capacity exceeded");
            staged[staged_count++] = {decision, false};
        }
        entries_ = staged;
        count_ = staged_count;
    }
    template<class ActionApi>
    void deliver(ActionApi& sink) {
        for (std::size_t index = 0; index < count_; ++index) {
            auto& entry = entries_[index];
            if (entry.delivered) continue;
            sink.send(entry.decision);
            entry.delivered = true;
        }
    }
};

class MockActionApi {
    std::array<Decision, max_decisions> accepted_{};
    std::size_t count_ = 0;
public:
    bool fail_send = false;
    std::size_t size() const noexcept { return count_; }
    void send(Decision const& decision) {
        if (fail_send) throw std::runtime_error("Mock action API failed");
        for (std::size_t index = 0; index < count_; ++index) {
            if (accepted_[index].id != decision.id) continue;
            if (!(accepted_[index] == decision))
                throw std::runtime_error("Conflicting mock action ID");
            return;
        }
        if (count_ == accepted_.size()) throw std::length_error("Mock action capacity exceeded");
        accepted_[count_++] = decision;
    }
};

inline DecisionBatch execute(Program& program, MockInputApi const& source) {
    Session session(program);
    session.load(source);
    return session.decisions();
}
inline void evaluate(Program& program, MockInputApi const& source,
                     MockDecisionApi& repository, MockActionApi& actions) {
    auto batch = execute(program, source);
    repository.commit(batch.rows());
    repository.deliver(actions);
}
inline int demo_main(int argc, char** argv, char const* name, std::span<FactInput const> input,
                     std::size_t expected_decisions) {
    if (argc != 2) {
        std::fprintf(stderr, "Usage: %s <bundle.json>\n", name);
        return 2;
    }
    try {
        Program program(argv[1]);
        MockInputApi source(input);
        MockDecisionApi repository;
        MockActionApi actions;
        // A fresh session replays the same input against persistent mock state.
        for (int replay = 0; replay < 2; ++replay) {
            auto batch = execute(program, source);
            if (batch.count != expected_decisions)
                throw std::runtime_error("Scenario did not produce every expected decision");
            repository.commit(batch.rows());
            repository.deliver(actions);
        }
        for (auto const& entry : repository.rows()) {
            auto const& row = entry.decision;
            std::printf("%s id=%s action=%s target=%s reason=%s metric=%.9g\n",
                name, row.id.c_str(), row.action.c_str(), row.target.c_str(),
                row.reason.c_str(), row.metric);
        }
        std::printf("persisted=%zu delivered=%zu replay=idempotent\n", repository.size(), actions.size());
        return 0;
    } catch (std::exception const& error) {
        std::fprintf(stderr, "%s: %s\n", name, error.what());
        return 1;
    }
}
} // namespace scenario_support

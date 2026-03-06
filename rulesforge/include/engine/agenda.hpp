#ifndef AGENDA_HPP
#define AGENDA_HPP

#include <chrono>
#include <cstdint>
#include <optional>
#include <queue>
#include <string>
#include <vector>

#include "core/token.hpp"


class Agenda {
public:
    struct PoppedActivation {
        int salience;
        size_t hash;
    };
    struct PoppedActivationItem {
        int salience;
        Activation activation;
    };

    struct DelayedActivation {
        std::chrono::steady_clock::time_point fire_time;
        Activation activation;
        bool operator>(DelayedActivation const& other) const {
            return fire_time > other.fire_time;
        }
    };

    void add(Activation const& activation) {
        agenda_map_[activation.hash_value] = activation;
        agenda_queue_.push({activation.rule->salience, activation.hash_value});
        if (activation.rule->activation_group) {
            activation_group_map_[*activation.rule->activation_group].insert(activation.hash_value);
        }
    }

    void add_with_duration(Activation const& activation) {
        if (activation.rule->duration > 0) {
            auto fire_time = std::chrono::steady_clock::now()
                             + std::chrono::milliseconds(activation.rule->duration);
            delayed_activations_.push({fire_time, activation});
            return;
        }
        add(activation);
    }

    void add_batch(std::vector<Activation> const& activations) {
        for (auto const& activation : activations) {
            add(activation);
        }
    }

    void flush_ready_delayed() {
        auto now = std::chrono::steady_clock::now();
        while (!delayed_activations_.empty()
               && delayed_activations_.top().fire_time <= now) {
            auto delayed = delayed_activations_.top();
            delayed_activations_.pop();
            add(delayed.activation);
        }
    }

    bool remove(size_t activation_hash) {
        auto it = agenda_map_.find(activation_hash);
        if (it == agenda_map_.end()) {
            return false;
        }

        if (it->second.rule->activation_group) {
            auto group_it = activation_group_map_.find(*it->second.rule->activation_group);
            if (group_it != activation_group_map_.end()) {
                group_it->second.erase(activation_hash);
                if (group_it->second.empty()) {
                    activation_group_map_.erase(group_it);
                }
            }
        }

        agenda_map_.erase(it);
        return true;
    }

    std::optional<Activation> pop_next() {
        size_t stale_skipped = 0;
        std::vector<std::pair<int, size_t>> deferred;
        deferred.reserve(8);
        std::string const current_focus = get_focus();
        bool const main_focus = (current_focus == "MAIN");

        while (!agenda_queue_.empty()) {
            auto [salience, activation_hash] = agenda_queue_.top();
            agenda_queue_.pop();

            auto map_it = agenda_map_.find(activation_hash);
            if (map_it == agenda_map_.end()) {
                stale_skipped++;
                continue;
            }

            Activation& candidate = map_it->second;
            bool in_focus = false;
            if (candidate.rule->agenda_group.has_value()) {
                in_focus = (*candidate.rule->agenda_group == current_focus);
            } else {
                in_focus = main_focus;
            }
            if (in_focus) {
                Activation out = std::move(map_it->second);
                agenda_map_.erase(map_it);
                for (auto const& entry : deferred) {
                    agenda_queue_.push(entry);
                }
                compact_if_needed(stale_skipped);
                return out;
            }
            deferred.push_back({salience, activation_hash});
        }

        for (auto const& entry : deferred) {
            agenda_queue_.push(entry);
        }
        compact_if_needed(stale_skipped);
        return std::nullopt;
    }

    std::vector<PoppedActivation> pop_next_batch(size_t max_items) {
        std::vector<PoppedActivation> out;
        out.reserve(max_items);
        if (max_items == 0) return out;

        size_t stale_skipped = 0;
        std::vector<std::pair<int, size_t>> deferred;
        deferred.reserve(16);
        std::string const current_focus = get_focus();
        bool const main_focus = (current_focus == "MAIN");

        while (!agenda_queue_.empty() && out.size() < max_items) {
            auto [salience, activation_hash] = agenda_queue_.top();
            agenda_queue_.pop();

            auto map_it = agenda_map_.find(activation_hash);
            if (map_it == agenda_map_.end()) {
                stale_skipped++;
                continue;
            }

            Activation& candidate = map_it->second;
            bool in_focus = false;
            if (candidate.rule->agenda_group.has_value()) {
                in_focus = (*candidate.rule->agenda_group == current_focus);
            } else {
                in_focus = main_focus;
            }

            if (in_focus) {
                out.push_back({salience, activation_hash});
            } else {
                deferred.push_back({salience, activation_hash});
            }
        }

        for (auto const& entry : deferred) {
            agenda_queue_.push(entry);
        }
        compact_if_needed(stale_skipped);
        return out;
    }

    std::vector<PoppedActivationItem> pop_next_batch_activations(size_t max_items,
                                                                 uint64_t* select_us = nullptr,
                                                                 uint64_t* detach_us = nullptr) {
        std::vector<PoppedActivationItem> out;
        out.reserve(max_items);
        if (select_us) *select_us = 0;
        if (detach_us) *detach_us = 0;
        if (max_items == 0) return out;

        size_t stale_skipped = 0;
        std::vector<std::pair<int, size_t>> deferred;
        deferred.reserve(16);
        std::string const current_focus = get_focus();
        bool const main_focus = (current_focus == "MAIN");
        uint64_t select_acc = 0;
        uint64_t detach_acc = 0;

        while (!agenda_queue_.empty() && out.size() < max_items) {
            auto sel_t0 = std::chrono::steady_clock::now();
            auto [salience, activation_hash] = agenda_queue_.top();
            agenda_queue_.pop();

            auto map_it = agenda_map_.find(activation_hash);
            if (map_it == agenda_map_.end()) {
                stale_skipped++;
                auto sel_t1 = std::chrono::steady_clock::now();
                select_acc += static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(sel_t1 - sel_t0).count());
                continue;
            }

            Activation& candidate = map_it->second;
            bool in_focus = false;
            if (candidate.rule->agenda_group.has_value()) {
                in_focus = (*candidate.rule->agenda_group == current_focus);
            } else {
                in_focus = main_focus;
            }
            auto sel_t1 = std::chrono::steady_clock::now();
            select_acc += static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(sel_t1 - sel_t0).count());

            if (in_focus) {
                auto det_t0 = std::chrono::steady_clock::now();
                Activation out_activation = std::move(map_it->second);
                if (out_activation.rule->activation_group) {
                    auto group_it = activation_group_map_.find(*out_activation.rule->activation_group);
                    if (group_it != activation_group_map_.end()) {
                        group_it->second.erase(activation_hash);
                        if (group_it->second.empty()) {
                            activation_group_map_.erase(group_it);
                        }
                    }
                }
                agenda_map_.erase(map_it);
                out.push_back({salience, std::move(out_activation)});
                auto det_t1 = std::chrono::steady_clock::now();
                detach_acc += static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(det_t1 - det_t0).count());
            } else {
                deferred.push_back({salience, activation_hash});
            }
        }

        for (auto const& entry : deferred) {
            agenda_queue_.push(entry);
        }
        compact_if_needed(stale_skipped);
        if (select_us) *select_us += select_acc;
        if (detach_us) *detach_us += detach_acc;
        return out;
    }

    std::optional<Activation> take_by_hash(size_t activation_hash) {
        auto it = agenda_map_.find(activation_hash);
        if (it == agenda_map_.end()) {
            return std::nullopt;
        }

        Activation out = std::move(it->second);

        if (out.rule->activation_group) {
            auto group_it = activation_group_map_.find(*out.rule->activation_group);
            if (group_it != activation_group_map_.end()) {
                group_it->second.erase(activation_hash);
                if (group_it->second.empty()) {
                    activation_group_map_.erase(group_it);
                }
            }
        }

        agenda_map_.erase(it);
        return out;
    }

    void requeue_batch(std::vector<PoppedActivation> const& batch, size_t start_index = 0) {
        if (start_index >= batch.size()) return;
        for (size_t i = start_index; i < batch.size(); ++i) {
            agenda_queue_.push({batch[i].salience, batch[i].hash});
        }
    }

    void requeue_batch_activations(std::vector<PoppedActivationItem> const& batch, size_t start_index = 0) {
        if (start_index >= batch.size()) return;
        for (size_t i = start_index; i < batch.size(); ++i) {
            auto const& item = batch[i];
            size_t const hash = item.activation.hash_value;
            agenda_map_[hash] = item.activation;
            agenda_queue_.push({item.salience, hash});
            if (item.activation.rule->activation_group) {
                activation_group_map_[*item.activation.rule->activation_group].insert(hash);
            }
        }
    }

    void set_focus(std::string const& group_name) {
        focus_stack_.push_back(group_name);
    }

    std::string get_focus() const {
        return focus_stack_.empty() ? "MAIN" : focus_stack_.back();
    }

    void clear_noloop() {
        no_loop_blocked_.clear();
    }

    void block_noloop(size_t key) {
        no_loop_blocked_.insert(key);
    }

    bool is_noloop_blocked(size_t key) const {
        return no_loop_blocked_.count(key) > 0;
    }

    void clear_lock_on_active() {
        lock_on_active_blocked_.clear();
    }

    bool is_lock_on_active_blocked(ParsedRule const* rule) const {
        return lock_on_active_blocked_.count(rule) > 0;
    }

    void seed_lock_on_active_for_focus() {
        lock_on_active_blocked_.clear();
        std::string current_focus = get_focus();
        for (auto const& [hash, activation] : agenda_map_) {
            (void)hash;
            if (!activation.rule->lock_on_active) {
                continue;
            }
            std::string rule_group = activation.rule->agenda_group.value_or("MAIN");
            if (rule_group == current_focus) {
                lock_on_active_blocked_.insert(activation.rule);
            }
        }
    }

    void cancel_activation_group(std::string const& group_name, size_t except_hash) {
        auto group_it = activation_group_map_.find(group_name);
        if (group_it == activation_group_map_.end()) {
            return;
        }
        auto activations_to_cancel = group_it->second;
        activations_to_cancel.erase(except_hash);
        for (size_t cancel_hash : activations_to_cancel) {
            remove(cancel_hash);
        }
        auto again = activation_group_map_.find(group_name);
        if (again != activation_group_map_.end()) {
            again->second.erase(except_hash);
            if (again->second.empty()) {
                activation_group_map_.erase(again);
            }
        }
    }

    void clear_activation_groups() {
        activation_group_map_.clear();
    }

    void remove_activations_with_fact_id(int64_t fact_id) {
        std::vector<size_t> to_remove;
        for (auto const& [hash, activation] : agenda_map_) {
            if (!activation.token.wme) {
                continue;
            }
            auto curr = activation.token.wme;
            while (curr && curr->depth > 0) {
                if (curr->fact && curr->fact->id == fact_id) {
                    to_remove.push_back(hash);
                    break;
                }
                curr = curr->parent;
            }
        }
        for (size_t hash : to_remove) {
            remove(hash);
        }
    }

    std::size_t size() const {
        return agenda_map_.size();
    }

private:
    void compact_if_needed(size_t stale_skipped) {
        if (stale_skipped <= 64) {
            return;
        }
        if (agenda_queue_.size() <= agenda_map_.size() * 2) {
            return;
        }
        std::priority_queue<std::pair<int, size_t>> compacted;
        for (auto const& [hash, activation] : agenda_map_) {
            compacted.push({activation.rule->salience, hash});
        }
        agenda_queue_ = std::move(compacted);
    }

    std::vector<std::string> focus_stack_;
    std::unordered_map<size_t, Activation> agenda_map_;
    std::priority_queue<std::pair<int, size_t>> agenda_queue_;
    std::unordered_set<size_t> no_loop_blocked_;
    std::unordered_set<ParsedRule const*> lock_on_active_blocked_;
    std::unordered_map<std::string, std::unordered_set<size_t>> activation_group_map_;
    std::priority_queue<DelayedActivation,
                        std::vector<DelayedActivation>,
                        std::greater<DelayedActivation>> delayed_activations_;
};

#endif  // AGENDA_HPP

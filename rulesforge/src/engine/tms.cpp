#include "engine/tms.hpp"

TruthMaintenanceSystem::TruthMaintenanceSystem(INetworkCallback& network) : network_(network) {}

void TruthMaintenanceSystem::clear() {
    justifications_.clear();
    token_to_justified_facts_.clear();
    activation_to_facts_.clear();
    fact_to_activations_.clear();
}

void TruthMaintenanceSystem::add_justification(TokenWME const* wme, Fact* fact) {
    if (!wme || !fact) return;

    justifications_[fact->id].insert(wme);
    token_to_justified_facts_[wme].push_back(fact->id);
}

void TruthMaintenanceSystem::add_logical_dependency(size_t activation_hash, int64_t fact_id) {
    activation_to_facts_[activation_hash].push_back(fact_id);
    fact_to_activations_[fact_id].insert(activation_hash);
}

void TruthMaintenanceSystem::remove_justifications_by_token(TokenWME const* wme) {
    if (!wme) return;

    auto it = token_to_justified_facts_.find(wme);
    if (it == token_to_justified_facts_.end()) {
        return;
    }

    std::vector<int64_t> justified_fact_ids = it->second;
    token_to_justified_facts_.erase(it);

    std::vector<Fact*> facts_to_retract;

    for (int64_t fact_id : justified_fact_ids) {
        auto just_it = justifications_.find(fact_id);
        if (just_it != justifications_.end()) {
            just_it->second.erase(wme);

            if (just_it->second.empty()) {
                // If it's also not supported by any activation, retract it
                if (fact_to_activations_[fact_id].empty()) {
                    auto* fact = network_.get_fact_by_id(fact_id);
                    if (fact) { facts_to_retract.push_back(fact); }
                }
            }
        }
    }

    for (auto* fact : facts_to_retract) {
        network_.retract_fact(fact);
    }
}

void TruthMaintenanceSystem::on_activation_retracted(size_t activation_hash) {
    auto it = activation_to_facts_.find(activation_hash);
    if (it == activation_to_facts_.end()) return;

    std::vector<int64_t> fact_ids = it->second;
    activation_to_facts_.erase(it);

    std::vector<Fact*> facts_to_retract;
    for (int64_t fact_id : fact_ids) {
        fact_to_activations_[fact_id].erase(activation_hash);
        if (fact_to_activations_[fact_id].empty()) {
            // If also not justified by tokens, retract
            if (justifications_[fact_id].empty()) {
                auto* fact = network_.get_fact_by_id(fact_id);
                if (fact) { facts_to_retract.push_back(fact); }
            }
        }
    }

    for (auto* fact : facts_to_retract) {
        network_.retract_fact(fact);
    }
}

void TruthMaintenanceSystem::on_fact_retracted(Fact const* fact) {
    if (!fact) return;

    // Clean up justifications
    auto just_it = justifications_.find(fact->id);
    if (just_it != justifications_.end()) {
        for (TokenWME const* supporter_wme : just_it->second) {
            auto reverse_it = token_to_justified_facts_.find(supporter_wme);
            if (reverse_it != token_to_justified_facts_.end()) {
                std::erase(reverse_it->second, fact->id);
                if (reverse_it->second.empty()) { token_to_justified_facts_.erase(reverse_it); }
            }
        }
        justifications_.erase(just_it);
    }

    // Clean up logical dependencies
    auto act_it = fact_to_activations_.find(fact->id);
    if (act_it != fact_to_activations_.end()) {
        for (size_t hash : act_it->second) {
            auto reverse_it = activation_to_facts_.find(hash);
            if (reverse_it != activation_to_facts_.end()) {
                std::erase(reverse_it->second, fact->id);
                if (reverse_it->second.empty()) { activation_to_facts_.erase(reverse_it); }
            }
        }
        fact_to_activations_.erase(act_it);
    }
}

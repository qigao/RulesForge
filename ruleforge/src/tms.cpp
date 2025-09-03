#include "tms.hpp"

#include "drools_rete_defs.hpp"

#include <iostream>

// The constructor now takes the interface
TruthMaintenanceSystem::TruthMaintenanceSystem(INetworkCallback& network) : network_(network) {}

void TruthMaintenanceSystem::clear() {
    justifications_.clear();
    token_to_justified_facts_.clear();
}

void TruthMaintenanceSystem::add_justification(std::shared_ptr<TokenWME const> wme, std::shared_ptr<Fact> fact) {
    if (!wme || !fact) return;

    TokenWME const* wme_ptr = wme.get();
    justifications_[fact->id].insert(wme_ptr);
    token_to_justified_facts_[wme_ptr].push_back(fact->id);
}

void TruthMaintenanceSystem::remove_justifications_by_token(TokenWME const* wme) {
    if (!wme) return;

    auto it = token_to_justified_facts_.find(wme);
    if (it == token_to_justified_facts_.end()) {
        return;   // This token did not support any facts.
    }

    // Copy the list of facts this token supported, as we'll be modifying maps.
    std::vector<int64_t> justified_fact_ids = it->second;
    token_to_justified_facts_.erase(it);

    std::vector<std::shared_ptr<Fact>> facts_to_retract;

    for (int64_t fact_id : justified_fact_ids) {
        auto just_it = justifications_.find(fact_id);
        if (just_it != justifications_.end()) {
            // Remove this token from the fact's set of supporters.
            just_it->second.erase(wme);

            // If the set is now empty, the fact has lost all support.
            if (just_it->second.empty()) {
                // The network call is now through the interface
                if (auto fact_opt = network_.get_fact_by_id(fact_id)) { facts_to_retract.push_back(*fact_opt); }
            }
        }
    }

    for (auto const& fact : facts_to_retract) {
        // The network call is now through the interface
        network_.retract_fact(fact);
    }
}

void TruthMaintenanceSystem::on_fact_retracted(Fact const* fact) {
    if (!fact) return;

    auto it = justifications_.find(fact->id);
    if (it != justifications_.end()) {
        // This fact is being removed. We must update the reverse map for every
        // token that used to support it.
        for (TokenWME const* supporter_wme : it->second) {
            auto reverse_it = token_to_justified_facts_.find(supporter_wme);
            if (reverse_it != token_to_justified_facts_.end()) {
                auto& fact_id_list = reverse_it->second;
                // Use the C++20 erase-remove idiom for vectors
                std::erase(fact_id_list, fact->id);
                if (fact_id_list.empty()) { token_to_justified_facts_.erase(reverse_it); }
            }
        }
        // Finally, remove the fact's own justification entry.
        justifications_.erase(it);
    }
}

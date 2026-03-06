#ifndef TMS_HPP
#define TMS_HPP

#include "core/fact.hpp"
#include "core/token.hpp"
#include "engine/i_network_callback.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>
#include <map>
struct TokenWME;

/**
 * @class TruthMaintenanceSystem
 * @brief Manages logical dependencies between tokens and facts.
 */
class TruthMaintenanceSystem {
public:
    explicit TruthMaintenanceSystem(INetworkCallback& network);

    void add_justification(TokenWME const* wme, Fact* fact);
    void add_logical_dependency(size_t activation_hash, int64_t fact_id);
    void remove_justifications_by_token(TokenWME const* wme);
    void on_activation_retracted(size_t activation_hash);
    void clear();
    void on_fact_retracted(Fact const* fact);

private:
    INetworkCallback& network_;

    std::map<int64_t, std::set<TokenWME const*>> justifications_;
    std::map<TokenWME const*, std::vector<int64_t>> token_to_justified_facts_;

    // Support for add_logical_dependency (activation-based)
    std::map<size_t, std::vector<int64_t>> activation_to_facts_;
    std::map<int64_t, std::set<size_t>> fact_to_activations_;
};

#endif   // TMS_HPP

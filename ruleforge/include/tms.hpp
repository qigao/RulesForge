#ifndef TMS_HPP
#define TMS_HPP

#include "drools_rete_defs.hpp"
#include "i_network_callback.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>
// Forward declarations are no longer needed for Fact/TokenWME as they are in the included header.
class TokenWME;

/**
 * @class TruthMaintenanceSystem
 * @brief Manages logical dependencies between tokens and facts.
 */
class TruthMaintenanceSystem {
public:
    // The TMS now depends on the interface, not the concrete class.
    explicit TruthMaintenanceSystem(INetworkCallback& network);

    void add_justification(std::shared_ptr<TokenWME const> wme, std::shared_ptr<Fact> fact);
    void remove_justifications_by_token(TokenWME const* wme);
    void clear();
    void on_fact_retracted(Fact const* fact);

private:
    INetworkCallback& network_;

    map<int64_t, std::set<TokenWME const*>> justifications_;
    map<TokenWME const*, std::vector<int64_t>> token_to_justified_facts_;
};

#endif   // TMS_HPP



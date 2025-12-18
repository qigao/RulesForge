#ifndef BETA_BUILDER_HPP
#define BETA_BUILDER_HPP

#include "rfl_rete_defs.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

class StatefulSession;   // The context for the build is now a session
class ReteNode;

class BetaNetworkBuilder {
public:
    // The builder now operates in the context of a StatefulSession.
    BetaNetworkBuilder(StatefulSession& session, std::vector<ParsedPattern> const& patterns, bool is_query = false,
                       int param_count = 0);

    std::shared_ptr<ReteNode> first_beta_node_in_chain;
    std::shared_ptr<ReteNode> build();
    map<std::string, int> const& get_bindings() const;

private:
    std::shared_ptr<ReteNode> create_node_for_pattern(ParsedPattern& pattern, int& pattern_depth);
    std::shared_ptr<ReteNode> create_standard_node(int pattern_depth, std::vector<std::shared_ptr<ReteNode>> const&,
                                                   std::vector<ParsedConstraint> const&);
    std::shared_ptr<ReteNode> create_eval_node(ParsedPattern&);
    std::shared_ptr<ReteNode> create_accumulate_node(ParsedPattern&, ParsedAccumulate&,
                                                     std::vector<std::shared_ptr<ReteNode>> const&,
                                                     std::vector<ParsedConstraint> const&);
    std::shared_ptr<ReteNode> create_unnest_node(ParsedPattern&, ParsedUnnest&);
    std::shared_ptr<ReteNode> create_negative_node(ParsedPattern&, std::vector<std::shared_ptr<ReteNode>> const&,
                                                   std::vector<ParsedConstraint> const&);
    std::shared_ptr<ReteNode> create_existential_node(ParsedPattern&, std::vector<std::shared_ptr<ReteNode>> const&,
                                                      std::vector<ParsedConstraint> const&);
    // Private state managed by the builder during a build.
    StatefulSession& session_;              // Operates on a session instance
    std::vector<ParsedPattern> patterns_;   // Store a copy to allow modification
    bool is_query_build_;
    int parameter_count_;
    std::shared_ptr<ReteNode> last_node_;
    map<std::string, int> binding_to_idx_;
};

#endif   // BETA_BUILDER_HPP



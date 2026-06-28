#ifndef BETA_BUILDER_HPP
#define BETA_BUILDER_HPP

#include "core/parsed_rule.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

struct CompiledNetwork;
class KnowledgeBase;
class ReteNode;

class BetaNetworkBuilder {
public:
    BetaNetworkBuilder(CompiledNetwork& network, KnowledgeBase const& kb,
                       std::vector<ParsedPattern> const& patterns, bool is_query = false,
                       int param_count = 0);

    std::shared_ptr<ReteNode> first_beta_node_in_chain;
    std::shared_ptr<ReteNode> build();
    std::map<std::string, int> const& get_bindings() const;

private:
    std::shared_ptr<ReteNode> create_node_for_pattern(ParsedPattern& pattern, int& pattern_depth);
    std::shared_ptr<ReteNode> create_standard_node(int pattern_depth,
                                                   std::string const& fact_type,
                                                   std::vector<std::shared_ptr<ReteNode>> const&,
                                                   std::vector<ParsedConstraint> const&);
    std::shared_ptr<ReteNode> create_eval_node(ParsedPattern&);
    std::shared_ptr<ReteNode> create_query_call_node(ParsedPattern&, ParsedQueryCall&);
    std::shared_ptr<ReteNode> create_accumulate_node(ParsedPattern&, ParsedAccumulate&,
                                                     std::vector<std::shared_ptr<ReteNode>> const&,
                                                     std::vector<ParsedConstraint> const&);
    std::shared_ptr<ReteNode> create_unnest_node(ParsedPattern&, ParsedUnnest&);
    std::shared_ptr<ReteNode> create_negative_node(ParsedPattern&,
                                                   std::string const& fact_type,
                                                   std::vector<std::shared_ptr<ReteNode>> const&,
                                                   std::vector<ParsedConstraint> const&);
    std::shared_ptr<ReteNode> create_existential_node(ParsedPattern&,
                                                      std::string const& fact_type,
                                                      std::vector<std::shared_ptr<ReteNode>> const&,
                                                      std::vector<ParsedConstraint> const&);

    std::vector<std::shared_ptr<ReteNode>> build_alpha_chain(
        ConstraintNode const* node,
        std::string const& fact_type,
        std::vector<std::shared_ptr<ReteNode>> parent_tails);

    void collect_inline_bindings(ConstraintNode const* node, int depth, std::string const& fact_type);
    void register_pattern_fact_type(ParsedPattern const& pattern);
    void normalize_inline_bound_fields(std::vector<ParsedConstraint>& constraints) const;

    CompiledNetwork& network_;
    KnowledgeBase const& kb_;
    std::vector<ParsedPattern> patterns_;
    bool is_query_build_;
    int parameter_count_;
    std::shared_ptr<ReteNode> last_node_;
    std::map<std::string, int> binding_to_idx_;
    std::map<std::string, std::string> binding_to_fact_type_;
    std::map<std::string, std::string> inline_binding_to_field_;
};

#endif   // BETA_BUILDER_HPP

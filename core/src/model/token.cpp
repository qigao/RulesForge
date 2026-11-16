#include "model/token.hpp"
#include "model/parsed_rule.hpp"

Token::Token(TokenWME const* w, PropagationType pt) : wme(w), type(pt) {}

Fact const* Token::get_fact() const { return wme ? wme->fact : nullptr; }

int Token::get_depth() const { return wme ? wme->depth : 0; }

std::vector<Fact*> Token::get_facts() const {
    if (!wme) return {};
    std::vector<Fact*> all_facts;
    // WME depth is 1-based size of chain (excluding dummy root if depth 0)
    // Wait, dummy has depth 0. Real WMEs start at 1.
    // So reserve depth.
    all_facts.resize(wme->depth);

    auto curr = wme;
    int idx = wme->depth - 1;
    while (curr && curr->depth > 0 && idx >= 0) {
        if (curr->fact) {
            all_facts[idx] = const_cast<Fact*>(curr->fact);
        }
        curr = curr->parent;
        idx--;
    }
    return all_facts;
}

Fact* Token::get_fact_at_depth(int d) const {
    if (!wme) return nullptr;
    // Target index is d. Current WME is at index (depth - 1).
    // Steps to walk up = (current_depth - 1) - d.
    int current_index = wme->depth - 1;
    if (d < 0 || d > current_index) return nullptr;

    int steps = current_index - d;
    auto curr = wme;
    while (steps > 0 && curr) {
        curr = curr->parent;
        steps--;
    }

    return (curr && curr->fact) ? const_cast<Fact*>(curr->fact) : nullptr;
}

bool TokenWME::operator==(TokenWME const& other) const {
    if (this == &other) return true;
    if (depth != other.depth) return false;
    bool fact_match = (fact && other.fact) ? (fact->id == other.fact->id) : (fact == other.fact);
    if (!fact_match) return false;
    // Use pointer comparison for parent to avoid recursive deep compare
    return parent == other.parent;
}

bool Activation::operator<(Activation const& other) const { return rule->salience < other.rule->salience; }

#pragma once

#include "core/rfl_strings.hpp"
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include "core/fact.hpp"

struct ParsedRule; // Forward declaration

enum class PropagationType { ASSERT, RETRACT, MODIFY };

struct TokenWME {
  TokenWME const *parent;
  Fact const *fact;
  int depth;
  size_t hash;
  bool operator==(TokenWME const &other) const;
  uintptr_t get_id() const { return reinterpret_cast<uintptr_t>(this); }
};

struct TokenWMEPtrHasher {
  size_t operator()(TokenWME const *wme) const { return wme ? wme->hash : 0; }
};

struct TokenWMEPtrEquals {
  bool operator()(TokenWME const *a, TokenWME const *b) const {
    if (!a || !b)
      return !a && !b;
    return *a == *b;
  }
};

struct Token {
  TokenWME const *wme;
  PropagationType type = PropagationType::ASSERT;
  Token() = default;
  Token(TokenWME const *w, PropagationType pt);
  Fact const *get_fact() const;
  int get_depth() const;
  std::vector<Fact *> get_facts() const;
  Fact *get_fact_at_depth(int d) const;
};

struct Activation {
  ParsedRule const *rule;
  Token token;
  size_t hash_value;
  std::map<std::string, int> const *bindings;
  bool operator<(Activation const &other) const;
};

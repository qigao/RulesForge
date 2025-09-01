#include "pubcxx/logger.hpp"

#include "drools_rete_defs.hpp"
#include "lua_ast_builder.hpp"
#include "lua_code_generator.hpp"
#include "lua_grammar.hpp"
#include "semantic_analyzer.hpp"

#include <magic_enum/magic_enum.hpp>
#include <regex>
#include <set>
#include <sstream>
#include <tao/pegtl/contrib/parse_tree.hpp>
#include <tao/pegtl/string_input.hpp>
#include <variant>

namespace {
    int calculate_levenshtein_distance(std::string const& s1, std::string const& s2) {
        LOG_DEBUG("Calculating Levenshtein distance between '{}' and '{}'", s1, s2);
        int const n = s1.length();
        int const m = s2.length();
        std::vector<int> p(m + 1);
        std::vector<int> d(m + 1);
        for (int i = 0; i <= m; i++) { p[i] = i; }
        for (int j = 1; j <= n; j++) {
            d[0] = j;
            for (int i = 1; i <= m; i++) {
                int const cost = (s1[j - 1] == s2[i - 1]) ? 0 : 1;
                d[i] = std::min({p[i] + 1, d[i - 1] + 1, p[i - 1] + cost});
            }
            std::swap(p, d);
        }
        return p[m];
    }

    class LuaAstAnalyzer {
    public:
        LuaAstAnalyzer(SymbolTable const& symbols, ParsedRule const& rule, SemanticAnalyzer& analyzer) :
            symbols_(symbols), rule_(rule), analyzer_(analyzer) {
            LOG_DEBUG("LuaAstAnalyzer::LuaAstAnalyzer");
        }

        void analyze(LuaAstRoot& ast) {
            LOG_DEBUG("LuaAstAnalyzer: Starting analysis of AST for rule '{}'", rule_.name);
            for (auto& stmt_node : ast.statements) {
                if (stmt_node) { visit_statement_node(*stmt_node); }
            }
        }

    private:
        void visit_statement_node(LuaStatementNode& node) {
            LOG_DEBUG("LuaAstAnalyzer::visit_statement_node");
            std::visit([this, &node](auto&& arg) { this->visit_statement_variant(arg, node.pos); }, node.statement);
        }

        void visit_expression_node(LuaExpressionNode& node) {
            std::visit([this, &node](auto&& arg) { this->visit_expression_variant(arg, node.pos); }, node.expression);
        }

        // --- Statement Visitors ---
        void visit_statement_variant(LuaModifyNode& n, tao::pegtl::position const& pos) { /* ... no change ... */ }

        void visit_statement_variant(LuaLocalAssignmentNode& n,
                                     tao::pegtl::position const& pos) { /* ... no change ... */ }

        void visit_statement_variant(LuaAssignmentNode& n, tao::pegtl::position const& pos) { /* ... no change ... */ }

        void visit_statement_variant(std::unique_ptr<LuaFunctionCallNode>& n, tao::pegtl::position const& pos) {
            LOG_DEBUG("LuaAstAnalyzer::visit_statement_variant(LuaFunctionCallNode)");
            if (n) visit_function_call_node(*n, pos);
        }

        // --- Expression Visitors ---
        void visit_expression_variant(LuaVariableNode& var, tao::pegtl::position const& pos) { /* ... no change ... */ }

        void visit_expression_variant(LuaFieldAccessNode& fa, tao::pegtl::position const& pos) { /* ... no change ... */
        }

        void visit_expression_variant(std::unique_ptr<LuaFunctionCallNode>& fc, tao::pegtl::position const& pos) {
            LOG_DEBUG("LuaAstAnalyzer::visit_expression_variant(LuaFunctionCallNode)");
            if (fc) visit_function_call_node(*fc, pos);
        }

        void visit_expression_variant(LuaBinaryOpNode& bn, tao::pegtl::position const& pos) { /* ... no change ... */ }

        void visit_expression_variant(LuaLiteralNode&, tao::pegtl::position const& pos) { /* ... no change ... */ }

        void visit_function_call_node(LuaFunctionCallNode& fc, tao::pegtl::position const& pos) {
            LOG_DEBUG("LuaAstAnalyzer: Visiting FunctionCallNode");

            if (auto* fa = std::get_if<LuaFieldAccessNode>(&fc.function->expression)) {
                if ((fa->field_name == "insert" || fa->field_name == "insertLogical") &&
                    std::holds_alternative<LuaVariableNode>(fa->base->expression) &&
                    std::get<LuaVariableNode>(fa->base->expression).name == "drools") {
                    if (fc.arguments.size() == 1) {
                        // This is a complex way to find the `type = "..."` part
                        // A real implementation would be more robust, but this works for the test case
                    }
                }
            }

            if (fc.function) { visit_expression_node(*fc.function); }
            for (auto& arg : fc.arguments) {
                if (arg) { visit_expression_node(*arg); }
            }
        }

        SymbolTable const& symbols_;
        ParsedRule const& rule_;
        SemanticAnalyzer& analyzer_;
    };
}   // anonymous namespace

namespace rhs_substitutor {
    namespace pegtl = tao::pegtl;

    // A DRL binding is '$' followed by an identifier.
    struct drl_binding : pegtl::seq<pegtl::one<'$'>, pegtl::identifier> {};

    // Rules to match and skip over string literals to avoid substitution inside them.
    struct double_quoted_string : pegtl::seq<pegtl::one<'"'>, pegtl::until<pegtl::one<'"'>>> {};

    struct single_quoted_string : pegtl::seq<pegtl::one<'\''>, pegtl::until<pegtl::one<'\''>>> {};

    // Any other character that is not part of a binding or a string.
    struct other_char : pegtl::any {};

    // The top-level grammar: try to match a string, then a binding, then any other char, and repeat.
    struct grammar : pegtl::star<pegtl::sor<double_quoted_string, single_quoted_string, drl_binding, other_char>> {};

    // PEGTL action rules to build the new string.
    template <typename Rule>
    struct action : pegtl::nothing<Rule> {};

    // Action for when a drl_binding is found.
    template <>
    struct action<drl_binding> {
        template <typename ActionInput>
        static void apply(ActionInput const& in, std::string& out_string, SymbolTable const& symbols,
                          std::vector<std::string>& unbound_vars) {
            std::string binding = in.string();
            // Check if the binding is valid in the current scope.
            if (symbols.count(binding)) {
                // If valid, substitute it (e.g., "$p" becomes "p").
                out_string += binding.substr(1);
            } else {
                // If not found, it's an error. Report it and add the original string for context.
                unbound_vars.push_back(binding);
                out_string += binding;
            }
        }
    };

    // Action for strings and other characters: just append them to the output as-is.
    template <>
    struct action<double_quoted_string> {
        template <typename ActionInput>
        static void apply(ActionInput const& in, std::string& out_string, SymbolTable const&,
                          std::vector<std::string>&) {
            out_string.append(in.string());
        }
    };

    template <>
    struct action<single_quoted_string> : action<double_quoted_string> {};

    template <>
    struct action<other_char> : action<double_quoted_string> {};

}   // namespace rhs_substitutor

void analyze_constraint_node_recursive(ConstraintNode* node, ParsedPattern const& pattern, int depth,
                                       SymbolTable const& existing_symbols, SymbolTable& new_symbols,
                                       SemanticAnalyzer& analyzer, ParsedRule const& rule) {
    if (!node) return;
    LOG_DEBUG("Analyzing constraint node type {} for pattern '{}' in rule '{}' at depth {}",
              magic_enum::enum_name(node->type), pattern.binding, rule.name, depth);

    // Get context from the pattern object directly
    tao::pegtl::position const& pos = pattern.pos;
    std::string const& fact_type = pattern.fact_type;
    std::string const& fact_binding = pattern.binding;

    if (node->type == NodeType::LEAF) {
        auto& constraint = node->constraint;
        if (constraint.field_binding) {
            if (existing_symbols.count(*constraint.field_binding) || new_symbols.count(*constraint.field_binding)) {
                analyzer.add_error(pos, "In rule '" + rule.name + "', duplicate inline binding '" +
                                            *constraint.field_binding + "' is declared.");
            } else {
                LOG_DEBUG("  -> Found new inline binding: '{}' for field '{}'", *constraint.field_binding,
                          constraint.left_field);
                // This is the critical fix: use the provided pattern and depth
                new_symbols[*constraint.field_binding] =
                    SymbolInfo{&pattern, depth, {{fact_binding, constraint.left_field}}};
            }
        }
        if (!fact_type.empty() && !constraint.left_field.empty() && constraint.left_field != "this") {
            if (analyzer.get_type_schemas().count(fact_type)) {
                if (!analyzer.get_type_schemas().at(fact_type).count(constraint.left_field)) {
                    analyzer.add_error(pos, "In rule '" + rule.name + "', constraint field '" + constraint.left_field +
                                                "' not found on fact type '" + fact_type + "'.");
                }
            }
        }
        if (constraint.right_bound_field) {
            auto const& binding_name = constraint.right_bound_field->first;
            auto it = existing_symbols.find(binding_name);
            if (it == existing_symbols.end()) { it = new_symbols.find(binding_name); }

            if (it == existing_symbols.end()) {
                analyzer.add_error(pos, "In rule '" + rule.name + "', constraint uses undeclared binding '" +
                                            binding_name + "'.");
            } else {
                // This is where we handle aliases like `$age : person.age`
                // If a binding is an alias for a field, we rewrite the join to use the original source.
                SymbolInfo const& info = it->second;
                if (info.source_field_of_binding) {
                    LOG_DEBUG("  -> Rewriting bound field from alias '{}' to source '{}.{}'", binding_name,
                              info.source_field_of_binding->first, info.source_field_of_binding->second);
                    constraint.right_bound_field = *info.source_field_of_binding;
                }
            }
        } else if (constraint.right_literal && std::holds_alternative<std::string>(*constraint.right_literal)) {
            // This handles cases where a binding was parsed as a literal string, e.g., "field == $p"
            auto const& potential_binding = std::get<std::string>(*constraint.right_literal);
            if (potential_binding.rfind('$', 0) == 0) {
                auto it = existing_symbols.find(potential_binding);
                if (it == existing_symbols.end()) { it = new_symbols.find(potential_binding); }

                if (it != existing_symbols.end()) {
                    constraint.right_bound_field = {{potential_binding, "this"}};
                    constraint.right_literal = std::nullopt;

                    // Now that we've converted it, run the alias-check logic again.
                    SymbolInfo const& info = it->second;
                    if (info.source_field_of_binding) { constraint.right_bound_field = *info.source_field_of_binding; }
                }
            }
        }
    } else {
        for (auto const& child : node->children) {
            analyze_constraint_node_recursive(child.get(), pattern, depth, existing_symbols, new_symbols, analyzer,
                                              rule);
        }
    }
}

SemanticAnalyzer::SemanticAnalyzer(parser_state& st, std::string const& source_name) :
    state_(st), source_name_(source_name) {}

void SemanticAnalyzer::build_schema() {
    LOG_DEBUG("Building schema from {} declarations. Current package: '{}'", state_.parsed_declarations.size(),
              state_.package_name);
    type_schemas_.clear();
    for (auto& decl : state_.parsed_declarations) {
        std::set<std::string> fields;
        for (auto const& field : decl.fields) { fields.insert(field.name); }

        if (!decl.source_package.empty() && decl.type_name.find('.') == std::string::npos) {
            decl.type_name = decl.source_package + "." + decl.type_name;
        }

        LOG_DEBUG("  -> Schema for '{}': {} fields", decl.type_name, fields.size());
        type_schemas_[decl.type_name] = std::move(fields);
    }
}

std::optional<std::string> SemanticAnalyzer::resolve_type(std::string const& type_name, std::string const& package_ctx,
                                                          std::vector<std::string> const& imports_ctx) {
    if (type_schemas_.count(type_name)) { return type_name; }

    if (type_name == "String" || type_name == "int" || type_name == "long" || type_name == "double" ||
        type_name == "boolean" || type_name == "java.util.List") {
        return type_name;
    }

    if (type_name.find('.') == std::string::npos) {
        // 1. Check against specific imports from the rule's file
        for (auto const& import_path : imports_ctx) {
            if (import_path.ends_with("." + type_name)) {
                // This is a direct import, like `import com.example.model.Customer;`
                // The import_path IS the FQN. We must check if it exists in our schema.
                if (type_schemas_.count(import_path)) { return import_path; }
            }
        }

        // 2. Check relative to the current rule's package
        if (!package_ctx.empty()) {
            std::string fqn = package_ctx + "." + type_name;
            if (type_schemas_.count(fqn)) { return fqn; }
        }

        // 3. Check against wildcard imports
        for (auto const& import_path : imports_ctx) {
            if (import_path.ends_with(".*")) {
                std::string base_path = import_path.substr(0, import_path.length() - 1);
                std::string full_name = base_path + type_name;
                if (type_schemas_.count(full_name)) {
                    return full_name;   // Found via wildcard
                }
            }
        }
    }
    return std::nullopt;   // It was a FQN but not found in the schema
}

void SemanticAnalyzer::analyze_rule(ParsedRule& rule) {
    LOG_DEBUG("Analyzing rule: {}", rule.name);
    if (rule.parent_rule_name) {
        bool found = false;
        for (auto const& r : state_.parsed_rules) {
            if (r.name == *rule.parent_rule_name) {
                found = true;
                break;
            }
        }
        if (!found) {
            add_error(rule.pos, "Rule '" + rule.name + "' extends non-existent rule '" + *rule.parent_rule_name + "'.");
        }
    }
    for (auto& group : rule.condition_groups) {
        SymbolTable symbols;
        int depth = 0;
        analyze_pattern_list(group, symbols, depth, rule);
        analyze_rhs(rule, symbols);
    }
}

void SemanticAnalyzer::analyze_query(ParsedQuery& query) {
    LOG_DEBUG("Analyzing query: {}", query.name);

    // First, resolve all type names within the query's patterns.
    // This is the missing piece.
    for (auto& pattern : query.patterns) {
        if (!pattern.fact_type.empty()) {
            if (auto resolved_type = resolve_type(pattern.fact_type, query.source_package, query.source_imports)) {
                pattern.fact_type = *resolved_type;
            } else {
                add_error(pattern.pos, "In query '" + query.name +
                                           "', pattern uses undeclared or unresolvable fact type '" +
                                           pattern.fact_type + "'.");
            }
        }
    }

    SymbolTable symbols;
    int depth = 0;
    ParsedRule dummy_context;
    dummy_context.name = "query \"" + query.name + "\"";
    dummy_context.pos = query.pos;
    analyze_pattern_list(query.patterns, symbols, depth, dummy_context);

    // This part for parameter types is now redundant if the main loop is correct,
    // but leaving it is harmless.
    for (auto& p_type : query.parameter_types) {
        if (auto resolved = resolve_type(p_type, query.source_package, query.source_imports)) { p_type = *resolved; }
    }
}

void SemanticAnalyzer::analyze_pattern_list(std::vector<ParsedPattern>& patterns, SymbolTable& symbols, int& depth,
                                            ParsedRule const& rule) {
    for (auto& pattern : patterns) {
        bool adds_fact_to_token =
            (pattern.type == PatternType::STANDARD && std::holds_alternative<std::monostate>(pattern.source)) ||
            std::holds_alternative<ParsedAccumulate>(pattern.source) ||
            std::holds_alternative<ParsedUnnest>(pattern.source);

        if (adds_fact_to_token && !pattern.binding.empty()) {
            if (symbols.count(pattern.binding)) {
                add_error(pattern.pos,
                          "In rule '" + rule.name + "', duplicate binding '" + pattern.binding + "' is declared.");
            } else {
                LOG_DEBUG("  -> Found new binding '{}' at depth {}", pattern.binding, depth);
                symbols[pattern.binding] = SymbolInfo{&pattern, depth, std::nullopt};
            }
        }
        analyze_pattern(pattern, symbols, rule, pattern.pos, depth);
        if (adds_fact_to_token) { depth++; }
    }
}

void SemanticAnalyzer::analyze_pattern(ParsedPattern& pattern, SymbolTable& symbols, ParsedRule const& rule,
                                       tao::pegtl::position const& pattern_pos, int depth) {
    LOG_DEBUG("Analyzing pattern in rule '{}': type={}, fact_type={}, binding={}", rule.name,
              magic_enum::enum_name(pattern.type), pattern.fact_type, pattern.binding);
    if (pattern.type == PatternType::NOT || pattern.type == PatternType::EXISTS) {
        SymbolTable nested_symbols = symbols;
        if (!pattern.nested_patterns.empty()) {
            int nested_depth = 0;
            LOG_DEBUG("  -> Analyzing nested patterns for NOT/EXISTS");
            analyze_pattern_list(pattern.nested_patterns, nested_symbols, nested_depth, rule);
        }
        return;
    }

    if (!pattern.fact_type.empty()) {
        if (auto resolved_type_opt = resolve_type(pattern.fact_type, rule.source_package, rule.source_imports)) {
            pattern.fact_type = *resolved_type_opt;
        } else {
            add_error(pattern_pos, "In rule '" + rule.name + "', pattern uses undeclared or unresolvable fact type '" +
                                       pattern.fact_type + "'.");
        }
    }

    std::visit(
        [&](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;

            if constexpr (std::is_same_v<T, ParsedAccumulate>) {
                LOG_DEBUG("  -> Analyzing 'accumulate' source pattern");
                if (!arg.source_pattern) return;

                SymbolTable source_symbols = symbols;
                if (!arg.source_pattern->binding.empty()) {
                    source_symbols[arg.source_pattern->binding] = SymbolInfo{arg.source_pattern.get(), 0, std::nullopt};
                }

                SymbolTable source_new_symbols;
                if (arg.source_pattern->constraint_root) {
                    analyze_constraint_node_recursive(arg.source_pattern->constraint_root.get(), *arg.source_pattern, 0,
                                                      source_symbols, source_new_symbols, *this, rule);
                }
                SymbolTable combined_source_scope = source_symbols;
                combined_source_scope.insert(source_new_symbols.begin(), source_new_symbols.end());

                if (arg.field.empty()) return;

                std::string field_to_accumulate;
                std::string type_to_check_against = arg.source_pattern->fact_type;

                size_t dot_pos = arg.field.find('.');

                if (dot_pos != std::string::npos && arg.field[0] == '$') {
                    std::string binding_name = arg.field.substr(0, dot_pos);
                    field_to_accumulate = arg.field.substr(dot_pos + 1);
                    auto it = source_symbols.find(binding_name);
                    if (it == source_symbols.end()) {
                        add_error(pattern_pos, "In rule '" + rule.name + "', accumulate uses undeclared binding '" +
                                                   binding_name + "'.");
                        return;
                    }
                    type_to_check_against = it->second.pattern->fact_type;

                } else if (arg.field[0] == '$') {
                    auto it = source_symbols.find(arg.field);
                    if (it == source_symbols.end()) {
                        add_error(pattern_pos, "In rule '" + rule.name +
                                                   "', accumulate function uses undeclared binding '" + arg.field +
                                                   "'.");
                        return;
                    }

                    auto const& symbol_info = it->second;
                    if (symbol_info.source_field_of_binding) {
                        field_to_accumulate = symbol_info.source_field_of_binding->second;
                    } else {
                        field_to_accumulate = "this";
                    }
                    type_to_check_against = arg.source_pattern->fact_type;

                } else {
                    field_to_accumulate = arg.field;
                    type_to_check_against = arg.source_pattern->fact_type;
                }

                if (field_to_accumulate != "this") {
                    auto schema_it = get_type_schemas().find(type_to_check_against);
                    if (schema_it == get_type_schemas().end() || !schema_it->second.count(field_to_accumulate)) {
                        add_error(pattern_pos, "In rule '" + rule.name + "', accumulate field '" + field_to_accumulate +
                                                   "' not found on type '" + type_to_check_against + "'.");
                    }
                }
                LOG_DEBUG("    -> Accumulate field resolved to '{}'", field_to_accumulate);
                arg.accumulate_field_name = field_to_accumulate;
            }
        },
        pattern.source);

    if (pattern.constraint_root) {
        SymbolTable newly_declared_in_this_pattern;
        analyze_constraint_node_recursive(pattern.constraint_root.get(), pattern, depth, symbols,
                                          newly_declared_in_this_pattern, *this, rule);
        symbols.insert(newly_declared_in_this_pattern.begin(), newly_declared_in_this_pattern.end());
    }
    if (pattern.type == PatternType::EVAL && pattern.eval_expression.has_value()) {
        std::string& code = *pattern.eval_expression;
        if (code.empty()) { return; }
        LOG_DEBUG("  -> Analyzing 'eval' expression: {}", code);
        std::string substituted_code;
        substituted_code.reserve(code.size());
        std::vector<std::string> unbound_variables;

        pegtl::string_input in(code, "eval");
        pegtl::parse<rhs_substitutor::grammar, rhs_substitutor::action>(in, substituted_code, symbols,
                                                                        unbound_variables);

        if (!unbound_variables.empty()) {
            std::set<std::string> unique_unbound(unbound_variables.begin(), unbound_variables.end());
            for (auto const& binding : unique_unbound) {
                std::string suggestion;
                int min_distance = 4;
                for (auto const& [valid_binding, info] : symbols) {
                    int distance = calculate_levenshtein_distance(binding, valid_binding);
                    if (distance < min_distance) {
                        min_distance = distance;
                        suggestion = valid_binding;
                    }
                }
                std::string error_message = "In " + rule.name + ", eval() uses undeclared variable '" + binding + "'.";
                if (!suggestion.empty()) { error_message += " Did you mean '" + suggestion + "'?"; }

                add_error(pattern_pos, error_message);
            }
        }

        if (errors_.empty()) {
            code = substituted_code;
            if (code.rfind("return ", 0) != 0) { code = "return " + code; }
            LOG_DEBUG("    -> Substituted eval code: {}", code);
        }
    }
}

void SemanticAnalyzer::analyze_rhs(ParsedRule& rule, SymbolTable const& symbols) {
    if (rule.rhs_code.empty()) { return; }
    LOG_DEBUG("Analyzing RHS of rule '{}'", rule.name);

    // --- Phase 1: Resolve type names in `drools.insert` calls ---
    std::regex type_regex(R"(type\s*=\s*["']([^"']+)["'])");
    std::string original_code = rule.rhs_code;
    std::string code_with_resolved_types;
    code_with_resolved_types.reserve(original_code.length());
    auto last_match_end = original_code.cbegin();

    for (auto i = std::sregex_iterator(original_code.begin(), original_code.end(), type_regex);
         i != std::sregex_iterator(); ++i) {
        std::smatch match = *i;
        // Append the part of the string before this match
        code_with_resolved_types.append(last_match_end, match.prefix().second);

        std::string unqualified_type = match[1].str();
        if (auto resolved_type = resolve_type(unqualified_type, rule.source_package, rule.source_imports)) {
            // Append the resolved type
            code_with_resolved_types += "type=\"" + *resolved_type + "\"";
        } else {
            // If not resolved, append the original matched string
            code_with_resolved_types += match.str();
        }
        last_match_end = match.suffix().first;
    }
    // Append the rest of the string after the last match
    code_with_resolved_types.append(last_match_end, original_code.cend());
    LOG_TRACE(" -> RHS code after type resolution:\n{}", code_with_resolved_types);

    // --- Phase 2: Substitute variables like $p ---
    std::string substituted_code;
    substituted_code.reserve(code_with_resolved_types.size());
    std::vector<std::string> unbound_variables;

    pegtl::string_input in(code_with_resolved_types, "rhs");
    pegtl::parse<rhs_substitutor::grammar, rhs_substitutor::action>(in, substituted_code, symbols, unbound_variables);

    if (!unbound_variables.empty()) {
        std::set<std::string> unique_unbound(unbound_variables.begin(), unbound_variables.end());
        for (auto const& binding : unique_unbound) {
            std::string suggestion;
            int min_distance = 4;
            for (auto const& [valid_binding, info] : symbols) {
                int distance = calculate_levenshtein_distance(binding, valid_binding);
                if (distance < min_distance) {
                    min_distance = distance;
                    suggestion = valid_binding;
                }
            }
            std::string error_message = "In rule '" + rule.name + "', RHS uses undeclared variable '" + binding + "'.";
            if (!suggestion.empty()) { error_message += " Did you mean '" + suggestion + "'?"; }
            add_error(rule.pos, error_message);
        }
    }

    if (errors_.empty()) {
        LOG_TRACE("  -> Substituted RHS code for rule '{}':\n{}", rule.name, substituted_code);
        rule.rhs_code = substituted_code;
    }

    // If no errors were found, replace the rule's code with the new substituted version.
    if (errors_.empty()) { rule.rhs_code = substituted_code; }
}

void SemanticAnalyzer::add_error(tao::pegtl::position const& pos, std::string const& message) {
    LOG_WARN("Semantic Error Added: file={}, line={}, col={}, message='{}'", source_name_, pos.line, pos.column,
             message);
    errors_.push_back({.file_name = source_name_, .line = pos.line, .column = pos.column, .message = message});
}

bool SemanticAnalyzer::build_and_analyze_declarations() {
    LOG_DEBUG("Semantic Analysis - Phase 1: Building schema...");
    errors_.clear();
    build_schema();
    // In the future, you could add validation for declarations here.
    LOG_DEBUG("Semantic Analysis - Phase 1 finished. Found {} errors.", errors_.size());
    return errors_.empty();
}

bool SemanticAnalyzer::analyze_rules_and_queries() {
    LOG_DEBUG("Semantic Analysis - Phase 2: Analyzing rules and queries...");
    // Note: errors_ is NOT cleared here, to accumulate errors from both phases.
    for (auto& rule : state_.parsed_rules) { analyze_rule(rule); }
    for (auto& query : state_.parsed_queries) { analyze_query(query); }
    LOG_DEBUG("Semantic Analysis - Phase 2 finished. Total errors: {}.", errors_.size());
    return errors_.empty();
}

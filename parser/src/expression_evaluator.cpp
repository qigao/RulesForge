#include "expression_evaluator.hpp"
#include <exprtk.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

extern "C" const exprtk_module_t* exprtk_module_math(void);
extern "C" const exprtk_module_t* exprtk_module_string(void);

namespace rulesforge {

namespace {
std::atomic<bool> g_exprtk_math_registered{false};

// Helper to sanitize RFL variable names ($var.field -> _v_var__field) for ExprTk
std::string sanitize_name(std::string const& name) {
    std::string s = name;
    if (!s.empty() && s[0] == '$') s[0] = '_';
    std::replace(s.begin(), s.end(), '.', '_');
    return s;
}

bool is_identifier_char(char c) {
    unsigned char uc = static_cast<unsigned char>(c);
    return std::isalnum(uc) || c == '_';
}

std::pair<std::string, size_t> extract_variable_name(std::string const& expr, size_t start) {
    if (start >= expr.size() || expr[start] != '$') {
        return {"", start};
    }

    size_t pos = start + 1;
    while (pos < expr.size() && std::isspace(static_cast<unsigned char>(expr[pos]))) {
        ++pos;
    }

    size_t seg_start = pos;
    while (pos < expr.size() && is_identifier_char(expr[pos])) {
        ++pos;
    }
    if (seg_start == pos) {
        return {"", start + 1};
    }

    std::string result = "$" + expr.substr(seg_start, pos - seg_start);

    while (true) {
        size_t checkpoint = pos;
        while (pos < expr.size() && std::isspace(static_cast<unsigned char>(expr[pos]))) {
            ++pos;
        }
        if (pos >= expr.size() || expr[pos] != '.') {
            pos = checkpoint;
            break;
        }
        ++pos;
        while (pos < expr.size() && std::isspace(static_cast<unsigned char>(expr[pos]))) {
            ++pos;
        }
        size_t next_seg_start = pos;
        while (pos < expr.size() && is_identifier_char(expr[pos])) {
            ++pos;
        }
        if (next_seg_start == pos) {
            pos = checkpoint;
            break;
        }
        result.push_back('.');
        result.append(expr, next_seg_start, pos - next_seg_start);
    }

    return {result, pos};
}

void initialize_exprtk_env(exprtk_env_t& env) {
    exprtk_env_init(&env);
    bool expected = false;
    if (g_exprtk_math_registered.compare_exchange_strong(expected, true)) {
        exprtk_registry_add_module(exprtk_module_math());
        exprtk_registry_add_module(exprtk_module_string());
        exprtk_registry_init();
    }
}

bool validate_function_calls(exprtk_node_t const* node, exprtk_env_t* env, std::string* error_out) {
    if (!node) {
        return true;
    }

    auto fail = [error_out](std::string const& name) {
        if (error_out) {
            *error_out = "Unknown function: " + name;
        }
        return false;
    };

    switch (node->type) {
        case EXPRTK_NODE_FUNCTION_CALL:
            for (size_t i = 0; i < node->data.function.arg_count; ++i) {
                if (!validate_function_calls(node->data.function.args[i], env, error_out)) {
                    return false;
                }
            }
            if (node->data.function.name &&
                std::strcmp(node->data.function.name, "import") != 0 &&
                std::strcmp(node->data.function.name, "print") != 0 &&
                std::strcmp(node->data.function.name, "println") != 0 &&
                !exprtk_find_builtin(node->data.function.name, env) &&
                !exprtk_registry_find(node->data.function.name)) {
                return fail(node->data.function.name);
            }
            return true;

        case EXPRTK_NODE_MEMBER_CALL: {
            if (!validate_function_calls(node->data.member_call.object, env, error_out)) {
                return false;
            }
            for (size_t i = 0; i < node->data.member_call.arg_count; ++i) {
                if (!validate_function_calls(node->data.member_call.args[i], env, error_out)) {
                    return false;
                }
            }

            if (node->data.member_call.object &&
                node->data.member_call.object->type == EXPRTK_NODE_VARIABLE &&
                node->data.member_call.object->data.variable.name &&
                node->data.member_call.method) {
                std::string full_name = std::string(node->data.member_call.object->data.variable.name) +
                                        "." + node->data.member_call.method;
                if (exprtk_find_builtin(full_name.c_str(), env) || exprtk_registry_find(full_name.c_str())) {
                    return true;
                }
            }

            if (node->data.member_call.method &&
                !exprtk_find_builtin(node->data.member_call.method, env) &&
                !exprtk_registry_find(node->data.member_call.method)) {
                return fail(node->data.member_call.method);
            }
            return true;
        }

        case EXPRTK_NODE_BINARY_OP:
            return validate_function_calls(node->data.binary.left, env, error_out) &&
                   validate_function_calls(node->data.binary.right, env, error_out);

        case EXPRTK_NODE_ASSIGNMENT:
        case EXPRTK_NODE_CONSTANT_DECL:
            return validate_function_calls(node->data.assignment.value, env, error_out);

        case EXPRTK_NODE_IF:
            return validate_function_calls(node->data.if_stmt.condition, env, error_out) &&
                   validate_function_calls(node->data.if_stmt.if_branch, env, error_out) &&
                   validate_function_calls(node->data.if_stmt.else_branch, env, error_out);

        case EXPRTK_NODE_WHILE:
            return validate_function_calls(node->data.while_loop.condition, env, error_out) &&
                   validate_function_calls(node->data.while_loop.body, env, error_out);

        case EXPRTK_NODE_DO_WHILE:
            return validate_function_calls(node->data.do_while.body, env, error_out) &&
                   validate_function_calls(node->data.do_while.condition, env, error_out);

        case EXPRTK_NODE_FOR:
            return validate_function_calls(node->data.for_loop.init, env, error_out) &&
                   validate_function_calls(node->data.for_loop.condition, env, error_out) &&
                   validate_function_calls(node->data.for_loop.post, env, error_out) &&
                   validate_function_calls(node->data.for_loop.body, env, error_out);

        case EXPRTK_NODE_FOR_IN:
            return validate_function_calls(node->data.for_in.collection, env, error_out) &&
                   validate_function_calls(node->data.for_in.body, env, error_out);

        case EXPRTK_NODE_BLOCK:
            for (size_t i = 0; i < node->data.block.count; ++i) {
                if (!validate_function_calls(node->data.block.statements[i], env, error_out)) {
                    return false;
                }
            }
            return true;

        case EXPRTK_NODE_VECTOR:
            for (size_t i = 0; i < node->data.vector.count; ++i) {
                if (!validate_function_calls(node->data.vector.elements[i], env, error_out)) {
                    return false;
                }
            }
            return true;

        case EXPRTK_NODE_MAP_LITERAL:
            for (size_t i = 0; i < node->data.map_literal.count; ++i) {
                if (!validate_function_calls(node->data.map_literal.values[i], env, error_out)) {
                    return false;
                }
            }
            return true;

        case EXPRTK_NODE_INDEX:
            return validate_function_calls(node->data.index_access.array, env, error_out) &&
                   validate_function_calls(node->data.index_access.index, env, error_out);

        case EXPRTK_NODE_SLICE:
            return validate_function_calls(node->data.slice.array, env, error_out) &&
                   validate_function_calls(node->data.slice.start, env, error_out) &&
                   validate_function_calls(node->data.slice.end, env, error_out);

        case EXPRTK_NODE_FUNCTION_DEFINITION:
        case EXPRTK_NODE_FUNCTION_EXPRESSION:
            return validate_function_calls(node->data.func_def.body, env, error_out);

        case EXPRTK_NODE_SPREAD:
            return validate_function_calls(node->data.spread.child, env, error_out);

        case EXPRTK_NODE_MEMBER_ACCESS:
            return validate_function_calls(node->data.member_access.object, env, error_out);

        case EXPRTK_NODE_MEMBER_SET:
            return validate_function_calls(node->data.member_set.object, env, error_out) &&
                   validate_function_calls(node->data.member_set.value, env, error_out);

        case EXPRTK_NODE_SWITCH:
            if (!validate_function_calls(node->data.switch_stmt.value, env, error_out)) {
                return false;
            }
            for (size_t i = 0; i < node->data.switch_stmt.case_count; ++i) {
                if (!validate_function_calls(node->data.switch_stmt.cases[i], env, error_out)) {
                    return false;
                }
            }
            return validate_function_calls(node->data.switch_stmt.default_case, env, error_out);

        case EXPRTK_NODE_TRY_CATCH:
            return validate_function_calls(node->data.try_catch.try_body, env, error_out) &&
                   validate_function_calls(node->data.try_catch.catch_body, env, error_out);

        case EXPRTK_NODE_THROW:
            return validate_function_calls(node->data.throw_stmt.value, env, error_out);

        case EXPRTK_NODE_DESTRUCTURING_ASSIGNMENT:
            return validate_function_calls(node->data.destructuring.targets, env, error_out) &&
                   validate_function_calls(node->data.destructuring.value, env, error_out);

        default:
            return true;
    }
}

ConstraintValue from_exprtk(exprtk_value_t const& val) {
    if (val.type == EXPRTK_VAL_INTEGER) return val.data.integer;
    if (val.type == EXPRTK_VAL_NUMBER) return val.data.number;
    if (val.type == EXPRTK_VAL_STRING) return std::string(val.data.string.data, val.data.string.len);
    return NilValue{};
}

exprtk_value_t to_exprtk(ConstraintValue const& val) {
    if (std::holds_alternative<int64_t>(val)) return exprtk_val_num(static_cast<double>(std::get<int64_t>(val)));
    if (std::holds_alternative<double>(val)) return exprtk_val_num(std::get<double>(val));
    if (std::holds_alternative<std::string>(val)) {
        std::string const& s = std::get<std::string>(val);
        tstr_v tv; tv.data = s.data(); tv.len = s.length();
        return exprtk_val_str(tv);
    }
    return exprtk_val_int(0);
}
}

struct ExpressionEvaluator::Impl {
    exprtk_node_t* ast = nullptr;
    mem_pool_t arena;
    bool has_arena = false;
    std::string original_expr;
    std::string sanitized_expr;
    std::vector<std::string> original_var_names;
    std::vector<std::string> sanitized_var_names;

    Impl() {
        if (mem_init(&arena, 4096) == 0) {
            has_arena = true;
        }
    }

    ~Impl() {
        if (has_arena) {
            mem_destroy(&arena);
        }
    }
};

ExpressionEvaluator::ExpressionEvaluator(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}

std::shared_ptr<ExpressionEvaluator> ExpressionEvaluator::compile(std::string const& expr, std::string* error_out) {
    if (expr.empty()) {
        if (error_out) *error_out = "Empty expression";
        return nullptr;
    }

    auto impl = std::make_shared<Impl>();
    if (!impl->has_arena) {
        if (error_out) *error_out = "Failed to initialize memory arena";
        return nullptr;
    }
    impl->original_expr = expr;

    std::string processed = expr;
    size_t pos = 0;
    std::unordered_map<std::string, std::string> name_map;

    while ((pos = processed.find('$', pos)) != std::string::npos) {
        auto [orig, end] = extract_variable_name(processed, pos);
        if (orig.empty()) {
            ++pos;
            continue;
        }
        std::string sanitized = sanitize_name(orig);

        if (name_map.find(orig) == name_map.end()) {
            name_map[orig] = sanitized;
            impl->original_var_names.push_back(orig);
            impl->sanitized_var_names.push_back(sanitized);
        }

        processed.replace(pos, end - pos, sanitized);
        pos += sanitized.length();
    }
    impl->sanitized_expr = processed;

    int error_code = 0;
    char error_msg[256] = {0};
    impl->ast = exprtk_parse_ext(impl->sanitized_expr.c_str(), impl->sanitized_expr.length(), &impl->arena, &error_code, error_msg, sizeof(error_msg));

    if (!impl->ast) {
        if (error_out) *error_out = error_msg;
        return nullptr;
    }

    exprtk_env_t env;
    initialize_exprtk_env(env);
    if (!validate_function_calls(impl->ast, &env, error_out)) {
        exprtk_env_free(&env);
        return nullptr;
    }
    exprtk_env_free(&env);

    return std::shared_ptr<ExpressionEvaluator>(new ExpressionEvaluator(impl));
}

ConstraintValue ExpressionEvaluator::evaluate(VariableResolver const& resolver) const {
    if (!impl_->ast) return NilValue{};

    exprtk_env_t env;
    initialize_exprtk_env(env);

    for (size_t i = 0; i < impl_->original_var_names.size(); ++i) {
        ConstraintValue val = resolver(impl_->original_var_names[i]);
        exprtk_env_set(&env, impl_->sanitized_var_names[i].c_str(), to_exprtk(val));
    }

    exprtk_value_t res = exprtk_eval(impl_->ast, &env);
    ConstraintValue result = from_exprtk(res);

    exprtk_env_free(&env);
    return result;
}

std::vector<std::string> const& ExpressionEvaluator::variables() const {
    return impl_->original_var_names;
}

std::string const& ExpressionEvaluator::expression_string() const {
    return impl_->original_expr;
}

} // namespace rulesforge


#include "js_ast_builder.hpp"
#include "logging_control.hpp"

#include <cstring>
#include <stdexcept>
#include <functional>

// Tree-sitter JavaScript language declaration
extern "C" const TSLanguage* tree_sitter_javascript();

JSAstBuilder::JSAstBuilder() {
    parser_ = ts_parser_new();
    if (!parser_) {
        throw std::runtime_error("Failed to create Tree-sitter parser");
    }
    ts_parser_set_language(parser_, tree_sitter_javascript());
}

JSAstBuilder::~JSAstBuilder() {
    if (parser_) {
        ts_parser_delete(parser_);
    }
}

std::string JSAstBuilder::node_text(TSNode node, std::string_view source) const {
    uint32_t start = ts_node_start_byte(node);
    uint32_t end = ts_node_end_byte(node);
    if (end > source.size()) end = static_cast<uint32_t>(source.size());
    return std::string(source.substr(start, end - start));
}

bool JSAstBuilder::validate_syntax(std::string_view js_code, std::string& error_message) {
    TSTree* tree = ts_parser_parse_string(parser_, nullptr, js_code.data(), static_cast<uint32_t>(js_code.size()));
    if (!tree) {
        error_message = "Failed to parse JavaScript code";
        return false;
    }

    TSNode root = ts_tree_root_node(tree);
    bool has_error = ts_node_has_error(root);

    if (has_error) {
        // Find the first error node for better error message
        std::function<TSNode(TSNode)> find_error = [&](TSNode node) -> TSNode {
            if (ts_node_is_error(node) || ts_node_is_missing(node)) {
                return node;
            }
            uint32_t child_count = ts_node_child_count(node);
            for (uint32_t i = 0; i < child_count; ++i) {
                TSNode child = ts_node_child(node, i);
                if (ts_node_has_error(child)) {
                    return find_error(child);
                }
            }
            return node;
        };

        TSNode error_node = find_error(root);
        TSPoint point = ts_node_start_point(error_node);
        error_message = "Syntax error at line " + std::to_string(point.row + 1) +
                        ", column " + std::to_string(point.column + 1);

        if (ts_node_is_missing(error_node)) {
            error_message += ": missing " + std::string(ts_node_type(error_node));
        }
    }

    ts_tree_delete(tree);
    return !has_error;
}

JSAstRoot JSAstBuilder::parse(std::string_view js_code) {
    JSAstRoot ast;
    ast.raw_code = std::string(js_code);

    if (!validate_syntax(js_code, ast.error_message)) {
        ast.is_valid = false;
        return ast;
    }

    auto function_calls = extract_function_calls(js_code);
    for (auto const& call : function_calls) {
        auto node = std::make_unique<JSAstNode>();
        node->type = JSAstNode::FUNCTION_CALL;
        node->function_call = call;
        node->raw_code = call.object_name + "." + call.method_name + "(...)";
        ast.statements.push_back(std::move(node));
    }

    auto variables = extract_variables(js_code);
    for (auto const& var : variables) {
        auto node = std::make_unique<JSAstNode>();
        node->type = JSAstNode::VARIABLE_REF;
        node->variable_ref = var;
        node->raw_code = var.name + (var.field ? "." + *var.field : "");
        ast.statements.push_back(std::move(node));
    }

    ast.is_valid = true;
    logd("JSAstBuilder: Successfully parsed {} statements", ast.statements.size());
    return ast;
}

void JSAstBuilder::collect_call_expressions(TSNode node, std::string_view source, std::vector<JSFunctionCall>& calls) {
    char const* type = ts_node_type(node);

    // Look for call_expression with member_expression as function
    // Pattern: object.method(args)
    if (std::strcmp(type, "call_expression") == 0) {
        TSNode function_node = ts_node_child_by_field_name(node, "function", 8);
        if (!ts_node_is_null(function_node) && std::strcmp(ts_node_type(function_node), "member_expression") == 0) {
            TSNode object_node = ts_node_child_by_field_name(function_node, "object", 6);
            TSNode property_node = ts_node_child_by_field_name(function_node, "property", 8);

            if (!ts_node_is_null(object_node) && !ts_node_is_null(property_node)) {
                // Only capture simple identifier.method patterns
                if (std::strcmp(ts_node_type(object_node), "identifier") == 0) {
                    JSFunctionCall call;
                    call.object_name = node_text(object_node, source);
                    call.method_name = node_text(property_node, source);

                    // Extract arguments
                    TSNode args_node = ts_node_child_by_field_name(node, "arguments", 9);
                    if (!ts_node_is_null(args_node)) {
                        uint32_t arg_count = ts_node_named_child_count(args_node);
                        for (uint32_t i = 0; i < arg_count; ++i) {
                            TSNode arg = ts_node_named_child(args_node, i);
                            call.arguments.push_back(node_text(arg, source));
                        }
                    }

                    logd("JSAstBuilder: Found call: {}.{}()", call.object_name, call.method_name);
                    calls.push_back(std::move(call));
                }
            }
        }
    }

    // Recurse into children
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; ++i) {
        collect_call_expressions(ts_node_child(node, i), source, calls);
    }
}

std::vector<JSFunctionCall> JSAstBuilder::extract_function_calls(std::string_view js_code) {
    std::vector<JSFunctionCall> calls;

    TSTree* tree = ts_parser_parse_string(parser_, nullptr, js_code.data(), static_cast<uint32_t>(js_code.size()));
    if (!tree) return calls;

    TSNode root = ts_tree_root_node(tree);
    collect_call_expressions(root, js_code, calls);

    ts_tree_delete(tree);
    return calls;
}

void JSAstBuilder::collect_member_expressions(TSNode node, std::string_view source, std::vector<JSVariableRef>& vars) {
    char const* type = ts_node_type(node);

    // Look for member_expression: object.property
    if (std::strcmp(type, "member_expression") == 0) {
        // Skip if this member_expression is the function part of a call_expression
        TSNode parent = ts_node_parent(node);
        if (!ts_node_is_null(parent) && std::strcmp(ts_node_type(parent), "call_expression") == 0) {
            // This is a function call like rfl.insert(), skip it
        } else {
            TSNode object_node = ts_node_child_by_field_name(node, "object", 6);
            TSNode property_node = ts_node_child_by_field_name(node, "property", 8);

            if (!ts_node_is_null(object_node) && !ts_node_is_null(property_node)) {
                // Only capture identifier.property patterns (not nested member expressions)
                if (std::strcmp(ts_node_type(object_node), "identifier") == 0) {
                    JSVariableRef var;
                    std::string obj_name = node_text(object_node, source);

                    // Handle $variable syntax
                    if (!obj_name.empty() && obj_name[0] == '$') {
                        var.name = obj_name.substr(1);
                    } else {
                        var.name = obj_name;
                    }
                    var.field = node_text(property_node, source);

                    logd("JSAstBuilder: Found member access: {}.{}", var.name, *var.field);
                    vars.push_back(std::move(var));
                }
            }
        }
    }

    // Recurse into children
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; ++i) {
        collect_member_expressions(ts_node_child(node, i), source, vars);
    }
}

void JSAstBuilder::collect_identifiers(TSNode node, std::string_view source, std::vector<JSVariableRef>& vars) {
    char const* type = ts_node_type(node);

    // Standalone identifier (not part of member_expression object or property)
    if (std::strcmp(type, "identifier") == 0) {
        TSNode parent = ts_node_parent(node);
        if (!ts_node_is_null(parent)) {
            char const* parent_type = ts_node_type(parent);
            // Skip if this identifier is part of a member_expression, call_expression function,
            // variable_declarator name, or property definition
            if (std::strcmp(parent_type, "member_expression") == 0 ||
                std::strcmp(parent_type, "call_expression") == 0 ||
                std::strcmp(parent_type, "variable_declarator") == 0 ||
                std::strcmp(parent_type, "property_identifier") == 0 ||
                std::strcmp(parent_type, "shorthand_property_identifier") == 0 ||
                std::strcmp(parent_type, "pair") == 0) {
                // Don't add - handled elsewhere or is a declaration
            } else {
                std::string name = node_text(node, source);
                if (!name.empty() && name[0] == '$') {
                    JSVariableRef var;
                    var.name = name.substr(1);
                    logd("JSAstBuilder: Found $identifier: {}", var.name);
                    vars.push_back(std::move(var));
                }
            }
        }
    }

    // Recurse into children
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; ++i) {
        collect_identifiers(ts_node_child(node, i), source, vars);
    }
}

std::vector<JSVariableRef> JSAstBuilder::extract_variables(std::string_view js_code) {
    std::vector<JSVariableRef> vars;

    TSTree* tree = ts_parser_parse_string(parser_, nullptr, js_code.data(), static_cast<uint32_t>(js_code.size()));
    if (!tree) return vars;

    TSNode root = ts_tree_root_node(tree);
    collect_member_expressions(root, js_code, vars);
    collect_identifiers(root, js_code, vars);

    ts_tree_delete(tree);
    return vars;
}

void JSAstBuilder::collect_variable_declarations(TSNode node, std::string_view source, std::set<std::string>& vars) {
    char const* type = ts_node_type(node);

    // Look for variable_declaration (var, let, const)
    if (std::strcmp(type, "variable_declaration") == 0 ||
        std::strcmp(type, "lexical_declaration") == 0) {
        uint32_t child_count = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < child_count; ++i) {
            TSNode declarator = ts_node_named_child(node, i);
            if (std::strcmp(ts_node_type(declarator), "variable_declarator") == 0) {
                TSNode name_node = ts_node_child_by_field_name(declarator, "name", 4);
                if (!ts_node_is_null(name_node) && std::strcmp(ts_node_type(name_node), "identifier") == 0) {
                    std::string var_name = node_text(name_node, source);
                    logd("JSAstBuilder: Found local declaration: {}", var_name);
                    vars.insert(var_name);
                }
            }
        }
    }

    // Recurse into children
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; ++i) {
        collect_variable_declarations(ts_node_child(node, i), source, vars);
    }
}

std::set<std::string> JSAstBuilder::extract_local_declarations(std::string_view js_code) {
    std::set<std::string> vars;

    TSTree* tree = ts_parser_parse_string(parser_, nullptr, js_code.data(), static_cast<uint32_t>(js_code.size()));
    if (!tree) return vars;

    TSNode root = ts_tree_root_node(tree);
    collect_variable_declarations(root, js_code, vars);

    ts_tree_delete(tree);
    return vars;
}

// Single-pass extraction: parse once, collect everything
JSExtractionResult JSAstBuilder::extract_all(std::string_view js_code) {
    JSExtractionResult result;

    TSTree* tree = ts_parser_parse_string(parser_, nullptr, js_code.data(), static_cast<uint32_t>(js_code.size()));
    if (!tree) {
        result.is_valid = false;
        result.error_message = "Failed to parse JavaScript code";
        return result;
    }

    TSNode root = ts_tree_root_node(tree);

    // Check for syntax errors
    if (ts_node_has_error(root)) {
        std::function<TSNode(TSNode)> find_error = [&](TSNode node) -> TSNode {
            if (ts_node_is_error(node) || ts_node_is_missing(node)) {
                return node;
            }
            uint32_t child_count = ts_node_child_count(node);
            for (uint32_t i = 0; i < child_count; ++i) {
                TSNode child = ts_node_child(node, i);
                if (ts_node_has_error(child)) {
                    return find_error(child);
                }
            }
            return node;
        };

        TSNode error_node = find_error(root);
        TSPoint point = ts_node_start_point(error_node);
        result.error_message = "Syntax error at line " + std::to_string(point.row + 1) +
                               ", column " + std::to_string(point.column + 1);
        if (ts_node_is_missing(error_node)) {
            result.error_message += ": missing " + std::string(ts_node_type(error_node));
        }
        result.is_valid = false;
        ts_tree_delete(tree);
        return result;
    }

    // Single traversal to collect everything
    collect_all(root, js_code, result);

    result.is_valid = true;
    ts_tree_delete(tree);
    return result;
}

void JSAstBuilder::collect_all(TSNode node, std::string_view source, JSExtractionResult& result) {
    char const* type = ts_node_type(node);

    // Collect call expressions (rfl.insert, etc.)
    if (std::strcmp(type, "call_expression") == 0) {
        TSNode function_node = ts_node_child_by_field_name(node, "function", 8);
        if (!ts_node_is_null(function_node) && std::strcmp(ts_node_type(function_node), "member_expression") == 0) {
            TSNode object_node = ts_node_child_by_field_name(function_node, "object", 6);
            TSNode property_node = ts_node_child_by_field_name(function_node, "property", 8);

            if (!ts_node_is_null(object_node) && !ts_node_is_null(property_node)) {
                if (std::strcmp(ts_node_type(object_node), "identifier") == 0) {
                    JSFunctionCall call;
                    call.object_name = node_text(object_node, source);
                    call.method_name = node_text(property_node, source);

                    TSNode args_node = ts_node_child_by_field_name(node, "arguments", 9);
                    if (!ts_node_is_null(args_node)) {
                        uint32_t arg_count = ts_node_named_child_count(args_node);
                        for (uint32_t i = 0; i < arg_count; ++i) {
                            TSNode arg = ts_node_named_child(args_node, i);
                            call.arguments.push_back(node_text(arg, source));
                        }
                    }
                    result.function_calls.push_back(std::move(call));
                }
            }
        }
    }
    // Collect member expressions ($p.name)
    else if (std::strcmp(type, "member_expression") == 0) {
        TSNode parent = ts_node_parent(node);
        if (ts_node_is_null(parent) || std::strcmp(ts_node_type(parent), "call_expression") != 0) {
            TSNode object_node = ts_node_child_by_field_name(node, "object", 6);
            TSNode property_node = ts_node_child_by_field_name(node, "property", 8);

            if (!ts_node_is_null(object_node) && !ts_node_is_null(property_node)) {
                if (std::strcmp(ts_node_type(object_node), "identifier") == 0) {
                    JSVariableRef var;
                    std::string obj_name = node_text(object_node, source);
                    if (!obj_name.empty() && obj_name[0] == '$') {
                        var.name = obj_name.substr(1);
                    } else {
                        var.name = obj_name;
                    }
                    var.field = node_text(property_node, source);
                    result.variables.push_back(std::move(var));
                }
            }
        }
    }
    // Collect variable declarations (var, let, const)
    else if (std::strcmp(type, "variable_declaration") == 0 || std::strcmp(type, "lexical_declaration") == 0) {
        uint32_t child_count = ts_node_named_child_count(node);
        for (uint32_t i = 0; i < child_count; ++i) {
            TSNode declarator = ts_node_named_child(node, i);
            if (std::strcmp(ts_node_type(declarator), "variable_declarator") == 0) {
                TSNode name_node = ts_node_child_by_field_name(declarator, "name", 4);
                if (!ts_node_is_null(name_node) && std::strcmp(ts_node_type(name_node), "identifier") == 0) {
                    result.local_declarations.insert(node_text(name_node, source));
                }
            }
        }
    }
    // Collect standalone $identifiers
    else if (std::strcmp(type, "identifier") == 0) {
        TSNode parent = ts_node_parent(node);
        if (!ts_node_is_null(parent)) {
            char const* parent_type = ts_node_type(parent);
            if (std::strcmp(parent_type, "member_expression") != 0 &&
                std::strcmp(parent_type, "call_expression") != 0 &&
                std::strcmp(parent_type, "variable_declarator") != 0 &&
                std::strcmp(parent_type, "property_identifier") != 0 &&
                std::strcmp(parent_type, "shorthand_property_identifier") != 0 &&
                std::strcmp(parent_type, "pair") != 0) {
                std::string name = node_text(node, source);
                if (!name.empty() && name[0] == '$') {
                    JSVariableRef var;
                    var.name = name.substr(1);
                    result.variables.push_back(std::move(var));
                }
            }
        }
    }

    // Recurse into children
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; ++i) {
        collect_all(ts_node_child(node, i), source, result);
    }
}

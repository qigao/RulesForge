/**
 * @file mir_codegen.cpp
 * @brief Direct MIR IR generation for binary parse functions
 */

#include "mir_codegen.h"
#include "core/rfl_parser_state.hpp"
#include <cstdio>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

enum class EmitKind {
    Primitive,
    VarString,
    ListInt,
    ListInt64,
    ListDouble,
    ListString,
    SetInt,
    SetInt64,
    SetDouble,
    SetString,
    MapStringString,
    MapStringInt,
    MapStringInt64,
    MapStringDouble
};

struct EmitField {
    std::string name;
    EmitKind kind = EmitKind::Primitive;
    FieldType value_type = FT_Unknown;
    int value_size = 0;
};

bool is_float_type(FieldType type) {
    return type == FT_Float || type == FT_Double;
}

int field_size(FieldType type) {
    switch (type) {
        case FT_Boolean: return 1;
        case FT_Int: return 4;
        case FT_Long: return 8;
        case FT_Float: return 4;
        case FT_Double: return 8;
        default: return 0;
    }
}

bool flatten_fields(std::vector<EmitField>& out, const std::vector<ParsedField>& fields,
                    const std::map<std::string, ParsedDeclaration>& declarations,
                    const std::map<std::string, ParsedEnum>& enums, const char* prefix,
                    std::string* error_message) {
    for (const auto& field : fields) {
        const std::string full_name = prefix ? std::string(prefix) + "." + field.name : field.name;

        if (field.type == FT_Object && !field.type_params.empty()) {
            const std::string& custom_type = field.type_params[0].custom_type;
            auto enum_it = enums.find(custom_type);
            if (enum_it != enums.end()) {
                const FieldType underlying_type = parse_field_type(enum_it->second.underlying_type);
                const int size = field_size(underlying_type);
                if (size != 0) {
                    out.push_back({full_name, EmitKind::Primitive, underlying_type, size});
                    continue;
                }
                if (error_message != nullptr) {
                    *error_message = "Unsupported enum underlying type for field '" + full_name + "'";
                }
                return false;
            } else {
                auto decl_it = declarations.find(custom_type);
                if (decl_it != declarations.end()) {
                    if (!flatten_fields(out, decl_it->second.fields, declarations, enums, full_name.c_str(),
                                        error_message)) {
                        return false;
                    }
                    continue;
                }
                if (error_message != nullptr) {
                    *error_message = "Unknown object type '" + custom_type + "' for field '" + full_name + "'";
                }
                return false;
            }
        }

        if (field.type == FT_String) {
            out.push_back({full_name, EmitKind::VarString, FT_String, 0});
            continue;
        }

        if (field.type == FT_List && !field.type_params.empty()) {
            const TypeParameter& elem = field.type_params[0];
            if (elem.base_type == FT_String) {
                out.push_back({full_name, EmitKind::ListString, elem.base_type, 0});
            } else if (elem.base_type == FT_Long) {
                out.push_back({full_name, EmitKind::ListInt64, elem.base_type, field_size(elem.base_type)});
            } else if (is_float_type(elem.base_type)) {
                out.push_back({full_name, EmitKind::ListDouble, elem.base_type, field_size(elem.base_type)});
            } else if (field_size(elem.base_type) != 0) {
                out.push_back({full_name, EmitKind::ListInt, elem.base_type, field_size(elem.base_type)});
            } else {
                if (error_message != nullptr) {
                    *error_message = "Unsupported List element type for field '" + full_name + "'";
                }
                return false;
            }
            continue;
        }

        if (field.type == FT_Set && !field.type_params.empty()) {
            const TypeParameter& elem = field.type_params[0];
            if (elem.base_type == FT_String) {
                out.push_back({full_name, EmitKind::SetString, elem.base_type, 0});
            } else if (elem.base_type == FT_Long) {
                out.push_back({full_name, EmitKind::SetInt64, elem.base_type, field_size(elem.base_type)});
            } else if (is_float_type(elem.base_type)) {
                out.push_back({full_name, EmitKind::SetDouble, elem.base_type, field_size(elem.base_type)});
            } else if (field_size(elem.base_type) != 0) {
                out.push_back({full_name, EmitKind::SetInt, elem.base_type, field_size(elem.base_type)});
            } else {
                if (error_message != nullptr) {
                    *error_message = "Unsupported Set element type for field '" + full_name + "'";
                }
                return false;
            }
            continue;
        }

        if (field.type == FT_Map && field.type_params.size() >= 2) {
            const TypeParameter& key_type = field.type_params[0];
            const TypeParameter& value_type = field.type_params[1];
            if (key_type.base_type != FT_String) {
                if (error_message != nullptr) {
                    *error_message = "Binary codec supports only Map<String, T>; field '" + full_name + "'";
                }
                return false;
            }
            if (value_type.base_type == FT_String) {
                out.push_back({full_name, EmitKind::MapStringString, value_type.base_type, 0});
            } else if (value_type.base_type == FT_Long) {
                out.push_back({full_name, EmitKind::MapStringInt64, value_type.base_type, field_size(value_type.base_type)});
            } else if (is_float_type(value_type.base_type)) {
                out.push_back(
                    {full_name, EmitKind::MapStringDouble, value_type.base_type, field_size(value_type.base_type)});
            } else if (field_size(value_type.base_type) != 0) {
                out.push_back(
                    {full_name, EmitKind::MapStringInt, value_type.base_type, field_size(value_type.base_type)});
            } else {
                if (error_message != nullptr) {
                    *error_message = "Unsupported Map value type for field '" + full_name + "'";
                }
                return false;
            }
            continue;
        }

        const int size = field_size(field.type);
        if (size != 0) {
            out.push_back({full_name, EmitKind::Primitive, field.type, size});
            continue;
        }
        if (error_message != nullptr) {
            *error_message = "Unsupported field type for '" + full_name + "'";
        }
        return false;
    }
    return true;
}

void collect_abi_requirements_from_fields(const std::vector<EmitField>& fields, BinaryAbiRequirements& requirements) {
    for (const auto& field : fields) {
        switch (field.kind) {
            case EmitKind::Primitive:
                switch (field.value_type) {
                    case FT_Boolean:
                    case FT_Int:
                        requirements.set_int = true;
                        break;
                    case FT_Long:
                        requirements.set_int64 = true;
                        break;
                    case FT_Float:
                    case FT_Double:
                        requirements.set_dbl = true;
                        break;
                    default:
                        break;
                }
                break;
            case EmitKind::VarString:
                requirements.set_str = true;
                requirements.read_varstr = true;
                requirements.free_string = true;
                break;
            case EmitKind::ListInt:
                requirements.create_list = true;
                requirements.add_list_int = true;
                requirements.set_list = true;
                break;
            case EmitKind::ListInt64:
                requirements.create_list = true;
                requirements.add_list_int64 = true;
                requirements.set_list = true;
                break;
            case EmitKind::ListDouble:
                requirements.create_list = true;
                requirements.add_list_dbl = true;
                requirements.set_list = true;
                break;
            case EmitKind::ListString:
                requirements.create_list = true;
                requirements.add_list_str = true;
                requirements.set_list = true;
                requirements.read_varstr = true;
                requirements.free_string = true;
                break;
            case EmitKind::SetInt:
                requirements.create_set = true;
                requirements.add_set_int = true;
                requirements.set_set = true;
                break;
            case EmitKind::SetInt64:
                requirements.create_set = true;
                requirements.add_set_int64 = true;
                requirements.set_set = true;
                break;
            case EmitKind::SetDouble:
                requirements.create_set = true;
                requirements.add_set_dbl = true;
                requirements.set_set = true;
                break;
            case EmitKind::SetString:
                requirements.create_set = true;
                requirements.add_set_str = true;
                requirements.set_set = true;
                requirements.read_varstr = true;
                requirements.free_string = true;
                break;
            case EmitKind::MapStringString:
                requirements.create_map = true;
                requirements.add_map_str_str = true;
                requirements.set_map = true;
                requirements.read_varstr = true;
                requirements.free_string = true;
                break;
            case EmitKind::MapStringInt:
                requirements.create_map = true;
                requirements.add_map_str_int = true;
                requirements.set_map = true;
                requirements.read_varstr = true;
                requirements.free_string = true;
                break;
            case EmitKind::MapStringInt64:
                requirements.create_map = true;
                requirements.add_map_str_int64 = true;
                requirements.set_map = true;
                requirements.read_varstr = true;
                requirements.free_string = true;
                break;
            case EmitKind::MapStringDouble:
                requirements.create_map = true;
                requirements.add_map_str_dbl = true;
                requirements.set_map = true;
                requirements.read_varstr = true;
                requirements.free_string = true;
                break;
        }
    }
}

struct ExternalRef {
    MIR_item_t import_item = nullptr;
    MIR_item_t proto_item = nullptr;
};

class ModuleBuilder {
public:
    explicit ModuleBuilder(MIR_context_t ctx) : ctx_(ctx), module_(MIR_new_module(ctx, "data_bind_binary")) {}

    MIR_context_t ctx() const { return ctx_; }
    MIR_module_t module() const { return module_; }

    void declare_external(const char* name, std::vector<MIR_type_t> result_types, std::vector<MIR_var_t> args) {
        ExternalRef ref;
        ref.import_item = MIR_new_import(ctx_, name);
        ref.proto_item = MIR_new_proto_arr(ctx_, own("p_" + std::string(name)), result_types.size(),
                                           result_types.empty() ? nullptr : result_types.data(), args.size(),
                                           args.empty() ? nullptr : args.data());
        externals_.emplace(name, ref);
    }

    const ExternalRef& external(const char* name) const { return externals_.at(name); }

    MIR_item_t string_item(const std::string& value) {
        auto it = strings_.find(value);
        if (it != strings_.end()) {
            return it->second;
        }

        std::string item_name = "__rf_str_" + std::to_string(strings_.size());
        std::string bytes = value;
        const size_t len = bytes.size() + 1;
        MIR_item_t item = MIR_new_data(ctx_, own(std::move(item_name)), MIR_T_U8, len, own(std::move(bytes)));
        strings_.emplace(value, item);
        return item;
    }

    const char* own(std::string value) {
        owned_strings_.push_back(std::move(value));
        return owned_strings_.back().c_str();
    }

private:
    MIR_context_t ctx_;
    MIR_module_t module_;
    std::deque<std::string> owned_strings_;
    std::unordered_map<std::string, ExternalRef> externals_;
    std::unordered_map<std::string, MIR_item_t> strings_;
};

class FunctionEmitter {
public:
    FunctionEmitter(ModuleBuilder& module_builder, const ParsedDeclaration& declaration,
                    const std::vector<EmitField>& fields)
        : builder_(module_builder), ctx_(module_builder.ctx()), fields_(fields) {
        MIR_type_t result_type = MIR_T_P;
        MIR_var_t args[] = {{MIR_T_P, "buf", 0}, {MIR_T_I64, "len", 0}};
        const std::string func_name = "parse_" + declaration.type_name;

        func_item_ = MIR_new_func_arr(ctx_, builder_.own(func_name), 1, &result_type, 2, args);
        func_ = func_item_->u.func;
        buf_reg_ = MIR_reg(ctx_, "buf", func_);
        len_reg_ = MIR_reg(ctx_, "len", func_);
        off_reg_ = new_reg(MIR_T_I64, "off");
        obj_reg_ = new_reg(MIR_T_I64, "obj");
        fail_label_ = MIR_new_label(ctx_);

        append(MIR_new_insn(ctx_, MIR_MOV, reg(off_reg_), MIR_new_int_op(ctx_, 0)));
        call_with_result("create_obj", obj_reg_, {});
        append(MIR_new_insn(ctx_, MIR_BEQ, label(fail_label_), reg(obj_reg_), MIR_new_int_op(ctx_, 0)));

        for (const auto& field : fields_) {
            emit_field(field);
        }

        append(MIR_new_ret_insn(ctx_, 1, reg(obj_reg_)));
        append(fail_label_);
        MIR_label_t return_null_label = MIR_new_label(ctx_);
        append(MIR_new_insn(ctx_, MIR_BEQ, label(return_null_label), reg(obj_reg_), MIR_new_int_op(ctx_, 0)));
        call_no_result("destroy_value", {reg(obj_reg_)});
        append(return_null_label);
        append(MIR_new_ret_insn(ctx_, 1, MIR_new_int_op(ctx_, 0)));
        MIR_finish_func(ctx_);
    }

private:
    MIR_reg_t new_reg(MIR_type_t type, const char* base_name) {
        const std::string name = "__" + std::string(base_name) + "_" + std::to_string(next_reg_id_++);
        return MIR_new_func_reg(ctx_, func_, type, builder_.own(name));
    }

    void append(MIR_insn_t insn) { MIR_append_insn(ctx_, func_item_, insn); }

    MIR_op_t reg(MIR_reg_t reg_value) const { return MIR_new_reg_op(ctx_, reg_value); }

    MIR_op_t label(MIR_label_t label_value) const { return MIR_new_label_op(ctx_, label_value); }

    MIR_op_t field_name(const std::string& name) { return MIR_new_ref_op(ctx_, builder_.string_item(name)); }

    MIR_op_t mem(MIR_type_t type) const { return MIR_new_mem_op(ctx_, type, 0, buf_reg_, off_reg_, 1); }

    void advance_off(uint64_t delta) {
        append(MIR_new_insn(ctx_, MIR_ADD, reg(off_reg_), reg(off_reg_), MIR_new_int_op(ctx_, delta)));
    }

    void bounds_check(uint64_t needed, MIR_label_t fail_target) {
        MIR_reg_t end_reg = new_reg(MIR_T_I64, "end");
        append(MIR_new_insn(ctx_, MIR_ADD, reg(end_reg), reg(off_reg_), MIR_new_int_op(ctx_, needed)));
        append(MIR_new_insn(ctx_, MIR_UBGT, label(fail_target), reg(end_reg), reg(len_reg_)));
    }

    void call_no_result(const char* name, std::vector<MIR_op_t> args) {
        const ExternalRef& ext = builder_.external(name);
        std::vector<MIR_op_t> ops;
        ops.reserve(args.size() + 2);
        ops.push_back(MIR_new_ref_op(ctx_, ext.proto_item));
        ops.push_back(MIR_new_ref_op(ctx_, ext.import_item));
        ops.insert(ops.end(), args.begin(), args.end());
        append(MIR_new_insn_arr(ctx_, MIR_CALL, ops.size(), ops.data()));
    }

    void call_with_result(const char* name, MIR_reg_t result_reg, std::vector<MIR_op_t> args) {
        const ExternalRef& ext = builder_.external(name);
        std::vector<MIR_op_t> ops;
        ops.reserve(args.size() + 3);
        ops.push_back(MIR_new_ref_op(ctx_, ext.proto_item));
        ops.push_back(MIR_new_ref_op(ctx_, ext.import_item));
        ops.push_back(reg(result_reg));
        ops.insert(ops.end(), args.begin(), args.end());
        append(MIR_new_insn_arr(ctx_, MIR_CALL, ops.size(), ops.data()));
    }

    MIR_op_t int_value_op(FieldType type) {
        switch (type) {
            case FT_Boolean: {
                MIR_reg_t value_reg = new_reg(MIR_T_I64, "bool");
                append(MIR_new_insn(ctx_, MIR_UEXT8, reg(value_reg), mem(MIR_T_U8)));
                return reg(value_reg);
            }
            case FT_Int: return mem(MIR_T_I32);
            case FT_Long: return mem(MIR_T_I64);
            default: return MIR_new_int_op(ctx_, 0);
        }
    }

    MIR_op_t double_value_op(FieldType type) {
        if (type == FT_Float) {
            MIR_reg_t value_reg = new_reg(MIR_T_D, "dbl");
            append(MIR_new_insn(ctx_, MIR_F2D, reg(value_reg), mem(MIR_T_F)));
            return reg(value_reg);
        }
        if (type == FT_Double) {
            return mem(MIR_T_D);
        }
        return MIR_new_double_op(ctx_, 0.0);
    }

    MIR_reg_t read_var_string() {
        return read_var_string(fail_label_);
    }

    MIR_reg_t read_var_string(MIR_label_t fail_target) {
        MIR_reg_t string_reg = new_reg(MIR_T_I64, "str");
        MIR_reg_t string_len_reg = new_reg(MIR_T_I64, "strlen");
        MIR_reg_t end_reg = new_reg(MIR_T_I64, "str_end");

        append(MIR_new_insn(ctx_, MIR_UEXT16, reg(string_len_reg), mem(MIR_T_U16)));
        append(MIR_new_insn(ctx_, MIR_ADD, reg(end_reg), reg(off_reg_), MIR_new_int_op(ctx_, 2)));
        append(MIR_new_insn(ctx_, MIR_ADD, reg(end_reg), reg(end_reg), reg(string_len_reg)));
        append(MIR_new_insn(ctx_, MIR_UBGT, label(fail_target), reg(end_reg), reg(len_reg_)));
        call_with_result("read_varstr", string_reg, {reg(buf_reg_), reg(off_reg_)});
        append(MIR_new_insn(ctx_, MIR_BEQ, label(fail_target), reg(string_reg), MIR_new_int_op(ctx_, 0)));
        advance_off(2);
        append(MIR_new_insn(ctx_, MIR_ADD, reg(off_reg_), reg(off_reg_), reg(string_len_reg)));
        return string_reg;
    }

    MIR_reg_t read_count() {
        MIR_reg_t count_reg = new_reg(MIR_T_I64, "count");
        bounds_check(4, fail_label_);
        append(MIR_new_insn(ctx_, MIR_UEXT32, reg(count_reg), mem(MIR_T_U32)));
        advance_off(4);
        return count_reg;
    }

    void emit_loop(MIR_reg_t count_reg, const std::function<void()>& body) {
        MIR_reg_t index_reg = new_reg(MIR_T_I64, "i");
        MIR_label_t loop_label = MIR_new_label(ctx_);
        MIR_label_t done_label = MIR_new_label(ctx_);

        append(MIR_new_insn(ctx_, MIR_MOV, reg(index_reg), MIR_new_int_op(ctx_, 0)));
        append(loop_label);
        append(MIR_new_insn(ctx_, MIR_UBGE, label(done_label), reg(index_reg), reg(count_reg)));
        body();
        append(MIR_new_insn(ctx_, MIR_ADD, reg(index_reg), reg(index_reg), MIR_new_int_op(ctx_, 1)));
        append(MIR_new_insn(ctx_, MIR_JMP, label(loop_label)));
        append(done_label);
    }

    void maybe_call_with_string(const char* callback, MIR_reg_t container_reg, const std::string& field_name_str,
                                MIR_reg_t string_reg) {
        if (field_name_str.empty()) {
            call_no_result(callback, {reg(container_reg), reg(string_reg)});
        } else {
            call_no_result(callback, {reg(container_reg), field_name(field_name_str), reg(string_reg)});
        }
        call_no_result("free", {reg(string_reg)});
    }

    template <typename Body>
    void emit_repeated_field(const EmitField& field, const char* reg_name, const char* create_callback,
                             const char* set_callback, Body&& body) {
        MIR_reg_t count_reg = read_count();
        MIR_reg_t container_reg = new_reg(MIR_T_I64, reg_name);
        MIR_label_t cleanup_label = MIR_new_label(ctx_);
        MIR_label_t done_label = MIR_new_label(ctx_);
        call_with_result(create_callback, container_reg, {});
        append(MIR_new_insn(ctx_, MIR_BEQ, label(fail_label_), reg(container_reg), MIR_new_int_op(ctx_, 0)));

        emit_loop(count_reg, [&]() { body(container_reg, cleanup_label); });
        call_no_result(set_callback, {reg(obj_reg_), field_name(field.name), reg(container_reg)});
        append(MIR_new_insn(ctx_, MIR_JMP, label(done_label)));
        append(cleanup_label);
        call_no_result("destroy_value", {reg(container_reg)});
        append(MIR_new_insn(ctx_, MIR_JMP, label(fail_label_)));
        append(done_label);
    }

    template <typename ValueEmitter>
    void emit_map_string_field(const EmitField& field, ValueEmitter&& emit_entry) {
        emit_repeated_field(field, "map", "create_map", "set_map", [&](MIR_reg_t map_reg, MIR_label_t fail_target) {
            bounds_check(2, fail_target);
            MIR_reg_t key_reg = read_var_string(fail_target);
            emit_entry(map_reg, key_reg, fail_target);
        });
    }

    template <typename ValueEmitter>
    void emit_map_string_scalar_field(const EmitField& field, const char* add_callback,
                                      uint64_t value_size, ValueEmitter&& emit_value) {
        emit_map_string_field(field, [&](MIR_reg_t map_reg, MIR_reg_t key_reg, MIR_label_t fail_target) {
            MIR_label_t free_key_and_fail = MIR_new_label(ctx_);
            MIR_label_t done = MIR_new_label(ctx_);
            MIR_op_t value_op = emit_value(free_key_and_fail);

            call_no_result(add_callback, {reg(map_reg), reg(key_reg), value_op});
            call_no_result("free", {reg(key_reg)});
            advance_off(value_size);
            append(MIR_new_insn(ctx_, MIR_JMP, label(done)));

            append(free_key_and_fail);
            call_no_result("free", {reg(key_reg)});
            append(MIR_new_insn(ctx_, MIR_JMP, label(fail_target)));

            append(done);
        });
    }

    void emit_primitive(const EmitField& field) {
        bounds_check(field.value_size, fail_label_);

        switch (field.value_type) {
            case FT_Boolean:
            case FT_Int:
                call_no_result("set_int", {reg(obj_reg_), field_name(field.name), int_value_op(field.value_type)});
                break;
            case FT_Long:
                call_no_result("set_int64", {reg(obj_reg_), field_name(field.name), int_value_op(field.value_type)});
                break;
            case FT_Float:
            case FT_Double:
                call_no_result("set_dbl", {reg(obj_reg_), field_name(field.name), double_value_op(field.value_type)});
                break;
            default:
                break;
        }

        advance_off(field.value_size);
    }

    void emit_var_string(const EmitField& field) {
        bounds_check(2, fail_label_);
        MIR_reg_t string_reg = read_var_string();
        maybe_call_with_string("set_str", obj_reg_, field.name, string_reg);
    }

    void emit_list_int(const EmitField& field) {
        emit_repeated_field(field, "list", "create_list", "set_list", [&](MIR_reg_t list_reg, MIR_label_t fail_target) {
            bounds_check(field.value_size, fail_target);
            call_no_result("add_list_int", {reg(list_reg), int_value_op(field.value_type)});
            advance_off(field.value_size);
        });
    }

    void emit_list_int64(const EmitField& field) {
        emit_repeated_field(field, "list", "create_list", "set_list", [&](MIR_reg_t list_reg, MIR_label_t fail_target) {
            bounds_check(field.value_size, fail_target);
            call_no_result("add_list_int64", {reg(list_reg), mem(MIR_T_I64)});
            advance_off(field.value_size);
        });
    }

    void emit_list_double(const EmitField& field) {
        emit_repeated_field(field, "list", "create_list", "set_list", [&](MIR_reg_t list_reg, MIR_label_t fail_target) {
            bounds_check(field.value_size, fail_target);
            call_no_result("add_list_dbl", {reg(list_reg), double_value_op(field.value_type)});
            advance_off(field.value_size);
        });
    }

    void emit_list_string(const EmitField& field) {
        emit_repeated_field(field, "list", "create_list", "set_list", [&](MIR_reg_t list_reg, MIR_label_t fail_target) {
            bounds_check(2, fail_target);
            MIR_reg_t string_reg = read_var_string(fail_target);
            maybe_call_with_string("add_list_str", list_reg, "", string_reg);
        });
    }

    void emit_set_int(const EmitField& field) {
        emit_repeated_field(field, "set", "create_set", "set_set", [&](MIR_reg_t set_reg, MIR_label_t fail_target) {
            bounds_check(field.value_size, fail_target);
            call_no_result("add_set_int", {reg(set_reg), int_value_op(field.value_type)});
            advance_off(field.value_size);
        });
    }

    void emit_set_double(const EmitField& field) {
        emit_repeated_field(field, "set", "create_set", "set_set", [&](MIR_reg_t set_reg, MIR_label_t fail_target) {
            bounds_check(field.value_size, fail_target);
            call_no_result("add_set_dbl", {reg(set_reg), double_value_op(field.value_type)});
            advance_off(field.value_size);
        });
    }

    void emit_set_string(const EmitField& field) {
        emit_repeated_field(field, "set", "create_set", "set_set", [&](MIR_reg_t set_reg, MIR_label_t fail_target) {
            bounds_check(2, fail_target);
            MIR_reg_t string_reg = read_var_string(fail_target);
            maybe_call_with_string("add_set_str", set_reg, "", string_reg);
        });
    }

    void emit_set_int64(const EmitField& field) {
        emit_repeated_field(field, "set", "create_set", "set_set", [&](MIR_reg_t set_reg, MIR_label_t fail_target) {
            bounds_check(field.value_size, fail_target);
            call_no_result("add_set_int64", {reg(set_reg), mem(MIR_T_I64)});
            advance_off(field.value_size);
        });
    }

    void emit_map_string_string(const EmitField& field) {
        emit_map_string_field(field, [&](MIR_reg_t map_reg, MIR_reg_t key_reg, MIR_label_t fail_target) {
            MIR_label_t free_key_and_fail = MIR_new_label(ctx_);
            MIR_label_t done = MIR_new_label(ctx_);

            bounds_check(2, free_key_and_fail);
            MIR_reg_t value_reg = read_var_string(free_key_and_fail);

            call_no_result("add_map_str_str", {reg(map_reg), reg(key_reg), reg(value_reg)});
            call_no_result("free", {reg(key_reg)});
            call_no_result("free", {reg(value_reg)});
            append(MIR_new_insn(ctx_, MIR_JMP, label(done)));

            append(free_key_and_fail);
            call_no_result("free", {reg(key_reg)});
            append(MIR_new_insn(ctx_, MIR_JMP, label(fail_target)));

            append(done);
        });
    }

    void emit_map_string_int(const EmitField& field) {
        emit_map_string_scalar_field(field, "add_map_str_int", field.value_size, [&](MIR_label_t fail_target) {
            bounds_check(field.value_size, fail_target);
            return int_value_op(field.value_type);
        });
    }

    void emit_map_string_double(const EmitField& field) {
        emit_map_string_scalar_field(field, "add_map_str_dbl", field.value_size, [&](MIR_label_t fail_target) {
            bounds_check(field.value_size, fail_target);
            return double_value_op(field.value_type);
        });
    }

    void emit_map_string_int64(const EmitField& field) {
        emit_map_string_scalar_field(field, "add_map_str_int64", field.value_size, [&](MIR_label_t fail_target) {
            bounds_check(field.value_size, fail_target);
            return mem(MIR_T_I64);
        });
    }

    void emit_field(const EmitField& field) {
        switch (field.kind) {
            case EmitKind::Primitive: emit_primitive(field); break;
            case EmitKind::VarString: emit_var_string(field); break;
            case EmitKind::ListInt: emit_list_int(field); break;
            case EmitKind::ListInt64: emit_list_int64(field); break;
            case EmitKind::ListDouble: emit_list_double(field); break;
            case EmitKind::ListString: emit_list_string(field); break;
            case EmitKind::SetInt: emit_set_int(field); break;
            case EmitKind::SetInt64: emit_set_int64(field); break;
            case EmitKind::SetDouble: emit_set_double(field); break;
            case EmitKind::SetString: emit_set_string(field); break;
            case EmitKind::MapStringString: emit_map_string_string(field); break;
            case EmitKind::MapStringInt: emit_map_string_int(field); break;
            case EmitKind::MapStringInt64: emit_map_string_int64(field); break;
            case EmitKind::MapStringDouble: emit_map_string_double(field); break;
        }
    }

    ModuleBuilder& builder_;
    MIR_context_t ctx_;
    const std::vector<EmitField>& fields_;
    MIR_item_t func_item_ = nullptr;
    MIR_func_t func_ = nullptr;
    MIR_reg_t buf_reg_ = 0;
    MIR_reg_t len_reg_ = 0;
    MIR_reg_t off_reg_ = 0;
    MIR_reg_t obj_reg_ = 0;
    MIR_label_t fail_label_ = nullptr;
    size_t next_reg_id_ = 0;
};

void declare_externals(ModuleBuilder& builder, const BinaryAbiRequirements& requirements) {
    builder.declare_external("create_obj", {MIR_T_P}, {});
    builder.declare_external("destroy_value", {}, {{MIR_T_P, "value", 0}});
    if (requirements.set_int)
        builder.declare_external("set_int", {}, {{MIR_T_P, "obj", 0}, {MIR_T_P, "name", 0}, {MIR_T_I32, "value", 0}});
    if (requirements.set_int64)
        builder.declare_external("set_int64", {},
                                 {{MIR_T_P, "obj", 0}, {MIR_T_P, "name", 0}, {MIR_T_I64, "value", 0}});
    if (requirements.set_dbl)
        builder.declare_external("set_dbl", {}, {{MIR_T_P, "obj", 0}, {MIR_T_P, "name", 0}, {MIR_T_D, "value", 0}});
    if (requirements.set_str)
        builder.declare_external("set_str", {}, {{MIR_T_P, "obj", 0}, {MIR_T_P, "name", 0}, {MIR_T_P, "value", 0}});
    if (requirements.read_varstr)
        builder.declare_external("read_varstr", {MIR_T_P},
                                 {{MIR_T_P, "buf", 0}, {MIR_T_I64, "offset", 0}});
    if (requirements.free_string)
        builder.declare_external("free", {}, {{MIR_T_P, "ptr", 0}});

    if (requirements.create_list)
        builder.declare_external("create_list", {MIR_T_P}, {});
    if (requirements.add_list_int)
        builder.declare_external("add_list_int", {}, {{MIR_T_P, "list", 0}, {MIR_T_I32, "value", 0}});
    if (requirements.add_list_int64)
        builder.declare_external("add_list_int64", {}, {{MIR_T_P, "list", 0}, {MIR_T_I64, "value", 0}});
    if (requirements.add_list_dbl)
        builder.declare_external("add_list_dbl", {}, {{MIR_T_P, "list", 0}, {MIR_T_D, "value", 0}});
    if (requirements.add_list_str)
        builder.declare_external("add_list_str", {}, {{MIR_T_P, "list", 0}, {MIR_T_P, "value", 0}});
    if (requirements.set_list)
        builder.declare_external("set_list", {}, {{MIR_T_P, "obj", 0}, {MIR_T_P, "name", 0}, {MIR_T_P, "list", 0}});

    if (requirements.create_set)
        builder.declare_external("create_set", {MIR_T_P}, {});
    if (requirements.add_set_int)
        builder.declare_external("add_set_int", {}, {{MIR_T_P, "set", 0}, {MIR_T_I32, "value", 0}});
    if (requirements.add_set_int64)
        builder.declare_external("add_set_int64", {}, {{MIR_T_P, "set", 0}, {MIR_T_I64, "value", 0}});
    if (requirements.add_set_dbl)
        builder.declare_external("add_set_dbl", {}, {{MIR_T_P, "set", 0}, {MIR_T_D, "value", 0}});
    if (requirements.add_set_str)
        builder.declare_external("add_set_str", {}, {{MIR_T_P, "set", 0}, {MIR_T_P, "value", 0}});
    if (requirements.set_set)
        builder.declare_external("set_set", {}, {{MIR_T_P, "obj", 0}, {MIR_T_P, "name", 0}, {MIR_T_P, "set", 0}});

    if (requirements.create_map)
        builder.declare_external("create_map", {MIR_T_P}, {});
    if (requirements.add_map_str_str)
        builder.declare_external("add_map_str_str", {},
                                 {{MIR_T_P, "map", 0}, {MIR_T_P, "key", 0}, {MIR_T_P, "value", 0}});
    if (requirements.add_map_str_int)
        builder.declare_external("add_map_str_int", {},
                                 {{MIR_T_P, "map", 0}, {MIR_T_P, "key", 0}, {MIR_T_I32, "value", 0}});
    if (requirements.add_map_str_int64)
        builder.declare_external("add_map_str_int64", {},
                                 {{MIR_T_P, "map", 0}, {MIR_T_P, "key", 0}, {MIR_T_I64, "value", 0}});
    if (requirements.add_map_str_dbl)
        builder.declare_external("add_map_str_dbl", {},
                                 {{MIR_T_P, "map", 0}, {MIR_T_P, "key", 0}, {MIR_T_D, "value", 0}});
    if (requirements.set_map)
        builder.declare_external("set_map", {}, {{MIR_T_P, "obj", 0}, {MIR_T_P, "name", 0}, {MIR_T_P, "map", 0}});
}

}  // namespace

bool collect_binary_abi_requirements(const std::vector<ParsedDeclaration>& decls,
                                     const std::vector<ParsedEnum>& enums,
                                     BinaryAbiRequirements& requirements,
                                     std::string* error_message) {
    requirements = {};

    std::map<std::string, ParsedDeclaration> declarations;
    for (const auto& declaration : decls) {
        declarations.emplace(declaration.type_name, declaration);
    }

    std::map<std::string, ParsedEnum> enum_map;
    for (const auto& enum_decl : enums) {
        enum_map.emplace(enum_decl.enum_name, enum_decl);
    }

    for (const auto& declaration : decls) {
        std::vector<EmitField> fields;
        if (!flatten_fields(fields, declaration.fields, declarations, enum_map, nullptr, error_message)) {
            return false;
        }
        collect_abi_requirements_from_fields(fields, requirements);
    }

    return true;
}

MIR_module_t mir_generate_parsers(MIR_context_t ctx, const std::vector<ParsedDeclaration>& decls,
                                  const std::vector<ParsedEnum>& enums,
                                  std::string* error_message) {
    if (ctx == nullptr) {
        return nullptr;
    }

    std::map<std::string, ParsedDeclaration> declarations;
    for (const auto& declaration : decls) {
        declarations.emplace(declaration.type_name, declaration);
    }

    std::map<std::string, ParsedEnum> enum_map;
    for (const auto& enum_decl : enums) {
        enum_map.emplace(enum_decl.enum_name, enum_decl);
    }

    BinaryAbiRequirements requirements;
    if (!collect_binary_abi_requirements(decls, enums, requirements, error_message)) {
        return nullptr;
    }

    ModuleBuilder builder(ctx);
    declare_externals(builder, requirements);

    for (const auto& declaration : decls) {
        std::vector<EmitField> fields;
        if (!flatten_fields(fields, declaration.fields, declarations, enum_map, nullptr, error_message)) {
            return nullptr;
        }
        FunctionEmitter emitter(builder, declaration, fields);
        (void)emitter;
    }

    MIR_finish_module(ctx);
    return builder.module();
}

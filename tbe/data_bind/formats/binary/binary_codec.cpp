/**
 * @file binary_codec.cpp
 * @brief Binary format codec with MIR JIT compilation
 */

#include "binary_codec.h"
#include "core/rfl_parser_state.hpp"
#include <mir.h>
#include <mir-gen.h>
#include <c2mir/c2mir.h>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>
#include <map>

extern "C" {
extern char* tbe_read_varstring(const uint8_t* buf, size_t offset);
}

/* ───── String buffer for code generation ───── */

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} strbuf_t;

static void sb_init(strbuf_t* sb, size_t initial) {
    sb->cap = initial > 0 ? initial : 4096;
    sb->data = static_cast<char*>(malloc(sb->cap));
    sb->data[0] = '\0';
    sb->len = 0;
}

static void sb_append(strbuf_t* sb, const char* s) {
    size_t slen = strlen(s);
    while (sb->len + slen + 1 > sb->cap) {
        sb->cap *= 2;
        sb->data = static_cast<char*>(realloc(sb->data, sb->cap));
    }
    memcpy(sb->data + sb->len, s, slen + 1);
    sb->len += slen;
}

static void sb_appendf(strbuf_t* sb, const char* fmt, ...) {
    char tmp[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    sb_append(sb, tmp);
}

static void sb_free(strbuf_t* sb) {
    free(sb->data);
    sb->data = NULL;
    sb->len = sb->cap = 0;
}

/* ───── c2mir string reader ───── */

typedef struct {
    const char* code;
    size_t pos;
    size_t len;
} str_reader_t;

static int str_getc(void* data) {
    str_reader_t* r = static_cast<str_reader_t*>(data);
    return r->pos >= r->len ? EOF : (unsigned char)r->code[r->pos++];
}

/* ───── Code generation helpers ───── */

static size_t get_field_size(FieldType type) {
    switch (type) {
        case FT_Int: return 4;
        case FT_Long: return 8;
        case FT_Float: return 4;
        case FT_Double: return 8;
        case FT_Boolean: return 1;
        default: return 0;
    }
}

static const char* get_c_type_from_field(FieldType type) {
    switch (type) {
        case FT_Int: return "int";
        case FT_Long: return "long long";
        case FT_Float: return "float";
        case FT_Double: return "double";
        case FT_Boolean: return "unsigned char";
        default: return "int";
    }
}

static bool is_float_field(FieldType type) {
    return type == FT_Float || type == FT_Double;
}

static bool is_long_field(FieldType type) {
    return type == FT_Long;
}

static void gen_field_read(strbuf_t* sb, const ParsedField& field,
                           const std::map<std::string, ParsedDeclaration>& composites) {
    const char* fname = field.name.c_str();
    size_t sz = get_field_size(field.type);

    if (field.type == FT_Object && !field.type_params.empty()) {
        const std::string& comp_type = field.type_params[0].custom_type;
        auto it = composites.find(comp_type);
        if (it != composites.end()) {
            for (const auto& subfield : it->second.fields) {
                size_t subsz = get_field_size(subfield.type);
                if (subsz == 0) continue;

                sb_appendf(sb, "  if (off + %zu > len) return (void*)0;\n", subsz);

                if (is_float_field(subfield.type)) {
                    sb_appendf(sb, "  set_dbl(obj, \"%s.%s\", (double)*(%s*)(buf + off));\n",
                               fname, subfield.name.c_str(), get_c_type_from_field(subfield.type));
                } else if (is_long_field(subfield.type)) {
                    sb_appendf(sb, "  set_dbl(obj, \"%s.%s\", (double)*(long long*)(buf + off));\n",
                               fname, subfield.name.c_str());
                } else {
                    sb_appendf(sb, "  set_int(obj, \"%s.%s\", (int)*(%s*)(buf + off));\n",
                               fname, subfield.name.c_str(), get_c_type_from_field(subfield.type));
                }
                sb_appendf(sb, "  off += %zu;\n", subsz);
            }
        }
        return;
    }

    if (sz == 0) return;

    sb_appendf(sb, "  if (off + %zu > len) return (void*)0;\n", sz);

    if (is_float_field(field.type)) {
        sb_appendf(sb, "  set_dbl(obj, \"%s\", (double)*(%s*)(buf + off));\n",
                   fname, get_c_type_from_field(field.type));
    } else if (is_long_field(field.type)) {
        sb_appendf(sb, "  set_dbl(obj, \"%s\", (double)*(long long*)(buf + off));\n", fname);
    } else {
        sb_appendf(sb, "  set_int(obj, \"%s\", (int)*(%s*)(buf + off));\n",
                   fname, get_c_type_from_field(field.type));
    }
    sb_appendf(sb, "  off += %zu;\n", sz);
}

static char* gen_all_parsers_c(const std::vector<ParsedDeclaration>& declarations) {
    strbuf_t sb;
    sb_init(&sb, 8192);

    sb_append(&sb,
        "typedef unsigned long long size_t;\n"
        "extern void* create_obj(void);\n"
        "extern void set_int(void* obj, const char* name, int val);\n"
        "extern void set_dbl(void* obj, const char* name, double val);\n"
        "extern void set_str(void* obj, const char* name, const char* val);\n"
        "\n"
    );

    std::map<std::string, ParsedDeclaration> composites;
    for (const auto& decl : declarations) {
        composites[decl.type_name] = decl;
    }

    for (const auto& decl : declarations) {
        sb_appendf(&sb, "void* parse_%s(const unsigned char* buf, size_t len) {\n",
                   decl.type_name.c_str());
        sb_append(&sb, "  size_t off = 0;\n");
        sb_append(&sb, "  void* obj = create_obj();\n");
        sb_append(&sb, "  if (!obj) return (void*)0;\n");

        for (const auto& field : decl.fields) {
            gen_field_read(&sb, field, composites);
        }

        sb_append(&sb, "  return obj;\n");
        sb_append(&sb, "}\n\n");
    }

    return sb.data;
}

/* ───── BinaryCodec Implementation ───── */

BinaryCodec::BinaryCodec(const std::vector<ParsedDeclaration>& declarations,
                         const DataBindValueApi* api)
    : declarations_(declarations), api_(*api), func_head_(nullptr), ctx_(nullptr) {

    error_[0] = '\0';

    char* c_source = gen_all_parsers_c(declarations_);
    if (!c_source) {
        snprintf(error_, sizeof(error_), "Failed to generate C source");
        return;
    }

    if (!compile_and_link(c_source)) {
        free(c_source);
        return;
    }
    free(c_source);

    MIR_module_t module = DLIST_TAIL(MIR_module_t, *MIR_get_module_list(ctx_));
    MIR_gen_finish(ctx_);
    register_parse_functions(module);
    c2mir_finish(ctx_);
}

BinaryCodec::~BinaryCodec() {
    mir_func_node* n = func_head_;
    while (n) {
        mir_func_node* next = n->next;
        free(n->type_name);
        free(n);
        n = next;
    }

    if (ctx_) MIR_finish(ctx_);
}

Value* BinaryCodec::parse(const char* type_name, const uint8_t* data, size_t len) {
    if (!type_name || !data) return nullptr;

    for (mir_func_node* n = func_head_; n; n = n->next) {
        if (strcmp(n->type_name, type_name) == 0) {
            Value* (*parse_fn)(const uint8_t*, size_t) = (Value* (*)(const uint8_t*, size_t))n->parse_fn;
            return parse_fn(data, len);
        }
    }

    snprintf(error_, sizeof(error_), "Type not found: %s", type_name);
    return nullptr;
}

const char* BinaryCodec::get_error() const {
    return error_[0] ? error_ : "Unknown error";
}

bool BinaryCodec::compile_and_link(const char* c_source) {
    ctx_ = MIR_init();
    c2mir_init(ctx_);

    struct c2mir_options opts = {0};
    opts.message_file = stderr;

    str_reader_t reader = {.code = c_source, .pos = 0, .len = strlen(c_source)};

    if (!c2mir_compile(ctx_, &opts, str_getc, &reader, "data_bind_gen.c", NULL)) {
        snprintf(error_, sizeof(error_), "c2mir compilation failed");
        c2mir_finish(ctx_);
        MIR_finish(ctx_);
        return false;
    }

    MIR_module_t module = DLIST_TAIL(MIR_module_t, *MIR_get_module_list(ctx_));
    if (!module) {
        snprintf(error_, sizeof(error_), "No module produced by c2mir");
        c2mir_finish(ctx_);
        MIR_finish(ctx_);
        return false;
    }

    MIR_load_module(ctx_, module);

    MIR_load_external(ctx_, "create_obj", api_.create_object);
    MIR_load_external(ctx_, "set_int", api_.set_field_int);
    MIR_load_external(ctx_, "set_dbl", api_.set_field_double);
    MIR_load_external(ctx_, "set_str", api_.set_field_string);
    if (api_.set_field_bytes)
        MIR_load_external(ctx_, "set_bytes", api_.set_field_bytes);
    MIR_load_external(ctx_, "read_varstr", tbe_read_varstring);

    MIR_gen_init(ctx_);
    MIR_link(ctx_, MIR_set_gen_interface, NULL);

    return true;
}

void BinaryCodec::register_parse_functions(MIR_module_t module) {
    for (const auto& decl : declarations_) {
        const char* msg_name = decl.type_name.c_str();

        char func_name[256];
        snprintf(func_name, sizeof(func_name), "parse_%s", msg_name);

        for (MIR_item_t item = DLIST_HEAD(MIR_item_t, module->items); item;
             item = DLIST_NEXT(MIR_item_t, item)) {
            if (item->item_type == MIR_func_item &&
                strcmp(item->u.func->name, func_name) == 0) {

                mir_func_node* node = (mir_func_node*)malloc(sizeof(mir_func_node));
                node->type_name = strdup(msg_name);
                node->parse_fn = item->addr;
                node->next = func_head_;
                func_head_ = node;
                break;
            }
        }
    }
}

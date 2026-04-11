/**
 * @file data_bind_multi.cpp
 * @brief Multi-format data binding - wraps binary and JSON codecs
 */

#include "data_bind.h"
#include "formats/json/json_codec.h"
#include "formats/csv/csv_codec.h"
#include "formats/binary/binary_codec.h"
#include "core/rfl_parser_state.hpp"
#include "core/errors.hpp"
#include "rfl_parser_impl.hpp"
#include "turbo_fs.h"
#include <cstring>
#include <fmt.h>

static char g_last_error[256] = {0};

struct DataBindMulti {
    DataBindFormat format;
    union {
        BinaryCodec* binary;
        JsonCodec* json;
        CsvCodec* csv;
    } impl;
};

/* ───── Shared codec creation from parsed declarations ───── */

static DataBind* create_from_decls(
    const std::vector<ParsedDeclaration>& decls,
    const std::vector<ParsedEnum>& enums,
    DataBindFormat format,
    const DataBindValueApi* api
) {
    DataBindMulti* db = new DataBindMulti();
    db->format = format;

    switch (format) {
        case DATA_BIND_FORMAT_BINARY:
            db->impl.binary = new BinaryCodec(decls, enums, api);
            if (!db->impl.binary->is_valid()) {
                fmt(g_last_error, sizeof(g_last_error), "{}", db->impl.binary->get_error());
                delete db->impl.binary;
                delete db;
                return nullptr;
            }
            break;

        case DATA_BIND_FORMAT_JSON:
            db->impl.json = new JsonCodec(decls, enums, api);
            if (!db->impl.json->is_valid()) {
                fmt(g_last_error, sizeof(g_last_error), "{}", db->impl.json->get_error());
                delete db->impl.json;
                delete db;
                return nullptr;
            }
            break;

        case DATA_BIND_FORMAT_CSV:
            db->impl.csv = new CsvCodec(decls, enums, api);
            if (!db->impl.csv->is_valid()) {
                fmt(g_last_error, sizeof(g_last_error), "{}", db->impl.csv->get_error());
                delete db->impl.csv;
                delete db;
                return nullptr;
            }
            break;

        default:
            fmt(g_last_error, sizeof(g_last_error), "Unknown format: {}", format);
            delete db;
            return nullptr;
    }

    return reinterpret_cast<DataBind*>(db);
}

extern "C" {

DataBind* data_bind_create_ex(const char* rfl_path, DataBindFormat format, const DataBindValueApi* api) {
    g_last_error[0] = '\0';

    if (!rfl_path || !api) {
        fmt(g_last_error, sizeof(g_last_error), "Invalid arguments");
        return nullptr;
    }

    turbo_fs_buf_t buf;
    if (turbo_fs_read_file(rfl_path, &buf) != 0) {
        fmt(g_last_error, sizeof(g_last_error), "Cannot open RFL file: {}", rfl_path);
        return nullptr;
    }
    std::string rfl_content(buf.base, buf.len);
    turbo_fs_buf_free(&buf);

    std::vector<StructuredError> errors;
    parser_state state = rfl_parse_lemon(rfl_content, rfl_path, errors);

    if (!errors.empty()) {
        fmt(g_last_error, sizeof(g_last_error), "Parse error at line {}: {}",
                 errors[0].line, errors[0].message);
        return nullptr;
    }

    if (state.parsed_declarations.empty()) {
        fmt(g_last_error, sizeof(g_last_error), "No declarations found");
        return nullptr;
    }

    return create_from_decls(state.parsed_declarations, state.parsed_enums, format, api);
}

DataBind* data_bind_create(const char* rfl_path, const DataBindValueApi* api) {
    return data_bind_create_ex(rfl_path, DATA_BIND_FORMAT_BINARY, api);
}

void data_bind_free(DataBind* codec) {
    if (!codec) return;

    DataBindMulti* db = reinterpret_cast<DataBindMulti*>(codec);

    switch (db->format) {
        case DATA_BIND_FORMAT_BINARY:
            delete db->impl.binary;
            break;
        case DATA_BIND_FORMAT_JSON:
            delete db->impl.json;
            break;
        case DATA_BIND_FORMAT_CSV:
            delete db->impl.csv;
            break;
        default:
            break;
    }

    delete db;
}

Value* data_bind_parse(DataBind* codec, const char* type_name,
                       const uint8_t* buf, size_t len) {
    if (!codec) return nullptr;

    DataBindMulti* db = reinterpret_cast<DataBindMulti*>(codec);

    switch (db->format) {
        case DATA_BIND_FORMAT_BINARY:
            return db->impl.binary->parse(type_name, buf, len);
        case DATA_BIND_FORMAT_JSON:
            return db->impl.json->parse(type_name, buf, len);
        case DATA_BIND_FORMAT_CSV:
            return db->impl.csv->parse(type_name, buf, len);
        default:
            return nullptr;
    }
}

Value* data_bind_parse_string(DataBind* codec, const char* type_name, const char* data) {
    if (!codec) return nullptr;

    DataBindMulti* db = reinterpret_cast<DataBindMulti*>(codec);

    switch (db->format) {
        case DATA_BIND_FORMAT_JSON:
            return db->impl.json->parse_string(type_name, data);
        case DATA_BIND_FORMAT_CSV:
            return db->impl.csv->parse_string(type_name, data);
        default:
            return nullptr;
    }
}

const char* data_bind_get_error(DataBind* codec) {
    if (!codec) {
        return g_last_error[0] ? g_last_error : "Unknown error";
    }

    DataBindMulti* db = reinterpret_cast<DataBindMulti*>(codec);

    switch (db->format) {
        case DATA_BIND_FORMAT_BINARY:
            return db->impl.binary->get_error();
        case DATA_BIND_FORMAT_JSON:
            return db->impl.json->get_error();
        case DATA_BIND_FORMAT_CSV:
            return db->impl.csv->get_error();
        default:
            return "Unknown format";
    }
}

DataBind* data_bind_create_from_declarations(
    const void** declarations,
    size_t count,
    DataBindFormat format,
    const DataBindValueApi* api
) {
    return data_bind_create_from_declarations_and_enums(
        declarations, count, nullptr, 0, format, api);
}

DataBind* data_bind_create_from_declarations_and_enums(
    const void** declarations,
    size_t declaration_count,
    const void** enums,
    size_t enum_count,
    DataBindFormat format,
    const DataBindValueApi* api
) {
    g_last_error[0] = '\0';

    if (!declarations || declaration_count == 0 || !api) {
        fmt(g_last_error, sizeof(g_last_error), "Invalid arguments");
        return nullptr;
    }

    std::vector<ParsedDeclaration> decls;
    decls.reserve(declaration_count);
    for (size_t i = 0; i < declaration_count; ++i) {
        if (!declarations[i]) {
            fmt(g_last_error, sizeof(g_last_error), "Declaration pointer at index {} is null", i);
            return nullptr;
        }
        decls.push_back(*static_cast<const ParsedDeclaration*>(declarations[i]));
    }

    std::vector<ParsedEnum> parsed_enums;
    if (enum_count > 0) {
        if (!enums) {
            fmt(g_last_error, sizeof(g_last_error), "Enum array is null");
            return nullptr;
        }
        parsed_enums.reserve(enum_count);
        for (size_t i = 0; i < enum_count; ++i) {
            if (!enums[i]) {
                fmt(g_last_error, sizeof(g_last_error), "Enum pointer at index {} is null", i);
                return nullptr;
            }
            parsed_enums.push_back(*static_cast<const ParsedEnum*>(enums[i]));
        }
    }

    return create_from_decls(decls, parsed_enums, format, api);
}

} // extern "C"

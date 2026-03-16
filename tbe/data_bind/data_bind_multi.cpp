/**
 * @file data_bind_multi.cpp
 * @brief Multi-format data binding - wraps binary and JSON codecs
 */

#include "data_bind.h"
#include "formats/json/json_codec.h"
#include "formats/binary/binary_codec.h"
#include "core/rfl_parser_state.hpp"
#include "core/errors.hpp"
#include "rfl_parser_impl.hpp"
#include <cstdio>
#include <cstring>

static char g_last_error[256] = {0};

struct DataBindMulti {
    DataBindFormat format;
    union {
        BinaryCodec* binary;
        JsonCodec* json;
    } impl;
};

extern "C" {

DataBind* data_bind_create_ex(const char* rfl_path, DataBindFormat format, const DataBindValueApi* api) {
    g_last_error[0] = '\0';

    if (!rfl_path || !api) {
        snprintf(g_last_error, sizeof(g_last_error), "Invalid arguments");
        return nullptr;
    }

    // Read RFL file
    FILE* f = fopen(rfl_path, "rb");
    if (!f) {
        snprintf(g_last_error, sizeof(g_last_error), "Cannot open RFL file: %s", rfl_path);
        return nullptr;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::string rfl_content;
    rfl_content.resize(size);
    if (fread(&rfl_content[0], 1, size, f) != (size_t)size) {
        fclose(f);
        snprintf(g_last_error, sizeof(g_last_error), "Failed to read RFL file");
        return nullptr;
    }
    fclose(f);

    // Parse RFL
    std::vector<StructuredError> errors;
    parser_state state = rfl_parse_lemon(rfl_content, rfl_path, errors);

    if (!errors.empty()) {
        snprintf(g_last_error, sizeof(g_last_error), "Parse error at line %zu: %s",
                 errors[0].line, errors[0].message.c_str());
        return nullptr;
    }

    if (state.parsed_declarations.empty()) {
        snprintf(g_last_error, sizeof(g_last_error), "No declarations found");
        return nullptr;
    }

    DataBindMulti* db = new DataBindMulti();
    db->format = format;

    switch (format) {
        case DATA_BIND_FORMAT_BINARY:
            db->impl.binary = new BinaryCodec(state.parsed_declarations, api);
            if (!db->impl.binary) {
                snprintf(g_last_error, sizeof(g_last_error), "Failed to create binary codec");
                delete db;
                return nullptr;
            }
            break;

        case DATA_BIND_FORMAT_JSON:
            db->impl.json = new JsonCodec(state.parsed_declarations, api);
            if (!db->impl.json) {
                snprintf(g_last_error, sizeof(g_last_error), "Failed to create JSON codec");
                delete db;
                return nullptr;
            }
            break;

        case DATA_BIND_FORMAT_CSV:
            snprintf(g_last_error, sizeof(g_last_error), "CSV format not yet implemented");
            delete db;
            return nullptr;

        default:
            snprintf(g_last_error, sizeof(g_last_error), "Unknown format: %d", format);
            delete db;
            return nullptr;
    }

    return reinterpret_cast<DataBind*>(db);
}

DataBind* data_bind_create(const char* rfl_path, const DataBindValueApi* api) {
    // Backward compatible: default to binary format
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
    g_last_error[0] = '\0';

    if (!declarations || count == 0 || !api) {
        snprintf(g_last_error, sizeof(g_last_error), "Invalid arguments");
        return nullptr;
    }

    // Cast back to C++ type
    std::vector<ParsedDeclaration> decls;
    decls.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        decls.push_back(*static_cast<const ParsedDeclaration*>(declarations[i]));
    }

    DataBindMulti* db = new DataBindMulti();
    db->format = format;

    switch (format) {
        case DATA_BIND_FORMAT_BINARY:
            db->impl.binary = new BinaryCodec(decls, api);
            if (!db->impl.binary) {
                snprintf(g_last_error, sizeof(g_last_error), "Failed to create binary codec");
                delete db;
                return nullptr;
            }
            break;

        case DATA_BIND_FORMAT_JSON:
            db->impl.json = new JsonCodec(decls, api);
            if (!db->impl.json) {
                snprintf(g_last_error, sizeof(g_last_error), "Failed to create JSON codec");
                delete db;
                return nullptr;
            }
            break;

        case DATA_BIND_FORMAT_CSV:
            snprintf(g_last_error, sizeof(g_last_error), "CSV format not yet implemented");
            delete db;
            return nullptr;

        default:
            snprintf(g_last_error, sizeof(g_last_error), "Unknown format: %d", format);
            delete db;
            return nullptr;
    }

    return reinterpret_cast<DataBind*>(db);
}

} // extern "C"

#include "codec/codec_registry.hpp"
#include <data_bind.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace rulesforge {

CodecRegistry::CodecRegistry() {
    adapter_ = std::make_unique<FactValueAdapter>(temp_arena_);
}

CodecRegistry::~CodecRegistry() {
    for (auto& [type_name, codec] : codecs_) {
        if (codec) {
            data_bind_free(codec);
        }
    }
    if (json_codec_) {
        data_bind_free(json_codec_);
    }

    // Cleanup DLL handles
    for (auto& [type_name, dll_handle] : dll_codecs_) {
        if (dll_handle && dll_handle->handle) {
#ifdef _WIN32
            FreeLibrary(static_cast<HMODULE>(dll_handle->handle));
#else
            dlclose(dll_handle->handle);
#endif
        }
    }
}

void CodecRegistry::load_declarations(std::vector<ParsedDeclaration> const& declarations) {
    declarations_ = declarations;
}

DataBind* CodecRegistry::get_codec(std::string const& type_name) {
    auto it = codecs_.find(type_name);
    if (it != codecs_.end()) {
        return it->second;
    }

    if (declarations_.empty()) {
        last_error_ = "No declarations loaded";
        return nullptr;
    }

    // Prepare declaration pointers for C API
    std::vector<const void*> decl_ptrs;
    decl_ptrs.reserve(declarations_.size());
    for (auto const& decl : declarations_) {
        decl_ptrs.push_back(&decl);
    }

    // Use new API: create directly from memory
    DataBind* codec = data_bind_create_from_declarations(
        decl_ptrs.data(),
        decl_ptrs.size(),
        DATA_BIND_FORMAT_BINARY,
        adapter_->get_api()
    );

    if (!codec) {
        last_error_ = "Failed to create codec for type: " + type_name;
        return nullptr;
    }

    codecs_[type_name] = codec;
    return codec;
}

Fact* CodecRegistry::parse_binary(rulesforge::FactArena& arena, std::string const& type_name, uint8_t const* buf, size_t len) {
    DataBind* codec = get_codec(type_name);
    if (!codec) {
        return nullptr;
    }

    // Temporarily switch adapter to use the provided arena
    adapter_ = std::make_unique<FactValueAdapter>(arena);

    Value* value = data_bind_parse(codec, type_name.c_str(), buf, len);
    if (!value) {
        last_error_ = std::string(data_bind_get_error(codec));
        return nullptr;
    }

    Fact* fact = adapter_->get_last_fact();
    if (fact) {
        fact->type = type_name;
    }

    // Restore adapter to temp arena
    adapter_ = std::make_unique<FactValueAdapter>(temp_arena_);

    return fact;
}

Fact* CodecRegistry::parse_json(rulesforge::FactArena& arena, std::string const& type_name, std::string const& json_str) {
    // Use data_bind JSON codec instead of JsonFactParser
    DataBind* codec = get_or_create_json_codec();
    if (!codec) {
        return nullptr;
    }

    // Temporarily switch adapter to use the provided arena
    adapter_ = std::make_unique<FactValueAdapter>(arena);

    Value* value = data_bind_parse_string(codec, type_name.c_str(), json_str.c_str());
    if (!value) {
        last_error_ = std::string(data_bind_get_error(codec));
        adapter_ = std::make_unique<FactValueAdapter>(temp_arena_);
        return nullptr;
    }

    // Get the root fact (first created), not the last one
    Fact* fact = adapter_->get_root_fact();
    if (fact) {
        fact->type = type_name;
    }

    // Clean up maps after parsing
    adapter_->clear_maps();

    // Restore adapter to temp arena
    adapter_ = std::make_unique<FactValueAdapter>(temp_arena_);

    return fact;
}

std::vector<Fact*> CodecRegistry::parse_csv(rulesforge::FactArena& arena, std::string const& type_name, std::string const& csv_str) {
    std::vector<Fact*> facts = CsvFactParser::parse(arena, type_name, csv_str, declarations_);
    if (facts.empty()) {
        last_error_ = CsvFactParser::get_last_error();
    }
    return facts;
}

DataBind* CodecRegistry::get_or_create_json_codec() {
    if (json_codec_) {
        return json_codec_;
    }

    if (declarations_.empty()) {
        last_error_ = "No declarations loaded";
        return nullptr;
    }

    // Prepare declaration pointers for C API
    std::vector<const void*> decl_ptrs;
    decl_ptrs.reserve(declarations_.size());
    for (auto const& decl : declarations_) {
        decl_ptrs.push_back(&decl);
    }

    // Create JSON codec from declarations
    json_codec_ = data_bind_create_from_declarations(
        decl_ptrs.data(),
        decl_ptrs.size(),
        DATA_BIND_FORMAT_JSON,
        adapter_->get_api()
    );

    if (!json_codec_) {
        last_error_ = "Failed to create JSON codec";
        return nullptr;
    }

    return json_codec_;
}

bool CodecRegistry::load_codec_from_dll(std::string const& type_name, std::string const& dll_path) {
#ifdef _WIN32
    HMODULE handle = LoadLibraryA(dll_path.c_str());
    if (!handle) {
        last_error_ = "Failed to load DLL: " + dll_path;
        return false;
    }
#else
    void* handle = dlopen(dll_path.c_str(), RTLD_LAZY);
    if (!handle) {
        last_error_ = std::string("Failed to load SO: ") + dlerror();
        return false;
    }
#endif

    // Resolve DataBind vtable symbol: {TypeName}_databind
    std::string symbol = type_name + "_databind";

#ifdef _WIN32
    DataBind* codec = reinterpret_cast<DataBind*>(GetProcAddress(handle, symbol.c_str()));
#else
    DataBind* codec = static_cast<DataBind*>(dlsym(handle, symbol.c_str()));
#endif

    if (!codec) {
#ifdef _WIN32
        FreeLibrary(handle);
#else
        dlclose(handle);
#endif
        last_error_ = "Symbol not found: " + symbol;
        return false;
    }

    // Store DLL handle and codec
    auto dll_handle = std::make_unique<DllHandle>();
    dll_handle->handle = handle;
    dll_handle->codec = codec;
    dll_codecs_[type_name] = std::move(dll_handle);

    last_error_.clear();
    return true;
}

} // namespace rulesforge

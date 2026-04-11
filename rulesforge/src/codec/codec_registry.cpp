#include "codec/codec_registry.hpp"
#include <data_bind.h>


namespace rulesforge {

CodecRegistry::CodecRegistry() {
    adapter_ = std::make_unique<FactValueAdapter>();
}

CodecRegistry::~CodecRegistry() {
    reset_codecs();
}

void CodecRegistry::load_declarations(std::vector<ParsedDeclaration> const& declarations) {
    load_declarations(declarations, {});
}

void CodecRegistry::load_declarations(std::vector<ParsedDeclaration> const& declarations,
                                      std::vector<ParsedEnum> const& enums) {
    reset_codecs();
    declarations_ = declarations;
    enums_ = enums;
    last_error_.clear();
}

Fact* CodecRegistry::parse_binary(rulesforge::FactArena& arena, std::string const& type_name, uint8_t const* buf, size_t len) {
    DataBind* codec = get_or_create_binary_codec();
    if (!codec) {
        return nullptr;
    }

    FactValueAdapter::ScopedParse scoped_parse(*adapter_, arena);

    Value* value = data_bind_parse(codec, type_name.c_str(), buf, len);
    if (!value) {
        last_error_ = std::string(data_bind_get_error(codec));
        return nullptr;
    }

    Fact* fact = adapter_->get_last_fact();
    if (fact) {
        fact->type = type_name;
    }

    return fact;
}

Fact* CodecRegistry::parse_json(rulesforge::FactArena& arena, std::string const& type_name, std::string const& json_str) {
    // Use data_bind JSON codec instead of JsonFactParser
    DataBind* codec = get_or_create_json_codec();
    if (!codec) {
        return nullptr;
    }

    FactValueAdapter::ScopedParse scoped_parse(*adapter_, arena);

    Value* value = data_bind_parse_string(codec, type_name.c_str(), json_str.c_str());
    if (!value) {
        last_error_ = std::string(data_bind_get_error(codec));
        return nullptr;
    }

    // Get the root fact (first created), not the last one
    Fact* fact = adapter_->get_root_fact();
    if (fact) {
        fact->type = type_name;
    }

    return fact;
}

std::vector<Fact*> CodecRegistry::parse_csv(rulesforge::FactArena& arena, std::string const& type_name, std::string const& csv_str) {
    if (!adapter_) {
        last_error_ = "DataBind adapter not initialized";
        return {};
    }

    DataBind* codec = get_or_create_csv_codec();
    if (!codec) {
        return {};
    }

    size_t pre_extra_size = arena.get_extra_facts().size();
    bool had_first = (arena.get_first_fact() != nullptr);

    FactValueAdapter::ScopedParse scoped_parse(*adapter_, arena);

    Value* value = data_bind_parse_string(codec, type_name.c_str(), csv_str.c_str());
    if (!value) {
        last_error_ = std::string(data_bind_get_error(codec));
        return {};
    }

    std::vector<Fact*> facts;
    
    if (!had_first && arena.get_first_fact() != nullptr) {
        facts.push_back(arena.get_first_fact());
    }
    
    const auto& extras = arena.get_extra_facts();
    for (size_t i = pre_extra_size; i < extras.size(); ++i) {
        facts.push_back(extras[i]);
    }

    for (auto* fact : facts) {
        if (fact) fact->type = type_name;
    }

    return facts;
}

namespace {

DataBind* create_codec_impl(
    std::vector<ParsedDeclaration> const& declarations,
    std::vector<ParsedEnum> const& enums,
    DataBindFormat format,
    DataBindValueApi const* api,
    std::string& last_error
) {
    if (declarations.empty()) {
        last_error = "No declarations loaded";
        return nullptr;
    }

    std::vector<const void*> decl_ptrs;
    decl_ptrs.reserve(declarations.size());
    for (auto const& decl : declarations) {
        decl_ptrs.push_back(&decl);
    }

    std::vector<const void*> enum_ptrs;
    enum_ptrs.reserve(enums.size());
    for (auto const& enum_decl : enums) {
        enum_ptrs.push_back(&enum_decl);
    }

    DataBind* codec = data_bind_create_from_declarations_and_enums(
        decl_ptrs.data(),
        decl_ptrs.size(),
        enum_ptrs.empty() ? nullptr : enum_ptrs.data(),
        enum_ptrs.size(),
        format,
        api
    );

    if (!codec) {
        last_error = std::string(data_bind_get_error(nullptr));
        return nullptr;
    }

    return codec;
}

} // namespace

void CodecRegistry::reset_codecs() {
    DataBind* codecs[] = {binary_codec_, json_codec_, csv_codec_};
    for (auto* codec : codecs) {
        if (codec) {
            data_bind_free(codec);
        }
    }
    binary_codec_ = nullptr;
    json_codec_ = nullptr;
    csv_codec_ = nullptr;
}

DataBind* CodecRegistry::get_or_create_binary_codec() {
    if (!binary_codec_) {
        binary_codec_ = create_codec_impl(declarations_, enums_, DATA_BIND_FORMAT_BINARY, adapter_->get_api(), last_error_);
    }
    return binary_codec_;
}

DataBind* CodecRegistry::get_or_create_json_codec() {
    if (!json_codec_) {
        json_codec_ = create_codec_impl(declarations_, enums_, DATA_BIND_FORMAT_JSON, adapter_->get_api(), last_error_);
    }
    return json_codec_;
}

DataBind* CodecRegistry::get_or_create_csv_codec() {
    if (!csv_codec_) {
        csv_codec_ = create_codec_impl(declarations_, enums_, DATA_BIND_FORMAT_CSV, adapter_->get_api(), last_error_);
    }
    return csv_codec_;
}

} // namespace rulesforge

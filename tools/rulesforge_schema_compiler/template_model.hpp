#pragma once

#include "mustache.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace rulesforge_compiler {

struct TemplateNode {
    enum class Type {
        String,
        Map,
        List
    };

    Type type = Type::String;
    std::string name;
    std::string string_value;
    std::vector<std::unique_ptr<TemplateNode>> children;

    explicit TemplateNode(Type t);
};

TemplateNode* add_string(TemplateNode& parent, std::string name, std::string value);
TemplateNode* add_map(TemplateNode& parent, std::string name);
TemplateNode* add_list(TemplateNode& parent, std::string name);
TemplateNode* list_append_map(TemplateNode& list);
TemplateNode* find_child(TemplateNode* parent, char const* name, size_t size);

MUSTACHE_DATAPROVIDER template_provider();

} // namespace rulesforge_compiler

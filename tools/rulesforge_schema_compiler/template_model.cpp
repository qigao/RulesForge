#include "template_model.hpp"

#include <cstring>

namespace rulesforge_compiler {

TemplateNode::TemplateNode(Type t) : type(t) {}

TemplateNode* add_string(TemplateNode& parent, std::string name, std::string value) {
    auto child = std::make_unique<TemplateNode>(TemplateNode::Type::String);
    child->name = std::move(name);
    child->string_value = std::move(value);
    auto* raw = child.get();
    parent.children.push_back(std::move(child));
    return raw;
}

TemplateNode* add_map(TemplateNode& parent, std::string name) {
    auto child = std::make_unique<TemplateNode>(TemplateNode::Type::Map);
    child->name = std::move(name);
    auto* raw = child.get();
    parent.children.push_back(std::move(child));
    return raw;
}

TemplateNode* add_list(TemplateNode& parent, std::string name) {
    auto child = std::make_unique<TemplateNode>(TemplateNode::Type::List);
    child->name = std::move(name);
    auto* raw = child.get();
    parent.children.push_back(std::move(child));
    return raw;
}

TemplateNode* list_append_map(TemplateNode& list) {
    auto child = std::make_unique<TemplateNode>(TemplateNode::Type::Map);
    auto* raw = child.get();
    list.children.push_back(std::move(child));
    return raw;
}

TemplateNode* find_child(TemplateNode* parent, char const* name, size_t size) {
    if (!parent || size == 0 || parent->type != TemplateNode::Type::Map) return nullptr;

    char const* dot = static_cast<char const*>(memchr(name, '.', size));
    if (dot) {
        size_t head_size = static_cast<size_t>(dot - name);
        TemplateNode* head = find_child(parent, name, head_size);
        if (!head) return nullptr;
        return find_child(head, dot + 1, size - head_size - 1);
    }

    for (auto const& child : parent->children) {
        if (child->name.size() == size && std::strncmp(child->name.c_str(), name, size) == 0) {
            return child.get();
        }
    }
    return nullptr;
}

namespace {

void* provider_get_root(void* provider_data) {
    return provider_data;
}

int provider_dump(void* node,
                  int (*out_fn)(char const*, size_t, void*),
                  void* renderer_data,
                  void*) {
    auto* n = static_cast<TemplateNode*>(node);
    if (!n || n->type != TemplateNode::Type::String) return 0;
    return out_fn(n->string_value.c_str(), n->string_value.size(), renderer_data);
}

void* provider_get_child_by_name(void* node,
                                 char const* name,
                                 size_t size,
                                 void*) {
    return find_child(static_cast<TemplateNode*>(node), name, size);
}

void* provider_get_child_by_index(void* node,
                                  unsigned index,
                                  void*) {
    auto* n = static_cast<TemplateNode*>(node);
    if (!n) return nullptr;

    if (n->type == TemplateNode::Type::List || n->type == TemplateNode::Type::Map) {
        if (index < n->children.size()) return n->children[index].get();
        return nullptr;
    }

    return index == 0 ? n : nullptr;
}

MUSTACHE_TEMPLATE* provider_get_partial(char const*, size_t, void*) {
    return nullptr;
}

} // namespace

MUSTACHE_DATAPROVIDER template_provider() {
    MUSTACHE_DATAPROVIDER provider{};
    provider.get_root = &provider_get_root;
    provider.dump = &provider_dump;
    provider.get_child_by_name = &provider_get_child_by_name;
    provider.get_child_by_index = &provider_get_child_by_index;
    provider.get_partial = &provider_get_partial;
    provider.is_lambda = nullptr;
    provider.call_lambda = nullptr;
    return provider;
}

} // namespace rulesforge_compiler

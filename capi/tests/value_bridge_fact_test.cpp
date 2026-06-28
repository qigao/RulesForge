#include "rfl_value_bridge.h"

#include "core/fact.hpp"
#include "core/value_types.hpp"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, char const* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename T>
T const& as(ConstraintValue const& value) {
    auto const* typed = std::get_if<T>(&value);
    require(typed != nullptr, "unexpected ConstraintValue type");
    return *typed;
}

ConstraintValue field(Fact const* fact, std::string const& name) {
    auto value = fact->get_field(name);
    require(value.has_value(), "missing fact field");
    return *value;
}

} // namespace

int main() {
    try {
        Value* root = value_create_object();
        require(root != nullptr, "root allocation failed");

        value_set_field_int(root, "id", 42);
        value_set_field_double(root, "price", 99.5);
        value_set_field_string(root, "name", "plan");

        Value* child = value_create_object();
        value_set_field_string(child, "code", "active");
        value_set_field_object(root, "status", child);

        Value* tiers = value_create_list();
        value_add_list_item_int(tiers, 1);
        value_add_list_item_double(tiers, 2.5);
        Value* tier_obj = value_create_object();
        value_set_field_string(tier_obj, "label", "bulk");
        value_add_list_item_object(tiers, tier_obj);
        value_set_field_list(root, "tiers", tiers);

        Value* tags = value_create_set();
        value_add_set_item_string(tags, "usd");
        value_add_set_item_string(tags, "cny");
        value_set_field_set(root, "currencies", tags);

        Value* attrs = value_create_map();
        value_add_map_entry_string_string(attrs, "region", "global");
        value_add_map_entry_string_int(attrs, "priority", 3);
        value_add_map_entry_string_double(attrs, "discount", 0.2);
        value_set_field_map(root, "attrs", attrs);

        Fact* fact = value_to_fact(root, "PricePlan");
        require(fact != nullptr, "fact conversion failed");
        require(fact->type == "PricePlan", "fact type mismatch");

        ConstraintValue id = field(fact, "id");
        ConstraintValue price = field(fact, "price");
        ConstraintValue name = field(fact, "name");
        require(as<int64_t>(id) == 42, "id mismatch");
        require(as<double>(price) == 99.5, "price mismatch");
        require(as<std::string>(name) == "plan", "name mismatch");

        ConstraintValue status_field = field(fact, "status");
        auto const& status = as<std::shared_ptr<ValueMap>>(status_field);
        require(status != nullptr, "status map missing");
        require(as<std::string>(status->entries.at(std::string("code"))) == "active",
                "status code mismatch");

        ConstraintValue tiers_field = field(fact, "tiers");
        auto const& tiers_value = as<std::shared_ptr<TypedList>>(tiers_field);
        require(tiers_value != nullptr, "tiers list missing");
        require(tiers_value->values.size() == 3, "tiers size mismatch");
        require(as<int64_t>(tiers_value->values[0]) == 1, "tiers[0] mismatch");
        require(as<double>(tiers_value->values[1]) == 2.5, "tiers[1] mismatch");
        auto const& tier_object = as<std::shared_ptr<ValueMap>>(tiers_value->values[2]);
        require(tier_object != nullptr, "tier object missing");
        require(as<std::string>(tier_object->entries.at(std::string("label"))) == "bulk",
                "tier label mismatch");

        ConstraintValue currencies_field = field(fact, "currencies");
        auto const& currencies = as<std::shared_ptr<ValueSet>>(currencies_field);
        require(currencies != nullptr, "currencies set missing");
        require(currencies->values.count(std::string("usd")) == 1, "usd tag missing");
        require(currencies->values.count(std::string("cny")) == 1, "cny tag missing");

        ConstraintValue attrs_field = field(fact, "attrs");
        auto const& attrs_value = as<std::shared_ptr<ValueMap>>(attrs_field);
        require(attrs_value != nullptr, "attrs map missing");
        require(as<std::string>(attrs_value->entries.at(std::string("region"))) == "global",
                "region mismatch");
        require(as<int64_t>(attrs_value->entries.at(std::string("priority"))) == 3,
                "priority mismatch");
        require(as<double>(attrs_value->entries.at(std::string("discount"))) == 0.2,
                "discount mismatch");

        Value* status_copy = value_get_field_object(root, "status");
        require(status_copy != nullptr, "status copy missing");
        require(std::strcmp(value_get_field_string(status_copy, "code"), "active") == 0,
                "status copy mismatch");

        value_free(status_copy);
        value_free_fact(fact);
        value_free(root);
        return 0;
    } catch (std::exception const& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

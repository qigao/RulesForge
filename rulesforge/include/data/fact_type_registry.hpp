#ifndef FACT_TYPE_REGISTRY_HPP
#define FACT_TYPE_REGISTRY_HPP

#include "core/fact.hpp"
#include "data/fact_arena.hpp"

#include <functional>
#include <string>
#include <typeindex>


// A type-erased function that knows how to convert a specific C++ struct (via void*) into a Fact using an arena.
using FactConverter = std::function<Fact*(rulesforge::FactArena&, void const*)>;

class FactTypeRegistry {
public:
    /**
     * @brief Registers a C++ type with the engine.
     * @tparam T The C++ struct type (e.g., Person).
     * @param name The corresponding type name in the RFL (e.g., "Person").
     * @param populator A function that copies fields from the C++ struct to the engine's Fact.
     */
    template <typename T>
    void register_type(std::string const& name, std::function<void(T const&, Fact&)> populator) {
        converters_[std::type_index(typeid(T))] = [name, populator](rulesforge::FactArena& arena, void const* obj_ptr) {
            T const* typed_obj = static_cast<T const*>(obj_ptr);
            auto* fact = arena.create_fact();
            fact->type = name;
            // Use the provided lambda to populate the fact's fields.
            populator(*typed_obj, *fact);
            return fact;
        };
    }

    /**
     * @brief Converts a registered C++ object into a generic Fact pointer using an arena.
     */
    template <typename T>
    Fact* convert(rulesforge::FactArena& arena, T const& obj) const {
        auto it = converters_.find(std::type_index(typeid(T)));
        if (it == converters_.end()) {
            return nullptr;   // This type was not registered.
        }
        return it->second(arena, &obj);
    }

private:
    std::unordered_map<std::type_index, FactConverter> converters_;
};

#endif   // FACT_TYPE_REGISTRY_HPP

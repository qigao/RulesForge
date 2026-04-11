#ifndef FACT_VALUE_ADAPTER_HPP
#define FACT_VALUE_ADAPTER_HPP

#include "core/fact.hpp"
#include "data/fact_arena.hpp"

#include <memory>
#include <unordered_map>
#include <vector>

extern "C" {
#include "data_bind.h"
#include "object_pool.h"
}

namespace rulesforge {

/**
 * @brief Adapter that bridges data_bind's Value API to rulesforge Fact
 *
 * Uses thread_local context to map Value* to Fact*
 */
class FactValueAdapter {
public:
    class ScopedCurrent {
    public:
        explicit ScopedCurrent(FactValueAdapter& adapter);
        ~ScopedCurrent();

        ScopedCurrent(ScopedCurrent const&) = delete;
        ScopedCurrent& operator=(ScopedCurrent const&) = delete;

    private:
        FactValueAdapter* previous_;
    };

    class ScopedParse {
    public:
        ScopedParse(FactValueAdapter& adapter, rulesforge::FactArena& arena);
        ~ScopedParse();

        ScopedParse(ScopedParse const&) = delete;
        ScopedParse& operator=(ScopedParse const&) = delete;

    private:
        FactValueAdapter& adapter_;
        ScopedCurrent current_;
    };

    FactValueAdapter();
    ~FactValueAdapter();

    DataBindValueApi const* get_api() const { return &api_; }

    Fact* get_last_fact() { return last_fact_; }
    Fact* get_root_fact() { return root_fact_; }

private:
    struct HandleSlot {
        enum class Kind : uint8_t {
            Object,
            List,
            Set,
            Map
        };

        Kind kind;
        void* payload;
    };

    // Core API
    static Value* create_object_impl();
    static void set_field_int_impl(Value* obj, const char* name, int32_t val);
    static void set_field_int64_impl(Value* obj, const char* name, int64_t val);
    static void set_field_double_impl(Value* obj, const char* name, double val);
    static void set_field_string_impl(Value* obj, const char* name, const char* val);
    static void set_field_bytes_impl(Value* obj, const char* name, const uint8_t* data, size_t len);

    // Extended API for complex types
    static Value* create_list_impl();
    static void add_list_item_int_impl(Value* list, int32_t val);
    static void add_list_item_int64_impl(Value* list, int64_t val);
    static void add_list_item_double_impl(Value* list, double val);
    static void add_list_item_string_impl(Value* list, const char* val);
    static void add_list_item_object_impl(Value* list, Value* obj);
    static void set_field_list_impl(Value* obj, const char* name, Value* list);
    static void set_field_object_impl(Value* obj, const char* name, Value* child);

    // Extended API for Set
    static Value* create_set_impl();
    static void add_set_item_int_impl(Value* set, int32_t val);
    static void add_set_item_int64_impl(Value* set, int64_t val);
    static void add_set_item_double_impl(Value* set, double val);
    static void add_set_item_string_impl(Value* set, const char* val);
    static void set_field_set_impl(Value* obj, const char* name, Value* set);

    // Extended API for Map
    static Value* create_map_impl();
    static void add_map_entry_string_string_impl(Value* map, const char* key, const char* val);
    static void add_map_entry_string_int_impl(Value* map, const char* key, int32_t val);
    static void add_map_entry_string_int64_impl(Value* map, const char* key, int64_t val);
    static void add_map_entry_string_double_impl(Value* map, const char* key, double val);
    static void set_field_map_impl(Value* obj, const char* name, Value* map);
    static void destroy_value_impl(Value* value);

    Value* register_handle(HandleSlot::Kind kind, void* payload);
    HandleSlot* find_handle(Value* value);
    void release_handle(Value* value);
    void reset_state();
    void begin_parse(rulesforge::FactArena& arena);
    void end_parse();
    static Fact* object_for(Value* value);
    static TypedList* list_for(Value* value);
    static ValueSet* set_for(Value* value);
    static ValueMap* map_for(Value* value);
    std::shared_ptr<TypedList> take_list_owner(TypedList* value);
    std::shared_ptr<ValueSet> take_set_owner(ValueSet* value);
    std::shared_ptr<ValueMap> take_map_owner(ValueMap* value);

    rulesforge::FactArena* arena_ = nullptr;
    DataBindValueApi api_;
    Fact* last_fact_ = nullptr;
    Fact* root_fact_ = nullptr;  // Track the first (root) fact created
    object_pool_t* handle_pool_ = nullptr;
    std::unordered_map<Value*, HandleSlot*> handles_;
    std::unordered_map<TypedList*, std::shared_ptr<TypedList>> list_owners_;
    std::unordered_map<ValueSet*, std::shared_ptr<ValueSet>> set_owners_;
    std::unordered_map<ValueMap*, std::shared_ptr<ValueMap>> map_owners_;

    static thread_local FactValueAdapter* current_adapter_;
};

} // namespace rulesforge

#endif // FACT_VALUE_ADAPTER_HPP

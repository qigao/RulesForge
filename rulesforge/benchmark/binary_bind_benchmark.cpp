#include "data_bind.h"
#include "codec/codec_registry.hpp"
#include "core/constraint_types.hpp"
#include "data/fact_arena.hpp"
#include "tinytest.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

enum ValueKind {
    VALUE_OBJECT,
    VALUE_LIST,
    VALUE_SET,
    VALUE_MAP
};

struct Value {
    ValueKind kind;
    uint64_t hash;
    uint32_t field_count;
    uint32_t item_count;
};

namespace {

static volatile uint64_t g_sink = 0;

static uint64_t mix_u64(uint64_t seed, uint64_t value) {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
    return seed;
}

static uint64_t hash_bytes(const void* data, size_t len) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint64_t hash = 1469598103934665603ULL;
    for (size_t i = 0; i < len; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static Value* alloc_value(ValueKind kind) {
    Value* value = static_cast<Value*>(std::calloc(1, sizeof(Value)));
    if (value != nullptr) value->kind = kind;
    return value;
}

static Value* bench_create_object(void) { return alloc_value(VALUE_OBJECT); }
static Value* bench_create_list(void) { return alloc_value(VALUE_LIST); }
static Value* bench_create_set(void) { return alloc_value(VALUE_SET); }
static Value* bench_create_map(void) { return alloc_value(VALUE_MAP); }

static void bench_set_field_int(Value* obj, const char* name, int32_t value) {
    if (obj == nullptr) return;
    obj->field_count++;
    obj->hash = mix_u64(obj->hash, hash_bytes(name, std::strlen(name)));
    obj->hash = mix_u64(obj->hash, static_cast<uint32_t>(value));
}

static void bench_set_field_int64(Value* obj, const char* name, int64_t value) {
    if (obj == nullptr) return;
    obj->field_count++;
    obj->hash = mix_u64(obj->hash, hash_bytes(name, std::strlen(name)));
    obj->hash = mix_u64(obj->hash, static_cast<uint64_t>(value));
}

static void bench_set_field_double(Value* obj, const char* name, double value) {
    if (obj == nullptr) return;
    obj->field_count++;
    obj->hash = mix_u64(obj->hash, hash_bytes(name, std::strlen(name)));
    obj->hash = mix_u64(obj->hash, hash_bytes(&value, sizeof(value)));
}

static void bench_set_field_string(Value* obj, const char* name, const char* value) {
    if (obj == nullptr) return;
    obj->field_count++;
    obj->hash = mix_u64(obj->hash, hash_bytes(name, std::strlen(name)));
    if (value != nullptr) obj->hash = mix_u64(obj->hash, hash_bytes(value, std::strlen(value)));
}

static void bench_set_field_bytes(Value* obj, const char* name, const uint8_t* data, size_t len) {
    if (obj == nullptr) return;
    obj->field_count++;
    obj->hash = mix_u64(obj->hash, hash_bytes(name, std::strlen(name)));
    if (data != nullptr && len != 0) obj->hash = mix_u64(obj->hash, hash_bytes(data, len));
}

static void bench_add_list_item_int(Value* list, int32_t value) {
    if (list == nullptr) return;
    list->item_count++;
    list->hash = mix_u64(list->hash, static_cast<uint32_t>(value));
}

static void bench_add_list_item_int64(Value* list, int64_t value) {
    if (list == nullptr) return;
    list->item_count++;
    list->hash = mix_u64(list->hash, static_cast<uint64_t>(value));
}

static void bench_add_list_item_double(Value* list, double value) {
    if (list == nullptr) return;
    list->item_count++;
    list->hash = mix_u64(list->hash, hash_bytes(&value, sizeof(value)));
}

static void bench_add_list_item_string(Value* list, const char* value) {
    if (list == nullptr) return;
    list->item_count++;
    if (value != nullptr) list->hash = mix_u64(list->hash, hash_bytes(value, std::strlen(value)));
}

static void bench_set_field_list(Value* obj, const char* name, Value* list) {
    if (obj != nullptr) {
        obj->field_count++;
        obj->hash = mix_u64(obj->hash, hash_bytes(name, std::strlen(name)));
        if (list != nullptr) {
            obj->hash = mix_u64(obj->hash, list->hash);
            obj->hash = mix_u64(obj->hash, list->item_count);
        }
    }
    std::free(list);
}

static void bench_add_set_item_int(Value* set, int32_t value) { bench_add_list_item_int(set, value); }
static void bench_add_set_item_int64(Value* set, int64_t value) { bench_add_list_item_int64(set, value); }
static void bench_add_set_item_double(Value* set, double value) { bench_add_list_item_double(set, value); }
static void bench_add_set_item_string(Value* set, const char* value) { bench_add_list_item_string(set, value); }
static void bench_set_field_set(Value* obj, const char* name, Value* set) { bench_set_field_list(obj, name, set); }

static void bench_add_map_entry_string_string(Value* map, const char* key, const char* value) {
    if (map == nullptr) return;
    map->item_count++;
    if (key != nullptr) map->hash = mix_u64(map->hash, hash_bytes(key, std::strlen(key)));
    if (value != nullptr) map->hash = mix_u64(map->hash, hash_bytes(value, std::strlen(value)));
}

static void bench_add_map_entry_string_int(Value* map, const char* key, int32_t value) {
    if (map == nullptr) return;
    map->item_count++;
    if (key != nullptr) map->hash = mix_u64(map->hash, hash_bytes(key, std::strlen(key)));
    map->hash = mix_u64(map->hash, static_cast<uint32_t>(value));
}

static void bench_add_map_entry_string_int64(Value* map, const char* key, int64_t value) {
    if (map == nullptr) return;
    map->item_count++;
    if (key != nullptr) map->hash = mix_u64(map->hash, hash_bytes(key, std::strlen(key)));
    map->hash = mix_u64(map->hash, static_cast<uint64_t>(value));
}

static void bench_add_map_entry_string_double(Value* map, const char* key, double value) {
    if (map == nullptr) return;
    map->item_count++;
    if (key != nullptr) map->hash = mix_u64(map->hash, hash_bytes(key, std::strlen(key)));
    map->hash = mix_u64(map->hash, hash_bytes(&value, sizeof(value)));
}

static void bench_set_field_map(Value* obj, const char* name, Value* map) { bench_set_field_list(obj, name, map); }
static void bench_destroy_value(Value* value) { std::free(value); }

static DataBindValueApi bench_api = {
    .create_object = bench_create_object,
    .set_field_int = bench_set_field_int,
    .set_field_int64 = bench_set_field_int64,
    .set_field_double = bench_set_field_double,
    .set_field_string = bench_set_field_string,
    .set_field_bytes = bench_set_field_bytes,
    .create_list = bench_create_list,
    .add_list_item_int = bench_add_list_item_int,
    .add_list_item_int64 = bench_add_list_item_int64,
    .add_list_item_double = bench_add_list_item_double,
    .add_list_item_string = bench_add_list_item_string,
    .add_list_item_object = nullptr,
    .set_field_list = bench_set_field_list,
    .set_field_object = nullptr,
    .create_set = bench_create_set,
    .add_set_item_int = bench_add_set_item_int,
    .add_set_item_double = bench_add_set_item_double,
    .add_set_item_string = bench_add_set_item_string,
    .set_field_set = bench_set_field_set,
    .create_map = bench_create_map,
    .add_map_entry_string_string = bench_add_map_entry_string_string,
    .add_map_entry_string_int = bench_add_map_entry_string_int,
    .add_map_entry_string_double = bench_add_map_entry_string_double,
    .set_field_map = bench_set_field_map,
    .add_set_item_int64 = bench_add_set_item_int64,
    .add_map_entry_string_int64 = bench_add_map_entry_string_int64,
    .destroy_value = bench_destroy_value,
};

static void write_schema(const char* path, const char* content) {
    FILE* file = std::fopen(path, "wb");
    check_not_null(file);
    std::fwrite(content, 1, std::strlen(content), file);
    std::fclose(file);
}

static void append_u16(uint8_t* buf, size_t* off, uint16_t value) {
    std::memcpy(buf + *off, &value, sizeof(value));
    *off += sizeof(value);
}

static void append_u32(uint8_t* buf, size_t* off, uint32_t value) {
    std::memcpy(buf + *off, &value, sizeof(value));
    *off += sizeof(value);
}

static void append_i32(uint8_t* buf, size_t* off, int32_t value) {
    std::memcpy(buf + *off, &value, sizeof(value));
    *off += sizeof(value);
}

static void append_i64(uint8_t* buf, size_t* off, int64_t value) {
    std::memcpy(buf + *off, &value, sizeof(value));
    *off += sizeof(value);
}

static void append_f64(uint8_t* buf, size_t* off, double value) {
    std::memcpy(buf + *off, &value, sizeof(value));
    *off += sizeof(value);
}

static void append_varstr(uint8_t* buf, size_t* off, const char* value) {
    const uint16_t len = static_cast<uint16_t>(std::strlen(value));
    append_u16(buf, off, len);
    std::memcpy(buf + *off, value, len);
    *off += len;
}

static size_t build_primitives_payload(uint8_t* buf) {
    size_t off = 0;
    append_i32(buf, &off, 1001);
    append_i32(buf, &off, 42);
    append_i64(buf, &off, 1700000000123LL);
    append_f64(buf, &off, 88.125);
    return off;
}

static size_t build_mixed_payload(uint8_t* buf) {
    size_t off = 0;
    append_i32(buf, &off, 7);
    append_i64(buf, &off, 1700000000456LL);
    append_f64(buf, &off, 1234.567);
    append_varstr(buf, &off, "AAPL");
    append_varstr(buf, &off, "NASDAQ");

    append_u32(buf, &off, 3);
    append_i32(buf, &off, 10);
    append_i32(buf, &off, 20);
    append_i32(buf, &off, 30);

    append_u32(buf, &off, 3);
    append_varstr(buf, &off, "open");
    append_varstr(buf, &off, "auction");
    append_varstr(buf, &off, "close");

    append_u32(buf, &off, 2);
    append_varstr(buf, &off, "bid");
    append_i32(buf, &off, 120);
    append_varstr(buf, &off, "ask");
    append_i32(buf, &off, 125);

    return off;
}

static size_t build_order_scalars_payload(uint8_t* buf) {
    size_t off = 0;
    append_i32(buf, &off, 7);
    append_i64(buf, &off, 1700000000456LL);
    append_f64(buf, &off, 1234.567);
    append_varstr(buf, &off, "AAPL");
    append_varstr(buf, &off, "NASDAQ");
    return off;
}

static size_t build_order_with_levels_payload(uint8_t* buf) {
    size_t off = build_order_scalars_payload(buf);
    append_u32(buf, &off, 3);
    append_i32(buf, &off, 10);
    append_i32(buf, &off, 20);
    append_i32(buf, &off, 30);
    return off;
}

static size_t build_order_with_tags_payload(uint8_t* buf) {
    size_t off = build_order_with_levels_payload(buf);
    append_u32(buf, &off, 3);
    append_varstr(buf, &off, "open");
    append_varstr(buf, &off, "auction");
    append_varstr(buf, &off, "close");
    return off;
}

static const char* build_primitives_json() {
    return "{\"id\":1001,\"qty\":42,\"ts\":1700000000123,\"px\":88.125}";
}

static const char* build_mixed_json() {
    return "{\"id\":7,\"ts\":1700000000456,\"px\":1234.567,"
           "\"symbol\":\"AAPL\",\"venue\":\"NASDAQ\","
           "\"levels\":[10,20,30],"
           "\"tags\":[\"open\",\"auction\",\"close\"],"
           "\"attrs\":{\"bid\":120,\"ask\":125}}";
}

static const char* build_order_scalars_json() {
    return "{\"id\":7,\"ts\":1700000000456,\"px\":1234.567,"
           "\"symbol\":\"AAPL\",\"venue\":\"NASDAQ\"}";
}

static const char* build_order_with_levels_json() {
    return "{\"id\":7,\"ts\":1700000000456,\"px\":1234.567,"
           "\"symbol\":\"AAPL\",\"venue\":\"NASDAQ\","
           "\"levels\":[10,20,30]}";
}

static const char* build_order_with_tags_json() {
    return "{\"id\":7,\"ts\":1700000000456,\"px\":1234.567,"
           "\"symbol\":\"AAPL\",\"venue\":\"NASDAQ\","
           "\"levels\":[10,20,30],"
           "\"tags\":[\"open\",\"auction\",\"close\"]}";
}

static const char* build_cold_json() {
    return "{\"id\":99,\"ts\":1700000000999,\"name\":\"startup\"}";
}

static void consume_value(Value* value) {
    check_not_null(value);
    g_sink ^= value->hash;
    g_sink ^= value->field_count;
    g_sink ^= value->item_count;
    std::free(value);
}

static void consume_fact(Fact* fact) {
    check_not_null(fact);
    g_sink ^= hash_bytes(fact->type.data(), fact->type.size());
    g_sink ^= fact->fields.size();
}

static ParsedField make_field(char const* name, FieldType type) {
    ParsedField field;
    field.name = name;
    field.type = type;
    return field;
}

static ParsedField make_list_field(char const* name, FieldType element_type) {
    ParsedField field;
    field.name = name;
    field.type = FT_List;
    field.type_params.emplace_back(element_type);
    return field;
}

static ParsedField make_map_field(char const* name, FieldType key_type, FieldType value_type) {
    ParsedField field;
    field.name = name;
    field.type = FT_Map;
    field.type_params.emplace_back(key_type);
    field.type_params.emplace_back(value_type);
    return field;
}

static std::vector<ParsedDeclaration> build_registry_declarations() {
    ParsedDeclaration tick;
    tick.type_name = "Tick";
    tick.fields.push_back(make_field("id", FT_Int));
    tick.fields.push_back(make_field("qty", FT_Int));
    tick.fields.push_back(make_field("ts", FT_Long));
    tick.fields.push_back(make_field("px", FT_Double));

    ParsedDeclaration order;
    order.type_name = "Order";
    order.fields.push_back(make_field("id", FT_Int));
    order.fields.push_back(make_field("ts", FT_Long));
    order.fields.push_back(make_field("px", FT_Double));
    order.fields.push_back(make_field("symbol", FT_String));
    order.fields.push_back(make_field("venue", FT_String));
    order.fields.push_back(make_list_field("levels", FT_Int));
    order.fields.push_back(make_list_field("tags", FT_String));
    order.fields.push_back(make_map_field("attrs", FT_String, FT_Int));

    ParsedDeclaration order_scalars;
    order_scalars.type_name = "OrderScalars";
    order_scalars.fields.push_back(make_field("id", FT_Int));
    order_scalars.fields.push_back(make_field("ts", FT_Long));
    order_scalars.fields.push_back(make_field("px", FT_Double));
    order_scalars.fields.push_back(make_field("symbol", FT_String));
    order_scalars.fields.push_back(make_field("venue", FT_String));

    ParsedDeclaration order_levels = order_scalars;
    order_levels.type_name = "OrderLevels";
    order_levels.fields.push_back(make_list_field("levels", FT_Int));

    ParsedDeclaration order_tags = order_levels;
    order_tags.type_name = "OrderTags";
    order_tags.fields.push_back(make_list_field("tags", FT_String));

    return {
        std::move(tick),
        std::move(order),
        std::move(order_scalars),
        std::move(order_levels),
        std::move(order_tags)
    };
}

}  // namespace

suite("DataBind Parse+Bind Benchmarks") {
    group("Hot Parse+Bind") {
        bench("primitive payload throughput") {
            static const char* schema_path = "bench_binary_primitive.rfl";
            write_schema(schema_path,
                         "declare Tick\n"
                         "    id: int\n"
                         "    qty: int\n"
                         "    ts: long\n"
                         "    px: double\n"
                         "end\n");

            DataBind* binary_codec = data_bind_create_ex(schema_path, DATA_BIND_FORMAT_BINARY, &bench_api);
            check_not_null(binary_codec);
            DataBind* json_codec = data_bind_create_ex(schema_path, DATA_BIND_FORMAT_JSON, &bench_api);
            check_not_null(json_codec);

            uint8_t payload[64];
            const size_t payload_len = build_primitives_payload(payload);
            const char* json_payload = build_primitives_json();

            {
                benchmark("binary parse+bind primitives x100k", 5, 100000.0) {
                    for (int i = 0; i < 100000; ++i) {
                        Value* value = data_bind_parse(binary_codec, "Tick", payload, payload_len);
                        consume_value(value);
                    }
                }
                benchmark("json parse+bind primitives x100k", 5, 100000.0) {
                    for (int i = 0; i < 100000; ++i) {
                        Value* value = data_bind_parse_string(json_codec, "Tick", json_payload);
                        consume_value(value);
                    }
                }
            }

            data_bind_free(binary_codec);
            data_bind_free(json_codec);
            std::remove(schema_path);
        }

        bench("mixed payload throughput") {
            static const char* schema_path = "bench_binary_mixed.rfl";
            write_schema(schema_path,
                         "declare Order\n"
                         "    id: int\n"
                         "    ts: long\n"
                         "    px: double\n"
                         "    symbol: String\n"
                         "    venue: String\n"
                         "    levels: List<int>\n"
                         "    tags: List<String>\n"
                         "    attrs: Map<String, int>\n"
                         "end\n");

            DataBind* binary_codec = data_bind_create_ex(schema_path, DATA_BIND_FORMAT_BINARY, &bench_api);
            check_not_null(binary_codec);
            DataBind* json_codec = data_bind_create_ex(schema_path, DATA_BIND_FORMAT_JSON, &bench_api);
            check_not_null(json_codec);

            uint8_t payload[256];
            const size_t payload_len = build_mixed_payload(payload);
            const char* json_payload = build_mixed_json();

            {
                benchmark("binary parse+bind mixed x25k", 5, 25000.0) {
                    for (int i = 0; i < 25000; ++i) {
                        Value* value = data_bind_parse(binary_codec, "Order", payload, payload_len);
                        consume_value(value);
                    }
                }
                benchmark("json parse+bind mixed x25k", 5, 25000.0) {
                    for (int i = 0; i < 25000; ++i) {
                        Value* value = data_bind_parse_string(json_codec, "Order", json_payload);
                        consume_value(value);
                    }
                }
            }

            data_bind_free(binary_codec);
            data_bind_free(json_codec);
            std::remove(schema_path);
        }
    }

    group("CodecRegistry Cold Path") {
        bench("first parse builds shared codec") {
            std::vector<ParsedDeclaration> declarations = build_registry_declarations();
            uint8_t primitive_payload[64];
            const size_t primitive_payload_len = build_primitives_payload(primitive_payload);
            const char* primitive_json = build_primitives_json();

            {
                benchmark("registry first parse", 5, 1.0) {
                    rulesforge::CodecRegistry registry;
                    registry.load_declarations(declarations);
                    rulesforge::FactArena arena;
                    Fact* fact = registry.parse_binary(arena, "Tick", primitive_payload, primitive_payload_len);
                    consume_fact(fact);
                }

                benchmark("registry first json", 5, 1.0) {
                    rulesforge::CodecRegistry registry;
                    registry.load_declarations(declarations);
                    rulesforge::FactArena arena;
                    Fact* fact = registry.parse_json(arena, "Tick", primitive_json);
                    consume_fact(fact);
                }
            }
        }
    }

    group("CodecRegistry Hot Reuse") {
        bench("shared codec reuse throughput") {
            std::vector<ParsedDeclaration> declarations = build_registry_declarations();
            uint8_t primitive_payload[64];
            const size_t primitive_payload_len = build_primitives_payload(primitive_payload);
            uint8_t mixed_payload[256];
            const size_t mixed_payload_len = build_mixed_payload(mixed_payload);
            uint8_t order_scalars_payload[128];
            const size_t order_scalars_payload_len = build_order_scalars_payload(order_scalars_payload);
            uint8_t order_levels_payload[192];
            const size_t order_levels_payload_len = build_order_with_levels_payload(order_levels_payload);
            uint8_t order_tags_payload[224];
            const size_t order_tags_payload_len = build_order_with_tags_payload(order_tags_payload);
            const char* primitive_json = build_primitives_json();
            const char* mixed_json = build_mixed_json();
            const char* order_scalars_json = build_order_scalars_json();
            const char* order_levels_json = build_order_with_levels_json();
            const char* order_tags_json = build_order_with_tags_json();

            {
                benchmark("registry reuse tick x10k", 5, 10000.0) {
                    rulesforge::CodecRegistry registry;
                    registry.load_declarations(declarations);
                    {
                        rulesforge::FactArena warmup_arena;
                        Fact* warmup = registry.parse_binary(
                            warmup_arena, "Tick", primitive_payload, primitive_payload_len);
                        consume_fact(warmup);
                    }
                    rulesforge::FactArena arena;
                    for (int i = 0; i < 10000; ++i) {
                        arena.reset();
                        Fact* fact = registry.parse_binary(arena, "Tick", primitive_payload, primitive_payload_len);
                        consume_fact(fact);
                    }
                }

                benchmark("registry json tick x10k", 5, 10000.0) {
                    rulesforge::CodecRegistry registry;
                    registry.load_declarations(declarations);
                    {
                        rulesforge::FactArena warmup_arena;
                        Fact* warmup = registry.parse_json(warmup_arena, "Tick", primitive_json);
                        consume_fact(warmup);
                    }
                    rulesforge::FactArena arena;
                    for (int i = 0; i < 10000; ++i) {
                        arena.reset();
                        Fact* fact = registry.parse_json(arena, "Tick", primitive_json);
                        consume_fact(fact);
                    }
                }

                benchmark("registry reuse order x5k", 5, 5000.0) {
                    rulesforge::CodecRegistry registry;
                    registry.load_declarations(declarations);
                    {
                        rulesforge::FactArena warmup_arena;
                        Fact* warmup = registry.parse_binary(
                            warmup_arena, "Order", mixed_payload, mixed_payload_len);
                        consume_fact(warmup);
                    }
                    rulesforge::FactArena arena;
                    for (int i = 0; i < 5000; ++i) {
                        arena.reset();
                        Fact* fact = registry.parse_binary(arena, "Order", mixed_payload, mixed_payload_len);
                        consume_fact(fact);
                    }
                }

                benchmark("registry order scalars x5k", 5, 5000.0) {
                    rulesforge::CodecRegistry registry;
                    registry.load_declarations(declarations);
                    {
                        rulesforge::FactArena warmup_arena;
                        Fact* warmup = registry.parse_binary(
                            warmup_arena, "OrderScalars", order_scalars_payload, order_scalars_payload_len);
                        consume_fact(warmup);
                    }
                    rulesforge::FactArena arena;
                    for (int i = 0; i < 5000; ++i) {
                        arena.reset();
                        Fact* fact = registry.parse_binary(arena, "OrderScalars", order_scalars_payload, order_scalars_payload_len);
                        consume_fact(fact);
                    }
                }

                benchmark("registry order +levels x5k", 5, 5000.0) {
                    rulesforge::CodecRegistry registry;
                    registry.load_declarations(declarations);
                    {
                        rulesforge::FactArena warmup_arena;
                        Fact* warmup = registry.parse_binary(
                            warmup_arena, "OrderLevels", order_levels_payload, order_levels_payload_len);
                        consume_fact(warmup);
                    }
                    rulesforge::FactArena arena;
                    for (int i = 0; i < 5000; ++i) {
                        arena.reset();
                        Fact* fact = registry.parse_binary(arena, "OrderLevels", order_levels_payload, order_levels_payload_len);
                        consume_fact(fact);
                    }
                }

                benchmark("registry order +tags x5k", 5, 5000.0) {
                    rulesforge::CodecRegistry registry;
                    registry.load_declarations(declarations);
                    {
                        rulesforge::FactArena warmup_arena;
                        Fact* warmup = registry.parse_binary(
                            warmup_arena, "OrderTags", order_tags_payload, order_tags_payload_len);
                        consume_fact(warmup);
                    }
                    rulesforge::FactArena arena;
                    for (int i = 0; i < 5000; ++i) {
                        arena.reset();
                        Fact* fact = registry.parse_binary(arena, "OrderTags", order_tags_payload, order_tags_payload_len);
                        consume_fact(fact);
                    }
                }

                benchmark("registry json order x5k", 5, 5000.0) {
                    rulesforge::CodecRegistry registry;
                    registry.load_declarations(declarations);
                    {
                        rulesforge::FactArena warmup_arena;
                        Fact* warmup = registry.parse_json(warmup_arena, "Order", mixed_json);
                        consume_fact(warmup);
                    }
                    rulesforge::FactArena arena;
                    for (int i = 0; i < 5000; ++i) {
                        arena.reset();
                        Fact* fact = registry.parse_json(arena, "Order", mixed_json);
                        consume_fact(fact);
                    }
                }

                benchmark("registry json order scalars x5k", 5, 5000.0) {
                    rulesforge::CodecRegistry registry;
                    registry.load_declarations(declarations);
                    {
                        rulesforge::FactArena warmup_arena;
                        Fact* warmup = registry.parse_json(warmup_arena, "OrderScalars", order_scalars_json);
                        consume_fact(warmup);
                    }
                    rulesforge::FactArena arena;
                    for (int i = 0; i < 5000; ++i) {
                        arena.reset();
                        Fact* fact = registry.parse_json(arena, "OrderScalars", order_scalars_json);
                        consume_fact(fact);
                    }
                }

                benchmark("registry json order +levels x5k", 5, 5000.0) {
                    rulesforge::CodecRegistry registry;
                    registry.load_declarations(declarations);
                    {
                        rulesforge::FactArena warmup_arena;
                        Fact* warmup = registry.parse_json(warmup_arena, "OrderLevels", order_levels_json);
                        consume_fact(warmup);
                    }
                    rulesforge::FactArena arena;
                    for (int i = 0; i < 5000; ++i) {
                        arena.reset();
                        Fact* fact = registry.parse_json(arena, "OrderLevels", order_levels_json);
                        consume_fact(fact);
                    }
                }

                benchmark("registry json order +tags x5k", 5, 5000.0) {
                    rulesforge::CodecRegistry registry;
                    registry.load_declarations(declarations);
                    {
                        rulesforge::FactArena warmup_arena;
                        Fact* warmup = registry.parse_json(warmup_arena, "OrderTags", order_tags_json);
                        consume_fact(warmup);
                    }
                    rulesforge::FactArena arena;
                    for (int i = 0; i < 5000; ++i) {
                        arena.reset();
                        Fact* fact = registry.parse_json(arena, "OrderTags", order_tags_json);
                        consume_fact(fact);
                    }
                }
            }
        }
    }

    group("Cold Start") {
        bench("codec creation plus first parse") {
            static const char* schema_path = "bench_binary_cold.rfl";
            write_schema(schema_path,
                         "declare Event\n"
                         "    id: int\n"
                         "    ts: long\n"
                         "    name: String\n"
                         "end\n");

            uint8_t payload[128];
            size_t payload_len = 0;
            append_i32(payload, &payload_len, 99);
            append_i64(payload, &payload_len, 1700000000999LL);
            append_varstr(payload, &payload_len, "startup");
            const char* json_payload = build_cold_json();

            {
                benchmark("binary create+jit+parse x1k", 3, 1000.0) {
                    for (int i = 0; i < 1000; ++i) {
                        DataBind* codec = data_bind_create_ex(schema_path, DATA_BIND_FORMAT_BINARY, &bench_api);
                        check_not_null(codec);
                        Value* value = data_bind_parse(codec, "Event", payload, payload_len);
                        consume_value(value);
                        data_bind_free(codec);
                    }
                }
                benchmark("json create+parse x1k", 3, 1000.0) {
                    for (int i = 0; i < 1000; ++i) {
                        DataBind* codec = data_bind_create_ex(schema_path, DATA_BIND_FORMAT_JSON, &bench_api);
                        check_not_null(codec);
                        Value* value = data_bind_parse_string(codec, "Event", json_payload);
                        consume_value(value);
                        data_bind_free(codec);
                    }
                }
            }

            std::remove(schema_path);
        }
    }
}

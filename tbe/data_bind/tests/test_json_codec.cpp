/**
 * @file test_json_codec.cpp
 * @brief Test JSON codec functionality using data_bind API
 */

#include "data_bind.h"
#include "core/rfl_parser_state.hpp"
#include "tinytest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Mock Value with List/Set/Map support */
typedef enum MockValueKind {
  MOCK_VALUE_OBJECT = 1,
  MOCK_VALUE_LIST = 2,
  MOCK_VALUE_SET = 3,
  MOCK_VALUE_MAP = 4
} MockValueKind;

typedef struct MockList {
  int kind;
  void** items;
  int* item_types;
  size_t count;
  size_t capacity;
} MockList;

typedef struct MockSet {
  int kind;
  void** items;
  int* item_types;
  size_t count;
  size_t capacity;
} MockSet;

typedef enum MockItemType {
  MOCK_ITEM_INT32,
  MOCK_ITEM_INT64,
  MOCK_ITEM_DOUBLE,
  MOCK_ITEM_STRING,
  MOCK_ITEM_OBJECT
} MockItemType;

typedef struct MockMapEntry {
  char* key;
  void* value;
  int value_type; // 0=string, 1=int, 2=double, 3=int64
} MockMapEntry;

typedef struct MockMap {
  int kind;
  MockMapEntry* entries;
  size_t count;
  size_t capacity;
} MockMap;

typedef struct Value {
  int kind;
  int id;
  int64_t id64;
  double price;
  int status;
  char name[256];
  MockList* tags;      // For List<string>
  MockList* scores;    // For List<int>
  struct Value* nested; // For nested objects
  MockSet* unique_tags; // For Set<string>
  MockMap* metadata;    // For Map<string, string>
} Value;

static int g_live_values = 0;
static int g_live_lists = 0;
static int g_live_sets = 0;
static int g_live_maps = 0;

static void reset_mock_live_counts(void) {
  g_live_values = 0;
  g_live_lists = 0;
  g_live_sets = 0;
  g_live_maps = 0;
}

static int total_mock_live_counts(void) {
  return g_live_values + g_live_lists + g_live_sets + g_live_maps;
}

static void mock_free_value(Value* value);

static MockList* mock_create_list(void) {
  MockList* list = (MockList*)calloc(1, sizeof(MockList));
  list->kind = MOCK_VALUE_LIST;
  list->capacity = 10;
  list->items = (void**)calloc(list->capacity, sizeof(void*));
  list->item_types = (int*)calloc(list->capacity, sizeof(int));
  g_live_lists++;
  return list;
}

static void mock_free_list(MockList* list) {
  if (list) {
    for (size_t i = 0; i < list->count; i++) {
      if (list->item_types[i] == MOCK_ITEM_OBJECT) {
        mock_free_value((Value*)list->items[i]);
      } else {
        free(list->items[i]);
      }
    }
    free(list->item_types);
    free(list->items);
    free(list);
    g_live_lists--;
  }
}

static MockSet* mock_create_set(void) {
  MockSet* set = (MockSet*)calloc(1, sizeof(MockSet));
  set->kind = MOCK_VALUE_SET;
  set->capacity = 10;
  set->items = (void**)calloc(set->capacity, sizeof(void*));
  set->item_types = (int*)calloc(set->capacity, sizeof(int));
  g_live_sets++;
  return set;
}

static void mock_free_set(MockSet* set) {
  if (set) {
    for (size_t i = 0; i < set->count; i++) {
      free(set->items[i]);
    }
    free(set->item_types);
    free(set->items);
    free(set);
    g_live_sets--;
  }
}

static MockMap* mock_create_map(void) {
  MockMap* map = (MockMap*)calloc(1, sizeof(MockMap));
  map->kind = MOCK_VALUE_MAP;
  map->capacity = 10;
  map->entries = (MockMapEntry*)calloc(map->capacity, sizeof(MockMapEntry));
  g_live_maps++;
  return map;
}

static void mock_free_map(MockMap* map) {
  if (map) {
    for (size_t i = 0; i < map->count; i++) {
      free(map->entries[i].key);
      free(map->entries[i].value);
    }
    free(map->entries);
    free(map);
    g_live_maps--;
  }
}

static Value *mock_create_object(void) {
  Value *v = (Value *)calloc(1, sizeof(Value));
  v->kind = MOCK_VALUE_OBJECT;
  g_live_values++;
  return v;
}

static void mock_free_value(Value* value) {
  if (!value) return;

  if (value->nested) {
    mock_free_value(value->nested);
    value->nested = NULL;
  }
  if (value->scores && value->scores != value->tags) {
    mock_free_list(value->scores);
    value->scores = NULL;
  }
  if (value->tags) {
    mock_free_list(value->tags);
    value->tags = NULL;
  }
  if (value->unique_tags) {
    mock_free_set(value->unique_tags);
    value->unique_tags = NULL;
  }
  if (value->metadata) {
    mock_free_map(value->metadata);
    value->metadata = NULL;
  }

  free(value);
  g_live_values--;
}

static void mock_destroy_value(Value* value) {
  if (!value) return;

  switch (value->kind) {
    case MOCK_VALUE_OBJECT:
      mock_free_value(value);
      break;
    case MOCK_VALUE_LIST:
      mock_free_list((MockList*)value);
      break;
    case MOCK_VALUE_SET:
      mock_free_set((MockSet*)value);
      break;
    case MOCK_VALUE_MAP:
      mock_free_map((MockMap*)value);
      break;
    default:
      free(value);
      break;
  }
}

static void mock_set_field_int(Value *obj, const char *name, int32_t val) {
  if (!obj) return;
  if (strcmp(name, "id") == 0 || strcmp(name, "orderId") == 0) {
    obj->id = val;
  } else if (strcmp(name, "status") == 0) {
    obj->status = val;
  }
}

static void mock_set_field_int64(Value *obj, const char *name, int64_t val) {
  if (!obj) return;
  if (strcmp(name, "ts") == 0 || strcmp(name, "id64") == 0) {
    obj->id64 = val;
  }
}

static void mock_set_field_double(Value *obj, const char *name, double val) {
  if (obj && strcmp(name, "price") == 0) {
    obj->price = val;
  }
}

static void mock_set_field_string(Value *obj, const char *name, const char *val) {
  if (obj && val) {
    if (strcmp(name, "name") == 0 || strcmp(name, "city") == 0) {
      strncpy(obj->name, val, sizeof(obj->name) - 1);
    }
  }
}

static void mock_set_field_bytes(Value *obj, const char *name, const uint8_t *data, size_t len) {
  (void)obj;
  (void)name;
  (void)data;
  (void)len;
}

static Value* mock_create_list_impl(void) {
  return (Value*)mock_create_list();
}

static void mock_add_list_item_int(Value* list, int32_t val) {
  MockList* ml = (MockList*)list;
  if (ml && ml->count < ml->capacity) {
    int32_t* item = (int32_t*)malloc(sizeof(int32_t));
    *item = val;
    ml->item_types[ml->count] = MOCK_ITEM_INT32;
    ml->items[ml->count++] = item;
  }
}

static void mock_add_list_item_int64(Value* list, int64_t val) {
  MockList* ml = (MockList*)list;
  if (ml && ml->count < ml->capacity) {
    int64_t* item = (int64_t*)malloc(sizeof(int64_t));
    *item = val;
    ml->item_types[ml->count] = MOCK_ITEM_INT64;
    ml->items[ml->count++] = item;
  }
}

static void mock_add_list_item_double(Value* list, double val) {
  MockList* ml = (MockList*)list;
  if (ml && ml->count < ml->capacity) {
    double* item = (double*)malloc(sizeof(double));
    *item = val;
    ml->item_types[ml->count] = MOCK_ITEM_DOUBLE;
    ml->items[ml->count++] = item;
  }
}

static void mock_add_list_item_string(Value* list, const char* val) {
  MockList* ml = (MockList*)list;
  if (ml && ml->count < ml->capacity) {
    char* item = (char*)malloc(strlen(val) + 1);
    strcpy(item, val);
    ml->item_types[ml->count] = MOCK_ITEM_STRING;
    ml->items[ml->count++] = item;
  }
}

static void mock_add_list_item_object(Value* list, Value* obj) {
  MockList* ml = (MockList*)list;
  if (ml && ml->count < ml->capacity) {
    ml->item_types[ml->count] = MOCK_ITEM_OBJECT;
    ml->items[ml->count++] = obj;
  }
}

static void mock_set_field_list(Value* obj, const char* name, Value* list) {
  if (!obj || !list) return;

  if (strcmp(name, "tags") == 0) {
    obj->tags = (MockList*)list;
  } else if (strcmp(name, "scores") == 0) {
    obj->scores = (MockList*)list;
  } else if (strcmp(name, "items") == 0) {
    obj->tags = (MockList*)list;  // Reuse tags for items
  }
}

static void mock_set_field_object(Value* obj, const char* name, Value* child) {
  if (obj && (strcmp(name, "nested") == 0 || strcmp(name, "address") == 0 ||
              strcmp(name, "config") == 0 || strcmp(name, "settings") == 0)) {
    obj->nested = child;
  }
}

// ───── Mock Set API ─────

static Value* mock_create_set_impl(void) {
  return (Value*)mock_create_set();
}

static void mock_add_set_item_int(Value* set, int32_t val) {
  MockSet* ms = (MockSet*)set;
  if (ms && ms->count < ms->capacity) {
    // Check for duplicates
    for (size_t i = 0; i < ms->count; i++) {
      if (*(int32_t*)ms->items[i] == val) return;
    }
    int32_t* item = (int32_t*)malloc(sizeof(int32_t));
    *item = val;
    ms->item_types[ms->count] = MOCK_ITEM_INT32;
    ms->items[ms->count++] = item;
  }
}

static void mock_add_set_item_int64(Value* set, int64_t val) {
  MockSet* ms = (MockSet*)set;
  if (ms && ms->count < ms->capacity) {
    for (size_t i = 0; i < ms->count; i++) {
      if (ms->item_types[i] == MOCK_ITEM_INT64 && *(int64_t*)ms->items[i] == val) return;
    }
    int64_t* item = (int64_t*)malloc(sizeof(int64_t));
    *item = val;
    ms->item_types[ms->count] = MOCK_ITEM_INT64;
    ms->items[ms->count++] = item;
  }
}

static void mock_add_set_item_double(Value* set, double val) {
  MockSet* ms = (MockSet*)set;
  if (ms && ms->count < ms->capacity) {
    for (size_t i = 0; i < ms->count; i++) {
      if (*(double*)ms->items[i] == val) return;
    }
    double* item = (double*)malloc(sizeof(double));
    *item = val;
    ms->item_types[ms->count] = MOCK_ITEM_DOUBLE;
    ms->items[ms->count++] = item;
  }
}

static void mock_add_set_item_string(Value* set, const char* val) {
  MockSet* ms = (MockSet*)set;
  if (ms && ms->count < ms->capacity) {
    for (size_t i = 0; i < ms->count; i++) {
      if (strcmp((char*)ms->items[i], val) == 0) return;
    }
    char* item = (char*)malloc(strlen(val) + 1);
    strcpy(item, val);
    ms->item_types[ms->count] = MOCK_ITEM_STRING;
    ms->items[ms->count++] = item;
  }
}

static void mock_set_field_set(Value* obj, const char* name, Value* set) {
  if (!obj || !set) return;

  if (strcmp(name, "unique_tags") == 0) {
    obj->unique_tags = (MockSet*)set;
  } else if (strcmp(name, "tags") == 0) {
    obj->unique_tags = (MockSet*)set;  // Reuse unique_tags slot for Set<string> item tags
  }
}

// ───── Mock Map API ─────

static Value* mock_create_map_impl(void) {
  return (Value*)mock_create_map();
}

static void mock_add_map_entry_string_string(Value* map, const char* key, const char* val) {
  MockMap* mm = (MockMap*)map;
  if (mm && mm->count < mm->capacity) {
    mm->entries[mm->count].key = (char*)malloc(strlen(key) + 1);
    strcpy(mm->entries[mm->count].key, key);
    mm->entries[mm->count].value = malloc(strlen(val) + 1);
    strcpy((char*)mm->entries[mm->count].value, val);
    mm->entries[mm->count].value_type = 0;
    mm->count++;
  }
}

static void mock_add_map_entry_string_int(Value* map, const char* key, int32_t val) {
  MockMap* mm = (MockMap*)map;
  if (mm && mm->count < mm->capacity) {
    mm->entries[mm->count].key = (char*)malloc(strlen(key) + 1);
    strcpy(mm->entries[mm->count].key, key);
    mm->entries[mm->count].value = malloc(sizeof(int32_t));
    *(int32_t*)mm->entries[mm->count].value = val;
    mm->entries[mm->count].value_type = 1;
    mm->count++;
  }
}

static void mock_add_map_entry_string_double(Value* map, const char* key, double val) {
  MockMap* mm = (MockMap*)map;
  if (mm && mm->count < mm->capacity) {
    mm->entries[mm->count].key = (char*)malloc(strlen(key) + 1);
    strcpy(mm->entries[mm->count].key, key);
    mm->entries[mm->count].value = malloc(sizeof(double));
    *(double*)mm->entries[mm->count].value = val;
    mm->entries[mm->count].value_type = 2;
    mm->count++;
  }
}

static void mock_add_map_entry_string_int64(Value* map, const char* key, int64_t val) {
  MockMap* mm = (MockMap*)map;
  if (mm && mm->count < mm->capacity) {
    mm->entries[mm->count].key = (char*)malloc(strlen(key) + 1);
    strcpy(mm->entries[mm->count].key, key);
    mm->entries[mm->count].value = malloc(sizeof(int64_t));
    *(int64_t*)mm->entries[mm->count].value = val;
    mm->entries[mm->count].value_type = 3;
    mm->count++;
  }
}

static void mock_set_field_map(Value* obj, const char* name, Value* map) {
  if (obj && strcmp(name, "metadata") == 0) {
    obj->metadata = (MockMap*)map;
  }
}

static DataBindValueApi test_api = {
    .create_object = mock_create_object,
    .set_field_int = mock_set_field_int,
    .set_field_int64 = mock_set_field_int64,
    .set_field_double = mock_set_field_double,
    .set_field_string = mock_set_field_string,
    .set_field_bytes = mock_set_field_bytes,
    .create_list = mock_create_list_impl,
    .add_list_item_int = mock_add_list_item_int,
    .add_list_item_int64 = mock_add_list_item_int64,
    .add_list_item_double = mock_add_list_item_double,
    .add_list_item_string = mock_add_list_item_string,
    .add_list_item_object = mock_add_list_item_object,
    .set_field_list = mock_set_field_list,
    .set_field_object = mock_set_field_object,
    .create_set = mock_create_set_impl,
    .add_set_item_int = mock_add_set_item_int,
    .add_set_item_double = mock_add_set_item_double,
    .add_set_item_string = mock_add_set_item_string,
    .set_field_set = mock_set_field_set,
    .create_map = mock_create_map_impl,
    .add_map_entry_string_string = mock_add_map_entry_string_string,
    .add_map_entry_string_int = mock_add_map_entry_string_int,
    .add_map_entry_string_double = mock_add_map_entry_string_double,
    .set_field_map = mock_set_field_map,
    .add_set_item_int64 = mock_add_set_item_int64,
    .add_map_entry_string_int64 = mock_add_map_entry_string_int64,
    .destroy_value = mock_destroy_value,
};

static void write_schema(const char *path, const char *content) {
  FILE *f = fopen(path, "w");
  if (f) {
    fwrite(content, 1, strlen(content), f);
    fclose(f);
  }
}

suite("JSON Codec") {
  section("Basic Parsing") {
    given("a simple schema") {
      write_schema("test_json.rfl", "declare Product\n"
                                    "    id: int\n"
                                    "    name: string\n"
                                    "    price: double\n"
                                    "end\n");

      when("parsing JSON data") {
        DataBind *codec = data_bind_create_ex("test_json.rfl", DATA_BIND_FORMAT_JSON, &test_api);

        then("codec should be created") { check_not_null(codec); }

        then("should parse JSON correctly") {
          if (codec) {
            const char *json = "{\"id\": 42, \"name\": \"Widget\", \"price\": 19.99}";
            Value *v = (Value *)data_bind_parse_string(codec, "Product", json);

            check_not_null(v);
            if (v) {
              check(v->id == 42);
              check(strcmp(v->name, "Widget") == 0);
              check(v->price == 19.99);
              mock_destroy_value(v);
            }
          }
        }

        if (codec)
          data_bind_free(codec);
      }

      remove("test_json.rfl");
    }
  }

  section("List Support") {
    given("a schema with List<int>") {
      write_schema("test_list_int.rfl", "declare Student\n"
                                        "    name: string\n"
                                        "    scores: List<int>\n"
                                        "end\n");

      when("parsing JSON with integer array") {
        DataBind *codec = data_bind_create_ex("test_list_int.rfl", DATA_BIND_FORMAT_JSON, &test_api);

        then("should parse List<int> correctly") {
          if (codec) {
            const char *json = "{\"name\": \"Alice\", \"scores\": [85, 90, 95]}";
            Value *v = (Value *)data_bind_parse_string(codec, "Student", json);

            if (!v) {
              printf("DEBUG: Parse failed: %s\n", data_bind_get_error(codec));
            }

            check_not_null(v);
            if (v) {
              printf("DEBUG: v->name = '%s'\n", v->name);
              printf("DEBUG: v->scores = %p\n", (void*)v->scores);

              check(strcmp(v->name, "Alice") == 0);
              check_not_null(v->scores);
              if (v->scores) {
                printf("DEBUG: v->scores->count = %zu\n", v->scores->count);
                check(v->scores->count == 3);
                if (v->scores->count >= 3) {
                  printf("DEBUG: items[0] = %d\n", *(int32_t*)v->scores->items[0]);
                  printf("DEBUG: items[1] = %d\n", *(int32_t*)v->scores->items[1]);
                  printf("DEBUG: items[2] = %d\n", *(int32_t*)v->scores->items[2]);
                  check(*(int32_t*)v->scores->items[0] == 85);
                  check(*(int32_t*)v->scores->items[1] == 90);
                  check(*(int32_t*)v->scores->items[2] == 95);
                }
              }
              mock_destroy_value(v);
            }
          }
        }

        if (codec)
          data_bind_free(codec);
      }

      remove("test_list_int.rfl");
    }

    given("a schema with List<string>") {
      write_schema("test_list_str.rfl", "declare User\n"
                                        "    name: string\n"
                                        "    tags: List<string>\n"
                                        "end\n");

      when("parsing JSON with string array") {
        DataBind *codec = data_bind_create_ex("test_list_str.rfl", DATA_BIND_FORMAT_JSON, &test_api);

        then("should parse List<string> correctly") {
          if (codec) {
            const char *json = "{\"name\": \"Bob\", \"tags\": [\"admin\", \"user\", \"developer\"]}";
            Value *v = (Value *)data_bind_parse_string(codec, "User", json);

            check_not_null(v);
            if (v) {
              check(strcmp(v->name, "Bob") == 0);
              check_not_null(v->tags);
              if (v->tags) {
                check(v->tags->count == 3);
                check(strcmp((char*)v->tags->items[0], "admin") == 0);
                check(strcmp((char*)v->tags->items[1], "user") == 0);
                check(strcmp((char*)v->tags->items[2], "developer") == 0);
              }
              mock_destroy_value(v);
            }
          }
        }

        if (codec)
          data_bind_free(codec);
      }

      remove("test_list_str.rfl");
    }
  }

  section("Nested Object Support") {
    given("a schema with nested object") {
      write_schema("test_nested.rfl", "declare Address\n"
                                      "    city: string\n"
                                      "end\n"
                                      "declare Person\n"
                                      "    name: string\n"
                                      "    address: Address\n"
                                      "end\n");

      when("parsing JSON with nested object") {
        DataBind *codec = data_bind_create_ex("test_nested.rfl", DATA_BIND_FORMAT_JSON, &test_api);

        then("should parse nested object correctly") {
          if (codec) {
            const char *json = "{\"name\": \"Charlie\", \"address\": {\"city\": \"London\"}}";
            Value *v = (Value *)data_bind_parse_string(codec, "Person", json);

            if (!v) {
              printf("Parse failed: %s\n", data_bind_get_error(codec));
            }

            check_not_null(v);
            if (v) {
              check(strcmp(v->name, "Charlie") == 0);
              check_not_null(v->nested);
              if (v->nested) {
                check(strcmp(v->nested->name, "London") == 0);
              }
              mock_destroy_value(v);
            }
          }
        }

        if (codec)
          data_bind_free(codec);
      }

      remove("test_nested.rfl");
    }
  }

  section("Schema-Driven API Requirements") {
    given("an API without List support for a primitive-only schema") {
      DataBindValueApi api_without_list{};
      api_without_list.create_object = mock_create_object;
      api_without_list.set_field_int = mock_set_field_int;
      api_without_list.set_field_double = mock_set_field_double;
      api_without_list.set_field_string = mock_set_field_string;
      api_without_list.set_field_bytes = mock_set_field_bytes;
      api_without_list.destroy_value = mock_destroy_value;

      write_schema("test_compat.rfl", "declare Product\n"
                                      "    id: int\n"
                                      "    name: string\n"
                                      "end\n");

      when("parsing basic JSON") {
        DataBind *codec = data_bind_create_ex("test_compat.rfl", DATA_BIND_FORMAT_JSON, &api_without_list);

        then("should still work for basic types") {
          if (codec) {
            const char *json = "{\"id\": 99, \"name\": \"OldStyle\"}";
            Value *v = (Value *)data_bind_parse_string(codec, "Product", json);

            check_not_null(v);
            if (v) {
              check(v->id == 99);
              check(strcmp(v->name, "OldStyle") == 0);
              mock_destroy_value(v);
            }
          }
        }

        if (codec)
          data_bind_free(codec);
      }

      remove("test_compat.rfl");
    }

    given("an API without destroy_value") {
      DataBindValueApi api_without_destroy{};
      api_without_destroy.create_object = mock_create_object;
      api_without_destroy.set_field_int = mock_set_field_int;
      api_without_destroy.set_field_int64 = mock_set_field_int64;
      api_without_destroy.set_field_double = mock_set_field_double;
      api_without_destroy.set_field_string = mock_set_field_string;
      api_without_destroy.set_field_bytes = mock_set_field_bytes;

      write_schema("test_json_requires_destroy.rfl", "declare Product\n"
                                                     "    id: int\n"
                                                     "end\n");

      when("creating the JSON codec") {
        DataBind *codec = data_bind_create_ex("test_json_requires_destroy.rfl", DATA_BIND_FORMAT_JSON,
                                              &api_without_destroy);

        then("codec creation should fail early") { check_null(codec); }

        then("error should name the missing destroy_value callback") {
          check_str_contains(data_bind_get_error(NULL), "destroy_value");
        }

        if (codec)
          data_bind_free(codec);
      }

      remove("test_json_requires_destroy.rfl");
    }
  }

  section("Set Support") {
    given("a schema with Set<string>") {
      write_schema("test_set_str.rfl", "declare User\n"
                                       "    name: string\n"
                                       "    unique_tags: Set<string>\n"
                                       "end\n");

      when("parsing JSON with string array (duplicates removed)") {
        DataBind *codec = data_bind_create_ex("test_set_str.rfl", DATA_BIND_FORMAT_JSON, &test_api);

        then("should parse Set<string> correctly") {
          if (codec) {
            const char *json = "{\"name\": \"Alice\", \"unique_tags\": [\"admin\", \"user\", \"admin\"]}";
            Value *v = (Value *)data_bind_parse_string(codec, "User", json);

            check_not_null(v);
            if (v) {
              check(strcmp(v->name, "Alice") == 0);
              check_not_null(v->unique_tags);
              if (v->unique_tags) {
                // Set should have only 2 items (duplicates removed)
                check(v->unique_tags->count == 2);
              }
              mock_destroy_value(v);
            }
          }
        }

        if (codec)
          data_bind_free(codec);
      }

      remove("test_set_str.rfl");
    }

    given("a schema with Set<long>") {
      write_schema("test_set_long_json.rfl", "declare User\n"
                                             "    unique_tags: Set<long>\n"
                                             "end\n");

      when("parsing JSON with long array") {
        DataBind *codec = data_bind_create_ex("test_set_long_json.rfl", DATA_BIND_FORMAT_JSON, &test_api);

        then("should parse Set<long> correctly") {
          if (codec) {
            Value *v = (Value *)data_bind_parse_string(codec, "User", "{\"unique_tags\": [11, 22, 11]}");

            check_not_null(v);
            if (v) {
              check_not_null(v->unique_tags);
              if (v->unique_tags) {
                check(v->unique_tags->count == 2);
                check(*(int64_t*)v->unique_tags->items[0] == 11 || *(int64_t*)v->unique_tags->items[1] == 11);
              }
              mock_free_value(v);
            }
          }
        }

        if (codec)
          data_bind_free(codec);
      }

      remove("test_set_long_json.rfl");
    }
  }

  section("Map Support") {
    given("a schema with Map<string, string>") {
      write_schema("test_map_str.rfl", "declare Config\n"
                                       "    name: string\n"
                                       "    metadata: Map<string, string>\n"
                                       "end\n");

      when("parsing JSON with object") {
        DataBind *codec = data_bind_create_ex("test_map_str.rfl", DATA_BIND_FORMAT_JSON, &test_api);

        then("should parse Map<string, string> correctly") {
          if (codec) {
            const char *json = "{\"name\": \"Config1\", \"metadata\": {\"env\": \"prod\", \"region\": \"us-west\"}}";
            Value *v = (Value *)data_bind_parse_string(codec, "Config", json);

            check_not_null(v);
            if (v) {
              check(strcmp(v->name, "Config1") == 0);
              check_not_null(v->metadata);
              if (v->metadata) {
                check(v->metadata->count == 2);
                // Verify entries
                if (v->metadata->count >= 2) {
                  check(strcmp(v->metadata->entries[0].key, "env") == 0);
                  check(strcmp((char*)v->metadata->entries[0].value, "prod") == 0);
                  check(v->metadata->entries[0].value_type == 0);
                }
              }
              mock_destroy_value(v);
            }
          }
        }

        if (codec)
          data_bind_free(codec);
      }

      remove("test_map_str.rfl");
    }

    given("a schema with Map<string, long>") {
      write_schema("test_map_long_json.rfl", "declare Config\n"
                                             "    metadata: Map<string, long>\n"
                                             "end\n");

      when("parsing JSON with integer values") {
        DataBind *codec = data_bind_create_ex("test_map_long_json.rfl", DATA_BIND_FORMAT_JSON, &test_api);

        then("should parse Map<string, long> correctly") {
          if (codec) {
            Value *v = (Value *)data_bind_parse_string(codec, "Config", "{\"metadata\": {\"a\": 101, \"b\": 202}}");

            check_not_null(v);
            if (v) {
              check_not_null(v->metadata);
              if (v->metadata) {
                check(v->metadata->count == 2);
                check(v->metadata->entries[0].value_type == 3);
                check(*(int64_t*)v->metadata->entries[0].value == 101 || *(int64_t*)v->metadata->entries[1].value == 101);
              }
              mock_free_value(v);
            }
          }
        }

        if (codec)
          data_bind_free(codec);
      }

      remove("test_map_long_json.rfl");
    }
  }

  section("Complex Nested Types") {
    given("a schema with nested object containing List") {
      write_schema("test_nested_list.rfl", "declare Address\n"
                                           "    city: string\n"
                                           "    tags: List<string>\n"
                                           "end\n"
                                           "declare Person\n"
                                           "    name: string\n"
                                           "    address: Address\n"
                                           "end\n");

      when("parsing JSON with nested object containing list") {
        DataBind *codec = data_bind_create_ex("test_nested_list.rfl", DATA_BIND_FORMAT_JSON, &test_api);

        then("should parse nested object with List correctly") {
          if (codec) {
            const char *json = "{\"name\": \"Alice\", \"address\": {\"city\": \"NYC\", \"tags\": [\"home\", \"work\"]}}";
            Value *v = (Value *)data_bind_parse_string(codec, "Person", json);

            check_not_null(v);
            if (v) {
              check(strcmp(v->name, "Alice") == 0);
              check_not_null(v->nested);
              if (v->nested) {
                check(strcmp(v->nested->name, "NYC") == 0);
                check_not_null(v->nested->tags);
                if (v->nested->tags) {
                  check(v->nested->tags->count == 2);
                  check(strcmp((char*)v->nested->tags->items[0], "home") == 0);
                  check(strcmp((char*)v->nested->tags->items[1], "work") == 0);
                }
              }
              mock_destroy_value(v);
            }
          }
        }

        if (codec)
          data_bind_free(codec);
      }

      remove("test_nested_list.rfl");
    }

    given("a schema with nested object containing Set") {
      write_schema("test_nested_set.rfl", "declare Config\n"
                                          "    name: string\n"
                                          "    unique_tags: Set<string>\n"
                                          "end\n"
                                          "declare System\n"
                                          "    id: int\n"
                                          "    config: Config\n"
                                          "end\n");

      when("parsing JSON with nested object containing set") {
        DataBind *codec = data_bind_create_ex("test_nested_set.rfl", DATA_BIND_FORMAT_JSON, &test_api);

        then("should parse nested object with Set correctly") {
          if (codec) {
            const char *json = "{\"id\": 1, \"config\": {\"name\": \"prod\", \"unique_tags\": [\"a\", \"b\", \"a\"]}}";
            Value *v = (Value *)data_bind_parse_string(codec, "System", json);

            check_not_null(v);
            if (v) {
              check(v->id == 1);
              check_not_null(v->nested);
              if (v->nested) {
                check(strcmp(v->nested->name, "prod") == 0);
                check_not_null(v->nested->unique_tags);
                if (v->nested->unique_tags) {
                  // Should have 2 items (duplicate "a" removed)
                  check(v->nested->unique_tags->count == 2);
                }
              }
              mock_destroy_value(v);
            }
          }
        }

        if (codec)
          data_bind_free(codec);
      }

      remove("test_nested_set.rfl");
    }

    given("a schema with nested object containing Map") {
      write_schema("test_nested_map.rfl", "declare Settings\n"
                                          "    name: string\n"
                                          "    metadata: Map<string, string>\n"
                                          "end\n"
                                          "declare App\n"
                                          "    id: int\n"
                                          "    settings: Settings\n"
                                          "end\n");

      when("parsing JSON with nested object containing map") {
        DataBind *codec = data_bind_create_ex("test_nested_map.rfl", DATA_BIND_FORMAT_JSON, &test_api);

        then("should parse nested object with Map correctly") {
          if (codec) {
            const char *json = "{\"id\": 100, \"settings\": {\"name\": \"app1\", \"metadata\": {\"env\": \"prod\", \"version\": \"1.0\"}}}";
            Value *v = (Value *)data_bind_parse_string(codec, "App", json);

            check_not_null(v);
            if (v) {
              check(v->id == 100);
              check_not_null(v->nested);
              if (v->nested) {
                check(strcmp(v->nested->name, "app1") == 0);
                check_not_null(v->nested->metadata);
                if (v->nested->metadata) {
                  check(v->nested->metadata->count == 2);
                }
              }
              mock_destroy_value(v);
            }
          }
        }

        if (codec)
          data_bind_free(codec);
      }

      remove("test_nested_map.rfl");
    }

    given("a schema with List of objects containing Set") {
      write_schema("test_list_obj_set.rfl", "declare Item\n"
                                            "    id: int\n"
                                            "    tags: Set<string>\n"
                                            "end\n"
                                            "declare Order\n"
                                            "    orderId: int\n"
                                            "    items: List<Item>\n"
                                            "end\n");

      when("parsing JSON with list of objects containing sets") {
        DataBind *codec = data_bind_create_ex("test_list_obj_set.rfl", DATA_BIND_FORMAT_JSON, &test_api);

        then("should parse List<Object> with Set correctly") {
          if (codec) {
            const char *json = "{\"orderId\": 123, \"items\": [{\"id\": 1, \"tags\": [\"new\", \"sale\"]}, {\"id\": 2, \"tags\": [\"used\"]}]}";
            Value *v = (Value *)data_bind_parse_string(codec, "Order", json);

            check_not_null(v);
            if (v) {
              check(v->id == 123);
              // Note: Full validation would require more complex mock structure
              mock_destroy_value(v);
            }
          }
        }

        if (codec)
          data_bind_free(codec);
      }

      remove("test_list_obj_set.rfl");
    }
  }

  section("Long And Validation") {
    given("a Set<string> parse with wrong element type") {
      write_schema("test_set_cleanup_json.rfl", "declare User\n"
                                                "    unique_tags: Set<string>\n"
                                                "end\n");

      when("parsing invalid JSON") {
        reset_mock_live_counts();
        DataBind *codec = data_bind_create_ex("test_set_cleanup_json.rfl", DATA_BIND_FORMAT_JSON, &test_api);

        then("should fail instead of silently dropping the bad value and should clean up") {
          if (codec) {
            Value *v = (Value *)data_bind_parse_string(codec, "User", "{\"unique_tags\": [\"ok\", 1]}");
            check_null(v);
            check_str_contains(data_bind_get_error(codec), "expected string");
            check(total_mock_live_counts() == 0);
          }
        }

        if (codec)
          data_bind_free(codec);
      }

      remove("test_set_cleanup_json.rfl");
    }

    given("a Map<string, string> parse with wrong value type") {
      write_schema("test_map_cleanup_json.rfl", "declare Config\n"
                                                "    metadata: Map<string, string>\n"
                                                "end\n");

      when("parsing invalid JSON") {
        reset_mock_live_counts();
        DataBind *codec = data_bind_create_ex("test_map_cleanup_json.rfl", DATA_BIND_FORMAT_JSON, &test_api);

        then("should fail and release temporary map/object values") {
          if (codec) {
            Value *v = (Value *)data_bind_parse_string(codec, "Config", "{\"metadata\": {\"ok\": \"yes\", \"bad\": 9}}");
            check_null(v);
            check_str_contains(data_bind_get_error(codec), "expected string");
            check(total_mock_live_counts() == 0);
          }
        }

        if (codec)
          data_bind_free(codec);
      }

      remove("test_map_cleanup_json.rfl");
    }

    given("a schema with long field") {
      write_schema("test_long_json.rfl", "declare Event\n"
                                         "    ts: long\n"
                                         "end\n");

      when("parsing JSON with a long number") {
        DataBind *codec = data_bind_create_ex("test_long_json.rfl", DATA_BIND_FORMAT_JSON, &test_api);

        then("should parse long via set_field_int64") {
          check_not_null(codec);
          if (codec) {
            const char *json = "{\"ts\": 1700000000123}";
            Value *v = (Value *)data_bind_parse_string(codec, "Event", json);
            check_not_null(v);
            if (v) {
              check(v->id64 == 1700000000123LL);
              mock_destroy_value(v);
            }
            data_bind_free(codec);
          }
        }
      }

      remove("test_long_json.rfl");
    }

    given("a schema that requires List support") {
      DataBindValueApi old_api{};
      old_api.create_object = mock_create_object;
      old_api.set_field_int = mock_set_field_int;
      old_api.set_field_int64 = mock_set_field_int64;
      old_api.set_field_double = mock_set_field_double;
      old_api.set_field_string = mock_set_field_string;
      old_api.set_field_bytes = mock_set_field_bytes;
      old_api.destroy_value = mock_destroy_value;

      write_schema("test_json_missing_list_api.rfl", "declare Student\n"
                                                     "    scores: List<int>\n"
                                                     "end\n");

      when("creating the JSON codec") {
        DataBind *codec = data_bind_create_ex("test_json_missing_list_api.rfl", DATA_BIND_FORMAT_JSON, &old_api);

        then("codec creation should fail early") { check_null(codec); }

        if (codec) {
          data_bind_free(codec);
        }
      }

      remove("test_json_missing_list_api.rfl");
    }

    given("a schema with enum field") {
      write_schema("test_json_enum.rfl", "enum Status<int>\n"
                                         "    PENDING\n"
                                         "    ACTIVE\n"
                                         "end\n"
                                         "declare Order\n"
                                         "    status: Status\n"
                                         "end\n");

      when("parsing JSON with enum backing value") {
        DataBind *codec = data_bind_create_ex("test_json_enum.rfl", DATA_BIND_FORMAT_JSON, &test_api);

        then("codec should parse enum as its underlying scalar") {
          check_not_null(codec);
          if (codec) {
            Value* v = static_cast<Value*>(data_bind_parse_string(codec, "Order", "{\"status\": 1}"));
            check_not_null(v);
            if (v) {
              check(v->status == 1);
              mock_destroy_value(v);
            }
            data_bind_free(codec);
          }
        }
      }

      remove("test_json_enum.rfl");
    }

    given("in-memory declarations and enums") {
      when("creating codec from declarations and enums") {
        ParsedDeclaration order_decl;
        order_decl.type_name = "Order";
        order_decl.fields.push_back({"status", FT_Object, {TypeParameter(FT_Object, "Status")}});

        ParsedEnum status_enum;
        status_enum.enum_name = "Status";
        status_enum.underlying_type = "int";
        status_enum.values = {"PENDING", "ACTIVE"};

        const void* declarations[] = {&order_decl};
        const void* enums[] = {&status_enum};

        DataBind* codec = data_bind_create_from_declarations_and_enums(
            declarations, 1, enums, 1, DATA_BIND_FORMAT_JSON, &test_api);

        then("codec should preserve enum metadata instead of dropping it") {
          check_not_null(codec);
          if (codec) {
            Value* v = static_cast<Value*>(data_bind_parse_string(codec, "Order", "{\"status\": 1}"));
            check_not_null(v);
            if (v) {
              check(v->status == 1);
              mock_destroy_value(v);
            }
            data_bind_free(codec);
          }
        }
      }
    }
  }
}

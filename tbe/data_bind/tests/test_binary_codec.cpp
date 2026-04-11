/**
 * @file test_binary_codec.cpp
 * @brief Test binary codec functionality using data_bind API with explicit format
 */

#include "data_bind.h"
#include "tinytest.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ───── Mock Value: stores up to 32 named fields ───── */

#define MAX_FIELDS 32
#define MAX_CONTAINER_ITEMS 16

typedef enum {
  MOCK_INT,
  MOCK_INT64,
  MOCK_DOUBLE,
  MOCK_STRING,
  MOCK_BYTES,
  MOCK_LIST,
  MOCK_SET,
  MOCK_MAP
} MockFieldType;

typedef enum {
  MOCK_VALUE_OBJECT = 1,
  MOCK_VALUE_LIST_CONTAINER,
  MOCK_VALUE_SET_CONTAINER,
  MOCK_VALUE_MAP_CONTAINER
} MockValueKind;

typedef struct MockContainer {
  int int_items[MAX_CONTAINER_ITEMS];
  double dbl_items[MAX_CONTAINER_ITEMS];
  char str_items[MAX_CONTAINER_ITEMS][64];
  char map_keys[MAX_CONTAINER_ITEMS][64];
  int count;
} MockContainer;

typedef struct {
  char name[128];
  MockFieldType type;
  int32_t int_val;
  int64_t int64_val;
  double dbl_val;
  char str_val[256];
  uint8_t bytes_val[256];
  size_t bytes_len;
  MockContainer container;
} MockField;

typedef struct Value {
  int kind;
  MockField fields[MAX_FIELDS];
  int field_count;
} Value;

static int g_live_binary_objects = 0;
static int g_live_binary_containers = 0;
static int g_fail_create_set = 0;

static void reset_binary_alloc_state(void) {
  g_live_binary_objects = 0;
  g_live_binary_containers = 0;
  g_fail_create_set = 0;
}

/* ───── Mock API functions ───── */

static Value *mock_create_object(void) {
  Value *v = (Value *)calloc(1, sizeof(Value));
  v->kind = MOCK_VALUE_OBJECT;
  g_live_binary_objects++;
  return v;
}

static void mock_set_field_int(Value *obj, const char *name, int32_t val) {
  if (!obj || obj->field_count >= MAX_FIELDS)
    return;
  MockField *f = &obj->fields[obj->field_count++];
  strncpy(f->name, name, sizeof(f->name) - 1);
  f->type = MOCK_INT;
  f->int_val = val;
}

static void mock_set_field_int64(Value *obj, const char *name, int64_t val) {
  if (!obj || obj->field_count >= MAX_FIELDS)
    return;
  MockField *f = &obj->fields[obj->field_count++];
  strncpy(f->name, name, sizeof(f->name) - 1);
  f->type = MOCK_INT64;
  f->int64_val = val;
}

static void mock_set_field_double(Value *obj, const char *name, double val) {
  if (!obj || obj->field_count >= MAX_FIELDS)
    return;
  MockField *f = &obj->fields[obj->field_count++];
  strncpy(f->name, name, sizeof(f->name) - 1);
  f->type = MOCK_DOUBLE;
  f->dbl_val = val;
}

static void mock_set_field_string(Value *obj, const char *name, const char *val) {
  if (!obj || obj->field_count >= MAX_FIELDS)
    return;
  MockField *f = &obj->fields[obj->field_count++];
  strncpy(f->name, name, sizeof(f->name) - 1);
  f->type = MOCK_STRING;
  if (val)
    strncpy(f->str_val, val, sizeof(f->str_val) - 1);
}

static void mock_set_field_bytes(Value *obj, const char *name, const uint8_t *data, size_t len) {
  if (!obj || obj->field_count >= MAX_FIELDS)
    return;
  MockField *f = &obj->fields[obj->field_count++];
  strncpy(f->name, name, sizeof(f->name) - 1);
  f->type = MOCK_BYTES;
  f->bytes_len = len < sizeof(f->bytes_val) ? len : sizeof(f->bytes_val);
  if (data)
    memcpy(f->bytes_val, data, f->bytes_len);
}

/* ───── Container mock (list/set/map stored as Value* with special name) ───── */

static void mock_free_container(Value *container) {
  if (!container)
    return;
  free(container);
  g_live_binary_containers--;
}

static void mock_destroy_value(Value *value) {
  if (!value)
    return;

  switch (value->kind) {
  case MOCK_VALUE_OBJECT:
    free(value);
    g_live_binary_objects--;
    break;
  case MOCK_VALUE_LIST_CONTAINER:
  case MOCK_VALUE_SET_CONTAINER:
  case MOCK_VALUE_MAP_CONTAINER:
    mock_free_container(value);
    break;
  default:
    free(value);
    break;
  }
}

static Value *mock_create_container(MockFieldType ctype) {
  Value *c = (Value *)calloc(1, sizeof(Value));
  c->kind = ctype == MOCK_LIST ? MOCK_VALUE_LIST_CONTAINER
           : ctype == MOCK_SET ? MOCK_VALUE_SET_CONTAINER
                               : MOCK_VALUE_MAP_CONTAINER;
  c->fields[0].type = ctype;
  c->field_count = 1;
  g_live_binary_containers++;
  return c;
}

static Value *mock_create_list(void) { return mock_create_container(MOCK_LIST); }
static Value *mock_create_set(void) {
  if (g_fail_create_set)
    return NULL;
  return mock_create_container(MOCK_SET);
}
static Value *mock_create_map(void) { return mock_create_container(MOCK_MAP); }

static void mock_add_list_int(Value *list, int32_t val) {
  if (!list)
    return;
  MockContainer *c = &list->fields[0].container;
  if (c->count < MAX_CONTAINER_ITEMS)
    c->int_items[c->count++] = val;
}
static void mock_add_list_int64(Value *list, int64_t val) {
  if (!list)
    return;
  MockContainer *c = &list->fields[0].container;
  if (c->count < MAX_CONTAINER_ITEMS)
    c->int_items[c->count++] = (int)val;
}
static void mock_add_list_dbl(Value *list, double val) {
  if (!list)
    return;
  MockContainer *c = &list->fields[0].container;
  if (c->count < MAX_CONTAINER_ITEMS)
    c->dbl_items[c->count++] = val;
}
static void mock_add_list_str(Value *list, const char *val) {
  if (!list)
    return;
  MockContainer *c = &list->fields[0].container;
  if (c->count < MAX_CONTAINER_ITEMS && val)
    strncpy(c->str_items[c->count++], val, 63);
}
static void mock_set_field_list(Value *obj, const char *name, Value *list) {
  if (!obj || obj->field_count >= MAX_FIELDS)
    return;
  MockField *f = &obj->fields[obj->field_count++];
  strncpy(f->name, name, sizeof(f->name) - 1);
  f->type = MOCK_LIST;
  if (list)
    f->container = list->fields[0].container;
  mock_free_container(list);
}

static void mock_add_set_int(Value *set, int32_t val) { mock_add_list_int(set, val); }
static void mock_add_set_int64(Value *set, int64_t val) { mock_add_list_int64(set, val); }
static void mock_add_set_dbl(Value *set, double val) { mock_add_list_dbl(set, val); }
static void mock_add_set_str(Value *set, const char *val) { mock_add_list_str(set, val); }
static void mock_set_field_set(Value *obj, const char *name, Value *set) {
  if (!obj || obj->field_count >= MAX_FIELDS)
    return;
  MockField *f = &obj->fields[obj->field_count++];
  strncpy(f->name, name, sizeof(f->name) - 1);
  f->type = MOCK_SET;
  if (set)
    f->container = set->fields[0].container;
  mock_free_container(set);
}

static void mock_add_map_str_str(Value *map, const char *key, const char *val) {
  if (!map)
    return;
  MockContainer *c = &map->fields[0].container;
  if (c->count < MAX_CONTAINER_ITEMS && key && val) {
    strncpy(c->map_keys[c->count], key, 63);
    strncpy(c->str_items[c->count], val, 63);
    c->count++;
  }
}
static void mock_add_map_str_int(Value *map, const char *key, int32_t val) {
  if (!map)
    return;
  MockContainer *c = &map->fields[0].container;
  if (c->count < MAX_CONTAINER_ITEMS && key) {
    strncpy(c->map_keys[c->count], key, 63);
    c->int_items[c->count++] = val;
  }
}
static void mock_add_map_str_int64(Value *map, const char *key, int64_t val) {
  mock_add_map_str_int(map, key, (int32_t)val);
}
static void mock_add_map_str_dbl(Value *map, const char *key, double val) {
  if (!map)
    return;
  MockContainer *c = &map->fields[0].container;
  if (c->count < MAX_CONTAINER_ITEMS && key) {
    strncpy(c->map_keys[c->count], key, 63);
    c->dbl_items[c->count++] = val;
  }
}
static void mock_set_field_map(Value *obj, const char *name, Value *map) {
  if (!obj || obj->field_count >= MAX_FIELDS)
    return;
  MockField *f = &obj->fields[obj->field_count++];
  strncpy(f->name, name, sizeof(f->name) - 1);
  f->type = MOCK_MAP;
  if (map)
    f->container = map->fields[0].container;
  mock_free_container(map);
}

static DataBindValueApi test_api = {
    .create_object = mock_create_object,
    .set_field_int = mock_set_field_int,
    .set_field_int64 = mock_set_field_int64,
    .set_field_double = mock_set_field_double,
    .set_field_string = mock_set_field_string,
    .set_field_bytes = mock_set_field_bytes,
    .create_list = mock_create_list,
    .add_list_item_int = mock_add_list_int,
    .add_list_item_int64 = mock_add_list_int64,
    .add_list_item_double = mock_add_list_dbl,
    .add_list_item_string = mock_add_list_str,
    .set_field_list = mock_set_field_list,
    .create_set = mock_create_set,
    .add_set_item_int = mock_add_set_int,
    .add_set_item_double = mock_add_set_dbl,
    .add_set_item_string = mock_add_set_str,
    .set_field_set = mock_set_field_set,
    .create_map = mock_create_map,
    .add_map_entry_string_string = mock_add_map_str_str,
    .add_map_entry_string_int = mock_add_map_str_int,
    .add_map_entry_string_double = mock_add_map_str_dbl,
    .set_field_map = mock_set_field_map,
    .add_set_item_int64 = mock_add_set_int64,
    .add_map_entry_string_int64 = mock_add_map_str_int64,
    .destroy_value = mock_destroy_value,
};

/* ───── Helpers ───── */

static MockField *find_field(Value *v, const char *name) {
  if (!v)
    return NULL;
  for (int i = 0; i < v->field_count; i++) {
    if (strcmp(v->fields[i].name, name) == 0)
      return &v->fields[i];
  }
  return NULL;
}

static void write_schema(const char *path, const char *content) {
  FILE *f = fopen(path, "w");
  if (f) {
    fwrite(content, 1, strlen(content), f);
    fclose(f);
  }
}

/* ───── Tests ───── */

suite("Binary Codec") {

  section("Codec Creation with Binary Format") {
    given("a valid RFL schema file") {
      write_schema("test_binary.rfl", "declare Message\n"
                                      "    id: int\n"
                                      "    value: int\n"
                                      "end\n");

      when("creating codec with BINARY format") {
        DataBind *codec =
            data_bind_create_ex("test_binary.rfl", DATA_BIND_FORMAT_BINARY, &test_api);
        if (!codec)
          printf("  [codec creation error] %s\n", data_bind_get_error(NULL));

        then("codec should be created") { check_not_null(codec); }

        if (codec)
          data_bind_free(codec);
      }
      remove("test_binary.rfl");
    }
  }

  section("Required Binary ABI") {
    given("a host API without destroy_value") {
      DataBindValueApi missing_destroy_api = test_api;
      missing_destroy_api.destroy_value = NULL;

      write_schema("test_binary_requires_destroy.rfl", "declare Message\n"
                                                       "    id: int\n"
                                                       "end\n");

      when("creating codec with BINARY format") {
        DataBind *codec =
            data_bind_create_ex("test_binary_requires_destroy.rfl", DATA_BIND_FORMAT_BINARY,
                                &missing_destroy_api);

        then("codec creation should fail early") { check_null(codec); }

        then("error should name the missing destroy_value callback") {
          check_str_contains(data_bind_get_error(NULL), "destroy_value");
        }

        if (codec)
          data_bind_free(codec);
      }

      remove("test_binary_requires_destroy.rfl");
    }
  }

  section("Binary Parsing - Primitive Types") {
    given("a schema with int fields") {
      write_schema("test_primitives.rfl", "declare Data\n"
                                          "    a: int\n"
                                          "    b: int\n"
                                          "    c: long\n"
                                          "end\n");

      when("parsing binary data") {
        DataBind *codec =
            data_bind_create_ex("test_primitives.rfl", DATA_BIND_FORMAT_BINARY, &test_api);
        if (!codec)
          fprintf(stderr, "  [error] %s", data_bind_get_error(NULL));

        then("codec should be created") { check_not_null(codec); }

        then("should parse binary fields correctly") {
          if (codec) {
            uint8_t buf[16];
            *(int32_t *)(buf + 0) = 100;
            *(int32_t *)(buf + 4) = 200;
            *(int64_t *)(buf + 8) = 300000LL;

            Value *v = (Value *)data_bind_parse(codec, "Data", buf, sizeof(buf));
            check_not_null(v);

            if (v) {
              MockField *f_a = find_field(v, "a");
              check_not_null(f_a);
              if (f_a) {
                check(f_a->type == MOCK_INT);
                check(f_a->int_val == 100);
              }

              MockField *f_b = find_field(v, "b");
              check_not_null(f_b);
              if (f_b) {
                check(f_b->type == MOCK_INT);
                check(f_b->int_val == 200);
              }

              MockField *f_c = find_field(v, "c");
              check_not_null(f_c);
              if (f_c) {
                check(f_c->type == MOCK_INT64);
                check(f_c->int64_val == 300000LL);
              }

              free(v);
            }
          }
        }

        if (codec)
          data_bind_free(codec);
      }
      remove("test_primitives.rfl");
    }
  }

  section("Binary Parsing - Float Types") {
    given("a schema with float and double") {
      write_schema("test_floats.rfl", "declare FloatData\n"
                                      "    price: float\n"
                                      "    amount: double\n"
                                      "end\n");

      when("parsing binary float data") {
        DataBind *codec =
            data_bind_create_ex("test_floats.rfl", DATA_BIND_FORMAT_BINARY, &test_api);
        if (!codec)
          fprintf(stderr, "  [error] %s", data_bind_get_error(NULL));

        then("should parse floats correctly") {
          if (codec) {
            uint8_t buf[12];
            *(float *)(buf + 0) = 19.99f;
            *(double *)(buf + 4) = 12345.67;

            Value *v = (Value *)data_bind_parse(codec, "FloatData", buf, sizeof(buf));
            check_not_null(v);

            if (v) {
              MockField *f_price = find_field(v, "price");
              check_not_null(f_price);
              if (f_price) {
                check(f_price->type == MOCK_DOUBLE);
                check(fabs(f_price->dbl_val - 19.99) < 0.01);
              }

              MockField *f_amount = find_field(v, "amount");
              check_not_null(f_amount);
              if (f_amount) {
                check(f_amount->type == MOCK_DOUBLE);
                check(fabs(f_amount->dbl_val - 12345.67) < 0.01);
              }

              free(v);
            }
          }
        }

        if (codec)
          data_bind_free(codec);
      }
      remove("test_floats.rfl");
    }
  }

  section("Binary Parsing - String Field") {
    given("a schema with string followed by int") {
      write_schema("test_string.rfl", "declare Message\n"
                                      "    title: String\n"
                                      "    qty: int\n"
                                      "end\n");

      when("parsing binary string data") {
        DataBind *codec =
            data_bind_create_ex("test_string.rfl", DATA_BIND_FORMAT_BINARY, &test_api);
        if (!codec)
          fprintf(stderr, "  [error] %s", data_bind_get_error(NULL));

        then("should parse string and following int correctly") {
          if (codec) {
            uint8_t buf[32];
            size_t off = 0;
            *(uint16_t *)(buf + off) = 5;
            off += 2;
            memcpy(buf + off, "hello", 5);
            off += 5;
            *(int32_t *)(buf + off) = 42;
            off += 4;

            Value *v = (Value *)data_bind_parse(codec, "Message", buf, off);
            check_not_null(v);

            if (v) {
              MockField *f_title = find_field(v, "title");
              check_not_null(f_title);
              if (f_title) {
                check(f_title->type == MOCK_STRING);
                check(strcmp(f_title->str_val, "hello") == 0);
              }

              MockField *f_qty = find_field(v, "qty");
              check_not_null(f_qty);
              if (f_qty) {
                check(f_qty->type == MOCK_INT);
                check(f_qty->int_val == 42);
              }

              free(v);
            }
          }
        }

        if (codec)
          data_bind_free(codec);
      }

      remove("test_string.rfl");
    }
  }

  section("Binary Parsing - Truncated String Payload") {
    given("a schema with one string field") {
      write_schema("test_string_trunc.rfl", "declare Message\n"
                                            "    title: String\n"
                                            "end\n");

      when("the length prefix exceeds the remaining buffer") {
        DataBind *codec =
            data_bind_create_ex("test_string_trunc.rfl", DATA_BIND_FORMAT_BINARY, &test_api);

        then("codec should be created") { check_not_null(codec); }

        if (codec) {
          uint8_t buf[8];
          *(uint16_t *)(buf + 0) = 5;
          memcpy(buf + 2, "abc", 3);

          Value *v = (Value *)data_bind_parse(codec, "Message", buf, 5);
          then("parse should fail instead of reading past the buffer") { check_null(v); }

          data_bind_free(codec);
        }
      }
      remove("test_string_trunc.rfl");
    }
  }

  section("Binary Parsing - Bounds Checking") {
    given("a schema with int field") {
      write_schema("test_bounds.rfl", "declare Small\n"
                                      "    x: int\n"
                                      "end\n");

      when("buffer is too short") {
        DataBind *codec =
            data_bind_create_ex("test_bounds.rfl", DATA_BIND_FORMAT_BINARY, &test_api);
        if (!codec)
          fprintf(stderr, "  [error] %s", data_bind_get_error(NULL));

        then("should return NULL for short buffer") {
          if (codec) {
            uint8_t buf[2] = {0x01, 0x02};
            Value *v = (Value *)data_bind_parse(codec, "Small", buf, sizeof(buf));
            check_null(v);
          }
        }

        if (codec)
          data_bind_free(codec);
      }

      when("buffer is exactly right size") {
        DataBind *codec =
            data_bind_create_ex("test_bounds.rfl", DATA_BIND_FORMAT_BINARY, &test_api);
        if (!codec)
          fprintf(stderr, "  [error] %s", data_bind_get_error(NULL));

        then("should parse correctly") {
          if (codec) {
            uint8_t buf[4];
            *(int32_t *)buf = 42;
            Value *v = (Value *)data_bind_parse(codec, "Small", buf, sizeof(buf));
            check_not_null(v);

            if (v) {
              MockField *f = find_field(v, "x");
              check_not_null(f);
              if (f)
                check(f->int_val == 42);
              free(v);
            }
          }
        }

        if (codec)
          data_bind_free(codec);
      }

      remove("test_bounds.rfl");
    }
  }

  section("Binary Failure Cleanup") {
    given("a schema whose Set container creation fails after the root object is created") {
      write_schema("test_binary_cleanup.rfl", "declare Data\n"
                                              "    ids: Set<int>\n"
                                              "end\n");

      when("parsing valid binary data") {
        reset_binary_alloc_state();
        g_fail_create_set = 1;
        DataBind *codec =
            data_bind_create_ex("test_binary_cleanup.rfl", DATA_BIND_FORMAT_BINARY, &test_api);

        then("parse should fail and destroy the root object allocated before the failure") {
          if (codec) {
            uint8_t buf[8];
            *(uint32_t *)(buf + 0) = 1;
            *(int32_t *)(buf + 4) = 42;

            Value *v = (Value *)data_bind_parse(codec, "Data", buf, sizeof(buf));
            check_null(v);
            check_str_contains(data_bind_get_error(codec), "Binary parse failed");
            check(g_live_binary_objects == 0);
            check(g_live_binary_containers == 0);
          }
        }

        g_fail_create_set = 0;
        if (codec)
          data_bind_free(codec);
      }

      remove("test_binary_cleanup.rfl");
    }
  }

  section("Binary vs JSON Format") {
    given("same schema for both formats") {
      write_schema("test_compare.rfl", "declare Product\n"
                                       "    id: int\n"
                                       "    price: double\n"
                                       "end\n");

      when("using binary format") {
        DataBind *binary_codec =
            data_bind_create_ex("test_compare.rfl", DATA_BIND_FORMAT_BINARY, &test_api);
        if (!binary_codec)
          fprintf(stderr, "  [error] %s", data_bind_get_error(NULL));

        then("should parse binary data") {
          if (binary_codec) {
            uint8_t buf[12];
            *(int32_t *)(buf + 0) = 123;
            *(double *)(buf + 4) = 99.99;

            Value *v = (Value *)data_bind_parse(binary_codec, "Product", buf, sizeof(buf));
            check_not_null(v);

            if (v) {
              MockField *f_id = find_field(v, "id");
              MockField *f_price = find_field(v, "price");
              check_not_null(f_id);
              check_not_null(f_price);
              if (f_id)
                check(f_id->int_val == 123);
              if (f_price)
                check(fabs(f_price->dbl_val - 99.99) < 0.01);
              free(v);
            }
          }
        }

        if (binary_codec)
          data_bind_free(binary_codec);
      }

      remove("test_compare.rfl");
    }
  }

  section("Complex Schema - Order with nested Header") {
    given("a schema with nested composite, enum-like int, long, float, double, bool") {
      write_schema("test_complex.rfl", "declare Header\n"
                                       "    version: int\n"
                                       "    seq: long\n"
                                       "end\n"
                                       "declare Order\n"
                                       "    header: Header\n"
                                       "    id: long\n"
                                       "    side: int\n"
                                       "    qty: int\n"
                                       "    price: double\n"
                                       "    commission: float\n"
                                       "    is_active: boolean\n"
                                       "end\n");

      when("parsing a fully populated Order") {
        DataBind *codec =
            data_bind_create_ex("test_complex.rfl", DATA_BIND_FORMAT_BINARY, &test_api);
        if (!codec)
          fprintf(stderr, "  [error] %s", data_bind_get_error(NULL));

        then("codec should be created") { check_not_null(codec); }

        then("should parse all fields correctly") {
          if (codec) {
            /* Layout:
             * header.version : int32   @ 0  (4)
             * header.seq     : int64   @ 4  (8)
             * id             : int64   @ 12 (8)
             * side           : int32   @ 20 (4)
             * qty            : int32   @ 24 (4)
             * price          : double  @ 28 (8)
             * commission     : float   @ 36 (4)
             * is_active      : bool    @ 40 (1)
             * total = 41 bytes
             */
            uint8_t buf[41];
            memset(buf, 0, sizeof(buf));
            *(int32_t *)(buf + 0) = 3;
            *(int64_t *)(buf + 4) = 99LL;
            *(int64_t *)(buf + 12) = 1234567890LL;
            *(int32_t *)(buf + 20) = 1;
            *(int32_t *)(buf + 24) = 500;
            *(double *)(buf + 28) = 123.45;
            *(float *)(buf + 36) = 0.5f;
            buf[40] = 1;

            Value *v = (Value *)data_bind_parse(codec, "Order", buf, sizeof(buf));
            check_not_null(v);

            if (v) {
              MockField *f;

              f = find_field(v, "header.version");
              check_not_null(f);
              if (f) {
                check(f->type == MOCK_INT);
                check(f->int_val == 3);
              }

              f = find_field(v, "header.seq");
              check_not_null(f);
              if (f) {
                check(f->type == MOCK_INT64);
                check(f->int64_val == 99LL);
              }

              f = find_field(v, "id");
              check_not_null(f);
              if (f) {
                check(f->type == MOCK_INT64);
                check(f->int64_val == 1234567890LL);
              }

              f = find_field(v, "side");
              check_not_null(f);
              if (f) {
                check(f->type == MOCK_INT);
                check(f->int_val == 1);
              }

              f = find_field(v, "qty");
              check_not_null(f);
              if (f) {
                check(f->type == MOCK_INT);
                check(f->int_val == 500);
              }

              f = find_field(v, "price");
              check_not_null(f);
              if (f) {
                check(f->type == MOCK_DOUBLE);
                check(fabs(f->dbl_val - 123.45) < 0.001);
              }

              f = find_field(v, "commission");
              check_not_null(f);
              if (f) {
                check(f->type == MOCK_DOUBLE);
                check(fabs(f->dbl_val - 0.5) < 0.001);
              }

              f = find_field(v, "is_active");
              check_not_null(f);
              if (f) {
                check(f->type == MOCK_INT);
                check(f->int_val == 1);
              }

              free(v);
            }
          }
        }

        if (codec)
          data_bind_free(codec);
      }
      remove("test_complex.rfl");
    }
  }

  section("Complex Schema - Multiple message types") {
    given("a schema with Ping and Pong messages") {
      write_schema("test_multi_msg.rfl", "declare Ping\n"
                                         "    seq: long\n"
                                         "    timestamp: long\n"
                                         "end\n"
                                         "declare Pong\n"
                                         "    seq: long\n"
                                         "    latency: int\n"
                                         "end\n");

      when("parsing both message types from same codec") {
        DataBind *codec =
            data_bind_create_ex("test_multi_msg.rfl", DATA_BIND_FORMAT_BINARY, &test_api);
        if (!codec)
          fprintf(stderr, "  [error] %s", data_bind_get_error(NULL));

        then("codec should be created") { check_not_null(codec); }

        then("should parse Ping correctly") {
          if (codec) {
            uint8_t buf[16];
            *(int64_t *)(buf + 0) = 42LL;
            *(int64_t *)(buf + 8) = 1700000000LL;

            Value *v = (Value *)data_bind_parse(codec, "Ping", buf, sizeof(buf));
            check_not_null(v);
            if (v) {
              MockField *f_seq = find_field(v, "seq");
              MockField *f_ts = find_field(v, "timestamp");
              check_not_null(f_seq);
              check_not_null(f_ts);
              if (f_seq) {
                check(f_seq->type == MOCK_INT64);
                check(f_seq->int64_val == 42LL);
              }
              if (f_ts) {
                check(f_ts->type == MOCK_INT64);
                check(f_ts->int64_val == 1700000000LL);
              }
              free(v);
            }
          }
        }

        then("should parse Pong correctly") {
          if (codec) {
            uint8_t buf[12];
            *(int64_t *)(buf + 0) = 42LL;
            *(int32_t *)(buf + 8) = 250;

            Value *v = (Value *)data_bind_parse(codec, "Pong", buf, sizeof(buf));
            check_not_null(v);
            if (v) {
              MockField *f_seq = find_field(v, "seq");
              MockField *f_lat = find_field(v, "latency");
              check_not_null(f_seq);
              check_not_null(f_lat);
              if (f_seq) {
                check(f_seq->type == MOCK_INT64);
                check(f_seq->int64_val == 42LL);
              }
              if (f_lat) {
                check(f_lat->type == MOCK_INT);
                check(f_lat->int_val == 250);
              }
              free(v);
            }
          }
        }

        if (codec)
          data_bind_free(codec);
      }
      remove("test_multi_msg.rfl");
    }
  }

  section("List<int> field") {
    given("a schema with List<int>") {
      write_schema("test_list_int.rfl", "declare Msg\n"
                                        "    id: int\n"
                                        "    scores: List<int>\n"
                                        "end\n");

      when("parsing binary with 3 int items") {
        DataBind *codec =
            data_bind_create_ex("test_list_int.rfl", DATA_BIND_FORMAT_BINARY, &test_api);
        
        then("codec should be created") { check_not_null(codec); }

        if (codec) {
          /* id:int32 + count:uint32 + 3*int32 = 4+4+12 = 20 */
          uint8_t buf[20];
          *(int32_t *)(buf + 0) = 7;
          *(uint32_t *)(buf + 4) = 3;
          *(int32_t *)(buf + 8) = 10;
          *(int32_t *)(buf + 12) = 20;
          *(int32_t *)(buf + 16) = 30;

          Value *v = (Value *)data_bind_parse(codec, "Msg", buf, sizeof(buf));
          
          then("result should be non-null") { check_not_null(v); }

          if (v) {
            then("id field should be 7") {
              MockField *f_id = find_field(v, "id");
              check_not_null(f_id);
              if (f_id) check(f_id->int_val == 7);
            }

            then("scores list should have 3 items: [10, 20, 30]") {
              MockField *f_scores = find_field(v, "scores");
              check_not_null(f_scores);
              if (f_scores) {
                check(f_scores->type == MOCK_LIST);
                check(f_scores->container.count == 3);
                check(f_scores->container.int_items[0] == 10);
                check(f_scores->container.int_items[1] == 20);
                check(f_scores->container.int_items[2] == 30);
              }
            }
            free(v);
          }
          data_bind_free(codec);
        }
      }
      remove("test_list_int.rfl");
    }
  }

  section("Set<int> field") {
    given("a schema with Set<int>") {
      write_schema("test_set_int.rfl", "declare Msg\n"
                                       "    tags: Set<int>\n"
                                       "end\n");

      when("parsing binary with 2 int items") {
        DataBind *codec =
            data_bind_create_ex("test_set_int.rfl", DATA_BIND_FORMAT_BINARY, &test_api);
        
        then("codec should be created") { check_not_null(codec); }

        if (codec) {
          /* count:uint32 + 2*int32 = 4+8 = 12 */
          uint8_t buf[12];
          *(uint32_t *)(buf + 0) = 2;
          *(int32_t *)(buf + 4) = 100;
          *(int32_t *)(buf + 8) = 200;

          Value *v = (Value *)data_bind_parse(codec, "Msg", buf, sizeof(buf));
          
          then("result should be non-null") { check_not_null(v); }

          if (v) {
            then("tags set should have 2 items: [100, 200]") {
              MockField *f = find_field(v, "tags");
              check_not_null(f);
              if (f) {
                check(f->type == MOCK_SET);
                check(f->container.count == 2);
                check(f->container.int_items[0] == 100);
                check(f->container.int_items[1] == 200);
              }
            }
            free(v);
          }
          data_bind_free(codec);
        }
      }
      remove("test_set_int.rfl");
    }
  }

  section("Set<long> field") {
    given("a schema with Set<long>") {
      write_schema("test_set_long.rfl", "declare Msg\n"
                                        "    ids: Set<long>\n"
                                        "end\n");

      when("parsing binary with 2 long items") {
        DataBind *codec =
            data_bind_create_ex("test_set_long.rfl", DATA_BIND_FORMAT_BINARY, &test_api);

        then("codec should be created") { check_not_null(codec); }

        if (codec) {
          uint8_t buf[20];
          *(uint32_t *)(buf + 0) = 2;
          *(int64_t *)(buf + 4) = 100;
          *(int64_t *)(buf + 12) = 200;

          Value *v = (Value *)data_bind_parse(codec, "Msg", buf, sizeof(buf));

          then("result should be non-null") { check_not_null(v); }

          if (v) {
            then("ids set should have 2 items: [100, 200]") {
              MockField *f = find_field(v, "ids");
              check_not_null(f);
              if (f) {
                check(f->type == MOCK_SET);
                check(f->container.count == 2);
                check(f->container.int_items[0] == 100);
                check(f->container.int_items[1] == 200);
              }
            }
            free(v);
          }
          data_bind_free(codec);
        }
      }
      remove("test_set_long.rfl");
    }
  }

  section("List<String> field") {
    given("a schema with List<String>") {
      write_schema("test_list_str.rfl", "declare Msg\n"
                                        "    names: List<String>\n"
                                        "end\n");

      when("parsing binary with 2 string items") {
        DataBind *codec =
            data_bind_create_ex("test_list_str.rfl", DATA_BIND_FORMAT_BINARY, &test_api);

        then("codec should be created") { check_not_null(codec); }

        if (codec) {
          uint8_t buf[64];
          size_t off = 0;
          *(uint32_t *)(buf + off) = 2;
          off += 4;

          *(uint16_t *)(buf + off) = 5;
          off += 2;
          memcpy(buf + off, "alice", 5);
          off += 5;

          *(uint16_t *)(buf + off) = 3;
          off += 2;
          memcpy(buf + off, "bob", 3);
          off += 3;

          Value *v = (Value *)data_bind_parse(codec, "Msg", buf, off);

          then("result should be non-null") { check_not_null(v); }

          if (v) {
            then("names list should have 2 items: [alice, bob]") {
              MockField *f = find_field(v, "names");
              check_not_null(f);
              if (f) {
                check(f->type == MOCK_LIST);
                check(f->container.count == 2);
                check(strcmp(f->container.str_items[0], "alice") == 0);
                check(strcmp(f->container.str_items[1], "bob") == 0);
              }
            }
            free(v);
          }
          data_bind_free(codec);
        }
      }
      remove("test_list_str.rfl");
    }
  }

  section("Set<String> field") {
    given("a schema with Set<String>") {
      write_schema("test_set_str.rfl", "declare Msg\n"
                                       "    tags: Set<String>\n"
                                       "end\n");

      when("parsing binary with 2 string items") {
        DataBind *codec =
            data_bind_create_ex("test_set_str.rfl", DATA_BIND_FORMAT_BINARY, &test_api);

        then("codec should be created") { check_not_null(codec); }

        if (codec) {
          uint8_t buf[64];
          size_t off = 0;
          *(uint32_t *)(buf + off) = 2;
          off += 4;

          *(uint16_t *)(buf + off) = 3;
          off += 2;
          memcpy(buf + off, "red", 3);
          off += 3;

          *(uint16_t *)(buf + off) = 4;
          off += 2;
          memcpy(buf + off, "blue", 4);
          off += 4;

          Value *v = (Value *)data_bind_parse(codec, "Msg", buf, off);

          then("result should be non-null") { check_not_null(v); }

          if (v) {
            then("tags set should have 2 items: [red, blue]") {
              MockField *f = find_field(v, "tags");
              check_not_null(f);
              if (f) {
                check(f->type == MOCK_SET);
                check(f->container.count == 2);
                check(strcmp(f->container.str_items[0], "red") == 0);
                check(strcmp(f->container.str_items[1], "blue") == 0);
              }
            }
            free(v);
          }
          data_bind_free(codec);
        }
      }
      remove("test_set_str.rfl");
    }
  }

  section("Enum field") {
    given("a schema with an enum field") {
      write_schema("test_enum.rfl", "enum Status<int>\n"
                                    "    PENDING\n"
                                    "    ACTIVE\n"
                                    "    CLOSED\n"
                                    "end\n"
                                    "declare Order\n"
                                    "    id: int\n"
                                    "    status: Status\n"
                                    "end\n");

      when("parsing binary with enum value") {
        DataBind *codec = data_bind_create_ex("test_enum.rfl", DATA_BIND_FORMAT_BINARY, &test_api);
        
        then("codec should be created") { check_not_null(codec); }

        if (codec) {
          /* id:int32 + status:int32 = 8 bytes */
          uint8_t buf[8];
          *(int32_t *)(buf + 0) = 42;
          *(int32_t *)(buf + 4) = 1; /* ACTIVE */

          Value *v = (Value *)data_bind_parse(codec, "Order", buf, sizeof(buf));
          
          then("result should be non-null") { check_not_null(v); }

          if (v) {
            then("id field should be 42") {
              MockField *f_id = find_field(v, "id");
              check_not_null(f_id);
              if (f_id) {
                check(f_id->type == MOCK_INT);
                check(f_id->int_val == 42);
              }
            }

            then("status field should be 1 (ACTIVE)") {
              MockField *f_status = find_field(v, "status");
              check_not_null(f_status);
              if (f_status) {
                check(f_status->type == MOCK_INT);
                check(f_status->int_val == 1);
              }
            }

            free(v);
          }
          data_bind_free(codec);
        }
      }
      remove("test_enum.rfl");
    }
  }

  section("Enum<long> field") {
    given("a schema with a long-backed enum field") {
      write_schema("test_enum_long.rfl", "enum EventType<long>\n"
                                         "    LOGIN\n"
                                         "    LOGOUT\n"
                                         "end\n"
                                         "declare Event\n"
                                         "    type: EventType\n"
                                         "    ts: long\n"
                                         "end\n");

      when("parsing binary with long enum value") {
        DataBind *codec =
            data_bind_create_ex("test_enum_long.rfl", DATA_BIND_FORMAT_BINARY, &test_api);
        
        then("codec should be created") { check_not_null(codec); }

        if (codec) {
          /* type:int64 + ts:int64 = 16 bytes */
          uint8_t buf[16];
          *(int64_t *)(buf + 0) = 1LL; /* LOGOUT */
          *(int64_t *)(buf + 8) = 1700000000LL;

          Value *v = (Value *)data_bind_parse(codec, "Event", buf, sizeof(buf));
          
          then("result should be non-null") { check_not_null(v); }

          if (v) {
            then("type field should be 1 (LOGOUT)") {
              MockField *f_type = find_field(v, "type");
              check_not_null(f_type);
              if (f_type) {
                check(f_type->type == MOCK_INT64);
                check(f_type->int64_val == 1LL);
              }
            }

            then("ts field should be 1700000000") {
              MockField *f_ts = find_field(v, "ts");
              check_not_null(f_ts);
              if (f_ts) {
                check(f_ts->type == MOCK_INT64);
                check(f_ts->int64_val == 1700000000LL);
              }
            }

            free(v);
          }
          data_bind_free(codec);
        }
      }
      remove("test_enum_long.rfl");
    }
  }

  section("Map<String, int> field") {
    given("a schema with Map<String, int>") {
      write_schema("test_map_str_int.rfl", "declare Msg\n"
                                           "    attrs: Map<String, int>\n"
                                           "end\n");

      when("parsing binary with 2 entries") {
        DataBind *codec =
            data_bind_create_ex("test_map_str_int.rfl", DATA_BIND_FORMAT_BINARY, &test_api);
        
        then("codec should be created") { check_not_null(codec); }

        if (codec) {
          /* count:uint32 + 2*(varstr key + int32 val) */
          uint8_t buf[64];
          size_t off = 0;
          *(uint32_t *)(buf + off) = 2;
          off += 4;
          /* entry 1: key="age", val=30 */
          *(uint16_t *)(buf + off) = 3;
          off += 2;
          memcpy(buf + off, "age", 3);
          off += 3;
          *(int32_t *)(buf + off) = 30;
          off += 4;
          /* entry 2: key="score", val=99 */
          *(uint16_t *)(buf + off) = 5;
          off += 2;
          memcpy(buf + off, "score", 5);
          off += 5;
          *(int32_t *)(buf + off) = 99;
          off += 4;

          Value *v = (Value *)data_bind_parse(codec, "Msg", buf, off);
          
          then("result should be non-null") { check_not_null(v); }

          if (v) {
            then("attrs map should have 2 entries") {
              MockField *f = find_field(v, "attrs");
              check_not_null(f);
              if (f) {
                check(f->type == MOCK_MAP);
                check(f->container.count == 2);
                check(strcmp(f->container.map_keys[0], "age") == 0);
                check(f->container.int_items[0] == 30);
                check(strcmp(f->container.map_keys[1], "score") == 0);
                check(f->container.int_items[1] == 99);
              }
            }
            free(v);
          }
          data_bind_free(codec);
        }
      }
      remove("test_map_str_int.rfl");
    }
  }

  section("Map<String, long> field") {
    given("a schema with Map<String, long>") {
      write_schema("test_map_str_long.rfl", "declare Msg\n"
                                            "    attrs: Map<String, long>\n"
                                            "end\n");

      when("parsing binary with 2 long entries") {
        DataBind *codec =
            data_bind_create_ex("test_map_str_long.rfl", DATA_BIND_FORMAT_BINARY, &test_api);

        then("codec should be created") { check_not_null(codec); }

        if (codec) {
          uint8_t buf[128];
          size_t off = 0;
          *(uint32_t *)(buf + off) = 2;
          off += 4;

          *(uint16_t *)(buf + off) = 3;
          off += 2;
          memcpy(buf + off, "min", 3);
          off += 3;
          *(int64_t *)(buf + off) = 10;
          off += 8;

          *(uint16_t *)(buf + off) = 3;
          off += 2;
          memcpy(buf + off, "max", 3);
          off += 3;
          *(int64_t *)(buf + off) = 20;
          off += 8;

          Value *v = (Value *)data_bind_parse(codec, "Msg", buf, off);

          then("result should be non-null") { check_not_null(v); }

          if (v) {
            then("attrs map should have 2 entries") {
              MockField *f = find_field(v, "attrs");
              check_not_null(f);
              if (f) {
                check(f->type == MOCK_MAP);
                check(f->container.count == 2);
                check(strcmp(f->container.map_keys[0], "min") == 0);
                check(f->container.int_items[0] == 10);
                check(strcmp(f->container.map_keys[1], "max") == 0);
                check(f->container.int_items[1] == 20);
              }
            }
            free(v);
          }
          data_bind_free(codec);
        }
      }
      remove("test_map_str_long.rfl");
    }
  }

  section("Map<String, String> field") {
    given("a schema with Map<String, String>") {
      write_schema("test_map_str_str.rfl", "declare Msg\n"
                                           "    attrs: Map<String, String>\n"
                                           "end\n");

      when("parsing binary with 2 string entries") {
        DataBind *codec =
            data_bind_create_ex("test_map_str_str.rfl", DATA_BIND_FORMAT_BINARY, &test_api);

        then("codec should be created") { check_not_null(codec); }

        if (codec) {
          uint8_t buf[96];
          size_t off = 0;
          *(uint32_t *)(buf + off) = 2;
          off += 4;

          *(uint16_t *)(buf + off) = 5;
          off += 2;
          memcpy(buf + off, "color", 5);
          off += 5;
          *(uint16_t *)(buf + off) = 5;
          off += 2;
          memcpy(buf + off, "green", 5);
          off += 5;

          *(uint16_t *)(buf + off) = 4;
          off += 2;
          memcpy(buf + off, "size", 4);
          off += 4;
          *(uint16_t *)(buf + off) = 5;
          off += 2;
          memcpy(buf + off, "large", 5);
          off += 5;

          Value *v = (Value *)data_bind_parse(codec, "Msg", buf, off);

          then("result should be non-null") { check_not_null(v); }

          if (v) {
            then("attrs map should have 2 entries") {
              MockField *f = find_field(v, "attrs");
              check_not_null(f);
              if (f) {
                check(f->type == MOCK_MAP);
                check(f->container.count == 2);
                check(strcmp(f->container.map_keys[0], "color") == 0);
                check(strcmp(f->container.str_items[0], "green") == 0);
                check(strcmp(f->container.map_keys[1], "size") == 0);
                check(strcmp(f->container.str_items[1], "large") == 0);
              }
            }
            free(v);
          }
          data_bind_free(codec);
        }
      }
      remove("test_map_str_str.rfl");
    }
  }

  section("Reject unsupported Map<int, int> schema") {
    given("a schema whose map key is not String") {
      write_schema("test_map_bad_key.rfl", "declare Msg\n"
                                           "    attrs: Map<int, int>\n"
                                           "end\n");

      when("creating the binary codec") {
        DataBind *codec =
            data_bind_create_ex("test_map_bad_key.rfl", DATA_BIND_FORMAT_BINARY, &test_api);

        then("codec creation should fail early") { check_null(codec); }

        if (codec)
          data_bind_free(codec);
      }
      remove("test_map_bad_key.rfl");
    }
  }
}

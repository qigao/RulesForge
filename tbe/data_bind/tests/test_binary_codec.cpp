/**
 * @file test_binary_codec.cpp
 * @brief Test binary codec functionality using data_bind API with explicit format
 */

#include "data_bind.h"
#include "tinytest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ───── Mock Value: stores up to 32 named fields ───── */

#define MAX_FIELDS 32

typedef enum { MOCK_INT, MOCK_DOUBLE, MOCK_STRING, MOCK_BYTES } MockFieldType;

typedef struct {
    char name[128];
    MockFieldType type;
    int32_t  int_val;
    double   dbl_val;
    char     str_val[256];
    uint8_t  bytes_val[256];
    size_t   bytes_len;
} MockField;

typedef struct Value {
    MockField fields[MAX_FIELDS];
    int field_count;
} Value;

/* ───── Mock API functions ───── */

static Value* mock_create_object(void) {
    Value* v = (Value*)calloc(1, sizeof(Value));
    return v;
}

static void mock_set_field_int(Value* obj, const char* name, int32_t val) {
    if (!obj || obj->field_count >= MAX_FIELDS) return;
    MockField* f = &obj->fields[obj->field_count++];
    strncpy(f->name, name, sizeof(f->name) - 1);
    f->type = MOCK_INT;
    f->int_val = val;
}

static void mock_set_field_double(Value* obj, const char* name, double val) {
    if (!obj || obj->field_count >= MAX_FIELDS) return;
    MockField* f = &obj->fields[obj->field_count++];
    strncpy(f->name, name, sizeof(f->name) - 1);
    f->type = MOCK_DOUBLE;
    f->dbl_val = val;
}

static void mock_set_field_string(Value* obj, const char* name, const char* val) {
    if (!obj || obj->field_count >= MAX_FIELDS) return;
    MockField* f = &obj->fields[obj->field_count++];
    strncpy(f->name, name, sizeof(f->name) - 1);
    f->type = MOCK_STRING;
    if (val) strncpy(f->str_val, val, sizeof(f->str_val) - 1);
}

static void mock_set_field_bytes(Value* obj, const char* name, const uint8_t* data, size_t len) {
    if (!obj || obj->field_count >= MAX_FIELDS) return;
    MockField* f = &obj->fields[obj->field_count++];
    strncpy(f->name, name, sizeof(f->name) - 1);
    f->type = MOCK_BYTES;
    f->bytes_len = len < sizeof(f->bytes_val) ? len : sizeof(f->bytes_val);
    if (data) memcpy(f->bytes_val, data, f->bytes_len);
}

static DataBindValueApi test_api = {
    .create_object   = mock_create_object,
    .set_field_int    = mock_set_field_int,
    .set_field_double = mock_set_field_double,
    .set_field_string = mock_set_field_string,
    .set_field_bytes  = mock_set_field_bytes,
};

/* ───── Helpers ───── */

static MockField* find_field(Value* v, const char* name) {
    if (!v) return NULL;
    for (int i = 0; i < v->field_count; i++) {
        if (strcmp(v->fields[i].name, name) == 0)
            return &v->fields[i];
    }
    return NULL;
}

static void write_schema(const char* path, const char* content) {
    FILE* f = fopen(path, "w");
    if (f) { fwrite(content, 1, strlen(content), f); fclose(f); }
}

/* ───── Tests ───── */

suite("Binary Codec") {

    section("Codec Creation with Binary Format") {
        given("a valid RFL schema file") {
            write_schema("test_binary.rfl",
                "declare Message\n"
                "    id: int\n"
                "    value: int\n"
                "end\n");

            when("creating codec with BINARY format") {
                DataBind* codec = data_bind_create_ex("test_binary.rfl", DATA_BIND_FORMAT_BINARY, &test_api);

                then("codec should be created") {
                    check_not_null(codec);
                }

                if (codec) data_bind_free(codec);
            }
            remove("test_binary.rfl");
        }
    }

    section("Binary Parsing - Primitive Types") {
        given("a schema with int fields") {
            write_schema("test_primitives.rfl",
                "declare Data\n"
                "    a: int\n"
                "    b: int\n"
                "    c: long\n"
                "end\n");

            when("parsing binary data") {
                DataBind* codec = data_bind_create_ex("test_primitives.rfl", DATA_BIND_FORMAT_BINARY, &test_api);

                then("codec should be created") {
                    check_not_null(codec);
                }

                then("should parse binary fields correctly") {
                    if (codec) {
                        uint8_t buf[16];
                        *(int32_t*)(buf + 0) = 100;
                        *(int32_t*)(buf + 4) = 200;
                        *(int64_t*)(buf + 8) = 300000LL;

                        Value* v = (Value*)data_bind_parse(codec, "Data", buf, sizeof(buf));
                        check_not_null(v);

                        if (v) {
                            MockField* f_a = find_field(v, "a");
                            check_not_null(f_a);
                            if (f_a) {
                                check(f_a->type == MOCK_INT);
                                check(f_a->int_val == 100);
                            }

                            MockField* f_b = find_field(v, "b");
                            check_not_null(f_b);
                            if (f_b) {
                                check(f_b->type == MOCK_INT);
                                check(f_b->int_val == 200);
                            }

                            MockField* f_c = find_field(v, "c");
                            check_not_null(f_c);
                            if (f_c) {
                                check(f_c->type == MOCK_DOUBLE);
                                check(fabs(f_c->dbl_val - 300000.0) < 1.0);
                            }

                            free(v);
                        }
                    }
                }

                if (codec) data_bind_free(codec);
            }
            remove("test_primitives.rfl");
        }
    }

    section("Binary Parsing - Float Types") {
        given("a schema with float and double") {
            write_schema("test_floats.rfl",
                "declare FloatData\n"
                "    price: float\n"
                "    amount: double\n"
                "end\n");

            when("parsing binary float data") {
                DataBind* codec = data_bind_create_ex("test_floats.rfl", DATA_BIND_FORMAT_BINARY, &test_api);

                then("should parse floats correctly") {
                    if (codec) {
                        uint8_t buf[12];
                        *(float*)(buf + 0) = 19.99f;
                        *(double*)(buf + 4) = 12345.67;

                        Value* v = (Value*)data_bind_parse(codec, "FloatData", buf, sizeof(buf));
                        check_not_null(v);

                        if (v) {
                            MockField* f_price = find_field(v, "price");
                            check_not_null(f_price);
                            if (f_price) {
                                check(f_price->type == MOCK_DOUBLE);
                                check(fabs(f_price->dbl_val - 19.99) < 0.01);
                            }

                            MockField* f_amount = find_field(v, "amount");
                            check_not_null(f_amount);
                            if (f_amount) {
                                check(f_amount->type == MOCK_DOUBLE);
                                check(fabs(f_amount->dbl_val - 12345.67) < 0.01);
                            }

                            free(v);
                        }
                    }
                }

                if (codec) data_bind_free(codec);
            }
            remove("test_floats.rfl");
        }
    }

    section("Binary Parsing - Bounds Checking") {
        given("a schema with int field") {
            write_schema("test_bounds.rfl",
                "declare Small\n"
                "    x: int\n"
                "end\n");

            when("buffer is too short") {
                DataBind* codec = data_bind_create_ex("test_bounds.rfl", DATA_BIND_FORMAT_BINARY, &test_api);

                then("should return NULL for short buffer") {
                    if (codec) {
                        uint8_t buf[2] = {0x01, 0x02};
                        Value* v = (Value*)data_bind_parse(codec, "Small", buf, sizeof(buf));
                        check_null(v);
                    }
                }

                if (codec) data_bind_free(codec);
            }

            when("buffer is exactly right size") {
                DataBind* codec = data_bind_create_ex("test_bounds.rfl", DATA_BIND_FORMAT_BINARY, &test_api);

                then("should parse correctly") {
                    if (codec) {
                        uint8_t buf[4];
                        *(int32_t*)buf = 42;
                        Value* v = (Value*)data_bind_parse(codec, "Small", buf, sizeof(buf));
                        check_not_null(v);

                        if (v) {
                            MockField* f = find_field(v, "x");
                            check_not_null(f);
                            if (f) check(f->int_val == 42);
                            free(v);
                        }
                    }
                }

                if (codec) data_bind_free(codec);
            }

            remove("test_bounds.rfl");
        }
    }

    section("Binary vs JSON Format") {
        given("same schema for both formats") {
            write_schema("test_compare.rfl",
                "declare Product\n"
                "    id: int\n"
                "    price: double\n"
                "end\n");

            when("using binary format") {
                DataBind* binary_codec = data_bind_create_ex("test_compare.rfl", DATA_BIND_FORMAT_BINARY, &test_api);

                then("should parse binary data") {
                    if (binary_codec) {
                        uint8_t buf[12];
                        *(int32_t*)(buf + 0) = 123;
                        *(double*)(buf + 4) = 99.99;

                        Value* v = (Value*)data_bind_parse(binary_codec, "Product", buf, sizeof(buf));
                        check_not_null(v);

                        if (v) {
                            MockField* f_id = find_field(v, "id");
                            MockField* f_price = find_field(v, "price");
                            check_not_null(f_id);
                            check_not_null(f_price);
                            if (f_id) check(f_id->int_val == 123);
                            if (f_price) check(fabs(f_price->dbl_val - 99.99) < 0.01);
                            free(v);
                        }
                    }
                }

                if (binary_codec) data_bind_free(binary_codec);
            }

            remove("test_compare.rfl");
        }
    }
}

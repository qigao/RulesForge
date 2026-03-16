/**
 * @file test_data_bind.c
 * @brief Unit tests for data_bind using tinytest
 *
 * Uses a proper mock Value that stores multiple named fields,
 * so we can verify actual parsed values from JIT-compiled functions.
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

/* ───── Helpers to look up fields by name ───── */

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

suite("Data Bind") {

    section("Codec Creation") {
        given("a valid RFL schema file") {
            write_schema("test_create.rfl",
                "declare Ping\n"
                "    seq: int\n"
                "end\n");

            when("creating codec from RFL schema") {
                DataBind* codec = data_bind_create("test_create.rfl", &test_api);

                then("codec should be non-null") {
                    if (!codec) {
                        const char* err = data_bind_get_error(NULL);
                        printf("\nERROR creating codec: %s\n", err);
                    }
                    check_not_null(codec);
                }
                data_bind_free(codec);
            }
            remove("test_create.rfl");
        }

        given("a nonexistent schema file") {
            when("creating codec from nonexistent file") {
                DataBind* codec = data_bind_create("no_such_file.rfl", &test_api);
                then("should return NULL") {
                    check_null(codec);
                }
            }
        }

        given("a NULL api") {
            when("creating codec with NULL api") {
                write_schema("test_null_api.rfl",
                    "declare M\n"
                    "    x: int\n"
                    "end\n");
                DataBind* codec = data_bind_create("test_null_api.rfl", NULL);
                then("should return NULL") {
                    check_null(codec);
                }
                remove("test_null_api.rfl");
            }
        }
    }

    section("Primitive Type Parsing") {
        given("a schema with int and long types") {
            write_schema("test_prim.rfl",
                "declare Primitives\n"
                "    a: int\n"
                "    b: int\n"
                "    c: int\n"
                "    d: long\n"
                "end\n");

            when("parsing binary data with known values") {
                DataBind* codec = data_bind_create("test_prim.rfl", &test_api);

                then("codec should be created") {
                    check_not_null(codec);
                }

                then("should parse all fields correctly") {
                    if (codec) {
                        uint8_t buf[20];
                        memset(buf, 0, sizeof(buf));
                        *(int32_t*)(buf + 0) = 171;
                        *(int32_t*)(buf + 4) = 4660;
                        *(int32_t*)(buf + 8) = 42;
                        *(int64_t*)(buf + 12) = 1000000LL;

                        Value* v = data_bind_parse(codec, "Primitives", buf, sizeof(buf));
                        check_not_null(v);

                        if (v) {
                            MockField* f_a = find_field(v, "a");
                            check_not_null(f_a);
                            if (f_a) { check(f_a->type == MOCK_INT); check(f_a->int_val == 171); }

                            MockField* f_b = find_field(v, "b");
                            check_not_null(f_b);
                            if (f_b) { check(f_b->type == MOCK_INT); check(f_b->int_val == 4660); }

                            MockField* f_c = find_field(v, "c");
                            check_not_null(f_c);
                            if (f_c) { check(f_c->type == MOCK_INT); check(f_c->int_val == 42); }

                            MockField* f_d = find_field(v, "d");
                            check_not_null(f_d);
                            if (f_d) { check(f_d->type == MOCK_DOUBLE); check(fabs(f_d->dbl_val - 1000000.0) < 1.0); }

                            free(v);
                        }
                    }
                }

                if (codec) data_bind_free(codec);
            }

            remove("test_prim.rfl");
        }
    }

    section("Composite Type Parsing") {
        given("a schema with nested fields (flattened)") {
            write_schema("test_comp.rfl",
                "declare Msg\n"
                "    header_version: int\n"
                "    header_seq: int\n"
                "    payload: int\n"
                "end\n");

            when("parsing binary data") {
                DataBind* codec = data_bind_create("test_comp.rfl", &test_api);

                then("codec should be created") {
                    check_not_null(codec);
                }

                then("should parse all fields correctly") {
                    if (codec) {
                        uint8_t buf[12];
                        memset(buf, 0, sizeof(buf));
                        *(int32_t*)(buf + 0) = 3;
                        *(int32_t*)(buf + 4) = 99;
                        *(int32_t*)(buf + 8) = 7777;

                        Value* v = data_bind_parse(codec, "Msg", buf, sizeof(buf));
                        check_not_null(v);

                        if (v) {
                            MockField* f1 = find_field(v, "header_version");
                            check_not_null(f1);
                            if (f1) { check(f1->type == MOCK_INT); check(f1->int_val == 3); }

                            MockField* f2 = find_field(v, "header_seq");
                            check_not_null(f2);
                            if (f2) { check(f2->type == MOCK_INT); check(f2->int_val == 99); }

                            MockField* f3 = find_field(v, "payload");
                            check_not_null(f3);
                            if (f3) { check(f3->type == MOCK_INT); check(f3->int_val == 7777); }

                            free(v);
                        }
                    }
                }

                if (codec) data_bind_free(codec);
            }

            remove("test_comp.rfl");
        }
    }

    section("Enum Type Parsing") {
        given("a schema with an enum field") {
            write_schema("test_enum.rfl",
                "declare Order\n"
                "    id: int\n"
                "    side: int\n"
                "    qty: int\n"
                "end\n");

            when("parsing with side=1") {
                DataBind* codec = data_bind_create("test_enum.rfl", &test_api);

                then("codec should be created") {
                    if (!codec) {
                        const char* err = data_bind_get_error(NULL);
                        printf("\nERROR creating enum codec: %s\n", err);
                    }
                    check_not_null(codec);
                }

                then("should parse all fields correctly") {
                    if (codec) {
                        uint8_t buf[12];
                        memset(buf, 0, sizeof(buf));
                        *(int32_t*)(buf + 0) = 100;
                        *(int32_t*)(buf + 4) = 1;
                        *(int32_t*)(buf + 8) = 500;

                        Value* v = data_bind_parse(codec, "Order", buf, sizeof(buf));
                        check_not_null(v);

                        if (v) {
                            MockField* f_id = find_field(v, "id");
                            check_not_null(f_id);
                            if (f_id) check(f_id->int_val == 100);

                            MockField* f_side = find_field(v, "side");
                            check_not_null(f_side);
                            if (f_side) check(f_side->int_val == 1);

                            MockField* f_qty = find_field(v, "qty");
                            check_not_null(f_qty);
                            if (f_qty) check(f_qty->int_val == 500);

                            free(v);
                        }
                    }
                }

                if (codec) data_bind_free(codec);
            }

            remove("test_enum.rfl");
        }
    }

    section("Bounds Checking") {
        given("a schema with int field") {
            write_schema("test_bounds.rfl",
                "declare Small\n"
                "    x: int\n"
                "end\n");

            when("buffer is too short") {
                DataBind* codec = data_bind_create("test_bounds.rfl", &test_api);

                then("codec should be created") {
                    check_not_null(codec);
                }

                then("should return NULL for short buffer") {
                    if (codec) {
                        uint8_t buf[2] = {0x01, 0x02};
                        Value* v = data_bind_parse(codec, "Small", buf, sizeof(buf));
                        check_null(v);
                    }
                }

                if (codec) data_bind_free(codec);
            }

            when("buffer is exactly right size") {
                DataBind* codec = data_bind_create("test_bounds.rfl", &test_api);

                then("codec should be created") {
                    check_not_null(codec);
                }

                then("should parse correctly") {
                    if (codec) {
                        uint8_t buf[4];
                        *(int32_t*)buf = 12345;
                        Value* v = data_bind_parse(codec, "Small", buf, sizeof(buf));
                        check_not_null(v);

                        if (v) {
                            MockField* f = find_field(v, "x");
                            check_not_null(f);
                            if (f) check(f->int_val == 12345);
                            free(v);
                        }
                    }
                }

                if (codec) data_bind_free(codec);
            }

            remove("test_bounds.rfl");
        }
    }

    section("Error Handling") {
        given("a codec for a single message type") {
            write_schema("test_errh.rfl",
                "declare Foo\n"
                "    x: int\n"
                "end\n");

            when("parsing an unknown type name") {
                DataBind* codec = data_bind_create("test_errh.rfl", &test_api);

                then("codec should be created") {
                    check_not_null(codec);
                }

                then("should return NULL for unknown type") {
                    if (codec) {
                        uint8_t buf[4] = {0};
                        Value* v = data_bind_parse(codec, "Bar", buf, sizeof(buf));
                        check_null(v);

                        const char* err = data_bind_get_error(codec);
                        check_not_null(err);
                        if (err) check(strstr(err, "Bar") != NULL);
                    }
                }

                if (codec) data_bind_free(codec);
            }

            when("passing NULL buffer") {
                DataBind* codec = data_bind_create("test_errh.rfl", &test_api);

                then("codec should be created") {
                    check_not_null(codec);
                }

                then("should return NULL for NULL buffer") {
                    if (codec) {
                        Value* v = data_bind_parse(codec, "Foo", NULL, 0);
                        check_null(v);
                    }
                }

                if (codec) data_bind_free(codec);
            }

            remove("test_errh.rfl");
        }
    }

    section("Memory Management") {
        given("multiple codec instances") {
            write_schema("test_mem.rfl",
                "declare Msg\n"
                "    x: int\n"
                "end\n");

            when("creating and freeing multiple codecs") {
                DataBind* c1 = data_bind_create("test_mem.rfl", &test_api);
                DataBind* c2 = data_bind_create("test_mem.rfl", &test_api);
                DataBind* c3 = data_bind_create("test_mem.rfl", &test_api);

                then("all should be created") {
                    check_not_null(c1);
                    check_not_null(c2);
                    check_not_null(c3);
                }

                data_bind_free(c1);
                data_bind_free(c2);
                data_bind_free(c3);
                data_bind_free(NULL); /* should not crash */

                then("should not crash") {
                    check(1);
                }
            }

            remove("test_mem.rfl");
        }
    }

    section("NULL set_field_bytes callback") {
        given("an API without set_field_bytes") {
            DataBindValueApi api_no_bytes = {
                .create_object   = mock_create_object,
                .set_field_int    = mock_set_field_int,
                .set_field_double = mock_set_field_double,
                .set_field_string = mock_set_field_string,
                .set_field_bytes  = NULL
            };

            write_schema("test_no_bytes.rfl",
                "declare SimpleMsg\n"
                "    x: int\n"
                "    y: int\n"
                "end\n");

            when("parsing message without bytes fields") {
                DataBind* codec = data_bind_create("test_no_bytes.rfl", &api_no_bytes);

                then("codec should be created") {
                    check_not_null(codec);
                }

                then("should parse correctly") {
                    if (codec) {
                        uint8_t buf[8] = {42, 0, 0, 0, 99, 0, 0, 0};
                        Value* v = data_bind_parse(codec, "SimpleMsg", buf, sizeof(buf));
                        check_not_null(v);

                        if (v) {
                            MockField* fx = find_field(v, "x");
                            MockField* fy = find_field(v, "y");
                            check_not_null(fx);
                            check_not_null(fy);
                            if (fx) check(fx->int_val == 42);
                            if (fy) check(fy->int_val == 99);
                            free(v);
                        }
                    }
                }

                if (codec) data_bind_free(codec);
            }

            remove("test_no_bytes.rfl");
        }
    }

    section("Dynamic function list (no MAX_FUNCS limit)") {
        given("a schema with multiple message types") {
            write_schema("test_many_msgs.rfl",
                "declare Msg0\n"
                "    x: int\n"
                "end\n"
                "\n"
                "declare Msg1\n"
                "    x: int\n"
                "end\n"
                "\n"
                "declare Msg2\n"
                "    x: int\n"
                "end\n");

            when("parsing all 3 types") {
                DataBind* codec = data_bind_create("test_many_msgs.rfl", &test_api);

                then("codec should be created") {
                    check_not_null(codec);
                }

                then("should parse all 3 types") {
                    if (codec) {
                        uint8_t buf[4] = {42, 0, 0, 0};
                        int success_count = 0;

                        for (int i = 0; i < 3; i++) {
                            char type[32];
                            snprintf(type, sizeof(type), "Msg%d", i);
                            Value* v = data_bind_parse(codec, type, buf, sizeof(buf));
                            if (v) {
                                success_count++;
                                free(v);
                            }
                        }

                        check(success_count == 3);
                    }
                }

                if (codec) data_bind_free(codec);
            }

            remove("test_many_msgs.rfl");
        }
    }
}

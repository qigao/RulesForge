/**
 * @file test_csv_codec.cpp
 * @brief Test CSV codec functionality using data_bind API
 */

#include "data_bind.h"
#include "core/rfl_parser_state.hpp"
#include "core/constraint_types.hpp"
#include "tinytest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Mock Value with List support */
typedef enum MockValueKind {
  MOCK_VALUE_OBJECT = 1,
  MOCK_VALUE_LIST = 2
} MockValueKind;

typedef struct MockList {
  int kind;
  void** items;
  size_t count;
  size_t capacity;
} MockList;

typedef struct Value {
  int kind;
  int id;
  double price;
  char name[256];
  int status;
} Value;

static int g_live_csv_values = 0;
static int g_live_csv_lists = 0;
static int g_fail_object_creation = 0;

static void reset_csv_mock_state(void) {
  g_live_csv_values = 0;
  g_live_csv_lists = 0;
  g_fail_object_creation = 0;
}

static MockList* mock_create_list(void) {
  MockList* list = (MockList*)calloc(1, sizeof(MockList));
  list->kind = MOCK_VALUE_LIST;
  list->capacity = 10;
  list->items = (void**)calloc(list->capacity, sizeof(void*));
  g_live_csv_lists++;
  return list;
}

static void mock_free_value(Value* value);

static void mock_free_list(MockList* list) {
  if (list) {
    for (size_t i = 0; i < list->count; i++) {
        mock_free_value((Value*)list->items[i]);
    }
    free(list->items);
    free(list);
    g_live_csv_lists--;
  }
}

static Value *mock_create_object(void) {
  if (g_fail_object_creation) {
    return NULL;
  }
  Value *v = (Value *)calloc(1, sizeof(Value));
  v->kind = MOCK_VALUE_OBJECT;
  g_live_csv_values++;
  return v;
}

static void mock_free_value(Value* value) {
  if (!value) return;
  free(value);
  g_live_csv_values--;
}

static void mock_destroy_value(Value* value) {
  if (!value) return;
  if (value->kind == MOCK_VALUE_LIST) {
    mock_free_list((MockList*)value);
    return;
  }
  mock_free_value(value);
}

static void mock_set_field_int(Value *obj, const char *name, int32_t val) {
  if (!obj) return;
  if (strcmp(name, "id") == 0) {
    obj->id = val;
  } else if (strcmp(name, "status") == 0) {
    obj->status = val;
  }
}

static void mock_set_field_double(Value *obj, const char *name, double val) {
  if (obj && strcmp(name, "price") == 0) {
    obj->price = val;
  }
}

static void mock_set_field_string(Value *obj, const char *name, const char *val) {
  if (obj && val) {
    if (strcmp(name, "name") == 0) {
      strncpy(obj->name, val, sizeof(obj->name) - 1);
    }
  }
}

static void mock_add_list_item_object(Value* list_val, Value* obj) {
  MockList* ml = (MockList*)list_val;
  if (ml && ml->count < ml->capacity) {
    ml->items[ml->count++] = obj;
  }
}

static DataBindValueApi api = {
    .create_object = mock_create_object,
    .set_field_int = mock_set_field_int,
    .set_field_int64 = NULL,
    .set_field_double = mock_set_field_double,
    .set_field_string = mock_set_field_string,
    .set_field_bytes = NULL,

    .create_list = (Value*(*)(void))mock_create_list,
    .add_list_item_int = NULL,
    .add_list_item_int64 = NULL,
    .add_list_item_double = NULL,
    .add_list_item_string = NULL,
    .add_list_item_object = mock_add_list_item_object,
    .set_field_list = NULL,
    .set_field_object = NULL,
    
    .create_set = NULL,
    .add_set_item_int = NULL,
    .add_set_item_double = NULL,
    .add_set_item_string = NULL,
    .set_field_set = NULL,
    
    .create_map = NULL,
    .add_map_entry_string_string = NULL,
    .add_map_entry_string_int = NULL,
    .add_map_entry_string_double = NULL,
    .set_field_map = NULL,
    .destroy_value = mock_destroy_value
};

static ParsedDeclaration create_test_declaration() {
    ParsedDeclaration decl;
    decl.type_name = "Product";
    
    ParsedField f1; f1.name = "id"; f1.type = FT_Int;
    ParsedField f2; f2.name = "name"; f2.type = FT_String;
    ParsedField f3; f3.name = "price"; f3.type = FT_Double;
    ParsedField f4; f4.name = "status"; f4.type = FT_Boolean;
    
    decl.fields.push_back(f1);
    decl.fields.push_back(f2);
    decl.fields.push_back(f3);
    decl.fields.push_back(f4);
    
    return decl;
}

suite("CSV Codec") {
  section("Basic Parsing") {
    given("an in-memory declaration") {
      ParsedDeclaration decl = create_test_declaration();
      const void* decls[] = { &decl };

      when("parsing simple CSV data") {
        DataBind* codec = data_bind_create_from_declarations(decls, 1, DATA_BIND_FORMAT_CSV, &api);

        then("codec should be created") { 
            check_not_null(codec); 
        }

        then("should parse CSV correctly") {
            if (codec) {
                const char* csv_data = "id,name,price,status\n"
                                       "101,Apple,1.99,1\n"
                                       "102,Banana,0.99,0\n";
                                   
                Value* result = data_bind_parse_string(codec, "Product", csv_data);
                check_not_null(result);
                if (result) {
                    MockList* list = (MockList*)result;
                    check(list->count == 2);
                    
                    Value* v1 = (Value*)list->items[0];
                    check(v1->id == 101);
                    check(strcmp(v1->name, "Apple") == 0);
                    check(v1->price > 1.98 && v1->price < 2.00);
                    check(v1->status == 1);

                    Value* v2 = (Value*)list->items[1];
                    check(v2->id == 102);
                    check(strcmp(v2->name, "Banana") == 0);
                    check(v2->price > 0.98 && v2->price < 1.00);
                    check(v2->status == 0);

                    mock_free_list(list);
                }
            }
        }

        if (codec) {
            data_bind_free(codec);
        }
      }
    }
  }

  section("Failure Cleanup") {
    given("row object creation fails after the list is created") {
      ParsedDeclaration decl = create_test_declaration();
      const void* decls[] = { &decl };

      when("parsing CSV data") {
        reset_csv_mock_state();
        g_fail_object_creation = 1;
        DataBind* codec = data_bind_create_from_declarations(decls, 1, DATA_BIND_FORMAT_CSV, &api);

        then("parse should fail and destroy the temporary list") {
          if (codec) {
            const char* csv_data = "id,name,price,status\n101,Apple,1.99,1\n";
            Value* result = data_bind_parse_string(codec, "Product", csv_data);
            check_null(result);
            check_str_contains(data_bind_get_error(codec), "Failed to create row object");
            check(g_live_csv_values == 0);
            check(g_live_csv_lists == 0);
          }
        }

        g_fail_object_creation = 0;
        if (codec) {
          data_bind_free(codec);
        }
      }
    }
  }

  section("Invalid Scalar Rejection") {
    given("a row with invalid boolean text") {
      ParsedDeclaration decl = create_test_declaration();
      const void* decls[] = { &decl };

      when("parsing CSV data") {
        reset_csv_mock_state();
        DataBind* codec = data_bind_create_from_declarations(decls, 1, DATA_BIND_FORMAT_CSV, &api);

        then("parse should fail instead of defaulting the bad value") {
          if (codec) {
            const char* csv_data = "id,name,price,status\n101,Apple,1.99,maybe\n";
            Value* result = data_bind_parse_string(codec, "Product", csv_data);
            check_null(result);
            check_str_contains(data_bind_get_error(codec), "Invalid boolean");
            check(g_live_csv_values == 0);
            check(g_live_csv_lists == 0);
          }
        }

        if (codec) {
          data_bind_free(codec);
        }
      }
    }
  }
}

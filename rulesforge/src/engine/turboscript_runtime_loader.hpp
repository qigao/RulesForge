#ifndef RULESFORGE_TURBOSCRIPT_RUNTIME_LOADER_HPP
#define RULESFORGE_TURBOSCRIPT_RUNTIME_LOADER_HPP

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

namespace rulesforge::turboscript_runtime {

struct Context;
struct Compiled;

struct StringView {
    char* data;
    std::size_t len;
};

struct VectorValue {
    double* data;
    std::size_t size;
};

enum class ValueType : int {
    Number = 0,
    String = 1,
    Integer = 2,
    Vector = 3,
    Map = 4,
    Object = 5,
    Null = 6,
    List = 7,
    Function = 8,
    Coroutine = 9,
    Class = 10,
    Instance = 11,
    BoundMethod = 12
};

struct Value {
    ValueType type;
    union {
        double number;
        StringView string;
        std::int64_t integer;
        VectorValue vector;
        struct {
            void* htab;
        } map;
        struct {
            Value* items;
            std::size_t count;
            std::size_t capacity;
            int heap_owned;
        } list;
        struct {
            void* arg_params;
            std::size_t arg_count;
            void* body;
            void* closure_env;
            void* owner_class;
            int is_static_method;
            int access_level;
            int is_override;
            int is_final;
        } function;
        struct {
            void* coro;
        } coroutine;
        struct {
            void* klass;
        } class_val;
        struct {
            void* instance;
        } instance_val;
        struct {
            void* instance;
            void* method;
        } bound_method_val;
    } data;
};

using Function = Value (*)(std::size_t arg_count, Value* args, void* user_data);

struct Api {
    Context* (*init)(int flags);
    void (*free)(Context* ctx);
    Compiled* (*compile)(Context* ctx, char const* script);
    int (*exec)(Context* ctx, Compiled* compiled);
    void (*compiled_free)(Compiled* compiled);
    char const* (*get_error)(Context* ctx);
    void (*bind_num)(Context* ctx, char const* name, double value);
    void (*bind_str)(Context* ctx, char const* name, char const* value);
    void (*bind_func)(Context* ctx, char const* name, Function fn, void* user_data);
    double (*get_num)(Context* ctx, char const* name);
    char const* (*get_str)(Context* ctx, char const* name);
};

constexpr int kInitBare = 1;

Value value_num(double value);
Value value_int(std::int64_t value);
Value value_str(StringView value);

Api const* load(std::string* error_out = nullptr);
bool available(std::string* error_out = nullptr);
std::string last_error();
std::recursive_mutex& api_mutex();

} // namespace rulesforge::turboscript_runtime

#endif // RULESFORGE_TURBOSCRIPT_RUNTIME_LOADER_HPP

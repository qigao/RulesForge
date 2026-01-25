#ifndef NATIVE_FUNCTION_TYPES_HPP
#define NATIVE_FUNCTION_TYPES_HPP

// Shared native function types used by both KnowledgeBase and JSScriptingManager
// This header prevents duplicate definitions and type mismatches

// Native function callback type
using NativeFunctionCallback = int (*)(void* ctx, int argc, const char** argv, char** out_result);

// Native function structure
struct NativeFunction {
    NativeFunctionCallback callback;
    void* user_data;
};

#endif // NATIVE_FUNCTION_TYPES_HPP

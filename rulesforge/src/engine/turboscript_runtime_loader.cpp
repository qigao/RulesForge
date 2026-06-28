#include "turboscript_runtime_loader.hpp"

#include <array>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
  #include <windows.h>
#else
  #include <dlfcn.h>
#endif

namespace rulesforge::turboscript_runtime {
namespace {

struct LibraryHandle {
#if defined(_WIN32)
    HMODULE handle = nullptr;
#else
    void* handle = nullptr;
#endif

    LibraryHandle() = default;
    explicit LibraryHandle(char const* path) {
#if defined(_WIN32)
        handle = LoadLibraryA(path);
#else
        handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
    }

    ~LibraryHandle() {
        if (!handle) return;
#if defined(_WIN32)
        FreeLibrary(handle);
#else
        dlclose(handle);
#endif
    }

    LibraryHandle(LibraryHandle const&) = delete;
    LibraryHandle& operator=(LibraryHandle const&) = delete;

    LibraryHandle(LibraryHandle&& other) noexcept : handle(other.handle) {
        other.handle = nullptr;
    }

    LibraryHandle& operator=(LibraryHandle&& other) noexcept {
        if (this != &other) {
            reset();
            handle = other.handle;
            other.handle = nullptr;
        }
        return *this;
    }

    explicit operator bool() const { return handle != nullptr; }

    void reset() {
        if (!handle) return;
#if defined(_WIN32)
        FreeLibrary(handle);
#else
        dlclose(handle);
#endif
        handle = nullptr;
    }

    void* symbol(char const* name) const {
        if (!handle) return nullptr;
#if defined(_WIN32)
        return reinterpret_cast<void*>(GetProcAddress(handle, name));
#else
        return dlsym(handle, name);
#endif
    }
};

struct RuntimeState {
    LibraryHandle library;
    Api api{};
    std::string error;
};

template <typename Fn>
bool resolve_symbol(LibraryHandle const& library,
                    char const* name,
                    Fn& out,
                    std::string& error) {
    auto* symbol = library.symbol(name);
    if (!symbol) {
        error = "missing symbol ";
        error += name;
        return false;
    }
    std::memcpy(&out, &symbol, sizeof(out));
    return true;
}

std::vector<std::string> candidate_library_paths() {
    std::vector<std::string> result;

    auto add = [&](char const* value) {
        if (value && value[0] != '\0') {
            result.emplace_back(value);
        }
    };

    add(std::getenv("RULESFORGE_TURBOSCRIPT_RUNTIME"));
    add(std::getenv("TURBOSCRIPT_RUNTIME"));

    if (char const* root = std::getenv("TURBOSCRIPT_ROOT")) {
        std::string base = root;
        if (!base.empty()) {
#if defined(_WIN32)
            result.push_back(base + "\\bin\\turbo_script.dll");
            result.push_back(base + "\\turbo_script.dll");
#else
            result.push_back(base + "/lib/libturbo_script.so");
            result.push_back(base + "/bin/libturbo_script.so");
            result.push_back(base + "/libturbo_script.so");
#endif
        }
    }

#if defined(_WIN32)
    result.emplace_back("turbo_script.dll");
    result.emplace_back("TurboScript.dll");
#elif defined(__APPLE__)
    result.emplace_back("libturbo_script.dylib");
    result.emplace_back("libTurboScript.dylib");
    result.emplace_back("turbo_script.dylib");
#else
    result.emplace_back("libturbo_script.so");
    result.emplace_back("libTurboScript.so");
    result.emplace_back("turbo_script.so");
#endif

    return result;
}

RuntimeState make_state() {
    RuntimeState state;
    auto const candidates = candidate_library_paths();
    std::ostringstream tried;

    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (i != 0) tried << "; ";
        tried << candidates[i];

        LibraryHandle library(candidates[i].c_str());
        if (!library) {
            continue;
        }

        Api api{};
        std::string error;
        if (!resolve_symbol(library, "turbo_script_init", api.init, error)
            || !resolve_symbol(library, "turbo_script_free", api.free, error)
            || !resolve_symbol(library, "turbo_script_compile", api.compile, error)
            || !resolve_symbol(library, "turbo_script_exec", api.exec, error)
            || !resolve_symbol(library, "turbo_script_compiled_free", api.compiled_free, error)
            || !resolve_symbol(library, "turbo_script_get_error", api.get_error, error)
            || !resolve_symbol(library, "ts_bind_num", api.bind_num, error)
            || !resolve_symbol(library, "ts_bind_str", api.bind_str, error)
            || !resolve_symbol(library, "ts_bind_func", api.bind_func, error)
            || !resolve_symbol(library, "ts_get_num", api.get_num, error)
            || !resolve_symbol(library, "ts_get_str", api.get_str, error)) {
            state.error = "turboscript_runtime_unavailable: ";
            state.error += candidates[i];
            state.error += ": ";
            state.error += error;
            return state;
        }

        state.library = std::move(library);
        state.api = api;
        return state;
    }

    state.error = "turboscript_runtime_unavailable: unable to load TurboScript runtime";
    if (!candidates.empty()) {
        state.error += " (tried: ";
        state.error += tried.str();
        state.error += ")";
    }
    return state;
}

RuntimeState const& state() {
    static RuntimeState const runtime_state = make_state();
    return runtime_state;
}

} // namespace

Value value_num(double value) {
    Value result{};
    result.type = ValueType::Number;
    result.data.number = value;
    return result;
}

Value value_int(std::int64_t value) {
    Value result{};
    result.type = ValueType::Integer;
    result.data.integer = value;
    return result;
}

Value value_str(StringView value) {
    Value result{};
    result.type = ValueType::String;
    result.data.string = value;
    return result;
}

Api const* load(std::string* error_out) {
    RuntimeState const& current = state();
    if (!current.library) {
        if (error_out) *error_out = current.error;
        return nullptr;
    }
    if (error_out) error_out->clear();
    return &current.api;
}

bool available(std::string* error_out) {
    return load(error_out) != nullptr;
}

std::string last_error() {
    RuntimeState const& current = state();
    return current.error;
}

std::recursive_mutex& api_mutex() {
    static std::recursive_mutex mutex;
    return mutex;
}

} // namespace rulesforge::turboscript_runtime

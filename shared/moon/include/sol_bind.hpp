/**
 * This is a set of helper macros and functions to bind C++ functions and classes to Lua.
 *
 * These macros provide a convenient way to expose C++ functionality to Lua scripts.
 * They handle the underlying complexity of binding C++ functions and classes to Lua,
 * allowing developers to focus on writing the functionality itself.
 *
 * The macros and functions provided include:
 *   - SOL_BIND_FUNCTION: Bind a C++ function to Lua.
 *   - SOL_BIND_FUNCTIONS: Bind multiple C++ functions to Lua.
 *   - SOL_BIND_CLASS: Bind a C++ class to Lua, with default constructors.
 *   - SOL_BIND_CLASS_CTOR: Bind a C++ class to Lua, with custom constructors.
 *   - SOL_BIND_MEMBERS: Bind member functions of a C++ class to Lua.
 *
 * To use these macros, simply include this header file and call the desired macro
 * with the necessary arguments. For example:
 *   - SOL_BIND_FUNCTION(lua, myFunction) to bind a function named myFunction to Lua.
 *   - SOL_BIND_CLASS(lua, MyClass, memFunc1, memFunc2) to bind a class named MyClass with member
 * functions memFunc1 and memFunc2 to Lua.
 *
 * These macros have been designed to be flexible and easy to use, allowing developers
 * to quickly and easily expose C++ functionality to Lua scripts.
 */
#pragma once

#include <sol/sol.hpp>
#include <utility>

// Helper macro to expand macro on each argument
#define EXPAND(x) x

// Helper macro to count the number of arguments
#define COUNT_ARGS(...) EXPAND(COUNT_ARGS_HELPER(__VA_ARGS__, 9, 8, 7, 6, 5, 4, 3, 2, 1))
#define COUNT_ARGS_HELPER(_1, _2, _3, _4, _5, _6, _7, _8, _9, N, ...) N

// Helper macro to concatenate two tokens
#define CONCAT(a, b) CONCAT_INNER(a, b)
#define CONCAT_INNER(a, b) a##b

/**
 * Binds a C++ function to Lua.
 *
 * This function takes a Lua state and a pair containing the function name and the function itself.
 * It then binds the function to Lua, making it available to Lua scripts.
 *
 * @param lua The Lua state to bind the function to.
 * @param function A pair containing the function name and the function itself.
 */
template <typename Function>
void bind_function(sol::state& lua, std::pair<char const*, Function> function) {
    lua[function.first] = function.second;
}

/**
 * Binds a C++ function to a Lua table.
 *
 * This function takes a Lua table and a pair containing the function name and the function itself.
 * It then binds the function to the Lua table, making it available to Lua scripts.
 *
 * @param lua The Lua table to bind the function to.
 * @param function A pair containing the function name and the function itself.
 */
template <typename Function>
void bind_function(sol::table& lua, std::pair<char const*, Function> function) {
    lua[function.first] = function.second;
}

// Macro definition
#define FUNCTION_NAME(name) std::make_pair(#name, &(name))

// Define SOL_BIND_FUNCTION_N macros for different numbers of arguments
#define SOL_BIND_FUNCTION(lua, name) bind_function(lua, FUNCTION_NAME(name))
#define SOL_BIND_FUNCTION_1(lua, name) SOL_BIND_FUNCTION(lua, name)
#define SOL_BIND_FUNCTION_2(lua, name1, name2) SOL_BIND_FUNCTION_1(lua, name1), SOL_BIND_FUNCTION(lua, name2)
#define SOL_BIND_FUNCTION_3(lua, name1, name2, name3)                                                                  \
    SOL_BIND_FUNCTION_2(lua, name1, name2), SOL_BIND_FUNCTION(lua, name3)
#define SOL_BIND_FUNCTION_4(lua, name1, name2, name3, name4)                                                           \
    SOL_BIND_FUNCTION_3(lua, name1, name2, name3), SOL_BIND_FUNCTION(lua, name4)
#define SOL_BIND_FUNCTION_5(lua, name1, name2, name3, name4, name5)                                                    \
    SOL_BIND_FUNCTION_4(lua, name1, name2, name3, name4), SOL_BIND_FUNCTION(lua, name5)
#define SOL_BIND_FUNCTION_6(lua, name1, name2, name3, name4, name5, name6)                                             \
    SOL_BIND_FUNCTION_5(lua, name1, name2, name3, name4, name5), SOL_BIND_FUNCTION(lua, name6)
#define SOL_BIND_FUNCTION_7(lua, name1, name2, name3, name4, name5, name6, name7)                                      \
    SOL_BIND_FUNCTION_6(lua, name1, name2, name3, name4, name5, name6), SOL_BIND_FUNCTION(lua, name7)
#define SOL_BIND_FUNCTION_8(lua, name1, name2, name3, name4, name5, name6, name7, name8)                               \
    SOL_BIND_FUNCTION_7(lua, name1, name2, name3, name4, name5, name6, name7), SOL_BIND_FUNCTION(lua, name8)
#define SOL_BIND_FUNCTION_9(lua, name1, name2, name3, name4, name5, name6, name7, name8, name9)                        \
    SOL_BIND_FUNCTION_8(lua, name1, name2, name3, name4, name5, name6, name7, name8), SOL_BIND_FUNCTION(lua, name9)

/**
 * @brief Binds multiple C++ functions to Lua.
 *
 * This macro takes a Lua state (or table) and a list of C++ function names.
 * It binds each function to Lua, making them available to Lua scripts.
 *
 * @param lua The Lua state or table to bind the functions to.
 * @param ... Function names to bind. Must provide at least one function name.
 *            Supports up to 9 functions.
 * @example SOL_BIND_FUNCTIONS(lua, myFunc1, myFunc2);
 */
#define SOL_BIND_FUNCTIONS(lua, ...) EXPAND(CONCAT(SOL_BIND_FUNCTION_, COUNT_ARGS(__VA_ARGS__)))(lua, __VA_ARGS__)

// Class binding function
/**
 * Binds a C++ class member function to a Lua table.
 *
 * This function takes a Lua usertype and a pair containing the function name and the function
 * itself. It then binds the function to the Lua usertype, making it available to Lua scripts.
 *
 * @param usertype The Lua usertype to bind the function to.
 * @param method A pair containing the function name and the function itself.
 */
template <typename Usertype, typename Method>
void bind_method(Usertype& usertype, std::pair<char const*, Method> method) {
    usertype[method.first] = method.second;
}

// Helper macro to define SOL_BIND_MEMBER_N macros
#define SOL_BIND_MEMBER_N(n) SOL_BIND_MEMBER_##n

// Define SOL_BIND_MEMBER_N macros for different numbers of arguments
#define MEMBER_NAME(Class, name) std::make_pair(#name, &Class::name)
#define SOL_BIND_MEMBER_1(Class, a) MEMBER_NAME(Class, a)
#define SOL_BIND_MEMBER_2(Class, a, b) MEMBER_NAME(Class, a), MEMBER_NAME(Class, b)
#define SOL_BIND_MEMBER_3(Class, a, b, c) SOL_BIND_MEMBER_2(Class, a, b), SOL_BIND_MEMBER_1(Class, c)
#define SOL_BIND_MEMBER_4(Class, a, b, c, d) SOL_BIND_MEMBER_3(Class, a, b, c), SOL_BIND_MEMBER_1(Class, d)
#define SOL_BIND_MEMBER_5(Class, a, b, c, d, e) SOL_BIND_MEMBER_4(Class, a, b, c, d), SOL_BIND_MEMBER_1(Class, e)
#define SOL_BIND_MEMBER_6(Class, a, b, c, d, e, f) SOL_BIND_MEMBER_5(Class, a, b, c, d, e), SOL_BIND_MEMBER_1(Class, f)
#define SOL_BIND_MEMBER_7(Class, a, b, c, d, e, f, g)                                                                  \
    SOL_BIND_MEMBER_6(Class, a, b, c, d, e, f), SOL_BIND_MEMBER_1(Class, g)
#define SOL_BIND_MEMBER_8(Class, a, b, c, d, e, f, g, h)                                                               \
    SOL_BIND_MEMBER_7(Class, a, b, c, d, e, f, g), SOL_BIND_MEMBER_1(Class, h)
#define SOL_BIND_MEMBER_9(Class, a, b, c, d, e, f, g, h, i)                                                            \
    SOL_BIND_MEMBER_8(Class, a, b, c, d, e, f, g, h), SOL_BIND_MEMBER_1(Class, i)

/**
 * @brief Generates a list of pairs for binding C++ class member functions.
 *
 * This macro is typically used as an argument to SOL_BIND_CLASS or SOL_BIND_CLASS_CTOR.
 * It takes a class name and a list of member function names.
 *
 * @param Class The C++ class name.
 * @param ... Member function names to bind. Must provide at least one member name.
 *            Supports up to 9 member functions.
 * @example SOL_BIND_MEMBERS(MyClass, memberFunc1, memberFunc2)
 */
#define SOL_BIND_MEMBERS(Class, ...) EXPAND(CONCAT(SOL_BIND_MEMBER_, COUNT_ARGS(__VA_ARGS__)))(Class, __VA_ARGS__)

/**
 * Binds a C++ class to a Lua state.
 *
 * This function takes a Lua state, a class name, and a series of member functions.
 * It creates a Lua usertype and binds each member function to it, making them available to Lua
 * scripts.
 *
 * @param lua The Lua state to bind the class to.
 * @param name The name of the class.
 * @param methods The member functions of the class.
 */
template <typename T, typename... Methods>
void bind_class_default(sol::state& lua, std::string const& name, Methods... methods) {
    auto usertype = lua.new_usertype<T>(name, sol::constructors<void()>());
    (bind_method(usertype, methods), ...);
}

/**
 * Binds a C++ class to a Lua state.
 *
 * This macro takes a Lua state, a class name, and a series of member functions.
 * It creates a Lua usertype and binds each member function to it, making them available to Lua
 * scripts.
 *
 * @param luaState The Lua state to bind the class to.
 * @param className The name of the class.
 * @param...methods The member functions of the class.
 */
#define SOL_BIND_CLASS(luaState, className, ...)                                                                       \
    bind_class_default<className>(luaState, #className, SOL_BIND_MEMBERS(className, __VA_ARGS__))

#define CTORS(...) sol::constructors<__VA_ARGS__>()

/**
 * Binds a C++ class to a Lua state, with custom constructors.
 *
 * This function takes a Lua state, a class name, a constructors type, and a series of member
 * functions. It creates a Lua usertype and binds each member function to it, making them available
 * to Lua scripts. The constructors type allows for custom constructors to be specified.
 *
 * @param lua The Lua state to bind the class to.
 * @param name The name of the class.
 * @param ctors The constructors type, specifying custom constructors.
 * @param methods The member functions of the class.
 */
template <typename T, typename Ctors, typename... Methods>
void bind_class_ctors(sol::state& lua, std::string const& name, Ctors ctors, Methods... methods) {
    auto usertype = lua.new_usertype<T>(name, ctors);
    (bind_method(usertype, methods), ...);
}

/**
 * Binds a C++ class to a Lua state, with custom constructors.
 *
 * This macro is a wrapper around the bind_class_ctors function, providing a more convenient
 * interface for binding C++ classes to Lua.
 *
 * @param LuaState The Lua state to bind the class to.
 * @param ClassName The name of the class.
 * @param Ctors The constructors type, specifying custom constructors.
 * @param... The member functions of the class.
 */
#define SOL_BIND_CLASS_CTOR(LuaState, ClassName, Ctors, ...)                                                           \
    bind_class_ctors<ClassName>(LuaState, #ClassName, Ctors, SOL_BIND_MEMBERS(ClassName, __VA_ARGS__))

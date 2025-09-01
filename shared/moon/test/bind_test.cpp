#include "sol_bind.hpp"

#include <catch2/catch_all.hpp>
#include <sol/sol.hpp>

struct MyStruct {
    float x = 0.0f;
    float y = 0.0f;

    MyStruct() = default;

    MyStruct(float x, float y) : x(x), y(y) {}

    void Print() const { std::cout << "x: " << x << ", y: " << y << std::endl; }

    float GetX() const { return x; }

    float GetY() const { return y; }

    void SetX(float value) { x = value; }

    void SetY(float value) { y = value; }
};

int test_function(int x) { return x * 2; }

void Print() { std::cout << "Print called" << std::endl; }

void GetX() { std::cout << "GetX called" << std::endl; }

void SetX() { std::cout << "SetX called" << std::endl; }

TEST_CASE("Binding functions with macro works correctly", "[binding]") {

    SECTION("Binding multiple functions together", "[binding]") {
        sol::state lua;
        lua.open_libraries(sol::lib::base);
        SOL_BIND_FUNCTIONS(lua, Print, GetX, SetX);
        REQUIRE_NOTHROW(lua.script(R"(
        Print()
        GetX()
        SetX()
        )"));
    }
}

TEST_CASE("Binding with SOL_BIND_MEMBERS macro works correctly", "[binding]") {

    SECTION("Binding a class with BIND_CLASS macro") {
        sol::state lua;
        lua.open_libraries(sol::lib::base);
        SOL_BIND_CLASS(lua, MyStruct, Print, GetX, SetX, x);

        REQUIRE_NOTHROW(lua.script(R"(
        local obj = MyStruct.new()
        obj:SetX(5)
        assert(obj:GetX() == 5)
        assert(obj.x == 5)
        obj:Print()
    )"));
    }

    SECTION("Binding a class with BIND_CLASS_CTOR macro") {
        sol::state lua;
        lua.open_libraries(sol::lib::base);
        SOL_BIND_CLASS_CTOR(lua, MyStruct, CTORS(void(), void(float, float)), Print, GetX, SetX, SetY, x, y);

        REQUIRE_NOTHROW(lua.script(R"(
        local defaultObj = MyStruct.new()
        assert(defaultObj.x == 0)

        local obj2 = MyStruct.new(10, 20)
        assert(obj2.x == 10)
        assert(obj2.y == 20)

        obj2:SetY(30)
        assert(obj2.y == 30)
    )"));
    }
}

-- Person Rules Example for Drills C API using Lua FFI
-- Prerequisites: LuaJIT with FFI support

local ffi = require("ffi")

-- Load the C API
local capi = ffi.load("drills_capi.dll") -- Adjust path for your platform

-- Declare the C API functions
ffi.cdef([[
    int drills_init(void);
    int drills_cleanup(void);
    const char* drills_get_last_error_message(void);

    // Knowledge Base functions
    typedef void* drills_knowledge_base_t;
    typedef void* drills_stateful_session_t;
    typedef void* drills_query_result_t;
    typedef void* drills_fact_t;

    int drills_kb_create(drills_knowledge_base_t* out_kb);
    int drills_kb_load_drl(drills_knowledge_base_t kb, const char* drl_source);
    int drills_kb_destroy(drills_knowledge_base_t kb);

    // Session functions
    int drills_session_create(drills_knowledge_base_t kb, drills_stateful_session_t* out_session);
    int drills_session_add_fact_json(drills_stateful_session_t session, const char* fact_type, const char* fact_json);
    int drills_session_fire_all_rules(drills_stateful_session_t session);
    int drills_session_query(drills_stateful_session_t session, const char* query_name, drills_query_result_t* out_query_result);
    int drills_session_destroy(drills_stateful_session_t session);

    // Query result functions
    int drills_query_result_get_size(drills_query_result_t query_result);
    int drills_query_result_get_fact_at_index(drills_query_result_t query_result, int row_index, const char* binding_name, drills_fact_t* out_fact);
    int drills_query_result_destroy(drills_query_result_t query_result);

    // Fact functions
    int drills_fact_get_field_as_string(drills_fact_t fact, const char* field_name, char* buffer, size_t buffer_size, size_t* out_actual_length);
    int drills_fact_get_field_as_int(drills_fact_t fact, const char* field_name, long long* out_value);
]])

-- Utility functions
local function check_result(result, operation)
    if result ~= 0 then
        local error_msg = ffi.string(capi.drills_get_last_error_message())
        print("ERROR in " .. operation .. ": " .. error_msg)
        return false, error_msg
    end
    return true
end

local function json_encode_table(tbl)
    local result = "{"
    local first = true
    for k, v in pairs(tbl) do
        if not first then result = result .. "," end
        if type(k) == "string" then
            result = result .. '"' .. k:gsub('"', '\\"') .. '":'
        end
        if type(v) == "string" then
            result = result .. '"' .. v:gsub('"', '\\"') .. '"'
        elseif type(v) == "number" then
            result = result .. tostring(v)
        elseif v == true then
            result = result .. "true"
        elseif v == false then
            result = result .. "false"
        end
        first = false
    end
    result = result .. "}"
    return result
end

-- Main example
print("🚀 Starting Drills C API Lua Example - Person Rules\n")

-- Initialize the engine
local ok, err = check_result(capi.drills_init(), "drills_init")
if not ok then
    print("Failed to initialize: " .. err)
    os.exit(1)
end
print("✅ Drills rule engine initialized successfully")

-- Create knowledge base
local kb_ptr = ffi.new("drills_knowledge_base_t[1]")
ok, err = check_result(capi.drills_kb_create(kb_ptr), "drills_kb_create")
if not ok then
    print("Failed to create knowledge base: " .. err)
    capi.drills_cleanup()
    os.exit(1)
end
local kb = kb_ptr[0]
print("✅ Knowledge base created successfully")

-- Define DRL rules
local drl_rules = [[
declare Person
    name: String
    age: int
end

rule "PersonRule"
when
    $p : Person(age > 18)
then
    // Rule matches persons over 18
    // Note: Lua uses print instead of console.log
end

query "AdultPersons"
    $p : Person(age > 18)
end
]]

print("📜 Loading DRL rules...")
local drl_cstr = ffi.new("char[" .. (#drl_rules + 1) .. "]", drl_rules)
ok, err = check_result(capi.drills_kb_load_drl(kb, drl_cstr), "drills_kb_load_drl")
if not ok then
    print("Failed to load DRL rules: " .. err)
    capi.drills_kb_destroy(kb)
    capi.drills_cleanup()
    os.exit(1)
end
print("✅ DRL rules loaded successfully")

-- Create session
local session_ptr = ffi.new("drills_stateful_session_t[1]")
ok, err = check_result(capi.drills_session_create(kb, session_ptr), "drills_session_create")
if not ok then
    print("Failed to create session: " .. err)
    capi.drills_kb_destroy(kb)
    capi.drills_cleanup()
    os.exit(1)
end
local session = session_ptr[0]
print("✅ Session created successfully")

-- Insert test data
print("\n📊 Inserting test data...")

local test_data = {
    {name = "Bob", age = 25},
    {name = "Charlie", age = 17},
    {name = "Diana", age = 30},
    {name = "Eve", age = 16}
}

for _, person in ipairs(test_data) do
    local fact_type = "Person"
    local json_str = json_encode_table(person)
    local fact_type_cstr = ffi.new("char[" .. (#fact_type + 1) .. "]", fact_type)
    local json_cstr = ffi.new("char[" .. (#json_str + 1) .. "]", json_str)

    print("📥 Inserting fact: " .. fact_type .. " - " .. json_str)
    ok, err = check_result(capi.drills_session_add_fact_json(session, fact_type_cstr, json_cstr), "drills_session_add_fact_json")
    if not ok then
        print("Failed to insert fact: " .. err)
    else
        print("✅ Fact inserted successfully")
    end
end

-- Fire rules
print("\n🔥 Firing all rules...")
ok, err = check_result(capi.drills_session_fire_all_rules(session), "drills_session_fire_all_rules")
if not ok then
    print("Failed to fire rules: " .. err)
end
print("✅ Rules fired successfully")

-- Execute query
print("\n==================================================")
local query_name = "AdultPersons"
local query_name_cstr = ffi.new("char[" .. (#query_name + 1) .. "]", query_name)
local query_result_ptr = ffi.new("drills_query_result_t[1]")

print("🔍 Executing query: " .. query_name)
ok, err = check_result(capi.drills_session_query(session, query_name_cstr, query_result_ptr), "drills_session_query")
if not ok then
    print("Failed to execute query: " .. err)
    capi.drills_session_destroy(session)
    capi.drills_kb_destroy(kb)
    capi.drills_cleanup()
    os.exit(1)
end
local query_result = query_result_ptr[0]
print("✅ Query executed successfully")

-- Process results
local result_count = capi.drills_query_result_get_size(query_result)
print("📊 Query returned " .. result_count .. " results:")

for i = 0, result_count - 1 do
    print("\n--- Result " .. (i + 1) .. " ---")

    local fact_ptr = ffi.new("drills_fact_t[1]")
    local binding_name = "p"
    local binding_name_cstr = ffi.new("char[" .. (#binding_name + 1) .. "]", binding_name)

    ok, err = check_result(capi.drills_query_result_get_fact_at_index(
        query_result, i, binding_name_cstr, fact_ptr), "drills_query_result_get_fact_at_index")

    if not ok then
        print("   ⚠️  Failed to get result at index " .. i)
    else
        local fact = fact_ptr[0]

        -- Extract name field
        local name_buffer = ffi.new("char[256]")
        local name_size_ptr = ffi.new("size_t[1]")
        local name_ok = (capi.drills_fact_get_field_as_string(
            fact, "name", name_buffer, 256, name_size_ptr) == 0)

        -- Extract age field
        local age_ptr = ffi.new("long long[1]")
        local age_ok = (capi.drills_fact_get_field_as_int(fact, "age", age_ptr) == 0)

        local name = name_ok and ffi.string(name_buffer) or "Unknown"
        local age = age_ok and tonumber(age_ptr[0]) or "Unknown"

        print("   👤 " .. name .. ", Age: " .. tostring(age))
    end
end

-- Summary
print("\n📈 Summary:")
print("   Total adults found: " .. result_count)
print("   Expected: Adults should be Bob (25) and Diana (30)")

if result_count == 2 then
    print("   ✅ Result validation: SUCCESS - Correct number of adults found")
else
    print("   ❌ Result validation: FAILED - Expected 2 adults, got " .. result_count)
end

-- Cleanup
print("\n🧹 Cleaning up...")
if query_result ~= nil then
    capi.drills_query_result_destroy(query_result)
end
if session ~= nil then
    capi.drills_session_destroy(session)
end
if kb ~= nil then
    capi.drills_kb_destroy(kb)
end
capi.drills_cleanup()
print("✅ Cleanup completed")

print("\n🎉 Person Rules Example Completed!")

#include "pubcxx/imdb/imdb.hpp"

#include <iostream>
#include <string>
#include <vector>

int main() {
    // Get the manager for string keys and int values
    auto& manager_int_string = simple_db::DatabaseManager<std::string, int>::GetInstance();

    // Get or create databases by name
    auto& userDb = manager_int_string.CreateDb("user_data_db");
    auto& appDb = manager_int_string.CreateDb("application_settings_db");
    auto& userDb_again = manager_int_string.CreateDb("user_data_db");   // Returns the same instance as userDb

    // Verify they are the same instance
    if (&userDb == &userDb_again) { std::cout << "user_data_db access returned the same instance." << std::endl; }

    // Verify they are different instances
    if (&userDb != &appDb) {
        std::cout << "user_data_db and application_settings_db are different instances." << std::endl;
    }

    // Use the specific database instances
    userDb.Put("alice", 30);
    appDb.Put("version", 1);

    std::cout << "User DB size: " << userDb.Size() << std::endl;   // Output: 1
    std::cout << "App DB size: " << appDb.Size() << std::endl;     // Output: 1

    // Access from another part of the code, get the manager and the named DB
    auto& userDb_from_elsewhere = simple_db::DatabaseManager<std::string, int>::GetInstance().CreateDb("user_data_db");
    auto age = userDb_from_elsewhere.Get("alice");
    if (age) {
        std::cout << "Alice's age from elsewhere: " << *age << std::endl;   // Output: 30
    }

    // Get the manager for a different Key/Value type
    auto& manager_int_vector = simple_db::DatabaseManager<int, std::vector<std::string>>::GetInstance();
    auto& listDb = manager_int_vector.CreateDb("log_entries_db");
    listDb.Append(101, "Error: Something failed.");

    // Remove a database instance
    bool removed = manager_int_string.RemoveDatabase("application_settings_db");
    if (removed) { std::cout << "Removed application_settings_db." << std::endl; }
    std::cout << "Manager now has " << manager_int_string.CountDatabases() << " DBs."
              << std::endl;   // Output: Manager now has 1 DBs.

    // Trying to get the removed DB will create a new one
    auto& appDb_new = manager_int_string.CreateDb("application_settings_db");   // Creates a NEW instance
    std::cout << "New appDb size: " << appDb_new.Size() << std::endl;           // Output: 0

    return 0;
}

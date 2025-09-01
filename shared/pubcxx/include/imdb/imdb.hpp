#pragma once

#include <optional>
#include <shared_mutex>   // For std::shared_mutex and std::shared_lock / std::unique_lock
#include <unordered_map>
#include <utility>   // For std::forward

namespace simple_db {

    /**
     * @brief A simple header-only in-memory key-value database based on unordered_map.
     *
     * This version is thread-safe using a shared_mutex.
     * It includes methods for appending a batch of elements to the Value,
     * assuming Value is a container (like std::vector<T>).
     * Copy/move operations are deleted. Iterators are NOT thread-safe if concurrent modifications occur.
     *
     * @tparam Key The type of the keys. Must be hashable and comparable (operator==).
     * @tparam Value The type of the values. EXPECTED to be a container type like std::vector<T>,
     *               std::list<T>, std::deque<T>, std::string, etc., with appropriate
     *               'insert' or 'push_back' methods.
     */
    template <typename Key, typename Value>
    class InMemoryDb {
    public:
        // --- Constructors and Assignment Operators ---
        InMemoryDb() = default;
        InMemoryDb(InMemoryDb const&) = delete;
        InMemoryDb& operator=(InMemoryDb const&) = delete;
        InMemoryDb(InMemoryDb&&) = delete;
        InMemoryDb& operator=(InMemoryDb&&) = delete;
        ~InMemoryDb() = default;

        // --- CRUD and Other Operations ---

        /**
         * @brief Inserts or updates a key-value pair.
         * Thread-safe: Acquires exclusive lock.
         */
        template <typename V>
        void Put(Key const& key, V&& value) {
            std::unique_lock<std::shared_mutex> lock(mutex_);
            data_[key] = std::forward<V>(value);
        }

        /**
         * @brief Retrieves the value associated with a key.
         * Thread-safe: Acquires shared lock. Returns a copy of the value.
         *
         * @return An optional containing the Value (container) or std::nullopt.
         */
        std::optional<Value> Get(Key const& key) const {
            std::shared_lock<std::shared_mutex> lock(mutex_);
            auto it = data_.find(key);
            if (it != data_.end()) {
                return it->second;   // Return a copy
            }
            return std::nullopt;
        }

        /**
         * @brief Modifies the value associated with a key using a provided function.
         * Thread-safe: Acquires exclusive lock.
         *
         * @tparam Modifier A callable type (Value& -> void).
         * @param key The key.
         * @param modifier The function to apply to the value (container).
         * @return true if found and modified, false otherwise.
         */
        template <typename Modifier>
        bool ModifyValue(Key const& key, Modifier modifier) {
            std::unique_lock<std::shared_mutex> lock(mutex_);
            auto it = data_.find(key);
            if (it != data_.end()) {
                modifier(it->second);   // Apply the modification function to the container value
                return true;
            }
            return false;
        }

        /**
         * @brief Appends a single element to the container value associated with a key.
         *
         * Requires Value to be a container with a push_back method accepting Element.
         * Thread-safe: Calls ModifyValue.
         *
         * @tparam Element The type of the element to append. Must be compatible with Value's element type.
         * @param key The key of the container value.
         * @param element The single element to append.
         * @return true if found and appended, false otherwise.
         */
        template <typename Element>
        bool Append(Key const& key, Element&& element) {
            // Calls ModifyValue, which acquires the exclusive lock
            return ModifyValue(key, [&](Value& val) {
                // Requires Value (the container) to have push_back(Element)
                val.push_back(std::forward<Element>(element));
            });
        }

        /**
         * @brief Appends a batch of elements from an initializer list to the container value associated with a key.
         *
         * Requires Value to be a container with an insert method that accepts an
         * initializer list iterator range (like std::vector, std::list, std::deque).
         * The element type of the initializer list must be compatible with Value's element type.
         * Thread-safe: Calls ModifyValue.
         *
         * @tparam ElementType The type of elements in the initializer list. Must be compatible with Value's element
         * type.
         * @param key The key of the container value.
         * @param elements The initializer list containing elements to append.
         * @return true if found and elements were appended, false otherwise.
         */
        template <typename ElementType>
        bool AppendBatch(Key const& key, std::initializer_list<ElementType> elements) {
            // Calls ModifyValue, which acquires the exclusive lock
            return ModifyValue(key, [&](Value& val) {
                // Requires Value (the container) to have insert(iterator, InputIt, InputIt)
                // and ElementType to be compatible with Value's element type.
                val.insert(val.end(), elements.begin(), elements.end());
                // Note: std::vector::insert also has an overload taking initializer_list directly:
                // val.insert(val.end(), elements); // This is slightly more direct for vector
                // Using begin/end is more generic across different container types supporting ranges.
            });
        }

        /**
         * @brief Appends a batch of elements from an iterator range to the container value associated with a key.
         *
         * Requires Value to be a container with an insert method that accepts an
         * iterator range (like std::vector, std::list, std::deque).
         * The elements pointed to by the iterators must be compatible with Value's element type.
         * Thread-safe: Calls ModifyValue.
         *
         * @tparam InputIt Input iterator type.
         * @param key The key of the container value.
         * @param first Iterator to the beginning of the range.
         * @param last Iterator to the end of the range.
         * @return true if found and elements were appended, false otherwise.
         */
        template <typename InputIt>
        bool AppendBatch(Key const& key, InputIt first, InputIt last) {
            // Calls ModifyValue, which acquires the exclusive lock
            return ModifyValue(key, [&](Value& val) {
                // Requires Value (the container) to have insert(iterator, InputIt, InputIt)
                // and elements pointed to by InputIt to be compatible with Value's element type.
                val.insert(val.end(), first, last);
            });
        }

        /**
         * @brief Deletes a key-value pair by key.
         * Thread-safe: Acquires exclusive lock.
         */
        bool Delete(Key const& key) {
            std::unique_lock<std::shared_mutex> lock(mutex_);
            return data_.erase(key) > 0;
        }

        /**
         * @brief Checks if a key exists in the database.
         * Thread-safe: Acquires shared lock.
         */
        bool Exists(Key const& key) const {
            std::shared_lock<std::shared_mutex> lock(mutex_);
            return data_.count(key) > 0;
        }

        /**
         * @brief Removes all key-value pairs from the database.
         * Thread-safe: Acquires exclusive lock.
         */
        void Clear() {
            std::unique_lock<std::shared_mutex> lock(mutex_);
            data_.clear();
        }

        /**
         * @brief Gets the number of key-value pairs in the database.
         * Thread-safe: Acquires shared lock.
         */
        size_t Size() const {
            std::shared_lock<std::shared_mutex> lock(mutex_);
            return data_.size();
        }

        // --- Iterator Access ---
        // WARNING: Iterators returned are NOT thread-safe if concurrent modifications occur.
        // Refer to previous explanation regarding iterator safety.

        auto begin() { return data_.begin(); }

        auto end() { return data_.end(); }

        auto cbegin() const { return data_.cbegin(); }

        auto cend() const { return data_.cend(); }

        auto begin() const { return data_.cbegin(); }

        auto end() const { return data_.cend(); }

    private:
        std::unordered_map<Key, Value> data_;   // Value is expected to be a container
        mutable std::shared_mutex mutex_;
    };

    // --- Database Manager (Singleton for Each Key/Value Pair Combination) ---
    // This manager class will hold multiple InMemoryDb instances for a specific
    // Key/Value template pair, identified by a string name.

    /**
     * @brief Manages multiple named InMemoryDb instances for a specific Key/Value type.
     *
     * This class acts as a singleton factory/registry for databases.
     * Access the manager instance using DatabaseManager<Key, Value>::GetInstance().
     * Get specific named databases via CreateDb().
     *
     * @tparam Key The type of the keys.
     * @tparam Value The type of the values.
     */
    template <typename Key, typename Value>
    class DatabaseManager {
    private:
        // Private constructor for the manager singleton
        DatabaseManager() = default;

        // The registry holds unique pointers to InMemoryDb instances, identified by name.
        // Using unique_ptr ensures the databases are properly destroyed when the manager is.
        std::unordered_map<std::string, std::unique_ptr<InMemoryDb<Key, Value>>> databases_;

        // Mutex to protect the internal 'databases_' map itself (adding/getting instances)
        mutable std::mutex manager_mutex_;   // Use std::mutex, shared_mutex not needed for map access

    public:
        // Delete copy/move for the manager singleton
        DatabaseManager(DatabaseManager const&) = delete;
        DatabaseManager& operator=(DatabaseManager const&) = delete;
        DatabaseManager(DatabaseManager&&) = delete;
        DatabaseManager& operator=(DatabaseManager&&) = delete;

        // Default destructor for the manager
        ~DatabaseManager() = default;

        // Static method to get the singleton instance of the manager
        static DatabaseManager<Key, Value>& GetInstance() {
            // Thread-safe static local variable (Construct on First Use)
            static DatabaseManager<Key, Value> instance;
            return instance;
        }

        /**
         * @brief Gets or creates a named database instance.
         *
         * If a database with the given name exists, returns a reference to it.
         * If not, creates a new InMemoryDb instance, registers it under the name,
         * and returns a reference to the new instance.
         * Thread-safe: Protects the internal map.
         *
         * @param name The unique name for the database.
         * @return A reference to the requested or created InMemoryDb instance.
         */
        InMemoryDb<Key, Value>& CreateDb(std::string const& name) {
            // Lock the manager's internal map before accessing it
            std::lock_guard<std::mutex> lock(manager_mutex_);

            // Find or create the database instance
            auto it = databases_.find(name);
            if (it == databases_.end()) {
                // Database doesn't exist, create it
                auto new_db = std::make_unique<InMemoryDb<Key, Value>>();
                auto insert_result = databases_.emplace(name, std::move(new_db));
                it = insert_result.first;   // Iterator to the newly inserted element
            }

            // Return a reference to the found or created database instance
            return *(it->second);
        }

        /**
         * @brief Attempts to retrieve an existing named database instance.
         *
         * Returns a pointer to the database if found, nullptr otherwise.
         * Does NOT create a new database if it doesn't exist.
         * Thread-safe: Protects the internal map.
         *
         * @param name The name of the database to retrieve.
         * @return A pointer to the InMemoryDb instance or nullptr if not found.
         */
        InMemoryDb<Key, Value>* FindDatabase(std::string const& name) {
            // Lock the manager's internal map before accessing it
            std::lock_guard<std::mutex> lock(manager_mutex_);

            auto it = databases_.find(name);
            if (it != databases_.end()) {
                return it->second.get();   // Return raw pointer from unique_ptr
            }
            return nullptr;   // Not found
        }

        /**
         * @brief Removes a named database instance.
         *
         * If a database with the given name exists, it is removed and destroyed.
         * Thread-safe: Protects the internal map.
         *
         * @param name The name of the database to remove.
         * @return true if a database was found and removed, false otherwise.
         */
        bool RemoveDatabase(std::string const& name) {
            // Lock the manager's internal map before modifying it
            std::lock_guard<std::mutex> lock(manager_mutex_);
            return databases_.erase(name) > 0;   // erase returns number of elements removed (0 or 1)
        }

        /**
         * @brief Gets the number of managed databases for this Key/Value type.
         * Thread-safe: Protects the internal map.
         *
         * @return The number of named databases.
         */
        size_t CountDatabases() const {
            std::lock_guard<std::mutex> lock(manager_mutex_);
            return databases_.size();
        }

        // Note: Providing iterators or ways to list all database names from the manager
        // would require similar care with locking the manager's mutex.
    };
}   // namespace simple_db

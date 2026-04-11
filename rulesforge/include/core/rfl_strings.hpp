#ifndef RFL_STRINGS_HPP
#define RFL_STRINGS_HPP


#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>


namespace rulesforge {

// String interning for constant strings (rule names, type names, etc.)
class StringInterner {
public:
  static StringInterner &instance() {
    static StringInterner instance;
    return instance;
  }

  std::string_view intern(std::string const &str) {
    auto it = interned_strings_.find(str);
    if (it != interned_strings_.end()) {
      return std::string_view(*it);
    }

    auto [inserted_it, success] = interned_strings_.insert(str);
    return std::string_view(*inserted_it);
  }

  std::string_view intern(std::string_view sv) { return intern(std::string(sv)); }

  // For rule names, type names, field names that are known at parse time
  std::string_view intern_persistent(std::string const &str) { return intern(str); }

  void clear() { interned_strings_.clear(); }

  size_t size() const { return interned_strings_.size(); }

private:
  // MUST use std::unordered_set (node-based) for pointer stability.
  // phmap::flat_hash_set moves elements on rehash, invalidating string_views.
  std::unordered_set<std::string> interned_strings_;
};

// Fast string lookup using string_view keys
template <typename Value> class StringViewMap {
public:
  using key_type = std::string_view;
  using mapped_type = Value;
  using value_type = std::pair<const std::string_view, Value>;

private:
  struct StringViewHash {
    std::size_t operator()(std::string_view sv) const noexcept {
      return std::hash<std::string_view>{}(sv);
    }
  };

  std::unordered_map<std::string_view, Value, StringViewHash> map_;

public:
  using iterator =
      typename std::unordered_map<std::string_view, Value, StringViewHash>::iterator;
  using const_iterator =
      typename std::unordered_map<std::string_view, Value, StringViewHash>::const_iterator;

  auto begin() const { return map_.begin(); }
  auto end() const { return map_.end(); }
  auto begin() { return map_.begin(); }
  auto end() { return map_.end(); }
  auto find(std::string_view key) const { return map_.find(key); }
  auto find(std::string_view key) { return map_.find(key); }
  auto count(std::string_view key) const { return map_.count(key); }
  auto size() const { return map_.size(); }
  auto empty() const { return map_.empty(); }

  Value &operator[](std::string_view key) { return map_[key]; }
  const Value &at(std::string_view key) const { return map_.at(key); }

  auto insert(const value_type &value) { return map_.insert(value); }
  // Add generic insert for std::pair
  template <typename P> auto insert(P &&value) { return map_.insert(std::forward<P>(value)); }

  auto emplace(std::string_view key, Value &&value) {
    return map_.emplace(key, std::forward<Value>(value));
  }

  auto emplace(std::string_view key, Value const &value) {
    Value copy = value;
    return map_.emplace(key, std::move(copy));
  }

  void erase(std::string_view key) { map_.erase(key); }
  void erase(typename std::unordered_map<std::string_view, Value,
                                               StringViewHash>::const_iterator it) {
    map_.erase(it);
  }
  void reserve(size_t count) { map_.reserve(count); }
  void clear() { map_.clear(); }
};

// Wrapper for already interned strings to bypass lookup
struct InternedString {
  std::string_view sv;
  explicit InternedString(std::string_view s) : sv(s) {}
};

// Map that automatically interns string keys on insertion
template <typename Value> class InternedKeyMap {
  StringViewMap<Value> map_;

public:
  using iterator = typename StringViewMap<Value>::iterator;
  using const_iterator = typename StringViewMap<Value>::const_iterator;
  using value_type = std::pair<const std::string_view, Value>;

  InternedKeyMap() = default;
  InternedKeyMap(std::initializer_list<std::pair<std::string, Value>> init) {
    for (auto const &p : init) {
      insert(p);
    }
  }

  Value &operator[](std::string_view key) {
    auto interned = StringInterner::instance().intern(key);
    return map_[interned];
  }

  Value &operator[](std::string const &key) {
    auto interned = StringInterner::instance().intern(key);
    return map_[interned];
  }

  Value &operator[](const char *key) {
    auto interned = StringInterner::instance().intern(std::string(key));
    return map_[interned];
  }

  Value &operator[](InternedString key) { return map_[key.sv]; }

  // Const access
  auto find(std::string_view key) const { return map_.find(key); }
  auto begin() const { return map_.begin(); }
  auto end() const { return map_.end(); }

  // Non-const access for iteration
  auto begin() { return map_.begin(); }
  auto end() { return map_.end(); }
  auto find(std::string_view key) { return map_.find(key); }

  size_t size() const { return map_.size(); }
  bool empty() const { return map_.empty(); }
  void reserve(size_t count) { map_.reserve(count); }
  void clear() { map_.clear(); }

  // Insert overloads
  void insert(std::pair<std::string, Value> const &val) {
    auto interned = StringInterner::instance().intern(val.first);
    map_.emplace(interned, val.second);
  }

  void insert(std::pair<std::string_view, Value> const &val) {
    auto interned = StringInterner::instance().intern(val.first);
    map_.emplace(interned, val.second);
  }

  void insert(std::pair<InternedString, Value> const &val) {
    map_.emplace(val.first.sv, val.second);
  }
};

} // namespace rulesforge

#endif // RFL_STRINGS_HPP

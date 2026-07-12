#ifndef RFL_STRINGS_HPP
#define RFL_STRINGS_HPP


#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>
#include <stdexcept>


namespace rulesforge {

// String interning for constant strings (rule names, type names, etc.)
class StringInterner {
public:
  static StringInterner &instance() {
    static StringInterner instance;
    return instance;
  }

  std::string_view intern(std::string_view value) {
    {
      std::shared_lock lock(mutex_);
      auto it = interned_strings_.find(value);
      if (it != interned_strings_.end()) {
        return std::string_view(*it);
      }
    }

    std::unique_lock lock(mutex_);
    auto it = interned_strings_.find(value);
    if (it != interned_strings_.end()) {
      return std::string_view(*it);
    }
    auto inserted = interned_strings_.emplace(value);
    return std::string_view(*inserted.first);
  }

  std::string_view intern(std::string const &str) { return intern(std::string_view(str)); }
  std::string_view intern(char const *str) { return intern(std::string_view(str)); }

  // For rule names, type names, field names that are known at parse time
  std::string_view intern_persistent(std::string const &str) { return intern(str); }

  void clear() {
    std::unique_lock lock(mutex_);
    interned_strings_.clear();
  }

  size_t size() const {
    std::shared_lock lock(mutex_);
    return interned_strings_.size();
  }

private:
  struct TransparentStringHash {
    using is_transparent = void;
    size_t operator()(std::string_view value) const noexcept {
      return std::hash<std::string_view>{}(value);
    }
  };

  struct TransparentStringEqual {
    using is_transparent = void;
    bool operator()(std::string_view lhs, std::string_view rhs) const noexcept {
      return lhs == rhs;
    }
  };

  // MUST use std::unordered_set (node-based) for pointer stability.
  // phmap::flat_hash_set moves elements on rehash, invalidating string_views.
  std::unordered_set<std::string, TransparentStringHash, TransparentStringEqual> interned_strings_;
  mutable std::shared_mutex mutex_;
};

// Fast string lookup using string_view keys
template <typename Value> class StringViewMap {
public:
  using key_type = std::string_view;
  using mapped_type = Value;
  using value_type = std::pair<std::string_view, Value>;

private:
  struct Comp {
    bool operator()(std::pair<std::string_view, Value> const& a, std::pair<std::string_view, Value> const& b) const {
      return a.first < b.first;
    }
    bool operator()(std::pair<std::string_view, Value> const& a, std::string_view b) const {
      return a.first < b;
    }
    bool operator()(std::string_view a, std::pair<std::string_view, Value> const& b) const {
      return a < b.first;
    }
  };

  std::vector<std::pair<std::string_view, Value>> vec_;

public:
  using iterator = typename std::vector<std::pair<std::string_view, Value>>::iterator;
  using const_iterator = typename std::vector<std::pair<std::string_view, Value>>::const_iterator;

  auto begin() const { return vec_.begin(); }
  auto end() const { return vec_.end(); }
  auto begin() { return vec_.begin(); }
  auto end() { return vec_.end(); }

  const_iterator find(std::string_view key) const {
    auto it = std::lower_bound(vec_.begin(), vec_.end(), key, Comp{});
    if (it != vec_.end() && it->first == key) return it;
    return vec_.end();
  }

  iterator find(std::string_view key) {
    auto it = std::lower_bound(vec_.begin(), vec_.end(), key, Comp{});
    if (it != vec_.end() && it->first == key) return it;
    return vec_.end();
  }

  auto count(std::string_view key) const {
    return find(key) != vec_.end() ? 1 : 0;
  }

  auto size() const { return vec_.size(); }
  auto empty() const { return vec_.empty(); }

  Value &operator[](std::string_view key) {
    auto it = std::lower_bound(vec_.begin(), vec_.end(), key, Comp{});
    if (it != vec_.end() && it->first == key) {
      return it->second;
    }
    auto new_it = vec_.insert(it, std::make_pair(key, Value{}));
    return new_it->second;
  }

  const Value &at(std::string_view key) const {
    auto it = find(key);
    if (it == vec_.end()) throw std::out_of_range("StringViewMap::at: key not found");
    return it->second;
  }

  std::pair<iterator, bool> emplace(std::string_view key, Value &&value) {
    auto it = std::lower_bound(vec_.begin(), vec_.end(), key, Comp{});
    if (it != vec_.end() && it->first == key) {
      return {it, false};
    }
    auto new_it = vec_.insert(it, std::make_pair(key, std::move(value)));
    return {new_it, true};
  }

  std::pair<iterator, bool> emplace(std::string_view key, Value const &value) {
    auto it = std::lower_bound(vec_.begin(), vec_.end(), key, Comp{});
    if (it != vec_.end() && it->first == key) {
      return {it, false};
    }
    auto new_it = vec_.insert(it, std::make_pair(key, value));
    return {new_it, true};
  }

  auto insert(const std::pair<std::string_view, Value> &value) {
    return emplace(value.first, value.second);
  }
  auto insert(std::pair<std::string_view, Value> &&value) {
    return emplace(value.first, std::move(value.second));
  }
  auto insert(const std::pair<const std::string_view, Value> &value) {
    return emplace(value.first, value.second);
  }
  auto insert(std::pair<const std::string_view, Value> &&value) {
    return emplace(value.first, std::move(value.second));
  }

  void erase(std::string_view key) {
    auto it = find(key);
    if (it != vec_.end()) vec_.erase(it);
  }

  void erase(const_iterator it) {
    vec_.erase(it);
  }

  void reserve(size_t count) { vec_.reserve(count); }
  void clear() { vec_.clear(); }
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
    map_.reserve(init.size());
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
    auto interned = StringInterner::instance().intern(std::string_view(key));
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

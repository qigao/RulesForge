#ifndef QUERY_RESULT_HPP
#define QUERY_RESULT_HPP

#include "drools_rete_defs.hpp"
#include "knowledge_base.hpp"

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

/**
 * @class QueryResultRow
 * @brief Represents a single row (a complete match) in a QueryResult.
 *
 * Provides safe methods to access facts and their fields by binding name.
 */
class QueryResultRow {
public:
    QueryResultRow(map<std::string, std::shared_ptr<Fact>> const& row_data,
                   std::shared_ptr<KnowledgeBase const> kb) : row_data_(row_data), kb_(std::move(kb)) {}

    /// @brief Gets the fact for a given binding name.
    /// @return An optional containing the fact, or std::nullopt if the binding is not found.
    std::optional<std::shared_ptr<Fact>> get(std::string const& binding) const {
        auto it = row_data_.find(binding);
        if (it != row_data_.end()) { return it->second; }
        return std::nullopt;
    }

    /// @brief Gets a specific field value from a bound fact, safely converted to type T.
    /// @return An optional containing the value, or std::nullopt if the binding, field, or type do not match.
    template <typename T>
    std::optional<T> getFieldAs(std::string const& binding, std::string const& field_name) const;

private:
    map<std::string, std::shared_ptr<Fact>> const& row_data_;
    std::shared_ptr<KnowledgeBase const> kb_;
};

class QueryResultIterator {
public:
    // C++ standard iterator traits
    using iterator_category = std::forward_iterator_tag;
    using value_type = QueryResultRow;
    using difference_type = std::ptrdiff_t;
    using pointer = QueryResultRow*;
    using reference = QueryResultRow;

    // Constructor
    QueryResultIterator(std::vector<map<std::string, std::shared_ptr<Fact>>>::const_iterator it,
                        std::shared_ptr<KnowledgeBase const> kb) : internal_iterator_(it), kb_(kb) {}

    // The magic: dereferencing creates and returns our wrapper row object
    reference operator*() const { return QueryResultRow(*internal_iterator_, kb_); }

    // Standard iterator operations
    QueryResultIterator& operator++() {
        ++internal_iterator_;
        return *this;
    }

    bool operator!=(QueryResultIterator const& other) const { return internal_iterator_ != other.internal_iterator_; }

private:
    std::vector<map<std::string, std::shared_ptr<Fact>>>::const_iterator internal_iterator_;
    std::shared_ptr<KnowledgeBase const> kb_;
};

/**
 * @class QueryResult
 * @brief A container for the results of a query execution.
 *
 * This class provides a safe and ergonomic way to access the rows and
 * facts returned by a query. It supports iteration and convenience methods
 * for common access patterns.
 */
class QueryResult {
public:
    // The query result now needs a reference to the KB to perform type conversions
    QueryResult(std::vector<map<std::string, std::shared_ptr<Fact>>> data,
                std::shared_ptr<KnowledgeBase const> kb) : data_(std::move(data)), kb_(std::move(kb)) {}

    /// @brief Returns the number of rows in the result set.
    size_t size() const { return data_.size(); }

    /// @brief Checks if the result set is empty.
    bool empty() const { return data_.empty(); }

    /// @brief Returns the first row of the result set, if it exists.
    std::optional<QueryResultRow> first() {
        if (data_.empty()) { return std::nullopt; }
        return QueryResultRow(data_[0], kb_);
    }

    /// @brief Returns the single row of the result set.
    /// @throws std::runtime_error if the result set does not contain exactly one row.
    QueryResultRow single() {
        if (data_.size() != 1) {
            throw std::runtime_error("QueryResult::single() called on a result set with " +
                                     std::to_string(data_.size()) + " rows. Expected exactly one row.");
        }
        return QueryResultRow(data_[0], kb_);
    }

    /// @brief Returns a vector of all facts for a specific binding.
    std::vector<std::shared_ptr<Fact>> getColumn(std::string const& binding) const {
        std::vector<std::shared_ptr<Fact>> column_facts;
        for (auto const& row_map : data_) {
            auto it = row_map.find(binding);
            if (it != row_map.end()) { column_facts.push_back(it->second); }
        }
        return column_facts;
    }

    /// @brief Returns a vector of specific field values for a binding.
    template <typename T>
    std::vector<T> getColumnFieldAs(std::string const& binding, std::string const& field_name) const;

    auto begin() const { return QueryResultIterator(data_.cbegin(), kb_); }

    auto end() const { return QueryResultIterator(data_.cend(), kb_); }

private:
    std::vector<map<std::string, std::shared_ptr<Fact>>> data_;
    std::shared_ptr<KnowledgeBase const> kb_;
};

// --- Template and Iterator Implementations ---

// In QueryResultRow class definition
template <typename T>
std::optional<T> QueryResultRow::getFieldAs(std::string const& binding, std::string const& field_name) const {
    auto fact_opt = get(binding);
    if (!fact_opt) {
        return std::nullopt;   // Binding not found
    }

    auto field_val_opt = (*fact_opt)->get_field(field_name);
    if (!field_val_opt) {
        return std::nullopt;   // Field not found
    }

    // 1. Try for a direct, exact type match (this is the fast path).
    if (T* pval = std::get_if<T>(&*field_val_opt)) { return *pval; }

    // 2. If the direct match fails, attempt safe, implicit conversions.
    //    Use 'if constexpr' to ensure this code only compiles for relevant types.

    // Allow conversion from int64_t -> double
    if constexpr (std::is_same_v<T, double>) {
        if (int64_t* p_int = std::get_if<int64_t>(&*field_val_opt)) { return static_cast<double>(*p_int); }
    }

    // Allow conversion from int64_t (0 or 1) -> bool
    if constexpr (std::is_same_v<T, bool>) {
        if (int64_t* p_int = std::get_if<int64_t>(&*field_val_opt)) { return (*p_int != 0); }
    }

    // Add any other desired safe conversions here...

    // 3. If no direct match or safe conversion is found, it's a true type mismatch.
    return std::nullopt;
}

// QueryResult implementation
template <typename T>
std::vector<T> QueryResult::getColumnFieldAs(std::string const& binding, std::string const& field_name) const {
    std::vector<T> column_data;
    for (auto const& row_map : data_) {
        QueryResultRow row(row_map, kb_);
        if (auto val = row.getFieldAs<T>(binding, field_name)) { column_data.push_back(*val); }
    }
    return column_data;
}

#endif   // QUERY_RESULT_HPP

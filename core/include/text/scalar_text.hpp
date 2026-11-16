#pragma once
#include "model/value_types.hpp"

namespace rulesforge {
// Comparison-local view. Existing string values are borrowed; formatting UUID,
// date/decimal/etc. owns its temporary until this scope ends. Do not move the
// adapter or retain its view beyond either this scope or the source value.
class ScalarTextView {
    std::optional<std::string> formatted_;
    std::string_view view_;
    bool valid_ = false;
public:
    explicit ScalarTextView(ConstraintValue const& value) {
        if (auto text = std::get_if<std::string>(&value)) {
            view_ = *text;
            valid_ = true;
        } else {
            formatted_ = scalar_text(value);
            if (formatted_) {
                view_ = *formatted_;
                valid_ = true;
            }
        }
    }
    ScalarTextView(ScalarTextView const&) = delete;
    ScalarTextView& operator=(ScalarTextView const&) = delete;
    explicit operator bool() const noexcept { return valid_; }
    std::string_view operator*() const noexcept { return view_; }
    std::string_view const* operator->() const noexcept { return &view_; }
};
} // namespace rulesforge

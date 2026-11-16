#pragma once

#include <core/tstr.h>
#include <memory>
#include <new>
#include <string_view>

namespace rulesforge {
// Unique mutable Base string. A view expires at append, move-assignment or
// destruction. Published interned text is never mutated.
// An append source must not alias this owner's storage.
class OwnedText {
  struct Free {
    void operator()(char *value) const noexcept { tstr_free(value); }
  };
  std::unique_ptr<char, Free> value_;

public:
  explicit OwnedText(std::string_view text = {})
      : value_(tstr_from_v(vstr_from_buf(text.data(), text.size()))) {
    if (!value_)
      throw std::bad_alloc();
  }
  OwnedText(OwnedText &&) noexcept = default;
  OwnedText &operator=(OwnedText &&) noexcept = default;
  void append(std::string_view text) {
    auto next = value_.get();
    if (!tstr_append(&next, vstr_from_buf(text.data(), text.size())))
      throw std::bad_alloc();
    value_.release();
    value_.reset(next);
  }
  std::string_view view() const noexcept {
    auto text = tstr_view(value_.get());
    return {text.data, text.len};
  }
};
} // namespace rulesforge

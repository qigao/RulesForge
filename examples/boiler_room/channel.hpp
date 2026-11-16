#pragma once
#include <array>
#include <condition_variable>
#include <cstring>
#include <exception>
#include <mutex>
#include <optional>
#include <queue/spsc_ring.h>
#include <stdexcept>
#include <type_traits>

namespace boiler_room {
// One source thread and one room owner. Storage outlives both endpoints.
// The mutex makes ring predicates and condition-variable wakeups one protocol;
// byte ownership still follows acquire/copy/release. No slot view escapes.
template <class Envelope> class SourceChannel {
  static_assert(std::is_trivially_copyable_v<Envelope>);
  static_assert(sizeof(Envelope) < 16384);
  std::array<uint8_t, 32768> storage_{};
  spsc_ring ring_{};
  std::mutex mutex_;
  std::condition_variable changed_;
  bool closed_ = false, cancelled_ = false;
  std::exception_ptr error_;

public:
  SourceChannel() {
    if (!spsc_ring_init(&ring_, storage_.data(), storage_.size()))
      throw std::runtime_error("Cannot initialize source SPSC ring");
  }
  SourceChannel(SourceChannel const &) = delete;
  bool send(Envelope const &value) {
    std::unique_lock lock(mutex_);
    uint8_t *slot = nullptr;
    changed_.wait(lock, [&] {
      return cancelled_ || closed_ || (slot = spsc_ring_write_acquire(&ring_, sizeof value));
    });
    if (cancelled_ || closed_)
      return false;
    std::memcpy(slot, &value, sizeof value);
    spsc_ring_write_release(&ring_, sizeof value);
    lock.unlock();
    changed_.notify_all();
    return true;
  }
  std::optional<Envelope> receive() {
    std::unique_lock lock(mutex_);
    uint8_t *slot = nullptr;
    size_t available = 0;
    changed_.wait(lock, [&] {
      slot = spsc_ring_read_acquire(&ring_, &available);
      return cancelled_ || slot || closed_;
    });
    if (cancelled_)
      return {};
    if (!slot) {
      if (error_)
        std::rethrow_exception(error_);
      return {};
    }
    if (available < sizeof(Envelope))
      throw std::logic_error("Partial source envelope");
    Envelope value;
    std::memcpy(&value, slot, sizeof value);
    spsc_ring_read_release(&ring_, sizeof value);
    lock.unlock();
    changed_.notify_all();
    return value;
  }
  void close(std::exception_ptr error = {}) {
    {
      std::lock_guard lock(mutex_);
      error_ = error;
      closed_ = true;
    }
    changed_.notify_all();
  }
  void cancel() {
    {
      std::lock_guard lock(mutex_);
      cancelled_ = true;
    }
    changed_.notify_all();
  }
};
} // namespace boiler_room

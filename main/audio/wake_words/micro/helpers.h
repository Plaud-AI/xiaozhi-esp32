#pragma once

#include <esp_heap_caps.h>
#include <cstdlib>
#include <memory>

namespace micro_wake_word {

/// STL make_unique backport for C++11
template<typename T, typename... Args>
std::unique_ptr<T> make_unique(Args &&...args) {
  return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

/** An STL allocator that uses SPI RAM (PSRAM).
 *
 * By setting flags, it can be configured to not try main memory if SPI RAM is full or unavailable,
 * and to return nullptr instead of aborting when no memory is available.
 */
template<class T>
class ExternalRAMAllocator {
 public:
  using value_type = T;

  enum Flags {
    NONE = 0,
    REFUSE_INTERNAL = 1 << 0,  ///< Refuse falling back to internal memory when external RAM is full or unavailable.
    ALLOW_FAILURE = 1 << 1,    ///< Don't abort when memory allocation fails.
  };

  ExternalRAMAllocator() = default;
  ExternalRAMAllocator(Flags flags) : flags_{flags} {}
  template<class U>
  constexpr ExternalRAMAllocator(const ExternalRAMAllocator<U> &other) : flags_{other.flags_} {}

  T *allocate(size_t n) {
    size_t size = n * sizeof(T);
    T *ptr = nullptr;
    // Try to allocate from PSRAM first
    ptr = static_cast<T *>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    
    if (ptr == nullptr && (this->flags_ & Flags::REFUSE_INTERNAL) == 0) {
      // Fall back to internal RAM if allowed
      ptr = static_cast<T *>(malloc(size));
    }
    
    if (ptr == nullptr && (this->flags_ & Flags::ALLOW_FAILURE) == 0) {
      abort();
    }
    
    return ptr;
  }

  void deallocate(T *p, size_t n) {
    free(p);
  }

 private:
  Flags flags_{Flags::NONE};
};

}  // namespace micro_wake_word


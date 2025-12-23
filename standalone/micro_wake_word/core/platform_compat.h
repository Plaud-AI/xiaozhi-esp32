/**
 * @file platform_compat.h
 * @brief Platform abstraction layer for Micro Wake Word
 * 
 * This header provides platform-independent abstractions for:
 * - Logging
 * - Memory allocation (with PSRAM/external RAM support)
 * - Basic types and utilities
 * 
 * Users should implement platform-specific versions in platform/ directory
 * and include their implementation before including this header.
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <memory>

namespace micro_wake_word {

// =============================================================================
// Logging Abstraction
// =============================================================================

#ifndef MWW_LOG_LEVEL_NONE
#define MWW_LOG_LEVEL_NONE  0
#define MWW_LOG_LEVEL_ERROR 1
#define MWW_LOG_LEVEL_WARN  2
#define MWW_LOG_LEVEL_INFO  3
#define MWW_LOG_LEVEL_DEBUG 4
#endif

// Default log level (can be overridden before including this header)
#ifndef MWW_LOG_LEVEL
#define MWW_LOG_LEVEL MWW_LOG_LEVEL_INFO
#endif

// Platform-specific logging implementation should define these macros:
// MWW_LOGE(tag, fmt, ...) - Error
// MWW_LOGW(tag, fmt, ...) - Warning
// MWW_LOGI(tag, fmt, ...) - Info
// MWW_LOGD(tag, fmt, ...) - Debug

// Default implementation using printf (can be overridden)
#ifndef MWW_LOGE
  #if MWW_LOG_LEVEL >= MWW_LOG_LEVEL_ERROR
    #include <cstdio>
    #define MWW_LOGE(tag, fmt, ...) printf("[E][%s] " fmt "\n", tag, ##__VA_ARGS__)
  #else
    #define MWW_LOGE(tag, fmt, ...) ((void)0)
  #endif
#endif

#ifndef MWW_LOGW
  #if MWW_LOG_LEVEL >= MWW_LOG_LEVEL_WARN
    #include <cstdio>
    #define MWW_LOGW(tag, fmt, ...) printf("[W][%s] " fmt "\n", tag, ##__VA_ARGS__)
  #else
    #define MWW_LOGW(tag, fmt, ...) ((void)0)
  #endif
#endif

#ifndef MWW_LOGI
  #if MWW_LOG_LEVEL >= MWW_LOG_LEVEL_INFO
    #include <cstdio>
    #define MWW_LOGI(tag, fmt, ...) printf("[I][%s] " fmt "\n", tag, ##__VA_ARGS__)
  #else
    #define MWW_LOGI(tag, fmt, ...) ((void)0)
  #endif
#endif

#ifndef MWW_LOGD
  #if MWW_LOG_LEVEL >= MWW_LOG_LEVEL_DEBUG
    #include <cstdio>
    #define MWW_LOGD(tag, fmt, ...) printf("[D][%s] " fmt "\n", tag, ##__VA_ARGS__)
  #else
    #define MWW_LOGD(tag, fmt, ...) ((void)0)
  #endif
#endif

// =============================================================================
// Memory Allocation Abstraction
// =============================================================================

/// STL make_unique backport for C++11
template<typename T, typename... Args>
std::unique_ptr<T> make_unique(Args &&...args) {
  return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

/**
 * @brief Memory allocator with optional external RAM (PSRAM) support
 * 
 * On platforms with external RAM, this allocator will prefer external memory.
 * On other platforms, it falls back to standard malloc.
 * 
 * Platform implementations can override:
 * - mww_alloc_external(size) - Allocate from external RAM
 * - mww_alloc_internal(size) - Allocate from internal RAM
 * - mww_is_external_ptr(ptr) - Check if pointer is in external RAM
 */
template<class T>
class ExternalRAMAllocator {
 public:
  using value_type = T;

  enum Flags {
    NONE = 0,
    REFUSE_INTERNAL = 1 << 0,  ///< Refuse falling back to internal memory
    ALLOW_FAILURE = 1 << 1,    ///< Don't abort when allocation fails
  };

  ExternalRAMAllocator() = default;
  ExternalRAMAllocator(Flags flags) : flags_{flags} {}
  template<class U>
  constexpr ExternalRAMAllocator(const ExternalRAMAllocator<U> &other) : flags_{other.flags_} {}

  T *allocate(size_t n) {
    size_t size = n * sizeof(T);
    T *ptr = nullptr;
    
    // Try to allocate from external RAM first (platform-specific)
    ptr = static_cast<T *>(mww_alloc_external(size));
    
    if (ptr == nullptr && (this->flags_ & Flags::REFUSE_INTERNAL) == 0) {
      // Fall back to internal RAM if allowed
      ptr = static_cast<T *>(mww_alloc_internal(size));
    }
    
    if (ptr == nullptr && (this->flags_ & Flags::ALLOW_FAILURE) == 0) {
      abort();
    }
    
    return ptr;
  }

  void deallocate(T *p, size_t n) {
    mww_free(p);
  }

  Flags flags_{Flags::NONE};

 private:
  // Platform-specific allocation functions (weak linkage for override)
  static void* mww_alloc_external(size_t size);
  static void* mww_alloc_internal(size_t size);
  static void mww_free(void* ptr);
};

// =============================================================================
// Platform Memory Functions (Default implementations)
// =============================================================================

// These can be overridden by platform-specific implementations
#ifndef MWW_CUSTOM_ALLOCATOR

// Check if we're on ESP-IDF
#if defined(ESP_PLATFORM) || defined(IDF_VER)
  // ESP-IDF implementation will be in platform/esp_idf_compat.h
  #define MWW_PLATFORM_ESP_IDF
#else
  // Default: use standard malloc
  inline void* mww_default_alloc_external(size_t size) {
    return malloc(size);
  }
  
  inline void* mww_default_alloc_internal(size_t size) {
    return malloc(size);
  }
  
  inline void mww_default_free(void* ptr) {
    free(ptr);
  }
  
  inline bool mww_is_external_ptr(const void* ptr) {
    (void)ptr;
    return false;  // No external RAM on default platform
  }
  
  template<class T>
  void* ExternalRAMAllocator<T>::mww_alloc_external(size_t size) {
    return mww_default_alloc_external(size);
  }
  
  template<class T>
  void* ExternalRAMAllocator<T>::mww_alloc_internal(size_t size) {
    return mww_default_alloc_internal(size);
  }
  
  template<class T>
  void ExternalRAMAllocator<T>::mww_free(void* ptr) {
    mww_default_free(ptr);
  }
#endif  // ESP_PLATFORM

#endif  // MWW_CUSTOM_ALLOCATOR

// =============================================================================
// Timer Abstraction
// =============================================================================

// Get current time in microseconds (for performance measurement)
#ifndef mww_get_time_us
  #if defined(MWW_PLATFORM_ESP_IDF)
    // ESP-IDF: use esp_timer_get_time()
  #else
    #include <chrono>
    inline int64_t mww_get_time_us() {
      auto now = std::chrono::high_resolution_clock::now();
      auto duration = now.time_since_epoch();
      return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
    }
  #endif
#endif

}  // namespace micro_wake_word


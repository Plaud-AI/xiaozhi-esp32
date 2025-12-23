/**
 * @file linux_compat.h
 * @brief Linux platform compatibility layer for Micro Wake Word
 * 
 * Include this file BEFORE including any core headers when building on Linux.
 * This provides standard implementations for:
 * - Logging (using printf)
 * - Memory allocation (using malloc)
 * - Timer functions (using chrono)
 * 
 * Usage:
 * ```cpp
 * #include "platform/linux_compat.h"  // Include first!
 * #include "core/micro_wake_word.h"
 * ```
 */

#pragma once

#if defined(__linux__) || defined(__APPLE__) || defined(_WIN32)

#include <cstdio>
#include <cstdlib>
#include <chrono>

// =============================================================================
// Logging - Standard printf
// =============================================================================

#ifndef MWW_LOG_LEVEL
#define MWW_LOG_LEVEL 3  // INFO level by default
#endif

#define MWW_LOGE(tag, fmt, ...) do { fprintf(stderr, "[E][%s] " fmt "\n", tag, ##__VA_ARGS__); } while(0)

#if MWW_LOG_LEVEL >= 2
  #define MWW_LOGW(tag, fmt, ...) do { fprintf(stderr, "[W][%s] " fmt "\n", tag, ##__VA_ARGS__); } while(0)
#else
  #define MWW_LOGW(tag, fmt, ...) ((void)0)
#endif

#if MWW_LOG_LEVEL >= 3
  #define MWW_LOGI(tag, fmt, ...) do { printf("[I][%s] " fmt "\n", tag, ##__VA_ARGS__); } while(0)
#else
  #define MWW_LOGI(tag, fmt, ...) ((void)0)
#endif

#if MWW_LOG_LEVEL >= 4
  #define MWW_LOGD(tag, fmt, ...) do { printf("[D][%s] " fmt "\n", tag, ##__VA_ARGS__); } while(0)
#else
  #define MWW_LOGD(tag, fmt, ...) ((void)0)
#endif

// =============================================================================
// Memory Allocation - Standard malloc (no PSRAM on desktop)
// =============================================================================

#define MWW_CUSTOM_ALLOCATOR 1

namespace micro_wake_word {

inline void* mww_linux_alloc_external(size_t size) {
  // No external RAM on Linux, just use malloc
  return malloc(size);
}

inline void* mww_linux_alloc_internal(size_t size) {
  return malloc(size);
}

inline void mww_linux_free(void* ptr) {
  free(ptr);
}

inline bool mww_is_external_ptr(const void* ptr) {
  (void)ptr;
  return false;  // No external RAM on Linux
}

// Forward declarations to avoid including platform_compat.h before definitions
template<class T>
class ExternalRAMAllocator;

// Implement template class static methods
template<class T>
void* ExternalRAMAllocator<T>::mww_alloc_external(size_t size) {
  return mww_linux_alloc_external(size);
}

template<class T>
void* ExternalRAMAllocator<T>::mww_alloc_internal(size_t size) {
  return mww_linux_alloc_internal(size);
}

template<class T>
void ExternalRAMAllocator<T>::mww_free(void* ptr) {
  mww_linux_free(ptr);
}

}  // namespace micro_wake_word

// =============================================================================
// Timer - Standard chrono
// =============================================================================

inline int64_t mww_get_time_us() {
  auto now = std::chrono::high_resolution_clock::now();
  auto duration = now.time_since_epoch();
  return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
}

#endif  // __linux__ || __APPLE__ || _WIN32


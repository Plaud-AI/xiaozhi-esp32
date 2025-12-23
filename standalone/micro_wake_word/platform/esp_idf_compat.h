/**
 * @file esp_idf_compat.h
 * @brief ESP-IDF platform compatibility layer for Micro Wake Word
 * 
 * Include this file BEFORE including any core headers when building on ESP-IDF.
 * This provides ESP-IDF specific implementations for:
 * - Logging (using ESP_LOG)
 * - Memory allocation (using heap_caps for PSRAM support)
 * - Timer functions
 * 
 * Usage:
 * ```cpp
 * #include "platform/esp_idf_compat.h"  // Include first!
 * #include "core/micro_wake_word.h"
 * ```
 */

#pragma once

// Only compile on ESP-IDF
#if defined(ESP_PLATFORM) || defined(IDF_VER)

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>
#include <esp_timer.h>

// =============================================================================
// Logging - Map to ESP-IDF logging
// =============================================================================

#define MWW_LOGE(tag, fmt, ...) ESP_LOGE(tag, fmt, ##__VA_ARGS__)
#define MWW_LOGW(tag, fmt, ...) ESP_LOGW(tag, fmt, ##__VA_ARGS__)
#define MWW_LOGI(tag, fmt, ...) ESP_LOGI(tag, fmt, ##__VA_ARGS__)
#define MWW_LOGD(tag, fmt, ...) ESP_LOGD(tag, fmt, ##__VA_ARGS__)

// =============================================================================
// Memory Allocation - ESP-IDF with PSRAM support
// =============================================================================

#define MWW_CUSTOM_ALLOCATOR 1

namespace micro_wake_word {

// ESP-IDF specific allocation functions
inline void* mww_esp_alloc_external(size_t size) {
  // Try PSRAM first
  void* ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return ptr;
}

inline void* mww_esp_alloc_internal(size_t size) {
  // Allocate from internal SRAM
  return heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

inline void mww_esp_free(void* ptr) {
  free(ptr);
}

inline bool mww_is_external_ptr(const void* ptr) {
  return esp_ptr_external_ram(ptr);
}

// Implement template class static methods
template<class T>
void* ExternalRAMAllocator<T>::mww_alloc_external(size_t size) {
  return mww_esp_alloc_external(size);
}

template<class T>
void* ExternalRAMAllocator<T>::mww_alloc_internal(size_t size) {
  return mww_esp_alloc_internal(size);
}

template<class T>
void ExternalRAMAllocator<T>::mww_free(void* ptr) {
  mww_esp_free(ptr);
}

}  // namespace micro_wake_word

// =============================================================================
// Timer - ESP-IDF timer
// =============================================================================

#define mww_get_time_us() esp_timer_get_time()

#endif  // ESP_PLATFORM


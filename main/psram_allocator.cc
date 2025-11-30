#include <new>
#include <cstdlib>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "sdkconfig.h"

// Only override if PSRAM is enabled
#if CONFIG_SPIRAM

void* operator new(std::size_t size) {
    // Try PSRAM first
    void* ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (ptr) return ptr;
    
    // Fallback to internal RAM if PSRAM fails or not available
    ptr = heap_caps_malloc(size, MALLOC_CAP_INTERNAL);
    if (ptr) return ptr;
    
    // Throw bad_alloc on failure (standard C++ behavior)
    throw std::bad_alloc();
}

void operator delete(void* ptr) noexcept {
    free(ptr);
}

void operator delete(void* ptr, std::size_t size) noexcept {
    free(ptr);
}

void* operator new[](std::size_t size) {
    void* ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (ptr) return ptr;
    
    ptr = heap_caps_malloc(size, MALLOC_CAP_INTERNAL);
    if (ptr) return ptr;
    
    throw std::bad_alloc();
}

void operator delete[](void* ptr) noexcept {
    free(ptr);
}

void operator delete[](void* ptr, std::size_t size) noexcept {
    free(ptr);
}

// Nothrow versions
void* operator new(std::size_t size, const std::nothrow_t& tag) noexcept {
    void* ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (ptr) return ptr;
    return heap_caps_malloc(size, MALLOC_CAP_INTERNAL);
}

void* operator new[](std::size_t size, const std::nothrow_t& tag) noexcept {
    void* ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (ptr) return ptr;
    return heap_caps_malloc(size, MALLOC_CAP_INTERNAL);
}

void operator delete(void* ptr, const std::nothrow_t& tag) noexcept {
    free(ptr);
}

void operator delete[](void* ptr, const std::nothrow_t& tag) noexcept {
    free(ptr);
}

#endif // CONFIG_SPIRAM


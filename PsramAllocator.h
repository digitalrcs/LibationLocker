/*
 * Libation Locker by Dan Roberts
 *
 * PSRAM-backed allocator for ArduinoJson v6.
 * Falls back to standard malloc when PSRAM is unavailable.
 *
 * Usage:
 *   BasicJsonDocument<PsramAllocator> doc(65536);
 *
 * Creator: Dan Roberts
 * License: GPL-3.0
 */

#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

struct PsramAllocator {
  void* allocate(size_t size) {
    if (psramFound()) {
      void* p = ps_malloc(size);
      if (p) return p;
    }
    // Fallback to internal heap
    return malloc(size);
  }

  void deallocate(void* ptr) {
    free(ptr); // free() handles both PSRAM and internal pointers
  }

  void* reallocate(void* ptr, size_t newSize) {
    if (psramFound()) {
      void* p = ps_realloc(ptr, newSize);
      if (p) return p;
    }
    return realloc(ptr, newSize);
  }
};

// Convenience alias: use like DynamicJsonDocument but in PSRAM
using PsramJsonDocument = BasicJsonDocument<PsramAllocator>;

// ---------------------------------------------------------------------------
// PsramStdAllocator<T> — std::allocator-compatible allocator that places
// the backing storage in PSRAM. Falls back to internal heap if PSRAM is
// absent or exhausted.
//
// Usage:
//   std::vector<Item, PsramStdAllocator<Item>> items;
//
// This moves the vector's contiguous backing array into PSRAM, freeing
// internal heap for WiFi, AsyncWebServer, and other fixed-allocation
// consumers. Note: objects stored in the vector that themselves allocate
// (e.g., Arduino String, nested std::vector) still allocate their own
// buffers through the default allocator (internal heap).
// ---------------------------------------------------------------------------
template <typename T>
struct PsramStdAllocator {
  using value_type = T;

  PsramStdAllocator() noexcept = default;

  template <typename U>
  PsramStdAllocator(const PsramStdAllocator<U>&) noexcept {}

  T* allocate(std::size_t n) {
    void* p = nullptr;
    if (psramFound()) {
      p = ps_malloc(n * sizeof(T));
    }
    if (!p) p = malloc(n * sizeof(T));
    if (!p) {
      // ESP32 doesn't support exceptions by default; return nullptr
      // and let the caller (std::vector) handle it.
      return nullptr;
    }
    return static_cast<T*>(p);
  }

  void deallocate(T* p, std::size_t) noexcept {
    free(p);  // free() handles both PSRAM and internal pointers
  }
};

template <typename T, typename U>
bool operator==(const PsramStdAllocator<T>&, const PsramStdAllocator<U>&) noexcept { return true; }

template <typename T, typename U>
bool operator!=(const PsramStdAllocator<T>&, const PsramStdAllocator<U>&) noexcept { return false; }

#ifndef CORE_HASH_H
#define CORE_HASH_H

#include <stddef.h>
#include <stdint.h>

// https://github.com/amakukha/minimal_hashes
static inline uint64_t FNV1a_64(uint64_t seed, uint64_t len,
                                const uint8_t *data) {
  uint64_t h = seed;

  for (uint64_t i = 0; i < len; i++) {
    h ^= data[i];
    h *= (uint64_t)0x00000100000001B3UL;
  }

  return h;
}

static inline uint64_t hash_bytes(uint64_t len, uint8_t *bytes) {
  return FNV1a_64(0xcbf29ce484222325UL, len * sizeof(uint8_t), (void *)bytes);
}

static inline uint64_t hash_words(uint64_t len, uint64_t *bytes) {
  return FNV1a_64(0xcbf29ce484222325UL, len * sizeof(uint64_t), (void *)bytes);
}

static inline uint64_t shash_words(uint64_t stride, uint64_t len,
                                   uint64_t *bytes) {
  uint64_t hash = 0xcbf29ce484222325UL;
  
  for (uint64_t i = 0; i < len; i++) {
    hash = FNV1a_64(hash, sizeof(uint64_t), (void *)&bytes[i * stride]);
  }

  return hash;
}

static inline uint64_t continue_hash_bytes(uint64_t sofar, uint64_t len,
                                           uint8_t *bytes) {
  return FNV1a_64(sofar, len * sizeof(uint8_t), (void *)bytes);
}

static inline uint64_t continue_hash_words(uint64_t sofar, uint64_t len,
                                           uint64_t *bytes) {
  return FNV1a_64(sofar, len * sizeof(uint64_t), (void *)bytes);
}
#endif

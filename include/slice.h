#include "hash.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifndef T
#error "Define a type T before including this header"
#endif

#define CONCAT(a, b) CONCAT_(a, b)
#define CONCAT_(a, b) a##b

#ifndef NAME
#define TYPENAME CONCAT(s_, T)
#else
#define TYPENAME CONCAT(s_, NAME)
#endif

#define PREFIX TYPENAME
#define LINKAGE static inline
#define METHOD(name) CONCAT(PREFIX, CONCAT(_, name))

typedef struct TYPENAME TYPENAME;
struct TYPENAME {
  const T *data;
  uint64_t len;
};

LINKAGE TYPENAME METHOD(create)(const T *data, uint64_t len) {
  return (TYPENAME){.data = data, .len = len};
}

LINKAGE bool METHOD(match)(TYPENAME self, TYPENAME other) {
  if (self.len != other.len)
    return false;

  return memcmp(self.data, other.data, self.len) == 0;
}

LINKAGE size_t METHOD(hash)(TYPENAME self) {
  return hash_bytes(self.len * sizeof(T), (uint8_t *)self.data);
}

#undef T
#undef TYPENAME
#undef PREFIX
#undef LINKAGE
#undef METHOD
#undef CONCAT
#undef CONCAT_
#undef NAME

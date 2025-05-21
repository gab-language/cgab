#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef T
#error "Define a type T before including this header"
#endif

#define CONCAT(a, b) CONCAT_(a, b)
#define CONCAT_(a, b) a##b

#ifndef NAME
#define TYPENAME CONCAT(a_, T)
#else
#define TYPENAME CONCAT(a_, NAME)
#endif

#define PREFIX TYPENAME
#define LINKAGE static inline
#define METHOD(name) CONCAT(PREFIX, CONCAT(_, name))

typedef struct TYPENAME TYPENAME;
struct TYPENAME {
  uint64_t len;
  T data[];
};

LINKAGE TYPENAME *METHOD(create)(const T *data, uint64_t len) {
  TYPENAME *self = (TYPENAME *)malloc(sizeof(TYPENAME) + sizeof(T) * len);
  memcpy(self->data, data, sizeof(T) * len);
  self->len = len;
  return self;
}

LINKAGE TYPENAME *METHOD(empty)(size_t len) {
  TYPENAME *self = (TYPENAME *)malloc(sizeof(TYPENAME) + sizeof(T) * len);
  memset(self->data, 0, sizeof(T) * len);
  self->len = len;
  return self;
}

LINKAGE TYPENAME *METHOD(one)(T value) { return METHOD(create)(&value, 1); }

LINKAGE bool METHOD(match)(TYPENAME *self, TYPENAME *other) {
  if (self->len != other->len)
    return false;
  return memcmp(self->data, other->data, self->len) == 0;
}

LINKAGE void METHOD(destroy)(TYPENAME *self) { free(self); }

#undef NAME
#undef T
#undef TYPENAME
#undef PREFIX
#undef LINKAGE
#undef METHOD
#undef CONCAT
#undef CONCAT_

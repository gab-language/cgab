#include "assert.h"
#include "string.h"

#ifndef T
#error "Define a type T before including this header"
#endif

#define CONCAT(a, b) CONCAT_(a, b)
#define CONCAT_(a, b) a##b

#ifndef NAME
#define TYPENAME CONCAT(v_, T)
#else
#define TYPENAME CONCAT(v_, NAME)
#endif

#define PREFIX TYPENAME
#define LINKAGE static inline
#define METHOD(name) CONCAT(PREFIX, CONCAT(_, name))

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define GROW(type, loc, new_count)                                             \
  ((type *)realloc(loc, sizeof(type) * (new_count)))

typedef struct TYPENAME TYPENAME;
struct TYPENAME {
  T *data;
  size_t len;
  size_t cap;
};

LINKAGE void METHOD(create)(TYPENAME *self, uint64_t cap) {
  self->cap = cap;
  self->len = 0;
  self->data = malloc(sizeof(T) * cap);
}

LINKAGE void METHOD(copy)(TYPENAME *self, TYPENAME *other) {
  self->cap = other->cap;
  self->len = other->len;
  self->data = malloc(sizeof(T) * other->cap);
  memcpy(self->data, other->data, other->len * sizeof(T));
}

LINKAGE void METHOD(destroy)(TYPENAME *self) {
  free(self->data);
  self->cap = 0;
  self->len = 0;
}

LINKAGE size_t METHOD(set)(TYPENAME *self, size_t index, T value) {
  assert(index < self->len);
  self->data[index] = value;
  return index;
}

LINKAGE size_t METHOD(push)(TYPENAME *self, T value) {
  if (self->len >= self->cap) {
    self->cap = MAX(8, self->cap * 2);
    self->data = GROW(T, self->data, self->cap);
  }

  return METHOD(set)(self, self->len++, value);
}

LINKAGE T METHOD(pop)(TYPENAME *self) {
  assert(self->len > 0);
  return self->data[--self->len];
}

LINKAGE T *METHOD(ref_at)(TYPENAME *self, size_t index) {
  assert(index < self->len);
  return self->data + index;
}

LINKAGE T METHOD(val_at)(TYPENAME *self, size_t index) {
  assert(index < self->len);
  return self->data[index];
}

LINKAGE void METHOD(cap)(TYPENAME *self, size_t cap) {
  if (self->cap < cap) {
    self->data = GROW(T, self->data, MAX(8, cap));
    self->cap = cap;
  }
}

LINKAGE T *METHOD(emplace)(TYPENAME *self) {
  if (self->len >= self->cap) {
    self->cap = MAX(8, self->cap * 2);
    self->data = GROW(T, self->data, self->cap);
  }
  return self->data + (self->len++);
}

LINKAGE T METHOD(del)(TYPENAME *self, size_t index) {
  assert(index < self->len);
  if (index + 1 == self->len) {
    return METHOD(pop)(self);
  }
  T removed = METHOD(val_at)(self, index);
  memcpy(self->data + index, self->data + index + 1, self->len-- - index);
  return removed;
}

#undef T
#undef TYPENAME
#undef NAME
#undef GROW
#undef MAX
#undef PREFIX
#undef LINKAGE
#undef METHOD
#undef CONCAT
#undef CONCAT_

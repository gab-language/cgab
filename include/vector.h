#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#ifndef T
#error Define a type T before including this header
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

#ifdef V_CONCURRENT
#include <threads.h>
#define INIT_LOCK(self) (mtx_init(&self->mtx, mtx_plain))
#define DESTROY_LOCK(self) (mtx_destroy(&self->mtx))
#define AQUIRE_LOCK(self) (mtx_lock(&self->mtx))
#define RELEASE_LOCK(self) (mtx_unlock(&self->mtx))
#else
#define INIT_LOCK(self)
#define DESTROY_LOCK(self)
#define AQUIRE_LOCK(self)
#define RELEASE_LOCK(self)
#endif

typedef struct TYPENAME TYPENAME;
struct TYPENAME {
  T *data;
  size_t len;
  size_t cap;
#ifdef V_CONCURRENT
  mtx_t mtx;
#endif
};

LINKAGE void METHOD(create)(TYPENAME *self, uint64_t cap) {
  self->cap = cap;
  self->len = 0;
  self->data = malloc(sizeof(T) * cap);
  INIT_LOCK(self);
}

LINKAGE void METHOD(copy)(TYPENAME *self, TYPENAME *other) {
  self->cap = other->cap;
  self->len = other->len;
  self->data = malloc(sizeof(T) * other->cap);
  memcpy(self->data, other->data, other->len * sizeof(T));
  INIT_LOCK(self);
}

LINKAGE void METHOD(destroy)(TYPENAME *self) {
  AQUIRE_LOCK(self);
  free(self->data);
  self->data = nullptr;
  self->cap = 0;
  self->len = 0;
  RELEASE_LOCK(self);
  DESTROY_LOCK(self);
}

LINKAGE size_t METHOD(set)(TYPENAME *self, size_t index, T value) {
  AQUIRE_LOCK(self);

  assert(index < self->len);
  self->data[index] = value;

  RELEASE_LOCK(self);
  return index;
}

LINKAGE size_t METHOD(push)(TYPENAME *self, T value) {
  AQUIRE_LOCK(self);

  if (self->len >= self->cap) {
    self->cap = MAX(8, self->cap * 2);
    self->data = GROW(T, self->data, self->cap);
  }

  RELEASE_LOCK(self);
  return METHOD(set)(self, self->len++, value);
}

LINKAGE T METHOD(pop)(TYPENAME *self) {
  AQUIRE_LOCK(self);

  assert(self->len > 0);
  T v = self->data[--self->len];

  RELEASE_LOCK(self);
  return v;
}

#ifndef V_CONCURRENT
LINKAGE T *METHOD(ref_at)(TYPENAME *self, size_t index) {
  AQUIRE_LOCK(self);

  assert(index < self->len);
  T * ref =  self->data + index;

  RELEASE_LOCK(self);
  return ref;
}
#endif

LINKAGE T METHOD(val_at)(TYPENAME *self, size_t index) {
  AQUIRE_LOCK(self);

  assert(index < self->len);

  T v = self->data[index];

  RELEASE_LOCK(self);
  return v;
}

LINKAGE void METHOD(cap)(TYPENAME *self, size_t cap) {
  AQUIRE_LOCK(self);

  if (self->cap < cap) {
    self->data = GROW(T, self->data, MAX(8, cap));
    self->cap = cap;
  }

  RELEASE_LOCK(self);
}

LINKAGE T *METHOD(emplace)(TYPENAME *self) {
  AQUIRE_LOCK(self);

  if (self->len >= self->cap) {
    self->cap = MAX(8, self->cap * 2);
    self->data = GROW(T, self->data, self->cap);
  }

  T* ref =  self->data + (self->len++);

  RELEASE_LOCK(self);
  return ref;
}

LINKAGE T METHOD(del)(TYPENAME *self, size_t index) {
  AQUIRE_LOCK(self);

  assert(index < self->len);
  if (index + 1 == self->len) {
    return METHOD(pop)(self);
  }

  T removed = METHOD(val_at)(self, index);
  memcpy(self->data + index, self->data + index + 1, self->len-- - index);

  RELEASE_LOCK(self);
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
#undef INIT_LOCK
#undef DESTROY_LOCK
#undef AQUIRE_LOCK
#undef RELEASE_LOCK

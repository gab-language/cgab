#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifndef T
#error "Define a type T before including this header"
#endif

#define CONCAT(a, b) CONCAT_(a, b)
#define CONCAT_(a, b) a##b

#ifndef NAME
#define TYPENAME CONCAT(q_, T)
#else
#define TYPENAME CONCAT(q_, NAME)
#endif

#ifndef SIZE
#define SIZE 128
#endif

#define MASK (SIZE - 1)

#ifndef DEF_T
#define DEF_T 0
#endif

#define PREFIX TYPENAME
#define LINKAGE static inline
#define METHOD(name) CONCAT(PREFIX, CONCAT(_, name))

typedef struct TYPENAME TYPENAME;
struct TYPENAME {
  T data[SIZE];
  uint64_t head, tail, size;
};

LINKAGE void METHOD(create)(TYPENAME *self) {
  self->head = 0;
  self->tail = 0;
  self->size = SIZE;
}

LINKAGE bool METHOD(is_empty)(TYPENAME *self) {
  return self->head == self->tail;
}

LINKAGE bool METHOD(is_full)(TYPENAME *self) {
  return self->tail - self->head >= SIZE;
}

LINKAGE bool METHOD(push)(TYPENAME *self, T value) {
  if (METHOD(is_full)(self))
    return false;

  self->data[self->tail++ & MASK] = value;

  return true;
}

LINKAGE T METHOD(peek)(TYPENAME *self) {
  if (METHOD(is_empty)(self))
    return DEF_T;

  T value = self->data[self->head & MASK];

  return value;
}

LINKAGE T METHOD(pop)(TYPENAME *self) {
  if (METHOD(is_empty)(self))
    return DEF_T;

  T value = self->data[self->head++ & MASK];

  return value;
}

#undef T
#undef SIZE
#undef MASK
#undef TYPENAME
#undef NAME
#undef GROW
#undef MAX
#undef PREFIX
#undef LINKAGE
#undef METHOD
#undef CONCAT
#undef CONCAT_

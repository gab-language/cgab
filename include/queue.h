#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

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
  self->head = -1;
  self->tail = -1;
  self->size = SIZE;
}

LINKAGE bool METHOD(is_empty)(TYPENAME *self) { return self->head == -1; }

LINKAGE bool METHOD(is_full)(TYPENAME *self) {
  if (self->head == 0 && self->tail == SIZE - 1)
    return true;

  if ((self->tail + 1) % SIZE == self->head)
    return true;

  return false;
}

LINKAGE bool METHOD(push)(TYPENAME *self, T value) {
  if (METHOD(is_full)(self))
    return false;

  self->tail++;

  if (self->tail == SIZE)
    self->tail = 0;

  if (self->head == -1)
    self->head = 0;

  self->data[self->tail] = value;

  return true;
}

LINKAGE T METHOD(peek)(TYPENAME *self) {
  if (METHOD(is_empty)(self))
    return DEF_T;

  T value = self->data[self->head];

  return value;
}

LINKAGE T METHOD(pop)(TYPENAME *self) {
  if (METHOD(is_empty)(self))
    return DEF_T;

  T value = self->data[self->head];

  if (self->head == self->tail) {
    self->head = -1;
    self->tail = -1;
  } else {
    if (self->head == SIZE)
      self->head = 0;
    else
      self->head++;
  }

  return value;
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

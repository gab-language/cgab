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

#define MASK(q) (q->cap - 1)

#ifndef DEF_T
#define DEF_T 0
#endif

#define PREFIX TYPENAME
#define LINKAGE static inline
#define METHOD(name) CONCAT(PREFIX, CONCAT(_, name))

typedef struct TYPENAME TYPENAME;
struct TYPENAME {
  T *data;
  uint32_t head, tail, cap;
};

LINKAGE uint64_t METHOD(len)(TYPENAME *self) { return self->tail - self->head; }

LINKAGE void METHOD(cap)(TYPENAME *self, uint32_t cap) {
  assert((cap > 0) && ((cap & (cap - 1)) == 0));

  T *newdata = (T*)malloc(sizeof(T) * cap);

  uint32_t len = METHOD(len)(self);

  for (int i = 0; i < len; i++)
    newdata[i] = self->data[(self->head + i) & MASK(self)];

  if (self->data)
    free(self->data);
  self->data = newdata;
  self->cap = cap;
  self->head = 0;
  self->tail = len;
}

LINKAGE void METHOD(create)(TYPENAME *self, uint32_t cap) {
  memset(self, 0, sizeof(*self));
  METHOD(cap)(self, cap);
}

LINKAGE bool METHOD(is_empty)(TYPENAME *self) {
  return self->head == self->tail;
}

LINKAGE bool METHOD(is_full)(TYPENAME *self) {
  return METHOD(len)(self) == self->cap;
}

LINKAGE bool METHOD(push)(TYPENAME *self, T value) {
  if (METHOD(is_full)(self))
    METHOD(cap)(self, self->cap * 2);

  self->data[self->tail++ & MASK(self)] = value;

  return true;
}

LINKAGE T METHOD(peek)(TYPENAME *self) {
  if (METHOD(is_empty)(self))
    return DEF_T;

  T value = self->data[self->head & MASK(self)];

  return value;
}

LINKAGE T METHOD(pop)(TYPENAME *self) {
  if (METHOD(is_empty)(self))
    return DEF_T;

  T value = self->data[self->head++ & MASK(self)];

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

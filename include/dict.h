#include <assert.h>
#include <stdbool.h>
#include <string.h>

#ifndef K
#error "Define a type K before including this header"
#endif

#ifndef HASH
#error "Define a hash function HASH before including this header"
#endif

#ifndef EQUAL
#error "Define an equal function EQUAL before including this header"
#endif

#ifndef V
#define V uint8_t
#define DEF_V 0
#else
#ifndef DEF_V
#error "Define a default DEF_V for V before including this header"
#endif
#endif

#ifndef NAME
#define NAME K
#endif

#ifndef LOAD
#define LOAD 0.6
#endif

#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define CONCAT(a, b) CONCAT_(a, b)
#define CONCAT_(a, b) a##b

#define TYPENAME CONCAT(d_, NAME)
#define BUCKET_T CONCAT(TYPENAME, _BUCKET)
#define PREFIX TYPENAME
#define LINKAGE static inline
#define METHOD(name) CONCAT(PREFIX, CONCAT(_, name))

#ifndef CORE_DICT_H
#define CORE_DICT_H

typedef enum d_status {
  D_EMPTY = 0,
  D_FULL = 1,
  D_TOMBSTONE = 2,
} d_status;

#endif

typedef struct BUCKET_T BUCKET_T;
struct BUCKET_T {
  K key;
  V val;
  d_status status;
};

typedef struct TYPENAME TYPENAME;
struct TYPENAME {
  size_t len, cap;
  BUCKET_T *buckets;
};

LINKAGE K METHOD(ikey)(TYPENAME *self, size_t index) {
  return self->buckets[index].key;
}

LINKAGE V METHOD(ival)(TYPENAME *self, size_t index) {
  return self->buckets[index].val;
}

LINKAGE void METHOD(iset_key)(TYPENAME *self, size_t index, K key) {
  assert(index < self->cap);
  self->buckets[index].key = key;
}

LINKAGE void METHOD(iset_val)(TYPENAME *self, size_t index, V val) {
  assert(index < self->cap);
  self->buckets[index].val = val;
}

LINKAGE bool METHOD(iexists)(TYPENAME *self, size_t index) {
  return self->buckets[index].status == D_FULL;
}

LINKAGE d_status METHOD(istatus)(TYPENAME *self, size_t index) {
  return self->buckets[index].status;
}

LINKAGE bool METHOD(iremove)(TYPENAME *self, size_t index) {
  if (self->len == 0)
    return false;

  BUCKET_T *bucket = self->buckets + index;
  if (bucket->status != D_FULL)
    return false;

  bucket->status = D_TOMBSTONE;
  return true;
}

LINKAGE size_t METHOD(inext)(TYPENAME *self, size_t index) {
  if (index >= self->cap)
    return -1;

  while (!METHOD(iexists)(self, index)) {
    index++;
    if (index == self->cap)
      return -1;
  }

  return index;
}

LINKAGE void METHOD(create)(TYPENAME *self, size_t cap) {
  assert(cap % 2 == 0);

  self->buckets = (BUCKET_T *)malloc(sizeof(BUCKET_T) * cap);
  memset(self->buckets, 0, cap * sizeof(BUCKET_T));

  self->cap = cap;
  self->len = 0;
}

LINKAGE void METHOD(destroy)(TYPENAME *self) { free(self->buckets); }

LINKAGE size_t METHOD(index_of)(TYPENAME *self, K key) {
  size_t index = HASH(key) & (self->cap - 1);
  int64_t tombstone = -1;

  for (;;) {
    BUCKET_T *bucket = self->buckets + index;
    K test_key = bucket->key;

    switch (bucket->status) {
    case D_TOMBSTONE:
      if (tombstone == -1)
        tombstone = index;
      break;
    case D_EMPTY:
      return tombstone == -1 ? index : tombstone;
    case D_FULL:
      if (EQUAL(test_key, key))
        return index;
    }

    index = (index + 1) & (self->cap - 1);
  }
}

LINKAGE void METHOD(cap)(TYPENAME *self, size_t cap) {
  TYPENAME other;
  METHOD(create)(&other, cap);

  self->len = 0;
  for (size_t i = 0; i < self->cap; i++) {
    BUCKET_T *bucket = self->buckets + i;

    if (!(bucket->status == D_FULL))
      continue;

    size_t index = METHOD(index_of)(&other, bucket->key);
    BUCKET_T *other_bucket = other.buckets + index;

    other_bucket->key = bucket->key;
    other_bucket->val = bucket->val;
    other_bucket->status = bucket->status;

    self->len++;
  }

  free(self->buckets);
  self->buckets = other.buckets;
  self->cap = cap;
}

LINKAGE bool METHOD(insert)(TYPENAME *self, K key, V val) {
  if (self->len >= (self->cap * LOAD))
    METHOD(cap)(self, MAX(self->cap * 2, 8));

  size_t index = METHOD(index_of)(self, key);

  BUCKET_T *bucket = self->buckets + index;

  bool is_new = bucket->status == D_EMPTY;

  if (is_new)
    self->len++;

  bucket->key = key;
  bucket->val = val;
  bucket->status = D_FULL;

  return is_new;
}

LINKAGE bool METHOD(remove)(TYPENAME *self, K key) {
  if (self->len == 0)
    return false;

  size_t index = METHOD(index_of)(self, key);

  BUCKET_T *bucket = self->buckets + index;

  if (bucket->status != D_FULL)
    return false;

  bucket->status = D_TOMBSTONE;
  return true;
}

LINKAGE bool METHOD(exists)(TYPENAME *self, K key) {
  if (self->len == 0)
    return false;

  size_t index = METHOD(index_of)(self, key);

  BUCKET_T *bucket = self->buckets + index;

  return bucket->status == D_FULL;
}

LINKAGE V METHOD(read)(TYPENAME *self, K key) {
  if (self->len == 0)
    return DEF_V;

  size_t index = METHOD(index_of)(self, key);

  BUCKET_T *bucket = self->buckets + index;

  return bucket->status == D_FULL ? bucket->val : DEF_V;
}

#undef NAME
#undef K
#undef V
#undef DEF_V
#undef DEF_K
#undef HASH
#undef LOAD
#undef EQUAL
#undef TYPENAME
#undef PREFIX
#undef LINKAGE
#undef METHOD
#undef CONCAT
#undef CONCAT_
#undef MAX

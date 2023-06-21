#ifndef GAB_OBJECT_H
#define GAB_OBJECT_H

#include "gc.h"
#include "value.h"
#include <stdio.h>

typedef struct gab_module gab_module;
typedef struct gab_engine gab_engine;

typedef struct gab_obj gab_obj;

typedef struct gab_obj_string gab_obj_string;
typedef struct gab_obj_prototype gab_obj_prototype;
typedef struct gab_obj_builtin gab_obj_builtin;
typedef struct gab_obj_block gab_obj_block;
typedef struct gab_obj_message gab_obj_message;
typedef struct gab_obj_shape gab_obj_shape;
typedef struct gab_obj_record gab_obj_record;
typedef struct gab_obj_container gab_obj_container;
typedef struct gab_obj_suspense gab_obj_suspense;

#define T gab_value
#include "include/array.h"

typedef enum gab_kind {
  kGAB_SUSPENSE,
  kGAB_STRING,
  kGAB_MESSAGE,
  kGAB_PROTOTYPE,
  kGAB_BUILTIN,
  kGAB_BLOCK,
  kGAB_CONTAINER,
  kGAB_RECORD,
  kGAB_SHAPE,
  kGAB_NIL,
  kGAB_UNDEFINED,
  kGAB_NUMBER,
  kGAB_BOOLEAN,
  kGAB_PRIMITIVE,
  kGAB_NKINDS,
} gab_kind;

/*
  This header appears at the front of all gab_obj_kind structs.

  This allows a kind of polymorphism, casting between a
  gab_obj* and the kind of pointer denoted by gab_obj->kind

  I need some sort of prototype. A pointer to a dict that
  objects can share.
*/
struct gab_obj {
  gab_obj *next;
  i32 references;
  gab_kind kind;
  u8 flags;
};

void *gab_obj_alloc(gab_engine *gab, gab_obj *loc, u64 size);

/*
  These macros are used to toggle flags in objects for RC garbage collection.

  Sol uses a pure RC garbage collection approach.

  The algorithm is described in this paper:
  https://researcher.watson.ibm.com/researcher/files/us-bacon/Bacon03Pure.pdf
*/
#define fGAB_OBJ_BUFFERED (1 << 0)
#define fGAB_OBJ_BLACK (1 << 1)
#define fGAB_OBJ_GRAY (1 << 2)
#define fGAB_OBJ_WHITE (1 << 3)
#define fGAB_OBJ_PURPLE (1 << 4)
#define fGAB_OBJ_GREEN (1 << 5)
#define fGAB_OBJ_GARBAGE (1 << 6)

#define GAB_OBJ_IS_BUFFERED(obj) ((obj)->flags & fGAB_OBJ_BUFFERED)
#define GAB_OBJ_IS_BLACK(obj) ((obj)->flags & fGAB_OBJ_BLACK)
#define GAB_OBJ_IS_GRAY(obj) ((obj)->flags & fGAB_OBJ_GRAY)
#define GAB_OBJ_IS_WHITE(obj) ((obj)->flags & fGAB_OBJ_WHITE)
#define GAB_OBJ_IS_PURPLE(obj) ((obj)->flags & fGAB_OBJ_PURPLE)
#define GAB_OBJ_IS_GREEN(obj) ((obj)->flags & fGAB_OBJ_GREEN)
#define GAB_OBJ_IS_GARBAGE(obj) ((obj)->flags & fGAB_OBJ_GARBAGE)

#define GAB_OBJ_BUFFERED(obj) ((obj)->flags |= fGAB_OBJ_BUFFERED)
#define GAB_OBJ_NOT_BUFFERED(obj) ((obj)->flags &= ~fGAB_OBJ_BUFFERED)

#define GAB_OBJ_GARBAGE(obj) ((obj)->flags |= fGAB_OBJ_GARBAGE)

#define GAB_OBJ_GREEN(obj)                                                     \
  ((obj)->flags = ((obj)->flags & fGAB_OBJ_BUFFERED) | fGAB_OBJ_GREEN)
#define GAB_OBJ_BLACK(obj)                                                     \
  ((obj)->flags = ((obj)->flags & fGAB_OBJ_BUFFERED) | fGAB_OBJ_BLACK)
#define GAB_OBJ_GRAY(obj)                                                      \
  ((obj)->flags = ((obj)->flags & fGAB_OBJ_BUFFERED) | fGAB_OBJ_GRAY)
#define GAB_OBJ_WHITE(obj)                                                     \
  ((obj)->flags = ((obj)->flags & fGAB_OBJ_BUFFERED) | fGAB_OBJ_WHITE)
#define GAB_OBJ_PURPLE(obj)                                                    \
  ((obj)->flags = ((obj)->flags & fGAB_OBJ_BUFFERED) | fGAB_OBJ_PURPLE)

void gab_obj_destroy(gab_obj *self);

u64 gab_obj_size(gab_obj *self);

static inline boolean gab_val_is_obj_kind(gab_value self, gab_kind k) {
  return GAB_VAL_IS_OBJ(self) && GAB_VAL_TO_OBJ(self)->kind == k;
}

/*
  ------------- OBJ_STRING -------------
  A sequence of chars. Interned by the module.
*/
struct gab_obj_string {
  gab_obj header;

  u64 hash;

  u64 len;

  i8 data[FLEXIBLE_ARRAY];
};
#define GAB_VAL_IS_STRING(value) (gab_val_is_obj_kind(value, kGAB_STRING))
#define GAB_VAL_TO_STRING(value) ((gab_obj_string *)GAB_VAL_TO_OBJ(value))
#define GAB_OBJ_TO_STRING(value) ((gab_obj_string *)value)

gab_obj_string *gab_obj_string_create(gab_engine *gab, s_i8 data);

gab_obj_string *gab_obj_string_concat(gab_engine *gab, gab_obj_string *a,
                                      gab_obj_string *b);

s_i8 gab_obj_string_ref(gab_obj_string *self);

/*
  ------------- OBJ_BUILTIN-------------
  A function pointer. to a native c function.
*/
typedef void (*gab_builtin)(gab_engine *gab, gab_vm *vm, u8 argc,
                            gab_value argv[argc]);
struct gab_obj_builtin {
  gab_obj header;

  gab_builtin function;

  gab_value name;
};

#define GAB_VAL_IS_BUILTIN(value) (gab_val_is_obj_kind(value, kGAB_BUILTIN))
#define GAB_VAL_TO_BUILTIN(value) ((gab_obj_builtin *)GAB_VAL_TO_OBJ(value))
#define GAB_OBJ_TO_BUILTIN(value) ((gab_obj_builtin *)value)

gab_obj_builtin *gab_obj_builtin_create(gab_engine *gab, gab_builtin function,
                                        gab_value name);

/*
  ------------- OBJ_PROTOTYPE -------------
*/
struct gab_obj_prototype {
  gab_obj header;

  u8 narguments;

  u8 nupvalues;

  u8 nslots;

  u8 nlocals;

  u8 var;

  gab_module *mod;

  u8 upv_desc[FLEXIBLE_ARRAY];
};

#define GAB_VAL_IS_PROTOTYPE(value) (gab_val_is_obj_kind(value, kGAB_PROTOTYPE))
#define GAB_VAL_TO_PROTOTYPE(value) ((gab_obj_prototype *)GAB_VAL_TO_OBJ(value))
#define GAB_OBJ_TO_PROTOTYPE(value) ((gab_obj_prototype *)value)

gab_obj_prototype *gab_obj_prototype_create(gab_engine *gab, gab_module *mod,
                                            u8 narguments, u8 nslots,
                                            u8 nlocals, u8 nupvalues,
                                            boolean var, u8 flags[nupvalues],
                                            u8 indexes[nupvalues]);

/*
  ------------- OBJ_CLOSURE-------------
  The wrapper to OBJ_FUNCTION, which is actually called at runtime.
*/
struct gab_obj_block {
  gab_obj header;

  u8 nupvalues;

  gab_obj_prototype *p;

  /*
   * The array of captured upvalues
   */
  gab_value upvalues[FLEXIBLE_ARRAY];
};

#define GAB_VAL_IS_BLOCK(value) (gab_val_is_obj_kind(value, kGAB_BLOCK))
#define GAB_VAL_TO_BLOCK(value) ((gab_obj_block *)GAB_VAL_TO_OBJ(value))
#define GAB_OBJ_TO_BLOCK(value) ((gab_obj_block *)value)

gab_obj_block *gab_obj_block_create(gab_engine *gab, gab_obj_prototype *p);

/*
 *------------- OBJ_MESSAGE -------------
 */

#define NAME specs
#define K gab_value
#define V gab_value
#define DEF_V GAB_VAL_UNDEFINED()
#define HASH(a) (a)
#define EQUAL(a, b) (a == b)
#include "dict.h"

#define NAME helps
#define K gab_value
#define V s_i8
#define DEF_V ((s_i8){0})
#define HASH(a) (a)
#define EQUAL(a, b) (a == b)
#include "dict.h"

struct gab_obj_message {
  gab_obj header;

  u8 version;

  gab_value name;

  u64 hash;

  d_specs specs;
};

#define GAB_VAL_IS_MESSAGE(value) (gab_val_is_obj_kind(value, kGAB_MESSAGE))
#define GAB_VAL_TO_MESSAGE(value) ((gab_obj_message *)GAB_VAL_TO_OBJ(value))
#define GAB_OBJ_TO_MESSAGE(value) ((gab_obj_message *)value)

gab_obj_message *gab_obj_message_create(gab_engine *gab, gab_value name);

static inline u64 gab_obj_message_find(gab_obj_message *self,
                                       gab_value receiver) {
  if (!d_specs_exists(&self->specs, receiver))
    return UINT64_MAX;

  return d_specs_index_of(&self->specs, receiver);
}

static inline void gab_obj_message_set(gab_obj_message *self, u64 offset,
                                       gab_value rec, gab_value spec) {
  d_specs_iset_key(&self->specs, offset, rec);
  d_specs_iset_val(&self->specs, offset, spec);
  self->version++;
}

static inline gab_value gab_obj_message_get(gab_obj_message *self, u64 offset) {
  return d_specs_ival(&self->specs, offset);
}

static inline void gab_obj_message_insert(gab_obj_message *self,
                                          gab_value receiver,
                                          gab_value specialization) {
  d_specs_insert(&self->specs, receiver, specialization);
  self->version++;
}

static inline gab_value gab_obj_message_read(gab_obj_message *self,
                                             gab_value receiver) {
  return d_specs_read(&self->specs, receiver);
}

/*
  ------------- OBJ_SHAPE-------------
  A javascript object, or a python dictionary, or a lua table.
  Known by many names.
*/
struct gab_obj_shape {
  gab_obj header;

  u64 hash;

  u64 len;

  gab_value data[FLEXIBLE_ARRAY];
};

#define GAB_VAL_IS_SHAPE(value) (gab_val_is_obj_kind(value, kGAB_SHAPE))
#define GAB_VAL_TO_SHAPE(value) ((gab_obj_shape *)GAB_VAL_TO_OBJ(value))
#define GAB_OBJ_TO_SHAPE(value) ((gab_obj_shape *)value)

gab_obj_shape *gab_obj_shape_create(gab_engine *gab, gab_vm *vm, u64 len,
                                    u64 stride, gab_value key[len]);

gab_obj_shape *gab_obj_shape_create_tuple(gab_engine *gab, gab_vm *vm, u64 len);

static inline u16 gab_obj_shape_find(gab_obj_shape *self, gab_value key) {

  for (u64 i = 0; i < self->len; i++) {
    assert(i < UINT16_MAX);

    if (self->data[i] == key)
      return i;
  }

  return UINT16_MAX;
};

static inline u16 gab_obj_shape_next(gab_obj_shape *self, gab_value key) {
  if (GAB_VAL_IS_UNDEFINED(key))
    return 0;

  u16 offset = gab_obj_shape_find(self, key);

  if (offset == UINT16_MAX)
    return UINT16_MAX;

  return offset + 1;
};

/*
 *------------- OBJ_RECORD -------------
 */
struct gab_obj_record {
  gab_obj header;
  /*
    The shape of this object.
  */
  gab_obj_shape *shape;

  u64 len;

  gab_value data[FLEXIBLE_ARRAY];
};

#define GAB_VAL_IS_RECORD(value) (gab_val_is_obj_kind(value, kGAB_RECORD))
#define GAB_VAL_TO_RECORD(value) ((gab_obj_record *)GAB_VAL_TO_OBJ(value))
#define GAB_OBJ_TO_RECORD(value) ((gab_obj_record *)value)

gab_obj_record *gab_obj_record_create(gab_engine *gab, gab_vm *vm,
                                      gab_obj_shape *shape, u64 stride,
                                      gab_value values[]);

gab_obj_record *gab_obj_record_create_empty(gab_engine *gab,
                                            gab_obj_shape *shape);

void gab_obj_record_set(gab_engine *gab, gab_vm *vm, gab_obj_record *self,
                        u16 offset, gab_value value);

gab_value gab_obj_record_get(gab_obj_record *self, u16 offset);

boolean gab_obj_record_put(gab_engine *gab, gab_vm *vm, gab_obj_record *self,
                           gab_value key, gab_value value);

gab_value gab_obj_record_at(gab_obj_record *self, gab_value prop);

boolean gab_obj_record_has(gab_obj_record *self, gab_value prop);

/*
  ------------- OBJ_CONTAINER-------------
  A container to some unknown data.
*/
typedef void (*gab_obj_container_destructor)(void *data);
typedef void (*gab_obj_container_visitor)(gab_gc *gc, gab_gc_visitor visitor,
                                          void *data);

struct gab_obj_container {
  gab_obj header;

  gab_obj_container_destructor do_destroy;

  gab_obj_container_visitor do_visit;

  gab_value type;

  void *data;
};

#define GAB_VAL_IS_CONTAINER(value) (gab_val_is_obj_kind(value, kGAB_CONTAINER))
#define GAB_VAL_TO_CONTAINER(value) ((gab_obj_container *)GAB_VAL_TO_OBJ(value))
#define GAB_OBJ_TO_CONTAINER(value) ((gab_obj_container *)value)

gab_obj_container *
gab_obj_container_create(gab_engine *gab, gab_vm *vm, gab_value type,
                         gab_obj_container_destructor destructor,
                         gab_obj_container_visitor visitor, void *data);

/*
  ------------- OBJ_SUSPENSE -------------
  A suspended call that can be handled.
*/
struct gab_obj_suspense {
  gab_obj header;

  // The number of arguments yielded
  u8 have;

  // The number of values wanted
  u8 want;

  // Size of the stack frame
  u8 len;

  // Closure
  gab_obj_block *c;

  // Instruction Pointer
  u64 offset;

  // Stack frame
  gab_value frame[FLEXIBLE_ARRAY];
};

#define GAB_VAL_IS_SUSPENSE(value) (gab_val_is_obj_kind(value, kGAB_SUSPENSE))
#define GAB_VAL_TO_SUSPENSE(value) ((gab_obj_suspense *)GAB_VAL_TO_OBJ(value))
#define GAB_OBJ_TO_SUSPENSE(value) ((gab_obj_suspense *)value)

gab_obj_suspense *gab_obj_suspense_create(gab_engine *gab, gab_vm *vm,
                                          gab_obj_block *c, u64 offset,
                                          u8 arity, u8 want, u8 len,
                                          gab_value frame[len]);

#endif

#ifndef gab_mod_H
#define gab_mod_H
#include "include/gab.h"
#include "lexer.h"

#define NAME gab_constant
#define T gab_value
#include "include/vector.h"

struct gab_mod {
  struct gab_mod *next;

  struct gab_src *source;

  /*
   * The name of the module
   **/
  gab_value name;

  /*
    The constant table.
  */
  v_gab_constant constants;

  /*
    The instructions, a contiguous vector of single-byte op-codes and args.
  */
  v_uint8_t bytecode;

  /*
    A sister vector to 'bytecode'.
    This vector relates each instruction to a token offset.
  */
  v_uint64_t bytecode_toks;

  uint8_t previous_compiled_op;
};

/*
  Creating and destroying modules, from nothing and from a base module.
*/
struct gab_mod *gab_mod(struct gab_eg *gab, gab_value name,
                        struct gab_src *src);

void gab_moddestroy(struct gab_triple gab, struct gab_mod *mod);

struct gab_mod *gab_modcpy(struct gab_eg *gab, struct gab_mod *self);

/*
  Helpers for pushing ops into the module.
*/
void gab_mod_push_op(struct gab_mod *, gab_opcode, size_t);

void gab_mod_push_byte(struct gab_mod *, uint8_t, size_t);

void gab_mod_push_short(struct gab_mod *, uint16_t, size_t);

void gab_mod_push_load_local(struct gab_mod *, uint8_t, size_t);

void gab_mod_push_store_local(struct gab_mod *, uint8_t, size_t);

void gab_mod_push_load_upvalue(struct gab_mod *, uint8_t, size_t);

void gab_mod_push_return(struct gab_mod *, uint8_t, bool mv, size_t);

void gab_mod_push_tuple(struct gab_mod *, uint8_t, bool mv, size_t);

void gab_mod_push_yield(struct gab_mod *, uint16_t proto, uint8_t have, bool mv,
                        size_t);

void gab_mod_push_pack(struct gab_mod *self, uint8_t below, uint8_t above,
                       size_t);

void gab_mod_push_send(struct gab_mod *mod, uint8_t have, uint16_t message,
                       bool mv, size_t);

void gab_mod_push_pop(struct gab_mod *, uint8_t, size_t);

void gab_mod_push_inline_cache(struct gab_mod *, size_t);

uint64_t gab_mod_push_iter(struct gab_mod *self, uint8_t start, uint8_t want,
                           size_t);

void gab_mod_push_next(struct gab_mod *self, uint8_t local, size_t);

uint64_t gab_mod_push_loop(struct gab_mod *gab);

uint64_t gab_mod_push_jump(struct gab_mod *gab, uint8_t, size_t);

void gab_mod_patch_jump(struct gab_mod *, uint64_t);

void gab_mod_patch_loop(struct gab_mod *, uint64_t, size_t);

bool gab_mod_try_patch_mv(struct gab_mod *, uint8_t);

uint16_t gab_mod_add_constant(struct gab_mod *, gab_value);

void gab_mod_dump(struct gab_mod *, s_char);
#endif

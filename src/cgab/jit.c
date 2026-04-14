#include "core.h"
#include "engine.h"
#include "gab.h"
#include <stddef.h>
#include <stdint.h>

struct gab_jit_reloc {
  enum gab_jit_reloc_k {
    kGAB_JIT_RELOC_CONST_ABSOLUTE,
    kGAB_JIT_RELOC_CONST_RELATIVE,
    kGAB_JIT_RELOC_32ARG1,
    kGAB_JIT_RELOC_32ARG2,
    kGAB_JIT_RELOC_64ARG1,
    kGAB_JIT_RELOC_64ARG2,
    kGAB_JIT_RELOC_TRMP,
    kGAB_JIT_RELOC_NEXT,
    kGAB_JIT_RELOC_EXIT,
    kGAB_JIT_RELOC_BAIL,
    kGAB_JIT_RELOC_IP,
    kGAB_JIT_RELOC_HV,
  } k;
  uint64_t offset;
  int64_t addend;
  union {
    struct {
      const char *symbol;
    } trampoline;

    struct {
      uint8_t len;
      unsigned char data[256];
    } constant;
  } as;
};

#include "stencil.h"

static const struct gab_jit_reloc *stencil_relocas[] = {
#define IR_CODE(ir) OP_MICRO_OP_##ir##_HANDLER_RELAS,
#include "ir.h"
#undef IR_CODE
};

static const uint64_t stencil_nrelocas[] = {
#define IR_CODE(ir)                                                            \
  sizeof(OP_MICRO_OP_##ir##_HANDLER_RELAS) /                                   \
      sizeof(OP_MICRO_OP_##ir##_HANDLER_RELAS[0]),
#include "ir.h"
#undef IR_CODE
};

static const uint8_t *stencil_bytes[] = {
#define IR_CODE(ir) OP_MICRO_OP_##ir##_HANDLER_BYTES,
#include "ir.h"
#undef IR_CODE
};

static const uint64_t stencil_bytesizes[] = {
#define IR_CODE(ir) sizeof(OP_MICRO_OP_##ir##_HANDLER_BYTES),
#include "ir.h"
#undef IR_CODE
};

#define OP_HANDLER_ARGS                                                        \
  struct gab_triple *__gab, struct gab_jtbb *__bb, uint8_t *__ip,              \
      gab_value *__kb, struct ir *__ir, struct gab_jtframe *__fp

typedef uint8_t *(*handler)(OP_HANDLER_ARGS);

// Forward declare all our opcode handlers
#define OP_CODE(name) uint8_t *OP_GAB_IR_##name##_HANDLER(OP_HANDLER_ARGS);

#include "bytecode.h"

#undef OP_CODE

static handler handlers[] = {

#define OP_CODE(name) OP_GAB_IR_##name##_HANDLER,

#include "bytecode.h"

#undef OP_CODE

};

const char *irnames[] = {
#define IR_CODE(name) #name,
#include "ir.h"
#undef IR_CODE
};

const char *irtypenames[] = {
    [kGAB_IRTYPE_STRING] = "[S]",       [kGAB_IRTYPE_BINARY] = "[X]",
    [kGAB_IRTYPE_MESSAGE] = "[M]",      [kGAB_IRTYPE_PRIMITIVE] = "[P]",
    [kGAB_IRTYPE_NUMBER] = "[N]",       [kGAB_IRTYPE_NATIVE] = "[C]",
    [kGAB_IRTYPE_PROTOTYPE] = "[T]",    [kGAB_IRTYPE_BLOCK] = "[K]",
    [kGAB_IRTYPE_BOX] = "[O]",          [kGAB_IRTYPE_RECORD] = "[R]",
    [kGAB_IRTYPE_RECORDNODE] = "[R]",   [kGAB_IRTYPE_SHAPE] = "[S]",
    [kGAB_IRTYPE_SHAPELIST] = "[L]",    [kGAB_IRTYPE_FIBER] = "[F]",
    [kGAB_IRTYPE_FIBERDONE] = "[F]",    [kGAB_IRTYPE_FIBERRUNNING] = "[F]",
    [kGAB_IRTYPE_CHANNEL] = "[H]",      [kGAB_IRTYPE_CHANNELCLOSED] = "[H]",
    [kGAB_IRTYPE_UNBOXF] = " F ",       [kGAB_IRTYPE_UNBOXI] = " I ",
    [kGAB_IRTYPE_UNBOXU] = " U ",       [kGAB_IRTYPE_UNBOXB] = " B ",
    [kGAB_IRTYPE_UNBOXS] = " S ",       [kGAB_IRTYPE_UNKNOWN] = "   ",
    [kGAB_IRTYPE_MESSAGE_BOOL] = "[B]",
};

bool ir_isk(uint16_t ssa) { return ssa < IR_VBIAS || ssa > IR_OBIAS + IR_SIZE; }
bool ir_isv(uint16_t ssa) { return ssa > IR_VBIAS && ssa < IR_OBIAS; }
bool ir_iso(uint16_t ssa) {
  return ssa >= IR_OBIAS && ssa < IR_OBIAS + IR_SIZE;
}

union gab_jtir ir_geto(struct ir *ir, uint16_t ssa) {
  return ir->ir[ssa - IR_BIAS];
}

union gab_jtir ir_getv(struct ir *ir, uint16_t ssa) {
  return ir->ir[IR_BIAS - (ssa - IR_BIAS)];
}

uint16_t emito(struct ir *ir, uint8_t *ip, uint64_t have, enum gab_irop_kind op,
               uint8_t type, uint16_t a, uint16_t b) {
  uint16_t ssa = ir->os++ + IR_BIAS;
  gab_assert(ssa < IR_SIZE, "Too many instructions in ir");

  gab_assert(ir->sp >= ir->sb, "Stack pointer fell below stack base for %s",
             irnames[op]);

  uint64_t slot = ir->sp - ir->sb;
  gab_assert(slot < UINT16_MAX, "Slot too big, tried %lu for %s", slot,
             irnames[op]);

  gab_assert(have < IR_SIZE, "Have nonsensical");

  ir->ir[ssa].op.kind = op;
  ir->ir[ssa].op.type = type;
  ir->ir[ssa].op.a = a;
  ir->ir[ssa].op.b = b;
  ir->ir[ssa].op.slot = slot;

  ir->ip[ssa - IR_BIAS] = ip;
  ir->hv[ssa - IR_BIAS] = have;

  return ssa + IR_BIAS;
}

uint16_t emitk_v(struct ir *ir, gab_value k) {
  uint32_t ssa = ++ir->ks;
  gab_assert(ssa <= IR_BIAS, "Too many constans in ir");
  ir->ir[IR_BIAS - ssa] = (union gab_jtir){.value = k};
  return ssa + IR_VBIAS;
}

uint16_t emitk_b(struct ir *ir, int64_t bits) {
  gab_assert(bits < IR_BIAS, "Bits '%b' (%li) not representable by immediate",
             bits, bits);

  return bits;
}

uint16_t emito_matchtailcall(struct ir *ir, uint16_t idx, uint64_t have) {
  return 0;
}

uint16_t emito_matchcall(struct ir *ir, uint16_t idx, uint64_t have) {
  return 0;
}

void ir_push(struct ir *ir, uint16_t ssa) {
  gab_assert(ir->sp - ir->sb < IR_SIZE, "Too many refs on stack");

  if (ir_iso(ssa)) {
    uint64_t slot = ir->sp - ir->sb;
    // gab_assert((ssa - IR_BIAS) < IR_SIZE, "Index %lu out of bounds (ssa: %u,
    // size: %u)", slot, ssa, IR_SIZE);
    gab_assert(slot < IR_SIZE, "Slot too big");
    ir->ir[ssa - IR_OBIAS].op.slot += slot;
  }

  uint16_t have = *ir->sp;
  *ir->sp++ = ssa;
  *ir->sp = have + 1;
}

void ir_dropn(struct ir *ir, int64_t n) {
  gab_assert(ir->sp - ir->sb >= n, "Too few refs on stack");
  uint16_t have = *ir->sp;
  gab_assert(have + 1 >= n, "Drop too many from tuple");
  ir->sp -= n;

  if (n < have + 1)
    *ir->sp = have - n;
}

uint16_t ir_pop(struct ir *ir) {
  gab_assert(ir->sp - ir->sb >= 0, "Too few refs on stack");
  uint16_t have = *ir->sp;
  uint16_t ssa = *(--ir->sp);
  *ir->sp = have - 1;
  // fprintf(stderr, "POP %i, %i -> %i\n", ssa, have, *ir->sp);
  return ssa;
}

uint16_t ir_peekn(struct ir *ir, int64_t n) {
  gab_assert((ir->sp - n) >= ir->sb, "Too few refs on stack, tried to peek %li",
             n);
  // fprintf(stderr, "PEEK_%li -> %u\n", n, ir->sp[-n]);
  return *(ir->sp - n);
}

static inline enum gab_irtype_kind instt(struct ir *ir, uint16_t inst) {
  if (ir_isv(inst))
    return (enum gab_irtype_kind)gab_valkind(ir_getv(ir, inst).value);

  if (ir_iso(inst))
    return ir_geto(ir, inst).op.type;

  gab_assert(
      false,
      "Cannot get the runtime type of compile time constant for local %u",
      inst);
  return kGAB_IRTYPE_UNKNOWN;
}

static inline enum gab_irtype_kind localt(struct ir *ir, struct gab_jtframe *fp,
                                          struct gab_jtbb *bb, uint16_t loc) {
  uint16_t inst = fp->fb[loc];

  return instt(ir, inst);
}

// Initialize jit state
void gab_jtcreate(struct gab_jt *jt) {
  d_bb_create(&jt->blocks, 32);

  for (int i = 0; i < HOTCOUNT_SIZE; i++)
    jt->hotcount[i] = HOTCOUNT_WARMUP;
}

void init_type_state_inline(struct gab_triple gab, struct ir *ir,
                            struct gab_jtbb *bb, struct gab_type_state *ts,
                            uint16_t *loc, gab_value *upv,
                            struct gab_oprototype *proto) {
  ts->nlocals = proto->nlocals;
  ts->nupvalues = proto->nupvalues;

  for (int i = 0; i < proto->nlocals; i++) {
    gab_value v = loc[i];

    if (v == gab_cinvalid || v == gab_cundefined)
      ts->slots[i] = kGAB_IRTYPE_UNKNOWN;
    else
      ts->slots[i] = instt(ir, loc[i]);
  }

  for (int i = 0; i < proto->nupvalues; i++) {
    uint8_t slot = ts->nlocals + i;
    gab_value v = upv[slot];

    if (v == gab_cinvalid || v == gab_cundefined)
      ts->slots[slot] = kGAB_IRTYPE_UNKNOWN;
    else
      ts->slots[slot] = (enum gab_irtype_kind)gab_valkind(v);
  }

  for (int i = ts->nlocals + ts->nupvalues; i < GAB_JIT_MAX_LOCALS; i++)
    ts->slots[i] = kGAB_IRTYPE_UNKNOWN;
}

void init_type_state(struct gab_triple gab, struct gab_type_state *ts,
                     gab_value *loc, gab_value *upv,
                     struct gab_oprototype *proto) {
  ts->nlocals = proto->nlocals;
  ts->nupvalues = proto->nupvalues;

  for (int i = 0; i < proto->nlocals; i++) {
    gab_value v = loc[i];

    if (v == gab_cinvalid || v == gab_cundefined)
      ts->slots[i] = kGAB_IRTYPE_UNKNOWN;
    else
      ts->slots[i] = (enum gab_irtype_kind)gab_valkind(v);
  }

  for (int i = 0; i < proto->nupvalues; i++) {
    uint8_t slot = ts->nlocals + i;
    gab_value v = upv[slot];

    if (v == gab_cinvalid || v == gab_cundefined)
      ts->slots[slot] = kGAB_IRTYPE_UNKNOWN;
    else
      ts->slots[slot] = (enum gab_irtype_kind)gab_valkind(v);
  }

  for (int i = ts->nlocals + ts->nupvalues; i < GAB_JIT_MAX_LOCALS; i++)
    ts->slots[i] = kGAB_IRTYPE_UNKNOWN;
}

void tsdump(struct gab_type_state ts) {
  flockfile(stderr);

  fprintf(stderr, "TS %u %u:\n", ts.nlocals, ts.nupvalues);

  for (int i = 0; i < ts.nlocals; i++) {
    fprintf(stderr, "local %i: %s\n", i, irtypenames[ts.slots[i]]);
  }
  for (int i = 0; i < ts.nupvalues; i++) {
    fprintf(stderr, "upvalue %i: %s\n", i,
            irtypenames[ts.slots[ts.nlocals + i]]);
  }

  funlockfile(stderr);
}

static inline enum gab_irtype_kind upvaluet(struct ir *ir, struct gab_jtbb *bb,
                                            uint8_t upv) {
  gab_assert(upv < bb->id.entry_state.nupvalues,
             "upv %i out of upvalue range %i\n", upv,
             bb->id.entry_state.nupvalues);

  gab_assert(bb->id.entry_state.nlocals + upv <
                 bb->id.entry_state.nlocals + bb->id.entry_state.nupvalues,
             "upv %i out of upvalue range %i\n", upv,
             bb->id.entry_state.nupvalues);

  return bb->id.entry_state.slots[bb->id.entry_state.nlocals + upv];
}

void irdumpa(struct ir *ir, uint16_t a) {
  if (ir_isk(a)) {
    fprintf(stderr, GAB_YELLOW "%04i" GAB_RESET, (int16_t)a);
  } else if (ir_isv(a)) {
    gab_value arg = ir->ir[IR_BIAS + IR_VBIAS - a].value;
    if (gab_valkind(arg) != kGAB_NUMBER)
      gab_fprintf(stderr, GAB_CYAN "$" GAB_RESET, arg);
    else
      fprintf(stderr, GAB_RED "%p" GAB_RESET, (void *)arg);
  } else {
    assert(ir_iso(a));
    fprintf(stderr, GAB_GREEN "%%%03i" GAB_RESET, a - IR_OBIAS);
  }
}

void irdump_(struct gab_triple gab, struct ir *ir, uint16_t i,
             union gab_jtir inst) {
  fprintf(stderr,
          "(%i) %s | " GAB_GREEN "%%%04i" GAB_RESET
          " | %-28s | %02u | %02lu |\n",
          gab.wkid, irtypenames[inst.op.type], i, irnames[inst.op.kind],
          inst.op.slot, ir->hv[i]);
}

void irdump_1(struct gab_triple gab, struct ir *ir, uint16_t i,
              union gab_jtir inst) {
  fprintf(stderr,
          "(%i) %s | " GAB_GREEN "%%%04i" GAB_RESET
          " | %-28s | %02u | %02lu | ",
          gab.wkid, irtypenames[inst.op.type], i, irnames[inst.op.kind],
          inst.op.slot, ir->hv[i]);

  irdumpa(ir, inst.op.a);

  fprintf(stderr, "\n");
}

void irdump_2(struct gab_triple gab, struct ir *ir, uint16_t i,
              union gab_jtir inst) {

  fprintf(stderr,
          "(%i) %s | " GAB_GREEN "%%%04i" GAB_RESET
          " | %-28s | %02u | %02lu | ",
          gab.wkid, irtypenames[inst.op.type], i, irnames[inst.op.kind],
          inst.op.slot, ir->hv[i]);

  irdumpa(ir, inst.op.a);

  fprintf(stderr, " ");

  irdumpa(ir, inst.op.b);

  fprintf(stderr, "\n");
}

uint8_t ir_nargs(union gab_jtir inst) {
  switch (inst.op.kind) {
  case kGAB_IR_LOAD_CHANNEL:
  case kGAB_IR_TUPLE:
  case kGAB_IR_ENTER:
  case kGAB_IR_INLINE_RETURN:
    return 0;
  case kGAB_IR_INLINE_LOCALTAILCALL_BLOCK:
  case kGAB_IR_LOCALTAILCALL_BLOCK:
  case kGAB_IR_MATCHTAILCALL_BLOCK:
  case kGAB_IR_DROP_N:
  case kGAB_IR_LOAD_LOCAL:
  case kGAB_IR_LOAD_UPVALUE:
  case kGAB_IR_LOAD_CONSTANT:
  case kGAB_IR_STORE_LOCAL:
  case kGAB_IR_TYPE:
  case kGAB_IR_UNBOXF:
  case kGAB_IR_UNBOXI:
  case kGAB_IR_UNBOXU:
  case kGAB_IR_UNBOXB:
  case kGAB_IR_UNBOXS:
  case kGAB_IR_UNBOXF2:
  case kGAB_IR_UNBOXI2:
  case kGAB_IR_UNBOXU2:
  case kGAB_IR_UNBOXB2:
  case kGAB_IR_UNBOXS2:
  case kGAB_IR_BOXN:
  case kGAB_IR_BOXB:
  case kGAB_IR_GUARD_SPECS:
  case kGAB_IR_GUARD_STACKSPACE:
  case kGAB_IR_GUARD_STACKSPACE_SPLATSHAPE:
  case kGAB_IR_GUARD_NILPAD:
  case kGAB_IR_TRIM:
  case kGAB_IR_CALL_NATIVE:
  case kGAB_IR_SPLAT_SHAPE:
  case kGAB_IR_LOAD_BLOCK:
  case kGAB_IR_LOAD_RECORD:
  case kGAB_IR_LOAD_RECORDFROM:
  case kGAB_IR_LOAD_SHAPE:
  case kGAB_IR_SEND_CONSTANT:
  case kGAB_IR_INLINE_LOCALCALL_BLOCK:
    return 1;
  case kGAB_IR_RETURN:
  case kGAB_IR_LOCALCALL_BLOCK:
  case kGAB_IR_LOAD_LIST:
  case kGAB_IR_JIT_EXIT:
  case kGAB_IR_FADD:
  case kGAB_IR_FSUB:
  case kGAB_IR_FMUL:
  case kGAB_IR_FDIV:
  case kGAB_IR_FLT:
  case kGAB_IR_FGT:
  case kGAB_IR_FLTE:
  case kGAB_IR_FGTE:
  case kGAB_IR_IMOD:
  case kGAB_IR_IBOR:
  case kGAB_IR_IBND:
  case kGAB_IR_ULSH:
  case kGAB_IR_URSH:
  case kGAB_IR_BLIN:
  case kGAB_IR_IBIN:
  case kGAB_IR_STRCONCAT:
  case kGAB_IR_STRLT:
  case kGAB_IR_STRLTE:
  case kGAB_IR_STRGT:
  case kGAB_IR_STRGTE:
  case kGAB_IR_VEQ:
  case kGAB_IR_VCONS:
  case kGAB_IR_VCONS_RECORD:
  case kGAB_IR_PACK_DICT:
  case kGAB_IR_PACK_LIST:
  case kGAB_IR_GUARD_TYPE:
  case kGAB_IR_GUARD_KIND:
  case kGAB_IR_GUARD_MATCH_TYPE:
  case kGAB_IR_GUARD_TRIM_EXACTLY_N:
  case kGAB_IR_GUARD_TRIM_DOWN_N:
  case kGAB_IR_GUARD_TRIM_UP_N:
  case kGAB_IR_UVRECAT:
  case kGAB_IR_UKRECAT:
  case kGAB_IR_SPILL:
    return 2;
  }
}

void irdump(struct gab_triple gab, struct ir *ir) {
  flockfile(stderr);

  for (int i = 0; i < ir->os; i++) {
    union gab_jtir inst = ir->ir[i + IR_BIAS];
    switch (ir_nargs(inst)) {
    case 0:
      irdump_(gab, ir, i, inst);
      break;
    case 1:
      irdump_1(gab, ir, i, inst);
      break;
    case 2:
      irdump_2(gab, ir, i, inst);
      break;
    }
  }

  funlockfile(stderr);
}

static const char *gab_opcode_names[] = {
#define OP_CODE(name) #name,
#include "bytecode.h"
#undef OP_CODE
#undef GAB_OPCODE_NAMES_IMPL
};

/*
 * A *basic block* is composed of two sections - code and data.
 *
 * This mimics an object file.
 *
 *  +-------------------------+ <- code offset (0)
 *  |  stencil bytes          |
 *  +-------------------------+ <- data offset (codesize)
 *  |  < 255 byte constants   |
 *  |  trampoline :symbol1    |
 *  |  trampoline :symbol2    |
 *  +-------------------------+
 */

static inline size_t pad_to_16(size_t num) { return (num + 15) & ~15; }

/*
 * Compute the combined size of the code in all the stencils for ir.
 */
size_t ir_codesize(struct ir *ir) {
  size_t sum = 0;

  for (uint16_t i = 0; i < ir->os; i++) {
    union gab_jtir inst = ir->ir[IR_BIAS + i];
    sum += stencil_bytesizes[inst.op.kind];
    sum = pad_to_16(sum);
  }

  return pad_to_16(sum);
}

static const uint8_t trampoline[] = {
    0x48, 0xb8,                   // movabs rax,
    0,    0,    0, 0, 0, 0, 0, 0, // <64-bit addr>
    0xff, 0xe0,
};

#define GAB_JIT_TRMPSIZE sizeof(trampoline);

void puti(long arg) { fprintf(stderr, "%li\n", arg); }
void putl(long arg) { fprintf(stderr, "%lu\n", arg); }
void putcs(const char *arg) { fprintf(stderr, "%s\n", arg); }
void putp(uintptr_t arg) { fprintf(stderr, "%p\n", (void *)arg); }
void putf(double arg) { fprintf(stderr, "%lf\n", arg); }
void putg(gab_value arg) { gab_fprintf(stderr, "$\n", arg); }
void puta(char arg) { fprintf(stderr, "%c\n", arg); }

void *address_for_symbol(const char *symbol) {
  if (!strcmp(symbol, "memmove"))
    return memmove;

  if (!strcmp(symbol, "putl"))
    return putl;

  if (!strcmp(symbol, "puta"))
    return puta;

  if (!strcmp(symbol, "putcs"))
    return putcs;

  if (!strcmp(symbol, "putp"))
    return putp;

  if (!strcmp(symbol, "puti"))
    return puti;

  if (!strcmp(symbol, "putg"))
    return putg;

  if (!strcmp(symbol, "putf"))
    return putf;

  if (!strcmp(symbol, "gab_boxtype"))
    return gab_boxtype;

  if (!strcmp(symbol, "gab_type"))
    return gab_type;

  if (!strcmp(symbol, "gab_recshp"))
    return gab_recshp;

  if (!strcmp(symbol, "gab_sigwaiting"))
    return gab_sigwaiting;

  if (!strcmp(symbol, "gab_yield"))
    return gab_yield;

  if (!strcmp(symbol, "gab_uvrecat"))
    return gab_uvrecat;

  if (!strcmp(symbol, "gab_ukrecat"))
    return gab_ukrecat;

  if (!strcmp(symbol, "gab_list"))
    return gab_list;

  if (!strcmp(symbol, "gab_record"))
    return gab_record;

  if (!strcmp(symbol, "gab_block"))
    return gab_block;

  if (!strcmp(symbol, "gab_gcepochnext"))
    return gab_gcepochnext;

  if (!strcmp(symbol, "gab_signext"))
    return gab_signext;

  if (!strcmp(symbol, "gab_thisfiber"))
    return gab_thisfiber;

  if (!strcmp(symbol, "vm_terminate"))
    return vm_terminate;

  if (!strcmp(symbol, "vm_error"))
    return vm_error;

  if (!strcmp(symbol, "vm_eerror"))
    return vm_eerror;

  if (!strcmp(symbol, "vm_ok"))
    return vm_ok;

  if (!strcmp(symbol, "vm_yield"))
    return vm_yield;

  if (!strcmp(symbol, "_jit_exit"))
    return gab_jtexit;

  if (!strcmp(symbol, "_jit_bail"))
    return gab_jtbail;

  return nullptr;
};

size_t relasize(const struct gab_jit_reloc *rela) {
  switch (rela->k) {
    /*
     * These relocations are actually 32-bit values burned into the code itself.
     *
     * They require no space in the symbol table.
     */
  case kGAB_JIT_RELOC_32ARG1:
  case kGAB_JIT_RELOC_32ARG2:
  case kGAB_JIT_RELOC_64ARG1:
  case kGAB_JIT_RELOC_64ARG2:
  case kGAB_JIT_RELOC_NEXT:
  case kGAB_JIT_RELOC_IP:
  case kGAB_JIT_RELOC_HV:
    return 0;

    /*
     * The constant's size is known.
     */
  case kGAB_JIT_RELOC_CONST_ABSOLUTE:
  case kGAB_JIT_RELOC_CONST_RELATIVE:
    return rela->as.constant.len;

    /*
     * A trampoline's size is statically known, but depends on the architecture.
     */
  case kGAB_JIT_RELOC_EXIT:
  case kGAB_JIT_RELOC_BAIL:
  case kGAB_JIT_RELOC_TRMP:
    return GAB_JIT_TRMPSIZE;
  }
}

size_t inst_datasize(union gab_jtir inst) {
  uint64_t sum = 0;

  int nrelas = stencil_nrelocas[inst.op.kind];

  for (int i = 0; i < nrelas; i++) {
    sum += relasize(stencil_relocas[inst.op.kind] + i);
    sum = pad_to_16(sum);
  }

  return sum;
}

/*
 * Compute the size of the symbol table needed by ir.
 */
size_t ir_datasize(struct ir *ir) {
  size_t sum = 0;

  for (uint16_t i = 0; i < ir->os; i++) {
    sum += inst_datasize(ir->ir[IR_BIAS + i]);
    sum = pad_to_16(sum);
  }

  return sum;
}

/*
 * Compute the total amount of memory needed by ir to create a basic block.
 */
size_t ir_size(struct ir *ir) {
  size_t sum = ir_codesize(ir) + ir_datasize(ir);
  return pad_to_16(sum);
}

struct bb_off {
  size_t code, data;
};

void bbpatch(uint8_t *data, struct ir *ir, struct bb_off begin, uint16_t ssa) {
  union gab_jtir inst = ir->ir[ssa];

  if (inst.op.kind == kGAB_IR_RETURN) {
    uint16_t ssa_branch = inst.op.b;

    const struct gab_jit_reloc *relocas = stencil_relocas[inst.op.kind];
    const uint64_t nrelocas = stencil_nrelocas[inst.op.kind];

    for (uint64_t i = 0; i < nrelocas; i++) {
      const struct gab_jit_reloc *reloca = relocas + i;

      if (reloca->k != kGAB_JIT_RELOC_64ARG2)
        continue;

      uint64_t offset = ir->of[ssa - IR_BIAS];
      uint64_t patch_off = offset + reloca->offset;
      int64_t patch_val = ir->of[ssa_branch] + reloca->addend - patch_off;
      // fprintf(stderr, "PATCH %s 0x%lx -> 0x%lx = %li\n",
      // irnames[inst.op.kind],
      //         offset + reloca->offset + reloca->addend, ir->of[ssa_branch],
      //         patch_val);
      gab_assert(patch_val < INT32_MAX, "Jump too big");
      int32_t patch_val32 = patch_val;

      /* Write the 32 bit value to data + patch_off */
      memcpy(data + patch_off, &patch_val32, sizeof(patch_val32));
    }
  }

  if (inst.op.kind == kGAB_IR_LOCALCALL_BLOCK) {
    uint16_t ssa_branch = inst.op.b;

    const struct gab_jit_reloc *relocas = stencil_relocas[inst.op.kind];
    const uint64_t nrelocas = stencil_nrelocas[inst.op.kind];

    for (uint64_t i = 0; i < nrelocas; i++) {
      const struct gab_jit_reloc *reloca = relocas + i;

      if (reloca->k != kGAB_JIT_RELOC_64ARG2)
        continue;

      uint64_t offset = ir->of[ssa - IR_BIAS];
      uint64_t patch_off = offset + reloca->offset;
      int64_t patch_val = ir->of[ssa_branch] + reloca->addend - patch_off;
      // fprintf(stderr, "PATCH %s 0x%lx -> 0x%lx = %li\n",
      // irnames[inst.op.kind],
      //         offset + reloca->offset + reloca->addend, ir->of[ssa_branch],
      //         patch_val);
      gab_assert(patch_val < INT32_MAX, "Jump too big");
      int32_t patch_val32 = patch_val;

      /* Write the 32 bit value to data + patch_off */
      memcpy(data + patch_off, &patch_val32, sizeof(patch_val32));
    }
  }
}

/*
 * Append an instruction to a basic block.
 */
struct bb_off bbappend(uint8_t *data, struct ir *ir, struct bb_off begin,
                       uint16_t ssa) {
  union gab_jtir inst = ir->ir[ssa];

  int64_t len = stencil_bytesizes[inst.op.kind];

  // printf("ASSEMBLE %s (nreloc %lu)\n", irnames[inst.op.kind],
  //        stencil_nrelocas[inst.op.kind]);
  gab_assert(begin.code - len >= 0, "Ran out of code");

  const struct gab_jit_reloc *relocas = stencil_relocas[inst.op.kind];
  const uint64_t nrelocas = stencil_nrelocas[inst.op.kind];

  if (nrelocas) {
    const struct gab_jit_reloc *reloca = &relocas[nrelocas - 1];
    uint64_t patch_off = begin.code + reloca->offset;
    int64_t patch_val = begin.code + len + reloca->addend - patch_off;

    if (reloca->k == kGAB_JIT_RELOC_NEXT && patch_val == 0) {
      gab_assert(patch_off == begin.code + len - 4,
                 "This optimization should only be attempted as the last "
                 "relocation. (%lu / %lu bytes)",
                 patch_off, begin.code + len);
      // TODO @jit @perf: Implement this for ARM properly.
      // On x86_64, we simply skip this last jump.
      // This is done by just chopping off the last 5 bytes.
      // This has to be done proactively, as the relocations
      // we perform later in this function depend on the *amount*
      // of code we emit here. (For trampolines, for instance)
      len -= 5;
    }
  }

  if (inst.op.kind == kGAB_IR_MATCHTAILCALL_BLOCK) {
    // Must rewrite the match with correct addresses into our bytecode.
    gab_value *ks = (void *)(uintptr_t)ir_getv(ir, inst.op.a).bits;
    for (int i = 0; i < cGAB_SEND_CACHE_LEN; i++) {
      uint8_t _idx = i * GAB_SEND_CACHE_SIZE;
      if (ks[GAB_SEND_KSPEC + _idx] == gab_cinvalid)
        continue;
      uint16_t ssa_branch = ks[GAB_SEND_KJIT + _idx];
      // It is possible to inline multiple of the same send
      gab_assert(ssa_branch < IR_SIZE >> 1, "Nonsensical match branch %u\n",
                 ssa_branch);
      ks[GAB_SEND_KJIT + _idx] = (uintptr_t)data + ir->of[ssa_branch];
    }
  }

  /* Copy the instruction, and increment code offset */
  begin.code -= len;

  /* Align jump targets down to 16 byte boundaries */
  if (ir->tg[ssa - IR_BIAS])
    begin.code &= ~0xF;

  memcpy(data + begin.code, stencil_bytes[inst.op.kind], len);

  for (uint64_t i = 0; i < stencil_nrelocas[inst.op.kind]; i++) {
    const struct gab_jit_reloc *reloca = relocas + i;

    /* Compute offset, relative to this stencil just copied */
    uint64_t patch_off = begin.code + reloca->offset;

    // This relocation has been optimized out.
    if (reloca->offset > len)
      break;

    switch (reloca->k) {
    case kGAB_JIT_RELOC_32ARG1:
    case kGAB_JIT_RELOC_32ARG2: {
      /* If the reloca type is arg 2, will be 1. Otherwise 0. */
      uint32_t arg = reloca->k == kGAB_JIT_RELOC_32ARG2;

      /* In this case, the argument is a constant, uint16_t value */
      if (ir_isk(inst.op.args[arg])) {
        int64_t patch_val = inst.op.args[arg] + reloca->addend;
        gab_assert(patch_val >= INT32_MIN && patch_val <= INT32_MAX,
                   "Patch value must fit within 32 bits.",
                   irnames[inst.op.kind]);
        /* Write the 32 bit value to data + patch_off */
        int32_t patch_val32 = patch_val;
        memcpy(data + patch_off, &patch_val32, sizeof(patch_val32));
        break;
      }

      if (ir_iso(inst.op.args[arg])) {
        union gab_jtir a = ir_geto(ir, inst.op.args[arg]);
        uint16_t arg_slot = a.op.slot;

        gab_assert(reloca->addend == 0,
                   "No addend allowed for this arg patch type");

        int32_t patch_val = (int32_t)inst.op.slot - (int32_t)arg_slot;
        memcpy(data + patch_off, &patch_val, sizeof(patch_val));
        break;
      }

      if (ir_isv(inst.op.args[arg])) {
        gab_assert(false, "[%u] 64 bit constant in 32 bit relocation in %s.",
                   arg, irnames[inst.op.kind]);
        break;
      }

      assert(false && "Unhandled arg type");
      break;
    };
    case kGAB_JIT_RELOC_64ARG1:
    case kGAB_JIT_RELOC_64ARG2: {
      /* If the reloca type is arg 2, will be 1. Otherwise 0. */
      uint64_t arg = reloca->k == kGAB_JIT_RELOC_64ARG2;

      /* In this case, the argument is a constant, uint16_t value */
      if (ir_isk(inst.op.args[arg])) {
        int64_t patch_val = inst.op.args[arg] + reloca->addend;
        /* Write the 64 bit value to data + patch_off */
        memcpy(data + patch_off, &patch_val, sizeof(patch_val));
        break;
      }

      if (ir_isv(inst.op.args[arg])) {
        union gab_jtir a = ir_getv(ir, inst.op.args[arg]);
        uint64_t patch_val = a.bits + reloca->addend;

        /* Write the 64 bit value to data + patch_off */
        memcpy(data + patch_off, &patch_val, sizeof(patch_val));

        break;
      }

      if (ir_iso(inst.op.args[arg])) {
        union gab_jtir a = ir_geto(ir, inst.op.args[arg]);
        uint16_t arg_slot = a.op.slot;

        gab_assert(reloca->addend == 0,
                   "No addend allowed for this arg patch type");

        int64_t patch_val = (int64_t)inst.op.slot - (int64_t)arg_slot;
        memcpy(data + patch_off, &patch_val, sizeof(patch_val));
        break;
      }

      assert(false && "Unhandled arg type");
      break;
    }
    case kGAB_JIT_RELOC_CONST_ABSOLUTE: {
      begin.data -= reloca->as.constant.len;

      /* Each data slot must be aligned to 16 bytes. */
      begin.data &= ~0xF;

      int64_t patch_val = (uintptr_t)(data + begin.data);

      gab_assert(
          patch_val >= INT32_MIN && patch_val < INT32_MAX,
          "A patched constant relocation must have address within 32 bit "
          "range. (%p)",
          patch_val);

      gab_assert(begin.data > ir_codesize(ir),
                 "Too many constants. %lu vs %lu:%lu. Needed %u.", begin.data,
                 ir_codesize(ir), ir_datasize(ir), reloca->as.constant.len);

      /* Copy the constant, and increment the data offset */
      memcpy(data + begin.data, reloca->as.constant.data,
             reloca->as.constant.len);

      /* Write the 32 bit value to data + patch_off */
      int32_t patch_val32 = patch_val;
      memcpy(data + patch_off, &patch_val32, sizeof(patch_val32));
      break;
    }
    case kGAB_JIT_RELOC_CONST_RELATIVE: {
      begin.data -= reloca->as.constant.len;

      /* Each data slot must be aligned to 16 bytes. */
      begin.data &= ~0xF;

      /* Compute value, next data address + addend - patch_offset; */
      int64_t patch_val = begin.data + reloca->addend - patch_off;

      gab_assert(patch_val >= INT32_MIN && patch_val < INT32_MAX,
                 "A patched constant relocation must have offset within 32 bit "
                 "range.");

      gab_assert(begin.data > ir_codesize(ir),
                 "Too many constants. %lu vs %lu:%lu. Needed %u.", begin.data,
                 ir_codesize(ir), ir_datasize(ir), reloca->as.constant.len);

      gab_assert(patch_val < ir_size(ir), "Nonsensical patch");

      /* Copy the constant, and increment the data offset */
      memcpy(data + begin.data, reloca->as.constant.data,
             reloca->as.constant.len);

      /* Write the 32 bit value to data + patch_off */
      int32_t patch_val32 = patch_val;
      memcpy(data + patch_off, &patch_val32, sizeof(patch_val32));
      break;
    };
    /*
     * TODO @jit @perf: Sometimes the patch_val calculated here is 0. Elim these
     * jumps.
     *
     * This proves to be more difficult than a simple check. Removing code
     * creates other problems.
     *
     * There are other jump offsets which are calculated, that don't take into
     * account these eliminated jumps (ie they end up having less code to jump
     * over than they thought)
     */
    case kGAB_JIT_RELOC_NEXT: {
      /* Compute value, code after stencil + addend - patch_offset; */
      gab_assert(reloca->addend == -4,
                 "NEXT patch relocation should have addend 0, got %i",
                 reloca->addend);
      int64_t patch_val = begin.code + len + reloca->addend - patch_off;

      gab_assert(patch_val >= INT32_MIN && patch_val < INT32_MAX,
                 "A patched next relocation must have offset within 32 bits.");

      gab_assert(patch_val != 0, "NEXT patch value should not be 0, got %i",
                 patch_val);

      /* Write the 32 bit value to data + patch_off */
      int32_t patch_val32 = patch_val;
      memcpy(data + patch_off, &patch_val32, sizeof(patch_val32));
      break;
    };
    case kGAB_JIT_RELOC_IP: {
      /* Compute value, code after stencil + addend - patch_offset; */
      int64_t patch_val = (uintptr_t)ir->ip[ssa - IR_BIAS] + reloca->addend;
      /* Write the 64 bit value to data + patch_off */
      memcpy(data + patch_off, &patch_val, sizeof(patch_val));
      break;
    }
    case kGAB_JIT_RELOC_HV: {
      /* Compute value, code after stencil + addend - patch_offset; */
      int64_t patch_val = (uintptr_t)ir->hv[ssa - IR_BIAS] + reloca->addend;
      /* Write the 64 bit value to data + patch_off */
      memcpy(data + patch_off, &patch_val, sizeof(patch_val));
      break;
    }
    case kGAB_JIT_RELOC_BAIL:
    case kGAB_JIT_RELOC_EXIT:
    case kGAB_JIT_RELOC_TRMP: {
      begin.data -= sizeof(trampoline);

      gab_assert(begin.data > ir_codesize(ir), "Too many trampoline");

      /* Compute value, next data address + addend - patch_offset; */
      int64_t patch_val = begin.data + reloca->addend - patch_off;

      /* Compute the 64 bit address */
      void *address = address_for_symbol(reloca->as.trampoline.symbol);

      gab_assert(
          address != nullptr,
          "Cannot patch trampoline to null address for symbol '%s' in %s\n",
          reloca->as.trampoline.symbol, irnames[inst.op.kind]);

      /* Copy/patch the trampoline with the 64 bit address. */
      memcpy(data + begin.data, trampoline, sizeof(trampoline));
      memcpy(data + begin.data + 2, &address, sizeof(uintptr_t));

      gab_assert(
          patch_val >= INT32_MIN && patch_val < INT32_MAX,
          "A patched trampoline relocation must have offset within 32 bits.");

      /* Write the 32 bit value to data + patch_off */
      int32_t patch_val32 = patch_val;
      memcpy(data + patch_off, &patch_val32, sizeof(patch_val32));
      break;
    };
    }
  }

  ir->of[ssa - IR_BIAS] = begin.code;
  return begin;
}

static void *assemble(struct ir *ir);

uint8_t *jtbbappend(struct gab_triple gab, struct gab_jtbb *bb, struct ir *ir,
                    struct gab_jtframe *f, struct gab_oprototype *proto,
                    uint64_t offset) {
  uint8_t *ip = proto_ip(gab, proto) + offset;
  gab_value *kb = proto_ks(gab, proto);

  uint8_t op = *ip++;
  fprintf(stderr, "COMPILE %s\n", gab_opcode_names[op]);
  uint8_t *result = handlers[op](&gab, bb, ip, kb, ir, f);

  return result;
};

struct gab_jtbb *gab_jtbbinline(struct gab_triple gab, struct ir *ir,
                                struct gab_jtbbid *id, struct gab_jtframe *f,
                                uint16_t *sp) {
  struct gab_jtbb *bb = calloc(sizeof(struct gab_jtbb), 1);

  if (!bb)
    return nullptr;

  bb->id = *id;
  bb->code_of = ir->os;
  f->bb = bb;
  ir->tg[ir->os] = true;

  // Insert before finishing - as we'll need recursive calls to see this.
  d_bb_insert(&gab.eg->jobs[gab.wkid].jt.blocks, &bb->id, bb);

  // gab_fmodinspect(stderr, __gab_obj(id->proto));

  uint8_t *result = jtbbappend(gab, bb, ir, f, id->proto, id->offset);

  if (!result)
    return free(bb), nullptr;

  struct gab_jtbb *ir_bb = ir->bb;
  while (ir_bb->next)
    ir_bb = ir_bb->next;
  ir_bb->next = bb;

  return bb;
}

void ir_checkpoint(struct ir *ir, uint16_t savedata[IR_SIZE], uint16_t **sp) {
  memcpy(savedata, ir->sb, IR_SIZE * sizeof(uint16_t));
  *sp = ir->sp;
}

void ir_restore(struct ir *ir, uint16_t savedata[IR_SIZE], uint16_t **sp) {
  memcpy(ir->sb, savedata, IR_SIZE * sizeof(uint16_t));
  ir->sp = *sp;
}

uint16_t gab_jtbbemitc(struct gab_triple gab, struct ir *ir,
                       struct gab_jtbb *bb, struct gab_jtframe *fp, uint8_t *ip,
                       uint64_t have) {}

uint16_t gab_jtbbemitr(struct gab_triple gab, struct ir *ir,
                       struct gab_jtbb *bb, struct gab_jtframe *fp, uint8_t *ip,
                       uint64_t have) {
  struct gab_jtbbid id = *fp->r_fp->id;
  id.offset = fp->r_ip - proto_ip(gab, id.proto);

  bool exists = d_bb_exists(&gab.eg->jobs[gab.wkid].jt.blocks, &id);

  uint16_t arg =
      exists ? d_bb_read(&gab.eg->jobs[gab.wkid].jt.blocks, &id)->code_of
             : ir->os + 1;

  emito(ir, ip, have, kGAB_IR_RETURN, kGAB_IRTYPE_UNKNOWN, 0, arg);

  if (!exists) {
    struct gab_jtframe new_fp = {
        .r_fp = fp->r_fp->r_fp,
        .fb = ir->sp - have,
        .id = &id,
        .ip = proto_ip(gab, id.proto) + id.offset,
    };

    if (!gab_jtbbinline(gab, ir, &id, &new_fp, ir->sp))
      return -1;
  }

  return arg;
}

uint16_t gab_jtbbemitb(struct gab_triple gab, struct ir *ir,
                       struct gab_jtbb *bb, struct gab_jtframe *pt, uint8_t *ip,
                       struct gab_oblock *b, uint64_t have) {
  struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(b->p);
  struct gab_type_state successor_state = {};
  init_type_state_inline(gab, ir, bb, &successor_state, ir->sp - have,
                         b->upvalues, p);

  struct gab_jtbbid id = {
      .proto = p,
      .entry_state = successor_state,
  };

  bool exists = d_bb_exists(&gab.eg->jobs[gab.wkid].jt.blocks, &id);

  uint16_t arg =
      exists ? d_bb_read(&gab.eg->jobs[gab.wkid].jt.blocks, &id)->code_of
             : ir->os;

  if (!exists) {
    struct gab_jtframe fp = {
        .r_fp = pt,
        .r_ip = ip,
        .fb = ir->sp - have,
        .id = &id,
        .ip = proto_ip(gab, id.proto),
    };

    if (!gab_jtbbinline(gab, ir, &id, &fp, ir->sp))
      return -1;
  }

  return arg;
}

struct gab_jtbb *gab_jtbbcreate(struct gab_triple gab, struct gab_jtbbid *id,
                                gab_value *sp) {
  struct gab_jtbb *bb = calloc(sizeof(struct gab_jtbb), 1);

  if (!bb)
    return nullptr;

  bb->id = *id;

  struct ir ir = {.bb = bb};
  ir.sp = ir.sb;

  uint64_t have = *sp;

  for (int i = have; i > 0; i--) {
    ir_push(&ir,
            emito(&ir, nullptr, 0, kGAB_IR_ENTER, gab_valkind(sp[-i]), 0, 0));
  }
  *ir.sp = have;

  struct gab_jtframe fp = {
      .fb = ir.sb,
      .id = id,
      .ip = proto_ip(gab, id->proto),
      .bb = bb,
  };

  d_bb_insert(&gab.eg->jobs[gab.wkid].jt.blocks, &bb->id, bb);

  uint8_t *result = jtbbappend(gab, bb, &ir, &fp, id->proto, id->offset);

  if (!result) {
    // TODO @jit @bug: Insert nullptr if we failed
    return free(bb), nullptr;
  }

  // gab_fmodinspect(stderr, __gab_obj(id->proto));
  irdump(gab, &ir);

  assemble(&ir);

  return bb;
}

// Tick a specific IP in the jit state.
bool gab_jttick(struct gab_triple gab, struct gab_jt *jt, uint8_t *ip) {
  uint8_t *hc = jt->hotcount + ((uintptr_t)ip & HOTCOUNT_MASK);

  if (*hc)
    return (*hc = *hc - 1), false;

  return true;
}

struct gab_jtbb *gab_jtchk(struct gab_triple gab, struct gab_jt *jt,
                           struct gab_oprototype *proto, uint8_t *ip,
                           gab_value *loc, gab_value *upv) {
  assert(proto->header.kind == kGAB_PROTOTYPE);

  // Build the type state for the successor block.
  // The inline cache tells us the receiver type at this send site.
  // The successor's entry state: local[recv_slot] = receiver_type.
  // All other slots: whatever the current local values' types are
  // (we can read them from the interpreter stack right now).
  struct gab_type_state successor_state = {};
  init_type_state(gab, &successor_state, loc, upv, proto);

  struct gab_jtbbid id = {
      .proto = proto,
      .entry_state = successor_state,
  };

  return d_bb_read(&jt->blocks, &id);
}

// Try to jit a basic block
struct gab_jtbb *gab_jttry(struct gab_triple gab, struct gab_jt *jt,
                           struct gab_oprototype *proto, uint8_t *ip,
                           gab_value *loc, gab_value *upv, gab_value *sp) {
  // return nullptr;
  assert(proto->header.kind == kGAB_PROTOTYPE);

  // Build the type state for the successor block.
  // The inline cache tells us the receiver type at this send site.
  // The successor's entry state: local[recv_slot] = receiver_type.
  // All other slots: whatever the current local values' types are
  // (we can read them from the interpreter stack right now).
  struct gab_type_state successor_state = {};
  init_type_state(gab, &successor_state, loc, upv, proto);

  struct gab_jtbbid id = {
      .proto = proto,
      .entry_state = successor_state,
  };

  if (d_bb_exists(&jt->blocks, &id))
    return d_bb_read(&jt->blocks, &id);

  struct gab_jtbb *version = gab_jtbbcreate(gab, &id, sp);

  return version;
}

#include <sys/mman.h>

static void *assemble(struct ir *ir) {
  uint64_t n = ir_size(ir);
  uint8_t *code = mmap(nullptr, n, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANON | MAP_32BIT, 0, 0);
  gab_assert(code != nullptr, "Failed to mmap");
  memset(code, 0x90, n);

  struct bb_off off = {ir_codesize(ir), n};

  for (int32_t i = ir->os - 1; i >= 0; i--)
    off = bbappend(code, ir, off, IR_BIAS + i);

  for (int32_t i = ir->os - 1; i >= 0; i--)
    bbpatch(code, ir, off, IR_BIAS + i);

  // Insert an INT3 at the beginning of the code to catch sleds.
  if (off.code > 0)
    code[off.code - 1] = 0xCC;

  gab_assert(off.code <= ir_codesize(ir),
             "The code should remain in the code section of the allocation.");

  for (struct gab_jtbb *bb = ir->bb; bb; bb = bb->next) {
    bb->native_code = code + ir->of[bb->code_of];
  }

  // fprintf(stderr, "ASSEMBLED FROM %lu -> %lu\n", off.code, ir_codesize(ir));
  // for (uint32_t i = off.code; i < ir_codesize(ir); i++) {
  //   fprintf(stderr, "%02X ", code[i]);
  // }
  // fprintf(stderr, "\n");

  mprotect(code, n, PROT_READ | PROT_EXEC);

  return code + off.code;
}

#define ATTRIBUTES

#define CASE_CODE(name)                                                        \
  ATTRIBUTES uint8_t *OP_GAB_IR_##name##_HANDLER(OP_HANDLER_ARGS)

#define DISPATCH_ARGS() __gab, __bb, __ip, __kb, __ir, __fp

#define DISPATCH(op)                                                           \
  ({                                                                           \
    uint8_t o = (op);                                                          \
    fprintf(stderr, "COMPILE %s\n", gab_opcode_names[o]);                      \
    [[clang::musttail]] return handlers[o](DISPATCH_ARGS());                   \
  })

#define FAIL()                                                                 \
  ({                                                                           \
    fprintf(stderr, "FAIL TO JIT COMPILE %s\n", __FUNCTION__);                 \
    return nullptr;                                                            \
  })

#define SUCCEED(diff) ({ return IP() - diff; })

#define NEXT() ({ DISPATCH(*(IP()++)); })
#define NEXT_CHECKED() (NEXT())

#define SKIP_BYTE (IP()++)

#define PUSH(ssa) (ir_push(IR(), ssa))
#define PUSHTUPLE(have)                                                        \
  ({                                                                           \
    emito(IR(), IP(), VAR(), kGAB_IR_TUPLE, kGAB_IRTYPE_UNKNOWN, 0, 0);        \
    ir_push(IR(), have);                                                       \
    *SP() = 0;                                                                 \
  })
#define POP() (ir_pop(IR()))
#define POPTUPLE(have)                                                         \
  ({                                                                           \
    ir_dropn(IR(), have + 1);                                                  \
    emito(IR(), IP(), VAR(), kGAB_IR_DROP_N, kGAB_IRTYPE_UNKNOWN, have + 1,    \
          0);                                                                  \
  })
#define IP() (__ip)
#define KB() (__kb)
#define IR() (__ir)
#define BB() (__bb)
#define FP() (__fp)
#define SP() (IR()->sp)
#define JT() (&GAB().eg->jobs[GAB().wkid].jt)
#define GAB() (*__gab)
#define DROP() (ir_dropn(IR(), 1))
#define DROP_N(n) ({ ir_dropn(IR(), n); })
#define PEEK() (ir_peekn(IR(), 1))
#define PEEK2() (ir_peekn(IR(), 2))
#define PEEK3() (ir_peekn(IR(), 3))
#define PEEK_N(n) (ir_peekn(IR(), n))
#define VAR() (*SP())
#define COMPUTE_TUPLE() (VAR())
#define READ_BYTE (*IP()++)
#define READ_SHORT (IP() += 2, (((uint16_t)IP()[-2] << 8) | IP()[-1]))
#define READ_CONSTANT                                                          \
  ({                                                                           \
    gab_value k = KB()[READ_SHORT];                                            \
    emito(IR(), IP(), VAR(), kGAB_IR_LOAD_CONSTANT, gab_valkind(k),            \
          emitk_v(IR(), k), 0);                                                \
  })
#define READ_SENDCONSTANTS                                                     \
  ({                                                                           \
    uint16_t shrt = READ_SHORT & (~(fHAVE_TAIL << 8));                         \
    KB() + shrt;                                                               \
  })

#define MICRO_OP_SENDK()                                                       \
  ({                                                                           \
    gab_value k = ks[GAB_SEND_KSPEC];                                          \
    emito(IR(), IP(), VAR(), kGAB_IR_SEND_CONSTANT, gab_valkind(k),            \
          emitk_v(IR(), k), 0);                                                \
  })

#define READ_SENDCONSTANTS_ANDTAIL(t)                                          \
  ({                                                                           \
    uint16_t shrt = READ_SHORT;                                                \
    t = ((shrt & (fHAVE_TAIL << 8)) != 0);                                     \
    KB() + (shrt & ~(fHAVE_TAIL << 8));                                        \
  })

#define SEND_GUARD_CACHED_MESSAGE_SPECS(e)                                     \
  emito(IR(), IP() - 3, VAR(), kGAB_IR_GUARD_SPECS, kGAB_IRTYPE_UNKNOWN,       \
        emitk_b(IR(), e), 0)

#define SEND_GUARD_CACHED_MATCH_TYPE(r, ks)                                    \
  emito(IR(), IP(), VAR(), kGAB_IR_GUARD_MATCH_TYPE, kGAB_IRTYPE_UNKNOWN, r,   \
        emitk_v(IR(), (uintptr_t)ks))

#define SEND_GUARD_CACHED_RECEIVER_TYPE(r)                                     \
  emito(IR(), IP() - 3, VAR(), kGAB_IR_GUARD_TYPE, kGAB_IRTYPE_UNKNOWN, r,     \
        emitk_v(IR(), ks[GAB_SEND_KTYPE]))

#define SEND_GUARD_KIND(v, k)                                                  \
  emito(IR(), IP() - 3, VAR(), kGAB_IR_GUARD_KIND, kGAB_IRTYPE_UNKNOWN, v, k)

#define SEND_GUARD_TYPE(v, k)                                                  \
  emito(IR(), IP() - 3, VAR(), kGAB_IR_GUARD_KIND, kGAB_IRTYPE_UNKNOWN, v, k)

#define SEND_GUARD_ISSHP(v) SEND_GUARD_KIND(v, kGAB_SHAPE)

#define SEND_GUARD_ISREC(v) SEND_GUARD_KIND(v, kGAB_RECORD)

// TODO @jit: This should be compile-time only check.
// Could probably implement this without a guard, even.
#define NILPAD_GUARD_ARGS_GTE(n)
// emito(IR(), IP(), VAR(), kGAB_IR_GUARD_NILPAD, kGAB_IRTYPE_UNKNOWN, n, have)

// In some cases, we may not have static stackspace to even compile this
// bytecode.
#define PANIC_GUARD_STACKSPACE(v)                                              \
  ({                                                                           \
    if (v > IR_SIZE - (IR()->sp - IR()->sb))                                   \
      FAIL();                                                                  \
  })
// else                                                                       \
// emito(IR(), IP() - 3, VAR(), kGAB_IR_GUARD_STACKSPACE,                   \
//       kGAB_IRTYPE_UNKNOWN, emitk_b(IR(), v), 0);

#define PANIC_GUARD_STACKSPACE_SPLATLIST(v)                                    \
  ({                                                                           \
    uint64_t n = gab_shplen(v);                                                \
    if (n > IR_SIZE - (IR()->sp - IR()->sb))                                   \
      FAIL();                                                                  \
    else                                                                       \
      emito(IR(), IP() - 3, VAR(), kGAB_IR_GUARD_STACKSPACE_SPLATLIST,         \
            kGAB_IRTYPE_UNKNOWN, emitk_b(IR(), n), 0);                         \
    n;                                                                         \
  })

#define PANIC_GUARD_STACKSPACE_SPLATDICT(v)                                    \
  ({                                                                           \
    uint64_t n = gab_shplen(v) * 2;                                            \
    if (n > IR_SIZE - (IR()->sp - IR()->sb))                                   \
      FAIL();                                                                  \
    else                                                                       \
      emito(IR(), IP() - 3, VAR(), kGAB_IR_GUARD_STACKSPACE_SPLATDICT,         \
            kGAB_IRTYPE_UNKNOWN, v, 0);                                        \
    n;                                                                         \
  })

#define PANIC_GUARD_STACKSPACE_SPLATSHAPE(v)                                   \
  emito(IR(), IP() - 3, VAR(), kGAB_IR_GUARD_STACKSPACE_SPLATSHAPE,            \
        kGAB_IRTYPE_UNKNOWN, v, 0)

#define PANIC_GUARD_KIND(v, k)                                                 \
  emito(IR(), IP() - 3, VAR(), kGAB_IR_GUARD_KIND, kGAB_IRTYPE_UNKNOWN, v, k)

#define PANIC_GUARD_ISN(v) PANIC_GUARD_KIND(v, kGAB_NUMBER)

// TODO
#define PANIC_GUARD_ISB(v) FAIL()

// TODO
#define PANIC_GUARD_ISS(v) FAIL()

// TODO
#define PANIC_GUARD_SHAPE_LEN(shape, len) FAIL()

// TOOD
#define SHORTCUT_GUARD_ARGS_LT(n) FAIL()

#define GUARD_NOP

#define TRIM_GUARD_EXACTLY_N(want, n)
// emito(IR(), IP() - 2, VAR(), kGAB_IR_GUARD_TRIM_EXACTLY_N, \
//       kGAB_IRTYPE_UNKNOWN, want, n)

#define TRIM_GUARD_DOWN_N(want, n)
// emito(IR(), IP() - 2, VAR(), kGAB_IR_GUARD_TRIM_DOWN_N, kGAB_IRTYPE_UNKNOWN,
// \
//       want, n)

#define TRIM_GUARD_UP_N(want, n)
// emito(IR(), IP() - 2, VAR(), kGAB_IR_GUARD_TRIM_UP_N, kGAB_IRTYPE_UNKNOWN, \
//       want, n)

#define MICRO_OP_TYPE(v)                                                       \
  emito(IR(), IP(), VAR(), kGAB_IR_TYPE, kGAB_IRTYPE_UNKNOWN, v, 0)

#define LOCAL(b)                                                               \
  ({                                                                           \
    uint8_t loc = (b);                                                         \
    emito(IR(), IP(), VAR(), kGAB_IR_LOAD_LOCAL,                               \
          localt(IR(), FP(), BB(), loc), loc, 0);                              \
  })

#define UPVALUE(b)                                                             \
  ({                                                                           \
    uint8_t upv = (b);                                                         \
    emito(IR(), IP(), VAR(), kGAB_IR_LOAD_UPVALUE, upvaluet(IR(), BB(), upv),  \
          upv, 0);                                                             \
  })

#define STORE_LOCAL(b, v)                                                      \
  emito(IR(), IP(), VAR(), kGAB_IR_STORE_LOCAL, kGAB_IRTYPE_UNKNOWN, b, v)

#define SET_VAR(n) ({ *SP() = n; })

#define MICRO_OP_CALL_NATIVE(n, have, below_have, message)                     \
  ({                                                                           \
    emito(IR(), IP(), VAR(), kGAB_IR_CALL_NATIVE, 0,                           \
          emitk_v(IR(), (uintptr_t)n), 0);                                     \
    SET_VAR(0);                                                                \
    SUCCEED(0);                                                                \
  })

#define MICRO_OP_CALL_BLOCK(blk, have) SUCCEED(GAB_SEND_CACHE_SIZE)

// TODO @jit @bug: If we detect we've already jit-compiled the block here, jump
// there.
#define MICRO_OP_LOCALCALL_BLOCK(blk, have)                                    \
  ({                                                                           \
    struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(blk->p);                   \
    PANIC_GUARD_STACKSPACE(3 + p->nslots - have);                              \
                                                                               \
    uint16_t arg = gab_jtbbemitb(GAB(), IR(), BB(), FP(), IP(), blk, have);    \
                                                                               \
    if (arg == -1)                                                             \
      FAIL();                                                                  \
                                                                               \
    emito(IR(), IP(), VAR(), kGAB_IR_LOCALCALL_BLOCK, kGAB_IRTYPE_UNKNOWN,     \
          emitk_v(IR(), __gab_obj(blk)), arg);                                 \
  })

// #define MICRO_OP_LOCALCALL_BLOCK(blk, have) SUCCEED(GAB_SEND_CACHE_SIZE);

#define MICRO_OP_MATCHCALL_BLOCK(idx, have) SUCCEED(GAB_SEND_CACHE_SIZE)

// TODO @jit @perf: Inline MATCHTAILCALL properly
// I can do this by compiling each branch, recording their offsets
// within ks.
#define MICRO_OP_MATCHTAILCALL_BLOCK(idx, have)                                \
  ({                                                                           \
    uint16_t ssa =                                                             \
        emito(IR(), IP(), VAR(), kGAB_IR_MATCHTAILCALL_BLOCK,                  \
              kGAB_IRTYPE_UNKNOWN, emitk_v(IR(), (uintptr_t)ks), 0);           \
                                                                               \
    uint16_t *_sp = SP();                                                      \
    uint16_t _sb[IR_SIZE];                                                     \
    ir_checkpoint(IR(), _sb, &_sp);                                            \
                                                                               \
    for (int i = 0; i < cGAB_SEND_CACHE_LEN; i++) {                            \
      uint8_t _idx = i * GAB_SEND_CACHE_SIZE;                                  \
                                                                               \
      if (ks[GAB_SEND_KSPEC + _idx] == gab_cinvalid)                           \
        continue;                                                              \
                                                                               \
      struct gab_oblock *blk = (void *)ks[GAB_SEND_KSPEC + _idx];              \
                                                                               \
      uint16_t arg = gab_jtbbemitb(GAB(), IR(), BB(), FP(), IP(), blk, have);  \
                                                                               \
      if (arg == -1)                                                           \
        FAIL();                                                                \
                                                                               \
      ir_restore(IR(), _sb, &_sp);                                             \
                                                                               \
      ks[GAB_SEND_KJIT + _idx] = arg;                                          \
    }                                                                          \
                                                                               \
    SUCCEED(GAB_SEND_CACHE_SIZE);                                              \
  })

// TODO @jit @perf: Break up local tail calls further.
// These operations are basically just a memmove.
// The LOCALTAILCALL_BLOCK used by vm_ops.h and therefore stencil.h
// is actually *too big* and does more than it needs to.
#define MICRO_OP_LOCALTAILCALL_BLOCK(blk, have)                                \
  ({                                                                           \
    uint16_t arg = gab_jtbbemitb(GAB(), IR(), BB(), FP(), IP(), blk, have);    \
                                                                               \
    if (arg == -1)                                                             \
      FAIL();                                                                  \
                                                                               \
    emito(IR(), IP(), VAR(), kGAB_IR_LOCALTAILCALL_BLOCK, kGAB_IRTYPE_UNKNOWN, \
          emitk_v(IR(), __gab_obj(blk)), arg);                                 \
                                                                               \
    SUCCEED(GAB_SEND_CACHE_SIZE);                                              \
  })

#define MICRO_OP_TAILCALL_BLOCK(blk, have) SUCCEED(GAB_SEND_CACHE_SIZE)

#define MICRO_OP_RETURN(have)                                                  \
  ({                                                                           \
    if (FP()->r_fp == nullptr) {                                               \
      emito(IR(), nullptr, 0, kGAB_IR_JIT_EXIT, kGAB_IRTYPE_UNKNOWN,           \
            emitk_v(IR(), (uintptr_t)IP() - 1), emitk_b(IR(), VAR()));         \
      return IP();                                                             \
    }                                                                          \
                                                                               \
    uint16_t below_have = FP()->fb[-1];                                        \
    memmove(FP()->fb - 1, SP() - have, have * sizeof(uint16_t));               \
    SP() = FP()->fb + have;                                                    \
    SET_VAR(below_have + have);                                                \
                                                                               \
    uint16_t arg = gab_jtbbemitr(GAB(), IR(), BB(), FP(), IP(), have);         \
                                                                               \
    if (arg == -1)                                                             \
      FAIL();                                                                  \
                                                                               \
    return IP();                                                               \
  })

// Invalid terminations of jitted bb's
#define MICRO_OP_USE(v) FAIL()

#define MICRO_OP_SEND(n) FAIL()

#define MICRO_OP_TAKE(c) FAIL()

#define MICRO_OP_PUT(c) FAIL()

#define MICRO_OP_FIBER(c) FAIL()

#define MICRO_OP_BINARY_ADD(a, b)                                              \
  emito(IR(), IP(), VAR(), kGAB_IR_FADD, kGAB_IRTYPE_UNBOXF, a, b)

#define MICRO_OP_BINARY_SUB(a, b)                                              \
  emito(IR(), IP(), VAR(), kGAB_IR_FSUB, kGAB_IRTYPE_UNBOXF, a, b)

#define MICRO_OP_BINARY_MUL(a, b)                                              \
  emito(IR(), IP(), VAR(), kGAB_IR_FMUL, kGAB_IRTYPE_UNBOXF, a, b)

#define MICRO_OP_BINARY_DIV(a, b)                                              \
  emito(IR(), IP(), VAR(), kGAB_IR_FDIV, kGAB_IRTYPE_UNBOXF, a, b)

#define MICRO_OP_BINARY_MOD(a, b)                                              \
  emito(IR(), IP(), VAR(), kGAB_IR_IMOD, kGAB_IRTYPE_UNBOXF, a, b)

#define MICRO_OP_BINARY_LT(a, b)                                               \
  emito(IR(), IP(), VAR(), kGAB_IR_FLT, kGAB_IRTYPE_UNBOXB, a, b)

#define MICRO_OP_BINARY_LTE(a, b)                                              \
  emito(IR(), IP(), VAR(), kGAB_IR_FLTE, kGAB_IRTYPE_UNBOXB, a, b)

#define MICRO_OP_BINARY_GT(a, b)                                               \
  emito(IR(), IP(), VAR(), kGAB_IR_FGT, kGAB_IRTYPE_UNBOXB, a, b)

#define MICRO_OP_BINARY_GTE(a, b)                                              \
  emito(IR(), IP(), VAR(), kGAB_IR_FGTE, kGAB_IRTYPE_UNBOXB, a, b)

#define MICRO_OP_BINARY_BOR(a, b)                                              \
  emito(IR(), IP(), VAR(), kGAB_IR_IBOR, kGAB_IRTYPE_UNBOXI, a, b)

#define MICRO_OP_BINARY_BND(a, b)                                              \
  emito(IR(), IP(), VAR(), kGAB_IR_IBND, kGAB_IRTYPE_UNBOXI, a, b)

#define MICRO_OP_BINARY_STRLT(a, b)                                            \
  emito(IR(), IP(), VAR(), kGAB_IR_STRLT, kGAB_IRTYPE_UNBOXB, a, b)

#define MICRO_OP_BINARY_STRLTE(a, b)                                           \
  emito(IR(), IP(), VAR(), kGAB_IR_STRLTE, kGAB_IRTYPE_UNBOXB, a, b)

#define MICRO_OP_BINARY_STRGT(a, b)                                            \
  emito(IR(), IP(), VAR(), kGAB_IR_STRGT, kGAB_IRTYPE_UNBOXB, a, b)

#define MICRO_OP_BINARY_STRGTE(a, b)                                           \
  emito(IR(), IP(), VAR(), kGAB_IR_STRGTE, kGAB_IRTYPE_UNBOXB, a, b)

#define MICRO_OP_BINARY_LSH(a, b)                                              \
  emito(IR(), IP(), VAR(), kGAB_IR_ULSH, kGAB_IRTYPE_UNBOXU, a, b)

#define MICRO_OP_BINARY_RSH(a, b)                                              \
  emito(IR(), IP(), VAR(), kGAB_IR_URSH, kGAB_IRTYPE_UNBOXU, a, b)

#define MICRO_OP_BINARY_CONCAT(a, b)                                           \
  emito(IR(), IP(), VAR(), kGAB_IR_STRCONCAT, kGAB_IRTYPE_STRING, a, b)

#define MICRO_OP_BINARY_EQ(a, b)                                               \
  emito(IR(), IP(), VAR(), kGAB_IR_VEQ, kGAB_IRTYPE_UNBOXB, a, b)

#define MICRO_OP_UNARY_LIN(a)                                                  \
  emito(IR(), IP(), VAR(), kGAB_IR_BLIN, kGAB_IRTYPE_UNBOXB, a, 0)

#define MICRO_OP_UNARY_BIN(a)                                                  \
  emito(IR(), IP(), VAR(), kGAB_IR_IBIN, kGAB_IRTYPE_UNBOXI, a, 0)

#define MICRO_OP_UNBOXF(ssa)                                                   \
  emito(IR(), IP(), VAR(), kGAB_IR_UNBOXF, kGAB_IRTYPE_UNBOXF, ssa, 0)

#define MICRO_OP_UNBOXI(ssa)                                                   \
  emito(IR(), IP(), VAR(), kGAB_IR_UNBOXI, kGAB_IRTYPE_UNBOXI, ssa, 0)

#define MICRO_OP_UNBOXU(ssa)                                                   \
  emito(IR(), IP(), VAR(), kGAB_IR_UNBOXU, kGAB_IRTYPE_UNBOXU, ssa, 0)

#define MICRO_OP_UNBOXB(ssa)                                                   \
  emito(IR(), IP(), VAR(), kGAB_IR_UNBOXB, kGAB_IRTYPE_UNBOXB, ssa, 0)

#define MICRO_OP_UNBOXS(ssa)                                                   \
  emito(IR(), IP(), VAR(), kGAB_IR_UNBOXS, kGAB_IRTYPE_UNBOXS, ssa, 0)

#define MICRO_OP_UNBOXV(ssa) 0

#define MICRO_OP_UNBOXF2(ssa)                                                  \
  emito(IR(), IP(), VAR(), kGAB_IR_UNBOXF2, kGAB_IRTYPE_UNBOXF, ssa, 0)

#define MICRO_OP_UNBOXI2(ssa)                                                  \
  emito(IR(), IP(), VAR(), kGAB_IR_UNBOXI2, kGAB_IRTYPE_UNBOXI, ssa, 0)

#define MICRO_OP_UNBOXU2(ssa)                                                  \
  emito(IR(), IP(), VAR(), kGAB_IR_UNBOXU2, kGAB_IRTYPE_UNBOXU, ssa, 0)

#define MICRO_OP_UNBOXB2(ssa)                                                  \
  emito(IR(), IP(), VAR(), kGAB_IR_UNBOXB2, kGAB_IRTYPE_UNBOXB, ssa, 0)

#define MICRO_OP_UNBOXS2(ssa)                                                  \
  emito(IR(), IP(), VAR(), kGAB_IR_UNBOXS2, kGAB_IRTYPE_UNBOXS, ssa, 0)

#define MICRO_OP_UNBOXV2(ssa) 0

#define MICRO_OP_UNBOXF_T uint16_t
#define MICRO_OP_UNBOXI_T uint16_t
#define MICRO_OP_UNBOXU_T uint16_t
#define MICRO_OP_UNBOXB_T uint16_t
#define MICRO_OP_UNBOXS_T uint16_t
#define MICRO_OP_UNBOXV_T uint16_t

#define MICRO_OP_BOXN(ssa)                                                     \
  emito(IR(), IP(), VAR(), kGAB_IR_BOXN, kGAB_IRTYPE_NUMBER, ssa, 0)

#define MICRO_OP_BOXI(ssa)                                                     \
  emito(IR(), IP(), VAR(), kGAB_IR_BOXI, kGAB_IRTYPE_NUMBER, ssa, 0)

#define MICRO_OP_BOXB(ssa)                                                     \
  emito(IR(), IP(), VAR(), kGAB_IR_BOXB, kGAB_IRTYPE_MESSAGE_BOOL, ssa, 0)

#define MICRO_OP_BOXV(ssa) ssa

#define MICRO_OP_CONS(a, b)                                                    \
  emito(IR(), IP(), VAR(), kGAB_IR_VCONS, kGAB_IRTYPE_RECORD, a, b)

#define MICRO_OP_CONS_RECORD(a, b)                                             \
  emito(IR(), IP(), VAR(), kGAB_IR_VCONS_RECORD, kGAB_IRTYPE_RECORD, a, b)

#define MICRO_OP_SPLATSHAPE(v)                                                 \
  emito(IR(), IP(), VAR(), kGAB_IR_SPLAT_SHAPE, kGAB_IRTYPE_UNKNOWN, v, 0)

#define MICRO_OP_SPLATDICT(v)                                                  \
  emito(IR(), IP(), VAR(), kGAB_IR_SPLAT_DICT, kGAB_IRTYPE_UNKNOWN, v, 0)

#define MICRO_OP_SPLATLIST(v)                                                  \
  emito(IR(), IP(), VAR(), kGAB_IR_SPLAT_LIST, kGAB_IRTYPE_UNKNOWN, v, 0)

#define MICRO_OP_UKRECAT(r, i)                                                 \
  emito(IR(), IP(), VAR(), kGAB_IR_UKRECAT, kGAB_IRTYPE_UNKNOWN, r, i)

#define MICRO_OP_UVRECAT(r, i)                                                 \
  emito(IR(), IP(), VAR(), kGAB_IR_UVRECAT, kGAB_IRTYPE_UNKNOWN, r, i)

#define MICRO_OP_BLOCK(v)                                                      \
  emito(IR(), IP(), VAR(), kGAB_IR_LOAD_BLOCK, kGAB_IRTYPE_BLOCK, v, 0)

#define MICRO_OP_CHANNEL()                                                     \
  emito(IR(), IP(), VAR(), kGAB_IR_LOAD_CHANNEL, kGAB_IRTYPE_CHANNEL, 0, 0)

#define MICRO_OP_TRIM(want, have)
// emito(IR(), IP(), VAR(), kGAB_IR_TRIM, kGAB_IRTYPE_UNKNOWN, want, 0)

#define MICRO_OP_PACK_DICT(below, above)                                       \
  ({                                                                           \
    emito(IR(), IP(), VAR(), kGAB_IR_PACK_DICT, kGAB_IRTYPE_RECORD, below,     \
          above);                                                              \
  })

#define MICRO_OP_PACK_LIST(below, above)                                       \
  ({                                                                           \
    uint64_t have = COMPUTE_TUPLE();                                           \
                                                                               \
    uint64_t want = below + above;                                             \
    int64_t len = have - want;                                                 \
                                                                               \
    uint16_t *ap = SP() - above;                                               \
    uint16_t ssa = emito(IR(), IP(), VAR(), kGAB_IR_PACK_LIST,                 \
                         kGAB_IRTYPE_RECORD, below, above);                    \
    DROP_N(len - 1);                                                           \
    memmove(ap - len + 1, ap, above * sizeof(gab_value));                      \
    IR()->sp[-(above + 1)] = ssa;                                              \
    SET_VAR(want + 1);                                                         \
    ssa;                                                                       \
  })

#define MICRO_OP_RECORD(len)                                                   \
  emito(IR(), IP(), VAR(), kGAB_IR_LOAD_RECORD, kGAB_IRTYPE_RECORD, len, 0)

#define MICRO_OP_RECORDFROM(shape, len)                                        \
  emito(IR(), IP(), VAR(), kGAB_IR_LOAD_RECORDFROM, kGAB_IRTYPE_RECORD, len,   \
        shape)

#define MICRO_OP_SHAPE(len)                                                    \
  emito(IR(), IP(), VAR(), kGAB_IR_LOAD_SHAPE, kGAB_IRTYPE_SHAPE, len, 0)

#define MICRO_OP_LIST(n, len)                                                  \
  emito(IR(), IP(), VAR(), kGAB_IR_LOAD_LIST, kGAB_IRTYPE_RECORD, n, len)

#define MICRO_OP_JIT_ENTER(code) NEXT();

#define MICRO_OP_JIT_TICK(block, ip, ks)
#define MICRO_OP_JIT_MATCHTICK(block, ip, ks)

#define MICRO_OP_NIL()                                                         \
  (emito(IR(), IP(), VAR(), kGAB_IR_LOAD_CONSTANT, kGAB_IRTYPE_MESSAGE,        \
         emitk_v(IR(), gab_nil), 0))

#define MICRO_OP_SET_PEEK_N(n, v)                                              \
  (emito(IR(), IP(), VAR(), kGAB_IR_MOV, kGAB_IRTYPE_UNKNOWN, v,               \
         emitk_b(IR(), n)))

#define MICRO_OP_SHIFT(n, amount)                                              \
  (emito(IR(), IP(), VAR(), kGAB_IR_SHIFT, kGAB_IRTYPE_UNKNOWN,                \
         emitk_b(IR(), n), emitk_b(IR(), amount)))

#define MICRO_OP_SPILL(r, n)                                                   \
  ({                                                                           \
    int computed = (n);                                                        \
    uint16_t clamped = computed < 0 ? 0 : computed;                            \
    uint16_t ssa = emito(IR(), IP(), VAR(), kGAB_IR_SPILL,                     \
                         kGAB_IRTYPE_UNKNOWN, r, emitk_b(IR(), clamped));      \
    IR()->ir[ssa - IR_BIAS].op.slot += clamped;                                \
    ssa;                                                                       \
  })

#define STORE_SP(n)

#include "ops.h"

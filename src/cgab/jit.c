#include "engine.h"
#include "gab.h"
#include <stdint.h>

#include "asm_x86.h"

struct asm {
  CODE *cb;
  CODE *cp;

  /* Mask to do determine available hardware registers */
  uint32_t hreg_mask;

  /* Array of virtual registers, tracking assignment and liveness */
  struct {
    enum gab_asmreg_kind assignment;
    bool live;
  } vregs[IR_BIAS];
};

void asmcreate(struct asm *asm) {
  memset(asm, 0, sizeof(*asm));

  asm->cb = calloc(1, 1024);
  asm->cp = asm->cb + 1023;
  asm->hreg_mask = ~0;
}

static void assemble(struct ir *ir, struct asm *asm);

#define OP_HANDLER_ARGS                                                        \
  struct gab_triple *__gab, struct gab_jtbb *__bb, uint8_t *__ip,              \
      gab_value *__kb, struct ir *__ir

typedef bool (*handler)(OP_HANDLER_ARGS);

// Forward declare all our opcode handlers
#define OP_CODE(name) bool OP_GAB_IR_##name##_HANDLER(OP_HANDLER_ARGS);

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

uint16_t emito(struct ir *ir, enum gab_irop_kind op, uint8_t type, uint16_t a,
               uint16_t b) {
  uint16_t ssa = ir->os++ + IR_BIAS;
  gab_assert(ssa < IR_SIZE, "Too many instructions in ir");

  ir->ir[ssa].op.kind = op;
  ir->ir[ssa].op.type = type;
  ir->ir[ssa].op.a = a;
  ir->ir[ssa].op.b = b;

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

bool ir_isk(uint16_t ssa) { return ssa < IR_VBIAS; }
bool ir_isv(uint16_t ssa) { return ssa > IR_VBIAS && ssa < IR_OBIAS; }
bool ir_iso(uint16_t ssa) { return ssa > IR_OBIAS; }

void ir_push(struct ir *ir, uint16_t ssa) {
  gab_assert(ir->sp - ir->sb < IR_SIZE, "Too many refs on stack");
  uint16_t have = *ir->sp;
  *ir->sp++ = ssa;
  *ir->sp = have + 1;
  // fprintf(stderr, "PUSH %i, %i -> %i\n", ssa, have, *ir->sp);
}

void ir_dropn(struct ir *ir, int64_t n) {
  gab_assert(ir->sp - ir->sb >= 0, "Too few refs on stack");
  uint16_t have = *ir->sp;
  ir->sp -= n;
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

// Initialize jit state
void gab_jtcreate(struct gab_jt *jt) {
  d_bb_create(&jt->blocks, 32);

  for (int i = 0; i < HOTCOUNT_SIZE; i++)
    jt->hotcount[i] = HOTCOUNT_WARMUP;
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
    gab_value v = loc[slot];

    if (v == gab_cinvalid || v == gab_cundefined)
      ts->slots[slot] = kGAB_IRTYPE_UNKNOWN;
    else
      ts->slots[slot] = (enum gab_irtype_kind)gab_valkind(v);
  }

  for (int i = ts->nlocals + ts->nupvalues; i < GAB_JIT_MAX_LOCALS; i++)
    ts->slots[i] = kGAB_IRTYPE_UNKNOWN;
}

static inline enum gab_irtype_kind localt(struct ir *ir, struct gab_jtbb *bb,
                                          uint16_t loc) {
  gab_assert(loc < bb->id.entry_state.nlocals, "loc %i out of local range %i\n",
             loc, bb->id.entry_state.nlocals);

  return bb->id.entry_state.slots[loc];
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
    fprintf(stderr, GAB_YELLOW "%04i" GAB_RESET, a);
  } else if (ir_isv(a)) {
    gab_fprintf(stderr, GAB_CYAN "$" GAB_RESET,
                ir->ir[IR_BIAS + IR_VBIAS - a].value);
  } else {
    assert(ir_iso(a));
    fprintf(stderr, GAB_GREEN "%%%03i" GAB_RESET, a - IR_OBIAS);
  }
}

void irdump_(struct ir *ir, uint16_t i, union gab_jtir inst) {
  fprintf(stderr, " %s | " GAB_GREEN "%%%04i" GAB_RESET " | %-20s\n",
          irtypenames[inst.op.type], i, irnames[inst.op.kind]);
}

void irdump_1(struct ir *ir, uint16_t i, union gab_jtir inst) {
  fprintf(stderr, " %s | " GAB_GREEN "%%%04i" GAB_RESET " | %-20s | ",
          irtypenames[inst.op.type], i, irnames[inst.op.kind]);

  irdumpa(ir, inst.op.a);

  fprintf(stderr, "\n");
}

void irdump_2(struct ir *ir, uint16_t i, union gab_jtir inst) {
  fprintf(stderr, " %s | " GAB_GREEN "%%%04i" GAB_RESET " | %-20s | ",
          irtypenames[inst.op.type], i, irnames[inst.op.kind]);

  irdumpa(ir, inst.op.a);

  fprintf(stderr, " ");

  irdumpa(ir, inst.op.b);

  fprintf(stderr, "\n");
}

uint8_t ir_nargs(union gab_jtir inst) {
  switch (inst.op.kind) {
  case kGAB_IR_LOAD_CHANNEL:
    return 0;
  case kGAB_IR_LOAD_LOCAL:
  case kGAB_IR_LOAD_UPVALUE:
  case kGAB_IR_STORE_LOCAL:
  case kGAB_IR_TYPE:
  case kGAB_IR_MATCH_HASHT:
  case kGAB_IR_UNBOXF:
  case kGAB_IR_UNBOXI:
  case kGAB_IR_UNBOXU:
  case kGAB_IR_UNBOXB:
  case kGAB_IR_UNBOXS:
  case kGAB_IR_BOXN:
  case kGAB_IR_BOXB:
  case kGAB_IR_GUARD_SHAPE:
  case kGAB_IR_GUARD_SPECS:
  case kGAB_IR_GUARD_STACKSPACE:
  case kGAB_IR_GUARD_STACKSPACE_SPLATLIST:
  case kGAB_IR_GUARD_STACKSPACE_SPLATDICT:
  case kGAB_IR_GUARD_STACKSPACE_SPLATSHAPE:
  case kGAB_IR_GUARD_NILPAD:
  case kGAB_IR_TRIM:
  case kGAB_IR_CALL_NATIVE:
  case kGAB_IR_CALL_BLOCK:
  case kGAB_IR_TAILCALL_BLOCK:
  case kGAB_IR_SPLAT_SHAPE:
  case kGAB_IR_SPLAT_LIST:
  case kGAB_IR_SPLAT_DICT:
  case kGAB_IR_LOAD_PROPERTY:
  case kGAB_IR_LOAD_BLOCK:
  case kGAB_IR_LOAD_RECORD:
  case kGAB_IR_LOAD_RECORDFROM:
  case kGAB_IR_LOAD_SHAPE:
  case kGAB_IR_LOAD_LIST:
    return 1;
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
  case kGAB_IR_ILSH:
  case kGAB_IR_IRSH:
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
  case kGAB_IR_PACK_LIST:
  case kGAB_IR_PACK_DICT:
  case kGAB_IR_GUARD_TYPE:
  case kGAB_IR_GUARD_KIND:
  case kGAB_IR_GUARD_MATCH_TYPE:
  case kGAB_IR_GUARD_TRIM_EXACTLY_N:
  case kGAB_IR_GUARD_TRIM_DOWN_N:
  case kGAB_IR_GUARD_TRIM_UP_N:
    return 2;
  }
}

void irdump(struct ir *ir) {
  for (int i = 0; i < ir->os; i++) {
    union gab_jtir inst = ir->ir[i + IR_BIAS];
    switch (ir_nargs(inst)) {
    case 0:
      irdump_(ir, i, inst);
      break;
    case 1:
      irdump_1(ir, i, inst);
      break;
    case 2:
      irdump_2(ir, i, inst);
      break;
    }
  }
}

static const char *gab_opcode_names[] = {
#define OP_CODE(name) #name,
#include "bytecode.h"
#undef OP_CODE
#undef GAB_OPCODE_NAMES_IMPL
};

struct gab_jtbb *gab_jtbbcreate(struct gab_triple gab, struct gab_jtbbid *id) {
  struct gab_jtbb *bb = calloc(sizeof(struct gab_jtbb), 1);

  if (!bb)
    return nullptr;

  bb->id = *id;

  uint8_t *ip = proto_ip(gab, id->proto) + id->block_offset;
  gab_value *kb = proto_ks(gab, id->proto);

  uint8_t op = *ip++;

  struct ir ir = {0};
  ir.sp = ir.sb + 1;

  bool result = handlers[op](&gab, bb, ip, kb, &ir);

  if (result) {
    gab_fmodinspect(stderr, __gab_obj(id->proto));

    irdump(&ir);

    struct asm asm = {0};
    asmcreate(&asm);

    assemble(&ir, &asm);
  }

  return bb;
}

// Tick a specific IP in the jit state.
bool gab_jttick(struct gab_triple gab, struct gab_jt *jt, uint8_t *ip) {
  uint8_t *hc = jt->hotcount + ((uintptr_t)ip & HOTCOUNT_MASK);

  if (*hc)
    return (*hc = *hc - 1), false;

  return true;
}

// Try to jit a basic block
bool gab_jttry(struct gab_triple gab, struct gab_jt *jt,
               struct gab_oprototype *proto, uint8_t *ip, gab_value *loc,
               gab_value *upv) {
  assert(proto->header.kind == kGAB_PROTOTYPE);
  uint64_t offset = ip - proto_ip(gab, proto);

  // Build the type state for the successor block.
  // The inline cache tells us the receiver type at this send site.
  // The successor's entry state: local[recv_slot] = receiver_type.
  // All other slots: whatever the current local values' types are
  // (we can read them from the interpreter stack right now).
  struct gab_type_state successor_state = {};
  init_type_state(gab, &successor_state, loc, upv, proto);

  struct gab_jtbbid id = {
      .block_offset = offset,
      .proto = proto,
      .entry_state = successor_state,
  };

  struct gab_jtbb *version = d_bb_read(&jt->blocks, &id);

  if (version)
    return true;

  version = gab_jtbbcreate(gab, &id);

  struct gab_jtbbid *pid = malloc(sizeof(struct gab_jtbbid));
  assert(pid);
  *pid = id;

  d_bb_insert(&jt->blocks, pid, version);

  // Patch the send site to jump to the compiled version on future hits.
  // The patched opcode checks the type guard and jumps; on miss, falls back
  // to the interpreter.
  // if (version)
  // return jit_patch_send_site(gab, proto, send_offset, ver);

  return true;
}

// This should be platform specific - we don't have 32 registers.
int pick_free(uint32_t hreg_mask) { return __builtin_ctzl(~hreg_mask); }

static void assemble(struct ir *ir, struct asm *asm) {
  for (uint16_t i = ir->os; i > 0; i--) {
    union gab_jtir inst = ir->ir[IR_BIAS + i];

    // If this SSA node was live, we've reached its definition.
    // It is no longer live.
    if (asm->vregs[i].live) {
      asm->hreg_mask |= (1 << asm->vregs[i].assignment);
      asm->vregs[i].live = false;
    }

    // For each of the arguments
    for (uint8_t n = 0; n < ir_nargs(inst); n++) {
      uint16_t arg = inst.op.args[n];

      // If the argument is itself an ssa node:
      if (ir_iso(arg))
        // If that ssa node is *not* live, we need to make it live and assign an
        // hreg.
        if (!asm->vregs[arg].live) {
          // TODO: Handle spillage when registers fill up.
          int reg = pick_free(asm->hreg_mask);
          asm->hreg_mask &= ~(1 << reg);
          asm->vregs[arg].assignment = reg;
          asm->vregs[arg].live = true;
        }
    }
  }
}

// typedef struct {
//   uint16_t bytecode_offset; // where to resume in the interpreter
//   uint8_t nlocals;
//   // register/spill assignments for each local at this guard point
//   // (populated by the register allocator)
//   struct {
//     enum { kREG, kSPILL, kCONST } location;
//     union {
//       uint8_t reg;
//       uint16_t spill_offset;
//       gab_value constant;
//     };
//   } local_locs[GAB_JIT_MAX_LOCALS];
// } gab_deopt_record;

#define ATTRIBUTES

#define CASE_CODE(name)                                                        \
  ATTRIBUTES bool OP_GAB_IR_##name##_HANDLER(OP_HANDLER_ARGS)

#define DISPATCH_ARGS() __gab, __bb, __ip, __kb, __ir

#define DISPATCH(op)                                                           \
  ({                                                                           \
    uint8_t o = (op);                                                          \
    [[clang::musttail]] return handlers[o](DISPATCH_ARGS());                   \
  })

#define NEXT() ({ DISPATCH(*(IP()++)); })
#define NEXT_CHECKED() (NEXT())

#define SKIP_BYTE (IP()++)

#define PUSH(ssa) (ir_push(IR(), ssa))
#define PUSHTUPLE(have)                                                        \
  ({                                                                           \
    ir_push(IR(), have);                                                       \
    *SP() = 0;                                                                 \
  })
#define POP() (ir_pop(IR()))
#define IP() (__ip)
#define KB() (__kb)
#define IR() (__ir)
#define BB() (__bb)
#define SP() (IR()->sp)
#define GAB() (*__gab)
#define DROP() (ir_dropn(IR(), 1))
#define DROP_N(n) (ir_dropn(IR(), n))
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
    emitk_v(IR(), k);                                                          \
  })
#define READ_SENDCONSTANTS                                                     \
  ({                                                                           \
    uint16_t shrt = READ_SHORT & (~(fHAVE_TAIL << 8));                         \
    KB() + shrt;                                                               \
  })

#define READ_SENDCONSTANTS_ANDTAIL(t)                                          \
  ({                                                                           \
    uint16_t shrt = READ_SHORT;                                                \
    t = ((shrt & (fHAVE_TAIL << 8)) != 0);                                     \
    KB() + (shrt & ~(fHAVE_TAIL << 8));                                        \
  })

#define SEND_GUARD_CACHED_MESSAGE_SPECS()                                      \
  emito(IR(), kGAB_IR_GUARD_SPECS, kGAB_IRTYPE_UNKNOWN,                        \
        emitk_b(IR(), ks[GAB_SEND_KSPECS]), 0)

#define SEND_GUARD_CACHED_MATCH_TYPE(idx, t)                                   \
  emito(IR(), kGAB_IR_GUARD_MATCH_TYPE, kGAB_IRTYPE_UNKNOWN, t, idx)

#define SEND_GUARD_CACHED_RECEIVER_TYPE(r)                                     \
  emito(IR(), kGAB_IR_GUARD_TYPE, kGAB_IRTYPE_UNKNOWN, r,                      \
        emitk_v(IR(), ks[GAB_SEND_KTYPE]))

#define SEND_GUARD_KIND(v, k)                                                  \
  emito(IR(), kGAB_IR_GUARD_KIND, kGAB_IRTYPE_UNKNOWN, v, k)

#define SEND_GUARD_ISSHP(v) SEND_GUARD_KIND(v, kGAB_SHAPE)

#define SEND_GUARD_ISREC(v) SEND_GUARD_KIND(v, kGAB_RECORD)

#define NILPAD_GUARD_ARGS_GTE(n)                                               \
  emito(IR(), kGAB_IR_GUARD_NILPAD, kGAB_IRTYPE_UNKNOWN, n, have)

#define PANIC_GUARD_STACKSPACE(v)                                              \
  emito(IR(), kGAB_IR_GUARD_STACKSPACE, kGAB_IRTYPE_UNKNOWN, v, 0)

#define PANIC_GUARD_STACKSPACE_SPLATLIST(v)                                    \
  emito(IR(), kGAB_IR_GUARD_STACKSPACE_SPLATLIST, kGAB_IRTYPE_UNKNOWN, v, 0)

#define PANIC_GUARD_STACKSPACE_SPLATDICT(v)                                    \
  emito(IR(), kGAB_IR_GUARD_STACKSPACE_SPLATDICT, kGAB_IRTYPE_UNKNOWN, v, 0)

#define PANIC_GUARD_STACKSPACE_SPLATSHAPE(v)                                   \
  emito(IR(), kGAB_IR_GUARD_STACKSPACE_SPLATSHAPE, kGAB_IRTYPE_UNKNOWN, v, 0)

#define PANIC_GUARD_KIND(v, k)                                                 \
  emito(IR(), kGAB_IR_GUARD_KIND, kGAB_IRTYPE_UNKNOWN, v, k)

#define PANIC_GUARD_ISN(v) PANIC_GUARD_KIND(v, kGAB_NUMBER)

// TODO
#define PANIC_GUARD_ISB(v) return false

// TODO
#define PANIC_GUARD_ISS(v) return false

// TODO
#define PANIC_GUARD_SHAPE_LEN(shape, len) return false

// TOOD
#define SHORTCUT_GUARD_ARGS_LT(n) return false

#define GUARD_NOP

#define TRIM_GUARD_EXACTLY_N(want, n)                                          \
  emito(IR(), kGAB_IR_GUARD_TRIM_EXACTLY_N, kGAB_IRTYPE_UNKNOWN, want, n)

#define TRIM_GUARD_DOWN_N(want, n)                                             \
  emito(IR(), kGAB_IR_GUARD_TRIM_DOWN_N, kGAB_IRTYPE_UNKNOWN, want, n)

#define TRIM_GUARD_UP_N(want, n)                                               \
  emito(IR(), kGAB_IR_GUARD_TRIM_UP_N, kGAB_IRTYPE_UNKNOWN, want, n)

#define PRIMITIVE_TYPE(v) emito(IR(), kGAB_IR_TYPE, kGAB_IRTYPE_UNKNOWN, v, 0)

#define PRIMITIVE_MATCH_HASHT(v)                                               \
  emito(IR(), kGAB_IR_MATCH_HASHT, kGAB_IRTYPE_UNBOXU, v, 0)

#define LOCAL(b)                                                               \
  ({                                                                           \
    uint8_t loc = (b);                                                         \
    emito(IR(), kGAB_IR_LOAD_LOCAL, localt(IR(), BB(), loc), loc, 0);          \
  })

#define UPVALUE(b)                                                             \
  ({                                                                           \
    uint8_t upv = (b);                                                         \
    emito(IR(), kGAB_IR_LOAD_UPVALUE, upvaluet(IR(), BB(), upv), upv, 0);      \
  })

#define STORE_LOCAL(b, v)                                                      \
  emito(IR(), kGAB_IR_STORE_LOCAL, kGAB_IRTYPE_UNKNOWN, b, v)

#define SET_VAR(n)

#define PRIMITIVE_CALL_NATIVE(n, have, below_have, message)                    \
  emito(IR(), kGAB_IR_CALL_NATIVE, 0, emitk_b(IR(), (uintptr_t)n), 0)

// Valid terminations of jitted bb's
// TODO: inline all these calls
#define PRIMITIVE_CALL_BLOCK(blk, have) return true

#define PRIMITIVE_LOCALCALL_BLOCK(blk, have) return true

#define PRIMITIVE_MATCHCALL_BLOCK(idx, have) return true

#define PRIMITIVE_LOCALTAILCALL_BLOCK(blk, have) return true

#define PRIMITIVE_MATCHTAILCALL_BLOCK(idx, have) return true

#define PRIMITIVE_TAILCALL_BLOCK(blk, have) return true

#define PRIMITIVE_RETURN() return true

// Invalid terminations of jitted bb's
#define PRIMITIVE_USE(v) return false

#define PRIMITIVE_SEND(n) return false

#define PRIMITIVE_TAKE(c) return false

#define PRIMITIVE_PUT(c) return false

#define PRIMITIVE_FIBER(c) return false

#define PRIMITIVE_BINARY_ADD(a, b)                                             \
  emito(IR(), kGAB_IR_FADD, kGAB_IRTYPE_UNBOXF, a, b)

#define PRIMITIVE_BINARY_SUB(a, b)                                             \
  emito(IR(), kGAB_IR_FSUB, kGAB_IRTYPE_UNBOXF, a, b)

#define PRIMITIVE_BINARY_MUL(a, b)                                             \
  emito(IR(), kGAB_IR_FMUL, kGAB_IRTYPE_UNBOXF, a, b)

#define PRIMITIVE_BINARY_DIV(a, b)                                             \
  emito(IR(), kGAB_IR_FDIV, kGAB_IRTYPE_UNBOXF, a, b)

#define PRIMITIVE_BINARY_MOD(a, b)                                             \
  emito(IR(), kGAB_IR_IMOD, kGAB_IRTYPE_UNBOXF, a, b)

#define PRIMITIVE_BINARY_LT(a, b)                                              \
  emito(IR(), kGAB_IR_FLT, kGAB_IRTYPE_UNBOXB, a, b)

#define PRIMITIVE_BINARY_LTE(a, b)                                             \
  emito(IR(), kGAB_IR_FLTE, kGAB_IRTYPE_UNBOXB, a, b)

#define PRIMITIVE_BINARY_GT(a, b)                                              \
  emito(IR(), kGAB_IR_FGT, kGAB_IRTYPE_UNBOXB, a, b)

#define PRIMITIVE_BINARY_GTE(a, b)                                             \
  emito(IR(), kGAB_IR_FGTE, kGAB_IRTYPE_UNBOXB, a, b)

#define PRIMITIVE_BINARY_BOR(a, b)                                             \
  emito(IR(), kGAB_IR_IBOR, kGAB_IRTYPE_UNBOXI, a, b)

#define PRIMITIVE_BINARY_BND(a, b)                                             \
  emito(IR(), kGAB_IR_IBND, kGAB_IRTYPE_UNBOXI, a, b)

#define PRIMITIVE_BINARY_STRLT(a, b)                                           \
  emito(IR(), kGAB_IR_STRLT, kGAB_IRTYPE_UNBOXB, a, b)

#define PRIMITIVE_BINARY_STRLTE(a, b)                                          \
  emito(IR(), kGAB_IR_STRLTE, kGAB_IRTYPE_UNBOXB, a, b)

#define PRIMITIVE_BINARY_STRGT(a, b)                                           \
  emito(IR(), kGAB_IR_STRGT, kGAB_IRTYPE_UNBOXB, a, b)

#define PRIMITIVE_BINARY_STRGTE(a, b)                                          \
  emito(IR(), kGAB_IR_STRGTE, kGAB_IRTYPE_UNBOXB, a, b)

#define PRIMITIVE_BINARY_LSH(a, b)                                             \
  emito(IR(), kGAB_IR_ILSH, kGAB_IRTYPE_UNBOXU, a, b)

#define PRIMITIVE_BINARY_RSH(a, b)                                             \
  emito(IR(), kGAB_IR_IRSH, kGAB_IRTYPE_UNBOXU, a, b)

#define PRIMITIVE_BINARY_CONCAT(a, b)                                          \
  emito(IR(), kGAB_IR_STRCONCAT, kGAB_IRTYPE_STRING, a, b)

#define PRIMITIVE_BINARY_EQ(a, b)                                              \
  emito(IR(), kGAB_IR_VEQ, kGAB_IRTYPE_UNBOXB, a, b)

#define PRIMITIVE_UNARY_LIN(a)                                                 \
  emito(IR(), kGAB_IR_BLIN, kGAB_IRTYPE_UNBOXB, a, 0)

#define PRIMITIVE_UNARY_BIN(a)                                                 \
  emito(IR(), kGAB_IR_IBIN, kGAB_IRTYPE_UNBOXI, a, 0)

#define PRIMITIVE_UNBOXF(ssa)                                                  \
  emito(IR(), kGAB_IR_UNBOXF, kGAB_IRTYPE_UNBOXF, ssa, 0)

#define PRIMITIVE_UNBOXI(ssa)                                                  \
  emito(IR(), kGAB_IR_UNBOXI, kGAB_IRTYPE_UNBOXI, ssa, 0)

#define PRIMITIVE_UNBOXU(ssa)                                                  \
  emito(IR(), kGAB_IR_UNBOXU, kGAB_IRTYPE_UNBOXU, ssa, 0)

#define PRIMITIVE_UNBOXB(ssa)                                                  \
  emito(IR(), kGAB_IR_UNBOXB, kGAB_IRTYPE_UNBOXB, ssa, 0)

#define PRIMITIVE_UNBOXS(ssa)                                                  \
  emito(IR(), kGAB_IR_UNBOXS, kGAB_IRTYPE_UNBOXS, ssa, 0)

#define PRIMITIVE_UNBOXV(ssa) 0

#define PRIMITIVE_UNBOXF_T uint16_t
#define PRIMITIVE_UNBOXI_T uint16_t
#define PRIMITIVE_UNBOXU_T uint16_t
#define PRIMITIVE_UNBOXB_T uint16_t
#define PRIMITIVE_UNBOXS_T uint16_t
#define PRIMITIVE_UNBOXV_T uint16_t

#define PRIMITIVE_BOXN(ssa)                                                    \
  emito(IR(), kGAB_IR_BOXN, kGAB_IRTYPE_NUMBER, ssa, 0)

#define PRIMITIVE_BOXB(ssa)                                                    \
  emito(IR(), kGAB_IR_BOXB, kGAB_IRTYPE_MESSAGE_BOOL, ssa, 0)

#define PRIMITIVE_BOXV(ssa) ssa

#define PRIMITIVE_CONS(a, b)                                                   \
  emito(IR(), kGAB_IR_VCONS, kGAB_IRTYPE_RECORD, a, b)

#define PRIMITIVE_CONS_RECORD(a, b)                                            \
  emito(IR(), kGAB_IR_VCONS_RECORD, kGAB_IRTYPE_RECORD, a, b)

#define PRIMITIVE_SPLATSHAPE(v)                                                \
  emito(IR(), kGAB_IR_SPLAT_SHAPE, kGAB_IRTYPE_UNKNOWN, v, 0)

#define PRIMITIVE_SPLATDICT(v)                                                 \
  emito(IR(), kGAB_IR_SPLAT_DICT, kGAB_IRTYPE_UNKNOWN, v, 0)

#define PRIMITIVE_SPLATLIST(v)                                                 \
  emito(IR(), kGAB_IR_SPLAT_LIST, kGAB_IRTYPE_UNKNOWN, v, 0)

#define PRIMITIVE_PROPERTY_RECORD(v, have, below_have)                         \
  emito(IR(), kGAB_IR_LOAD_PROPERTY, 0, v, 0)

#define PRIMITIVE_BLOCK(v)                                                     \
  emito(IR(), kGAB_IR_LOAD_BLOCK, kGAB_IRTYPE_BLOCK, v, 0)

#define PRIMITIVE_CHANNEL()                                                    \
  emito(IR(), kGAB_IR_LOAD_CHANNEL, kGAB_IRTYPE_CHANNEL, 0, 0)

#define PRIMITIVE_TRIM(want, have)                                             \
  emito(IR(), kGAB_IR_TRIM, kGAB_IRTYPE_UNKNOWN, want, 0)

#define PRIMITIVE_PACKRECORD(below, above)                                     \
  emito(IR(), kGAB_IR_PACK_DICT, kGAB_IRTYPE_UNKNOWN, below, above)

#define PRIMITIVE_PACKLIST(below, above)                                       \
  emito(IR(), kGAB_IR_PACK_LIST, kGAB_IRTYPE_UNKNOWN, below, above)

#define PRIMITIVE_RECORD(len)                                                  \
  emito(IR(), kGAB_IR_LOAD_RECORD, kGAB_IRTYPE_RECORD, len, 0)

#define PRIMITIVE_RECORDFROM(shape, len)                                       \
  emito(IR(), kGAB_IR_LOAD_RECORDFROM, kGAB_IRTYPE_RECORD, len, shape)

#define PRIMITIVE_SHAPE(len)                                                   \
  emito(IR(), kGAB_IR_LOAD_SHAPE, kGAB_IRTYPE_SHAPE, len, 0)

#define PRIMITIVE_LIST(len)                                                    \
  emito(IR(), kGAB_IR_LOAD_LIST, kGAB_IRTYPE_RECORD, len, 0)

#define STORE_SP(n)

#include "ops.h"

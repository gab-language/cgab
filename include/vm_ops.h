#include "gab.h"
#include "stdint.h"

/*
 * This file defines implementations for an extensive set of macros.
 *
 * These macros are used by `ops.h`, which definies the bytecode operations
 * in terms of these smaller, primitive bytecodes.
 */

/* IMPL in vm.c */
union gab_value_pair vm_terminate(struct gab_triple gab, const char *fmt, ...);

/* IMPL in vm.c */
union gab_value_pair vm_yield(struct gab_triple gab, uintptr_t value);

extern void putl(uintptr_t arg);
extern void putp(uintptr_t arg);
extern void puti(int64_t arg);
extern void putf(double arg);
extern void putg(gab_value arg);
extern void putcs(char *arg);

/*
 * In x86, the maximum number of ponter arguments that are passed
 * in registers is 6.
 *
 * On arm, it is 8.
 *
 * we *cannot* go above this limit and pass args on the stack here.
 */
#define OP_HANDLER_ARGS                                                        \
  struct gab_triple *__gab, struct gab_vm *__vm, uint8_t *__ip,                \
      gab_value *__kb, gab_value *__fb, gab_value *__sp

#define ATTRIBUTES

#define CASE_CODE(name)                                                        \
  ATTRIBUTES union gab_value_pair OP_##name##_HANDLER(OP_HANDLER_ARGS)

#define DISPATCH_ARGS() __gab, __vm, __ip, __kb, __fb, __sp

#if cGAB_LOG_VM
#define LOG(gab, op)                                                           \
  fprintf(stderr, "%p OP_%s [%lu] (%i)\n", IP() - 1, gab_opcode_names[op],     \
          VAR(), GAB().wkid);
#else
#define LOG(gab, op)
#endif

#define CHECK_SIGNAL()                                                         \
  if (gab_sigwaiting(GAB()))                                                   \
    switch (gab_yield(GAB())) {                                                \
    case sGAB_COLL:                                                            \
      STORE_SP();                                                              \
      gab_gcepochnext(GAB());                                                  \
      gab_sigpropagate(GAB());                                                 \
      break;                                                                   \
    case sGAB_TERM:                                                            \
      STORE_SP();                                                              \
      VM_TERM();                                                               \
    default:                                                                   \
      break;                                                                   \
    }

#define DISPATCH(op)                                                           \
  ({                                                                           \
    uint8_t o = (op);                                                          \
                                                                               \
    LOG(GAB(), o);                                                             \
                                                                               \
    assert(SP() < VM()->sb + cGAB_STACK_MAX);                                  \
    assert(SP() >= FB());                                                      \
                                                                               \
    [[clang::musttail]] return handlers[o](DISPATCH_ARGS());                   \
  })

#define NEXT_CHECKED()                                                         \
  ({                                                                           \
    CHECK_SIGNAL();                                                            \
    NEXT();                                                                    \
  })

#define NEXT() ({ DISPATCH(*(IP()++)); })
// #define NEXT() NEXT_CHECKED()

#define VM_PANIC(status, help, ...)                                            \
  ({                                                                           \
    STORE();                                                                   \
    return vm_error(GAB(), status, help __VA_OPT__(, ) __VA_ARGS__);           \
  })

#define VM_GIVEN(err)                                                          \
  ({                                                                           \
    STORE();                                                                   \
    return vm_givenerr(GAB(), err);                                            \
  })

#define GAB() (*__gab)
#define EG() (GAB().eg)
#define FIBER() (GAB_VAL_TO_FIBER(gab_thisfiber(GAB())))
#define REENTRANT() (FIBER()->reentrant)
#define RESET_REENTRANT() (FIBER()->reentrant = 0)
#define RESET_BUMP() (FIBER()->allocator.len = 0)
#define GC() (GAB().eg->gc)
#define VM() (__vm)
#define SET_BLOCK(b) (FB()[-FRAME_BK] = (uintptr_t)(b));
#define BLOCK() ((struct gab_oblock *)(uintptr_t)FB()[-FRAME_BK])
#define BLOCK_PROTO() (GAB_VAL_TO_PROTOTYPE(BLOCK()->p))
#define IP() (__ip)
#define SP() (__sp)
#define SB() (VM()->sb)
#define VAR() (*SP())
#define FB() (__fb)
#define UPV() (BLOCK()->upvalues)
#define KB() (__kb)
#define LOCAL(i) (FB()[i])
#define STORE_LOCAL(i, v) (LOCAL(i) = v)
#define UPVALUE(i) (BLOCK()->upvalues[i])

#define GET_STACKSPACE(sp, sb) ((sp - sb) + 3)
#define HAS_STACKSPACE(sp, sb, space)                                          \
  (GET_STACKSPACE(sp, sb) + space < cGAB_STACK_MAX)

#define PANIC_GUARD_STACKSPACE(space)                                          \
  if (__gab_unlikely(!HAS_STACKSPACE(SP(), SB(), space)))                      \
    VM_PANIC(GAB_OVERFLOW, "");

#define PANIC_GUARD_STACKSPACE_SPLATDICT(r)                                    \
  ({                                                                           \
    uint64_t n = gab_shplen(r) * 2;                                            \
    if (__gab_unlikely(!HAS_STACKSPACE(SP(), SB(), n)))                        \
      VM_PANIC(GAB_OVERFLOW, "");                                              \
    n;                                                                         \
  })

#define PANIC_GUARD_STACKSPACE_SPLATLIST(r)                                    \
  ({                                                                           \
    uint64_t n = gab_shplen(r);                                                \
    if (__gab_unlikely(!HAS_STACKSPACE(SP(), SB(), n)))                        \
      VM_PANIC(GAB_OVERFLOW, "");                                              \
    n;                                                                         \
  })

#define PANIC_GUARD_STACKSPACE_SPLATSHAPE(r)                                   \
  if (__gab_unlikely(!HAS_STACKSPACE(SP(), SB(), gab_shplen(r))))              \
    VM_PANIC(GAB_OVERFLOW, "");

#define PANIC_GUARD_SHAPE_LEN(shape, len)                                      \
  if (__gab_unlikely(gab_shplen(shape) != len))                                \
    VM_PANIC(GAB_PANIC, "Expected $ arguments, got $",                         \
             gab_number(gab_shplen(shape)), gab_number(len));

// fprintf(stderr, "(%i) COMPILED JIT CODE FOR %p, ENTER AT %p\n",        \
//         GAB().wkid, IP(), ip - GAB_SEND_CACHE_SIZE);                   \
// for (int i = 1; i < VAR() + 1; i++) {                                  \
//   gab_fprintf(stderr, "($) $: $\n", gab_number(GAB().wkid),            \
//               gab_number(i), PEEK_N(i));                               \
// }

#define MICRO_OP_JIT_TICK(ip, ks, block)                                       \
  ({                                                                           \
    if (gab_jttick(GAB(), &GAB().eg->jobs[GAB().wkid].jt, IP())) {             \
      struct gab_jtbb *bb =                                                    \
          gab_jttry(GAB(), &GAB().eg->jobs[GAB().wkid].jt,                     \
                    GAB_VAL_TO_PROTOTYPE(block->p), IP(), FB(), UPV(), SP());  \
      if (bb && bb->native_code) {                                             \
        ks[GAB_SEND_KJIT] = (uintptr_t)bb->native_code;                        \
        IP() = ip - GAB_SEND_CACHE_SIZE;                                       \
        WRITE_BYTE(0, *IP() + 6);                                              \
        [[clang::musttail]] return ((handler)bb->native_code)(                 \
            DISPATCH_ARGS());                                                  \
      }                                                                        \
    }                                                                          \
  })

#define MICRO_OP_JIT_MATCHTICK(ip, ks, idx)                                    \
  ({                                                                           \
    if (gab_jttick(GAB(), &GAB().eg->jobs[GAB().wkid].jt, IP())) {             \
      bool success = true;                                                     \
      for (int i = 0; i < 4; i++) {                                            \
        uint8_t _idx = i * GAB_SEND_CACHE_SIZE;                                \
        if (ks[GAB_SEND_KSPEC + _idx] == gab_cinvalid)                         \
          continue;                                                            \
        struct gab_oblock *block = (void *)ks[GAB_SEND_KSPEC + _idx];          \
        struct gab_jtbb *bb = gab_jttry(GAB(), &GAB().eg->jobs[GAB().wkid].jt, \
                                        GAB_VAL_TO_PROTOTYPE(block->p), IP(),  \
                                        FB(), UPV(), SP());                    \
        if (!bb || !bb->native_code) {                                         \
          success = false;                                                     \
          break;                                                               \
        }                                                                      \
                                                                               \
        ks[GAB_SEND_KJIT + _idx] = (uintptr_t)bb->native_code;                 \
      }                                                                        \
      if (success) {                                                           \
        IP() = ip - GAB_SEND_CACHE_SIZE;                                       \
        WRITE_BYTE(0, *IP() + 6);                                              \
        handler code = (void *)(uintptr_t)ks[GAB_SEND_KJIT + idx];             \
        [[clang::musttail]] return code(DISPATCH_ARGS());                      \
      }                                                                        \
    }                                                                          \
  })

#define MICRO_OP_CALL_BLOCK(blk, have)                                         \
  ({                                                                           \
    struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(blk->p);                   \
                                                                               \
    PANIC_GUARD_STACKSPACE(3 + p->nslots - have);                              \
                                                                               \
    PUSH_FRAME(blk, have);                                                     \
                                                                               \
    IP() = proto_ip(GAB(), p);                                                 \
    KB() = proto_ks(GAB(), p);                                                 \
    FB() = SP() - have;                                                        \
                                                                               \
    SET_VAR(have);                                                             \
  })

#define MICRO_OP_LOCALCALL_BLOCK(blk, have)                                    \
  ({                                                                           \
    struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(blk->p);                   \
                                                                               \
    PANIC_GUARD_STACKSPACE(3 + p->nslots - have);                              \
                                                                               \
    PUSH_FRAME(blk, have);                                                     \
                                                                               \
    IP() = proto_ip(GAB(), p);                                                 \
    FB() = SP() - have;                                                        \
                                                                               \
    SET_VAR(have);                                                             \
  })

#define MICRO_OP_TAILCALL_BLOCK(blk, have)                                     \
  ({                                                                           \
    struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(blk->p);                   \
                                                                               \
    PANIC_GUARD_STACKSPACE(p->nslots - have);                                  \
                                                                               \
    gab_value *from = SP() - have;                                             \
    gab_value *to = FB();                                                      \
                                                                               \
    memmove(to, from, have * sizeof(gab_value));                               \
    SP() = to + have;                                                          \
                                                                               \
    IP() = proto_ip(GAB(), p);                                                 \
    KB() = proto_ks(GAB(), p);                                                 \
                                                                               \
    SET_BLOCK(blk);                                                            \
    SET_VAR(have);                                                             \
  })

#define MICRO_OP_LOCALTAILCALL_BLOCK(blk, have)                                \
  ({                                                                           \
    struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(blk->p);                   \
                                                                               \
    PANIC_GUARD_STACKSPACE(p->nslots - have);                                  \
                                                                               \
    gab_value *from = SP() - have;                                             \
    gab_value *to = FB();                                                      \
                                                                               \
    memmove(to, from, have * sizeof(gab_value));                               \
    SP() = to + have;                                                          \
                                                                               \
    IP() = proto_ip(GAB(), p);                                                 \
                                                                               \
    SET_BLOCK(blk);                                                            \
    SET_VAR(have);                                                             \
  })

#define MICRO_OP_MATCHTAILCALL_BLOCK(idx, have)                                \
  ({                                                                           \
    struct gab_oblock *b = GAB_VAL_TO_BLOCK(ks[GAB_SEND_KSPEC + idx]);         \
                                                                               \
    gab_value *from = SP() - have;                                             \
    gab_value *to = FB();                                                      \
                                                                               \
    memmove(to, from, have * sizeof(gab_value));                               \
                                                                               \
    IP() = (void *)ks[GAB_SEND_KOFFSET + idx];                                 \
    SP() = to + have;                                                          \
                                                                               \
    SET_BLOCK(b);                                                              \
    SET_VAR(have);                                                             \
  })

#define MICRO_OP_MATCHCALL_BLOCK(idx, have)                                    \
  ({                                                                           \
    struct gab_oblock *blk = GAB_VAL_TO_BLOCK(ks[GAB_SEND_KSPEC + idx]);       \
                                                                               \
    struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(blk->p);                   \
                                                                               \
    PANIC_GUARD_STACKSPACE(3 + p->nslots - have);                              \
                                                                               \
    PUSH_FRAME(blk, have);                                                     \
                                                                               \
    IP() = (void *)ks[GAB_SEND_KOFFSET + idx];                                 \
    FB() = SP() - have;                                                        \
                                                                               \
    SET_VAR(have);                                                             \
  })

#define MICRO_OP_CALL_NATIVE(native, have, below_have, message)                \
  ({                                                                           \
    STORE();                                                                   \
                                                                               \
    CHECK_SIGNAL();                                                            \
                                                                               \
    gab_value *returnptr = RETURN_FB();                                        \
                                                                               \
    gab_value *to = SP() - (have + message);                                   \
    gab_assert(to >= FB(),                                                     \
               "EXPECTED DEST TO BE GREATER THAN FRAME BASE. DIST: %li\n",     \
               to - FB());                                                     \
                                                                               \
    gab_value *before = SP();                                                  \
                                                                               \
    uint64_t pass = (have - !message);                                         \
                                                                               \
    union gab_value_pair res =                                                 \
        (*native->function)(GAB(), pass, SP() - pass, REENTRANT());            \
                                                                               \
    RESET_REENTRANT();                                                         \
                                                                               \
    assert(SP() >= before);                                                    \
    SP() = VM()->sp;                                                           \
                                                                               \
    if (__gab_unlikely(res.status == gab_ctimeout))                            \
      assert(res.bresult != 0), VM_YIELD(res.bresult);                         \
                                                                               \
    RESET_BUMP();                                                              \
                                                                               \
    if (__gab_unlikely(res.status == gab_cvalid))                              \
      return res;                                                              \
                                                                               \
    gab_assert(SP() >= before, "Fewer than zero values returned from native"); \
    uint64_t have = SP() - before;                                             \
                                                                               \
    if (!have)                                                                 \
      PUSH(MICRO_OP_NIL()), have++;                                            \
                                                                               \
    memmove(to, before, have * sizeof(gab_value));                             \
    SP() = to + have;                                                          \
                                                                               \
    SET_VAR(below_have + have);                                                \
                                                                               \
    assert(returnptr == RETURN_FB());                                          \
  })

// fprintf(stderr, "(%i) ENTER JIT AT %p, HAVE %lu\n", GAB().wkid, IP() - 3,  \
//         VAR());                                                            \
// for (int i = 1; i < VAR() + 1; i++) {                                      \
//   gab_fprintf(stderr, "($) $: $\n", gab_number(GAB().wkid), gab_number(i), \
//               PEEK_N(i));                                                  \
// }

#define MICRO_OP_JIT_ENTER(code)                                               \
  ({                                                                           \
    [[clang::musttail]] return code(DISPATCH_ARGS());                          \
  })

/*
 * These primitives need some sort of control-flow in order
 * to work cleanly with the JIT IR.
 */
#define MICRO_OP_TAKE(channel)                                                 \
  ({                                                                           \
    if (!REENTRANT()) {                                                        \
      SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);                    \
      SEND_GUARD_ISCHN(c);                                                     \
    }                                                                          \
                                                                               \
    STORE_SP();                                                                \
                                                                               \
    CHECK_SIGNAL();                                                            \
                                                                               \
    /*                                                                         \
     * Adjust for the tuple-len value at *SP() on the stack.                   \
     * Store above it, and subract one from the stackspace to reserve it.      \
     */                                                                        \
    uint64_t stackspace = GET_STACKSPACE(SP(), SB()) - 1;                      \
                                                                               \
    gab_value v = gab_ntchntake(GAB(), c, stackspace, SP() + 1,                \
                                cGAB_VM_CHANNEL_TAKE_TRIES);                   \
                                                                               \
    RESET_REENTRANT();                                                         \
                                                                               \
    switch (v) {                                                               \
    case gab_ctimeout:                                                         \
      VM_YIELD(gab_ctimeout);                                                  \
    case gab_cinvalid:                                                         \
      VM_TERM();                                                               \
    case gab_cundefined:                                                       \
      DROP_N(have + 1);                                                        \
      PUSH(gab_none);                                                          \
                                                                               \
      SET_VAR(below_have + 1);                                                 \
      NEXT();                                                                  \
    default:                                                                   \
      DROP_N(have + 1);                                                        \
      PUSH(gab_ok);                                                            \
                                                                               \
      uint64_t len = gab_valtou(v);                                            \
      /*                                                                       \
       * ntchntake returns the number of values *available*, but will only     \
       * write up to *stackspace*.                                             \
       *                                                                       \
       * If there were more available to take than we had room for on the      \
       * stack, return an overflow.                                            \
       * */                                                                    \
      if (__gab_unlikely(len > stackspace))                                    \
        VM_PANIC(GAB_OVERFLOW, "");                                            \
                                                                               \
      /*                                                                       \
       * We now know that we wrote *len* values to the buffer, because         \
       * it is guaranteed that len <= stackspace                               \
       * */                                                                    \
      memmove(SP(), SP() + have + 1, len * sizeof(gab_value));                 \
      SP() += len;                                                             \
                                                                               \
      SET_VAR(below_have + len + 1);                                           \
      NEXT();                                                                  \
    }                                                                          \
  })

#define MICRO_OP_PUT(channel)                                                  \
  ({                                                                           \
    if (!REENTRANT()) {                                                        \
      SEND_GUARD_CACHED_MESSAGE_SPECS(ks[GAB_SEND_KSPECS]);                    \
      SEND_GUARD_ISCHN(c);                                                     \
    }                                                                          \
                                                                               \
    STORE_SP();                                                                \
                                                                               \
    CHECK_SIGNAL();                                                            \
                                                                               \
    if (REENTRANT() == c) {                                                    \
      /* If we're reentering, check that our channel                           \
       is still holding our data ptr.                                          \
       I *believe* this is sound based on the following principles:            \
        - Fibers only ever run on *one* thread, they never migrate.            \
        - Fibers don't share vm's - the address range of one stack             \
          can never overlap with another's. (If stacks become resizeable, this \
          changes)                                                             \
        - Fiber's stacks never resize                                          \
      */                                                                       \
      if (!gab_chnisclosed(c) && gab_chnmatches(c, SP() - (have - 1)))         \
        VM_YIELD(c);                                                           \
                                                                               \
      RESET_REENTRANT();                                                       \
                                                                               \
      /* If not, our put is complete and we can move on */                     \
      DROP_N(have + 1);                                                        \
                                                                               \
      PUSH(c);                                                                 \
                                                                               \
      SET_VAR(below_have + 1);                                                 \
                                                                               \
      NEXT();                                                                  \
    }                                                                          \
                                                                               \
    /* All values *but* the channel are put into the channel. */               \
    gab_value r = gab_untchnput(GAB(), c, have - 1, SP() - (have - 1),         \
                                cGAB_VM_CHANNEL_PUT_TRIES);                    \
                                                                               \
    switch (r) {                                                               \
    case gab_cinvalid:                                                         \
      VM_TERM();                                                               \
    case gab_ctimeout:                                                         \
      /* The put timed-out */                                                  \
      VM_YIELD(gab_ctimeout);                                                  \
    default:                                                                   \
      /* The put succeeded, we must yield until it completes.*/                \
      VM_YIELD(c);                                                             \
    }                                                                          \
  })

#define MICRO_OP_FIBER(block)                                                  \
  ({                                                                           \
    STORE_SP();                                                                \
                                                                               \
    CHECK_SIGNAL();                                                            \
                                                                               \
    gab_value fb = REENTRANT();                                                \
                                                                               \
    if (!REENTRANT()) {                                                        \
      fb = gab_fiber(GAB(), (struct gab_fiber_argt){                           \
                                .message = gab_message(GAB(), mGAB_CALL),      \
                                .receiver = block,                             \
                                .flags = GAB().flags,                          \
                            });                                                \
    }                                                                          \
                                                                               \
    bool spawned = gab_wkspawn(GAB(), fb);                                     \
                                                                               \
    if (spawned)                                                               \
      goto fin;                                                                \
                                                                               \
    gab_value result = gab_tchnput(GAB(), EG()->work_channel, fb, 1 << 16);    \
                                                                               \
    switch (result) {                                                          \
    /* Timed out */                                                            \
    case gab_ctimeout:                                                         \
      VM_YIELD(fb);                                                            \
    /* Terminated */                                                           \
    case gab_cinvalid:                                                         \
      VM_TERM();                                                               \
    /* For the successful put & closed case */                                 \
    case gab_cundefined:                                                       \
    case gab_cvalid:                                                           \
    fin:                                                                       \
      RESET_REENTRANT();                                                       \
                                                                               \
      DROP_N(have + 1);                                                        \
      PUSH(fb);                                                                \
                                                                               \
      SET_VAR(below_have + 1);                                                 \
                                                                               \
      NEXT();                                                                  \
    default:                                                                   \
      gab_fprintf(stdout, "UNREACHABLE: $\n", result);                         \
      assert(false && "UNEXPECTED");                                           \
    }                                                                          \
  })

#define MICRO_OP_SEND(have)                                                    \
  ({                                                                           \
    /* Have can not be 0. We need a receiver. */                               \
    if (__gab_unlikely(!have)) {                                               \
      PUSH(MICRO_OP_NIL());                                                    \
      SET_VAR(1);                                                              \
      have++;                                                                  \
    }                                                                          \
                                                                               \
    gab_value r = PEEK_N(have);                                                \
    gab_value m = ks[GAB_SEND_KMESSAGE];                                       \
                                                                               \
    if (BLOCK() && try_setup_localmatch(GAB(), m, ks, BLOCK_PROTO())) {        \
      WRITE_BYTE(GAB_SEND_CACHE_SIZE, OP_MATCHSEND_BLOCK + adjust);            \
      IP() -= GAB_SEND_CACHE_SIZE;                                             \
      NEXT();                                                                  \
    }                                                                          \
                                                                               \
    /* Do the expensive lookup */                                              \
    struct gab_impl_rest res = gab_impl(GAB(), m, r);                          \
                                                                               \
    if (__gab_unlikely(!res.status)) {                                         \
      VM_PANIC(GAB_SPECIALIZATION_MISSING, FMT_MISSINGIMPL, m, r,              \
               gab_valtype(GAB(), r));                                         \
    }                                                                          \
                                                                               \
    gab_value spec = res.status == kGAB_IMPL_PROPERTY                          \
                         ? gab_primitive(OP_SEND_PROPERTY)                     \
                         : res.as.spec;                                        \
                                                                               \
    ks[GAB_SEND_KSPECS] = atomic_load(&EG()->messages_epoch);                  \
    ks[GAB_SEND_KTYPE] = gab_valtype(GAB(), r);                                \
    ks[GAB_SEND_KSPEC] = res.as.spec;                                          \
                                                                               \
    switch (gab_valkind(spec)) {                                               \
    case kGAB_PRIMITIVE: {                                                     \
      uint8_t op = gab_valtop(spec);                                           \
                                                                               \
      if (op == OP_SEND_PRIMITIVE_CALL_BLOCK)                                  \
        op += adjust;                                                          \
                                                                               \
      WRITE_BYTE(GAB_SEND_CACHE_SIZE, op);                                     \
                                                                               \
      break;                                                                   \
    }                                                                          \
    case kGAB_BLOCK: {                                                         \
      struct gab_oblock *b = GAB_VAL_TO_BLOCK(spec);                           \
      struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(b->p);                   \
                                                                               \
      uint8_t local = (GAB_VAL_TO_PROTOTYPE(b->p)->src == BLOCK_PROTO()->src); \
      adjust |= (local << 1);                                                  \
                                                                               \
      if (local) {                                                             \
        ks[GAB_SEND_KOFFSET] = (intptr_t)proto_ip(GAB(), p);                   \
      }                                                                        \
                                                                               \
      WRITE_BYTE(GAB_SEND_CACHE_SIZE, OP_SEND_BLOCK + adjust);                 \
                                                                               \
      break;                                                                   \
    }                                                                          \
    case kGAB_NATIVE: {                                                        \
      WRITE_BYTE(GAB_SEND_CACHE_SIZE, OP_SEND_NATIVE);                         \
      break;                                                                   \
    }                                                                          \
    default:                                                                   \
      WRITE_BYTE(GAB_SEND_CACHE_SIZE, OP_SEND_CONSTANT);                       \
      break;                                                                   \
    }                                                                          \
                                                                               \
    IP() -= GAB_SEND_CACHE_SIZE;                                               \
                                                                               \
    NEXT();                                                                    \
  })

#define MICRO_OP_TRIM(want, have)                                              \
  ({                                                                           \
    uint64_t nulls = 0;                                                        \
                                                                               \
    if (have == want && want < 10) {                                           \
      WRITE_BYTE(2, OP_TRIM_EXACTLY0 + want);                                  \
                                                                               \
      IP() -= 2;                                                               \
                                                                               \
      NEXT();                                                                  \
    }                                                                          \
                                                                               \
    if (have > want && have - want < 10) {                                     \
      WRITE_BYTE(2, OP_TRIM_DOWN1 - 1 + (have - want));                        \
                                                                               \
      IP() -= 2;                                                               \
                                                                               \
      NEXT();                                                                  \
    }                                                                          \
                                                                               \
    if (want > have && want - have < 10) {                                     \
      WRITE_BYTE(2, OP_TRIM_UP1 - 1 + (want - have));                          \
                                                                               \
      IP() -= 2;                                                               \
                                                                               \
      NEXT();                                                                  \
    }                                                                          \
                                                                               \
    SP() -= have;                                                              \
                                                                               \
    if (__gab_unlikely(have != want && want != VAR_EXP)) {                     \
      if (have > want) {                                                       \
        have = want;                                                           \
      } else {                                                                 \
        nulls = want - have;                                                   \
      }                                                                        \
    }                                                                          \
                                                                               \
    SP() += have + nulls;                                                      \
                                                                               \
    while (nulls--)                                                            \
      PEEK_N(nulls + 1) = gab_nil;                                             \
                                                                               \
    SET_VAR(want);                                                             \
  })

#define MICRO_OP_USE(have)                                                     \
  ({                                                                           \
    CHECK_SIGNAL();                                                            \
                                                                               \
    STORE();                                                                   \
    uintptr_t reentrant = REENTRANT();                                         \
    union gab_value_pair mod;                                                  \
                                                                               \
    if (reentrant) {                                                           \
      assert(gab_valisfib(reentrant));                                         \
                                                                               \
      mod = gab_tfibawait(GAB(), reentrant, 0);                                \
                                                                               \
      RESET_REENTRANT();                                                       \
    } else {                                                                   \
      /*                                                                       \
       * TODO @cgab @api: Really fix this, Its rough in a lot of ways chief.   \
       * This pulling in of args/values to pass on to use'd module             \
       * is a little scuffed. I'd rather do it a different way.                \
       */                                                                      \
      gab_value shp = gab_prtshp(BLOCK()->p);                                  \
                                                                               \
      gab_value rec =                                                          \
          gab_recordfrom(GAB(), shp, 1, gab_shplen(shp), FB() + 1, nullptr);   \
                                                                               \
      bool should_reload = have > 1 ? PEEK_N(have - 1) == gab_true : false;    \
                                                                               \
      gab_value module = have > 1 ? PEEK_N(have - 1) : 0;                      \
                                                                               \
      mod = gab_use(GAB(), (struct gab_use_argt){                              \
                               .flags = should_reload ? fGAB_USE_RELOAD : 0,   \
                               .vpackage_name = r,                             \
                               .vmodule_name = module,                         \
                               .env = rec,                                     \
                           });                                                 \
    }                                                                          \
                                                                               \
    if (mod.status == gab_ctimeout) {                                          \
      assert(gab_valisfib(mod.vresult));                                       \
      VM_YIELD(mod.vresult);                                                   \
    }                                                                          \
                                                                               \
    if (mod.status != gab_cvalid)                                              \
      VM_GIVEN(mod);                                                           \
                                                                               \
    if (mod.aresult->data[0] != gab_ok)                                        \
      VM_GIVEN(mod);                                                           \
                                                                               \
    DROP_N(have + 1);                                                          \
                                                                               \
    for (uint64_t i = 1; i < mod.aresult->len; i++)                            \
      PUSH(mod.aresult->data[i]);                                              \
                                                                               \
    SET_VAR(below_have + mod.aresult->len - 1);                                \
  })

#define SET_VAR(n)                                                             \
  ({                                                                           \
    assert(SP() >= FB());                                                      \
    *SP() = n;                                                                 \
  })

#define COMPUTE_TUPLE() (VAR())

#define FRAME_SIZE 2
#define FRAME_IP 1
#define FRAME_BK 2

#define PUSH_FRAME(b, have)                                                    \
  ({                                                                           \
    assert(have < UINT32_MAX);                                                 \
    int64_t delta = (SP() - have + FRAME_SIZE) - FB();                         \
    assert((SP() - have + FRAME_SIZE) > FB());                                 \
    assert(delta > 0);                                                         \
    assert(delta < UINT32_MAX);                                                \
    SP()[-(int64_t)(have + 1)] |= ((uint64_t)delta << 32);                     \
    memmove(SP() - have + FRAME_SIZE, SP() - have, have * sizeof(gab_value));  \
    SP() += FRAME_SIZE;                                                        \
    SP()[-(int64_t)have - FRAME_IP] = (uintptr_t)IP();                         \
    SP()[-(int64_t)have - FRAME_BK] = (uintptr_t)b;                            \
    assert(*GAB_VAL_TO_FIBER(gab_thisfiber(GAB()))->vm.sb == 0);               \
  })

#define PUSH_VM_PANIC_FRAME(have) ({})

#define STORE_MICRO_OP_VM_PANIC_FRAME(have)                                    \
  ({                                                                           \
    STORE();                                                                   \
    PUSH_VM_PANIC_FRAME(have);                                                 \
  })

#define RETURN_FB_DELTA() (FB()[-(FRAME_SIZE + 1)] >> 32)
#define RETURN_FB() ((FB() - RETURN_FB_DELTA()))

#define RETURN_IP() ((uint8_t *)(void *)FB()[-FRAME_IP])
#define RETURN_BK() ((struct gab_oblock *)(void *)FB()[-FRAME_BK])
#define RETURN_HAVE() (FB()[-(FRAME_SIZE + 1)] & 0xffffffff)

#define LOAD_FRAME()                                                           \
  ({                                                                           \
    IP() = RETURN_IP();                                                        \
    FB() = RETURN_FB();                                                        \
    KB() = proto_ks(GAB(), BLOCK_PROTO());                                     \
    assert(*GAB_VAL_TO_FIBER(gab_thisfiber(GAB()))->vm.sb == 0);               \
  })

#if cGAB_DEBUG_VM
#define PUSH(value)                                                            \
  ({                                                                           \
    if (SP() > (FB() + BLOCK_PROTO()->nslots + 1)) {                           \
      fprintf(gab.eg->stderr,                                                  \
              "Stack exceeded frame "                                          \
              "(%d). %lu passed\n",                                            \
              BLOCK_PROTO()->nslots, SP() - FB() - BLOCK_PROTO()->nslots);     \
      gab_fvminspect(stdout, VM(), 0);                                         \
      exit(1);                                                                 \
    }                                                                          \
    *SP()++ = value;                                                           \
  })

#else
#define PUSH(value) (*SP()++ = value)
#endif
#define POP() (*(--SP()))
#define DROP() (SP()--)
#define POP_N(n) (SP() -= (n))
#define DROP_N(n) (SP() -= (n))
#define PEEK() (*(SP() - 1))
#define PEEK2() (*(SP() - 2))
#define PEEK3() (*(SP() - 3))
#define PEEK_N(n) (*(SP() - (n)))

#define WRITE_BYTE(dist, n) ({ *(IP() - dist) = (n); })

#define WRITE_INLINEBYTE(n) (*IP()++ = (n))

#define SKIP_BYTE (IP()++)
#define SKIP_SHORT (IP() += 2)

#define READ_BYTE (*IP()++)
#define READ_SHORT (IP() += 2, (((uint16_t)IP()[-2] << 8) | IP()[-1]))

#define READ_CONSTANT (KB()[READ_SHORT])

// Turn off the highest bit, as this is used to store tail-calling information.
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

#define MISS_CACHED_SEND(reason)                                               \
  ({                                                                           \
    IP() -= GAB_SEND_CACHE_SIZE - 1;                                           \
    [[clang::musttail]] return OP_SEND_HANDLER(DISPATCH_ARGS());               \
  })

#define MISS_CACHED_TRIM(reason)                                               \
  ({                                                                           \
    IP()--;                                                                    \
    [[clang::musttail]] return OP_TRIM_HANDLER(DISPATCH_ARGS());               \
  })

#define VM_YIELD(value)                                                        \
  ({                                                                           \
    IP() -= GAB_SEND_CACHE_SIZE;                                               \
    STORE();                                                                   \
    return vm_yield(GAB(), value);                                             \
  })

#define VM_TERM()                                                              \
  ({                                                                           \
    STORE();                                                                   \
    return vm_terminate(GAB(), "While executing $\n", gab_thisfiber(GAB()));   \
  })

#define STORE_SP() (VM()->sp = SP())
#define STORE_FP() (VM()->fp = FB())
#define STORE_IP() (VM()->ip = IP())
#define STORE_KB() (VM()->kb = KB())

#define STORE()                                                                \
  ({                                                                           \
    STORE_SP();                                                                \
    STORE_FP();                                                                \
    STORE_IP();                                                                \
    STORE_KB();                                                                \
  })

#define PANIC_GUARD_KIND(value, kind)                                          \
  if (__gab_unlikely(gab_valkind(value) != kind)) {                            \
    STORE_MICRO_OP_VM_PANIC_FRAME(1);                                          \
    VM_PANIC(GAB_TYPE_MISMATCH, FMT_TYPEMISMATCH, ks[GAB_SEND_KMESSAGE],       \
             ks[GAB_SEND_KSPEC], value, gab_valtype(GAB(), value),             \
             gab_type(GAB(), kind));                                           \
  }

#define PANIC_GUARD_ISB(value)                                                 \
  if (__gab_unlikely(!__gab_valisb(value))) {                                  \
    STORE_MICRO_OP_VM_PANIC_FRAME(have);                                       \
    VM_PANIC(GAB_TYPE_MISMATCH, FMT_TYPEMISMATCH, ks[GAB_SEND_KMESSAGE],       \
             ks[GAB_SEND_KSPEC], value, gab_valtype(GAB(), value),             \
             gab_type(GAB(), kGAB_MESSAGE));                                   \
  }

#define PANIC_GUARD_ISN(value)                                                 \
  if (__gab_unlikely(!__gab_valisn(value))) {                                  \
    STORE_MICRO_OP_VM_PANIC_FRAME(have);                                       \
    VM_PANIC(GAB_TYPE_MISMATCH, FMT_TYPEMISMATCH, ks[GAB_SEND_KMESSAGE],       \
             ks[GAB_SEND_KSPEC], value, gab_valtype(GAB(), value),             \
             gab_type(GAB(), kGAB_NUMBER));                                    \
  }

#define PANIC_GUARD_ISS(value) SEND_GUARD_KIND(value, kGAB_STRING)

#define SEND_GUARD(clause, reason)                                             \
  if (__gab_unlikely(!(clause)))                                               \
    MISS_CACHED_SEND(reason);

#define SEND_GUARD_KIND(r, k) SEND_GUARD(gab_valkind(r) == k, "Unexpected kind")

/*
 * SEND guard which checks that the
 * world is as we expect, and the receiver is a channel.
 * */
#define SEND_GUARD_ISCHN(r)                                                    \
  SEND_GUARD(gab_valkind(r) >= kGAB_CHANNEL &&                                 \
                 gab_valkind(r) <= kGAB_CHANNELCLOSED,                         \
             "Not Channel")

/*
 * SEND guard which checks that the world
 * is as we expect, and the receiver is a record.
 */
#define SEND_GUARD_ISREC(r) SEND_GUARD_KIND(r, kGAB_RECORD)

/*
 * SEND guard which checks that the world
 * is as we expect, and the receiver is a shape.
 */
#define SEND_GUARD_ISSHP(r)                                                    \
  SEND_GUARD(gab_valkind(r) == kGAB_SHAPE || gab_valkind(r) == kGAB_SHAPELIST, \
             "Not shape")

/*
 * SEND guard which compares the message record checked against last time
 * to the current rec.
 *
 * IS IT POSSIBLE THAT THE MESSAGE SPECS are *replaced* by another at the same
 * address after a collection happens, and then some sends *think* they have it
 * cached but they havent?
 */
#define SEND_GUARD_CACHED_MESSAGE_SPECS(epoch)                                 \
  SEND_GUARD(gab_valeq(atomic_load(&EG()->messages_epoch), epoch),             \
             "Global message change detected.")

#define SEND_GUARD_TYPE(r, type)                                               \
  SEND_GUARD(gab_valisa(GAB(), r, type), "Type failed")

/*
 * SEND guard which checks that the world is
 * as we expect, and that the receiver type is the
 * same as seen last time.
 */
#define SEND_GUARD_CACHED_RECEIVER_TYPE(r)                                     \
  SEND_GUARD_TYPE(r, ks[GAB_SEND_KTYPE])

#define SEND_GUARD_CACHED_MATCH_TYPE(r, ks)                                    \
  ({                                                                           \
    gab_value t = gab_valtype(GAB(), r);                                       \
    int64_t idx = MATCH_HASHT(t);                                              \
    SEND_GUARD(ks[GAB_SEND_KTYPE + idx] == t, "Match type missed");            \
  })

#define TRIM_GUARD(clause, reason)                                             \
  if (__gab_unlikely(!(clause)))                                               \
    MISS_CACHED_TRIM(reason);

#define TRIM_GUARD_EXACTLY_N(want, n)                                          \
  TRIM_GUARD(VAR() == want, "Mismatched tuple length")

#define TRIM_GUARD_UP_N(want, n)                                               \
  TRIM_GUARD((VAR() + n) == want, "Mismatched tuple length")

#define TRIM_GUARD_DOWN_N(want, n)                                             \
  TRIM_GUARD((VAR() - n) == want, "Mismatched tuple length")

#define SHORTCUT_GUARD_ARGS_LT(n)                                              \
  ({                                                                           \
    if (__gab_unlikely(have < n))                                              \
      SET_VAR(below_have + 1), NEXT();                                         \
  })

#define NILPAD_GUARD_ARGS_GTE(n)                                               \
  ({                                                                           \
    if (__gab_unlikely(have < n))                                              \
      PUSH(MICRO_OP_NIL()), have++;                                            \
  })

// If the LSB is 1, the number is not divisible by 2.
#define MICRO_OP_RECORD(len)                                                   \
  ({                                                                           \
    STORE_SP();                                                                \
    gab_record(GAB(), 2, len / 2, SP() - len, SP() + 1 - len);                 \
  })

#define MICRO_OP_RECORDFROM(shape, len)                                        \
  ({                                                                           \
    STORE_SP();                                                                \
    gab_recordfrom(GAB(), shape, 1, len, SP() - len, nullptr);                 \
  })

#define MICRO_OP_SHAPE(len)                                                    \
  ({                                                                           \
    STORE_SP();                                                                \
    gab_shape(GAB(), 1, len, SP() - len, nullptr);                             \
  })

#define MICRO_OP_LIST(n, len)                                                  \
  ({                                                                           \
    STORE_SP();                                                                \
    gab_list(GAB(), (len), SP() - ((n) + (len)));                              \
  })

#define MICRO_OP_CHANNEL()                                                     \
  ({                                                                           \
    STORE_SP();                                                                \
    gab_channel(GAB());                                                        \
  })

#define MICRO_OP_PACK_LIST(below, above)                                       \
  ({                                                                           \
    uint64_t have = COMPUTE_TUPLE();                                           \
                                                                               \
    uint64_t want = below + above;                                             \
                                                                               \
    while (have < want)                                                        \
      PUSH(MICRO_OP_NIL()), have++;                                            \
                                                                               \
    assert(have >= want);                                                      \
    int64_t len = have - want;                                                 \
                                                                               \
    gab_value *ap = SP() - above;                                              \
                                                                               \
    STORE_SP();                                                                \
                                                                               \
    gab_value rec = gab_list(GAB(), len, ap - len);                            \
                                                                               \
    DROP_N(len - 1);                                                           \
                                                                               \
    memmove(ap - len + 1, ap, above * sizeof(gab_value));                      \
                                                                               \
    PEEK_N(above + 1) = rec;                                                   \
                                                                               \
    SET_VAR(want + 1);                                                         \
  })

#define MICRO_OP_PACK_DICT(below, above)                                       \
  ({                                                                           \
    uint64_t have = COMPUTE_TUPLE();                                           \
                                                                               \
    uint64_t want = below + above;                                             \
                                                                               \
    while (have < want)                                                        \
      PUSH(MICRO_OP_NIL()), have++;                                            \
                                                                               \
    assert(have >= want);                                                      \
    int64_t len = have - want;                                                 \
                                                                               \
    gab_value *ap = SP() - above;                                              \
                                                                               \
    STORE_SP();                                                                \
                                                                               \
    gab_value rec = gab_record(GAB(), 2, len / 2, ap - len, ap - len + 1);     \
                                                                               \
    DROP_N(len - 1);                                                           \
                                                                               \
    memmove(ap - len + 1, ap, above * sizeof(gab_value));                      \
                                                                               \
    PEEK_N(above + 1) = rec;                                                   \
                                                                               \
    SET_VAR(want + 1);                                                         \
  })

#define MICRO_OP_BLOCK(p)                                                      \
  ({                                                                           \
    STORE_SP();                                                                \
    gab_value blk = gab_block(GAB(), p);                                       \
                                                                               \
    struct gab_oblock *b = GAB_VAL_TO_BLOCK(blk);                              \
    struct gab_oprototype *proto = GAB_VAL_TO_PROTOTYPE(p);                    \
                                                                               \
    for (int i = 0; i < proto->nupvalues; i++) {                               \
      uint8_t is_local = proto->data[i] & fLOCAL_LOCAL;                        \
      uint8_t index = proto->data[i] >> 1;                                     \
                                                                               \
      if (is_local)                                                            \
        b->upvalues[i] = LOCAL(index);                                         \
      else                                                                     \
        b->upvalues[i] = UPVALUE(index);                                       \
    }                                                                          \
                                                                               \
    blk;                                                                       \
  })

#define MICRO_OP_TYPE(v) (gab_valtype(GAB(), v))

#define PUSHTUPLE(n) PUSH(n);
#define POPTUPLE(n) DROP_N(n + 1);

#define MICRO_OP_RETURN()                                                      \
  ({                                                                           \
    uint64_t have = COMPUTE_TUPLE();                                           \
    uint64_t below_have = RETURN_HAVE();                                       \
                                                                               \
    gab_value *from = SP() - have;                                             \
    gab_value *to = FB() - (FRAME_SIZE + 1);                                   \
                                                                               \
    if (__gab_unlikely(RETURN_FB_DELTA() == 0))                                \
      return STORE(), SET_VAR(have), vm_ok(DISPATCH_ARGS());                   \
                                                                               \
    assert(RETURN_IP() != nullptr);                                            \
                                                                               \
    LOAD_FRAME();                                                              \
    memmove(to, from, have * sizeof(gab_value));                               \
    SP() = to + have;                                                          \
    SET_VAR(have + below_have);                                                \
                                                                               \
    assert(FB() >= VM()->sb + FRAME_SIZE);                                     \
    assert(BLOCK()->header.kind == kGAB_BLOCK);                                \
    assert(BLOCK_PROTO()->header.kind == kGAB_PROTOTYPE);                      \
  })

#define MICRO_OP_UVRECAT(r, i) (gab_uvrecat(r, i))

#define MICRO_OP_UKRECAT(r, i) (gab_ukrecat(r, i))

#define MICRO_OP_SPLATSHAPE(s)                                                 \
  ({                                                                           \
    uint64_t len = gab_shplen(s);                                              \
                                                                               \
    for (uint64_t i = 0; i < len; i++)                                         \
      PUSH(gab_ushpat(s, i));                                                  \
                                                                               \
    len;                                                                       \
  })

#define MICRO_OP_CONS_RECORD(r, arg) (gab_lstpush(GAB(), r, arg))

#define MICRO_OP_CONS(a, b) (gab_listof(GAB(), a, b))

#define MICRO_OP_SENDK() (ks[GAB_SEND_KSPEC])

#define MICRO_OP_NIL() (gab_nil)

#define MICRO_OP_SPILL(r, n) (r)

#define MICRO_OP_BINARY_ADD(a, b) (a + b)
#define MICRO_OP_BINARY_SUB(a, b) (a - b)
#define MICRO_OP_BINARY_MUL(a, b) (a * b)
#define MICRO_OP_BINARY_DIV(a, b) (a / b)
#define MICRO_OP_BINARY_LT(a, b) (a < b)
#define MICRO_OP_BINARY_LTE(a, b) (a <= b)
#define MICRO_OP_BINARY_GT(a, b) (a > b)
#define MICRO_OP_BINARY_GTE(a, b) (a >= b)
#define MICRO_OP_BINARY_BOR(a, b) (a | b)
#define MICRO_OP_BINARY_BND(a, b) (a & b)
#define MICRO_OP_BINARY_MOD(a, b)                                              \
  (__gab_unlikely(b == 0) ? (0.0 / 0.0) : (a % b))

#define BINARY_SHIFT(a, b, op, op_op)                                          \
  ({                                                                           \
    gab_int result = ((__gab_unlikely(b >= GAB_INTWIDTH)) ? (0)                \
                      : (__gab_unlikely(b < 0)) ? (a op_op(uint32_t)(-b))      \
                                                : (a op(uint32_t) b));         \
    result;                                                                    \
  })

#define MICRO_OP_BINARY_LSH(a, b) BINARY_SHIFT(a, b, <<, >>)
#define MICRO_OP_BINARY_RSH(a, b) BINARY_SHIFT(a, b, >>, <<)

#define MICRO_OP_BINARY_EQ(a, b) (gab_valeq(a, b))

#define MICRO_OP_BINARY_CONCAT(a, b)                                           \
  ({                                                                           \
    gab_value val_ab = gab_tstrcat(GAB(), a, b);                               \
                                                                               \
    CHECK_SIGNAL();                                                            \
                                                                               \
    if (val_ab == gab_cinvalid)                                                \
      VM_TERM();                                                               \
                                                                               \
    if (val_ab == gab_ctimeout)                                                \
      VM_YIELD(gab_nil);                                                       \
                                                                               \
    assert(gab_valkind(val_ab) == kGAB_STRING);                                \
                                                                               \
    val_ab;                                                                    \
  })

#define MICRO_OP_BINARY_STRLT(a, b) (strcoll(a, b) < 0)
#define MICRO_OP_BINARY_STRLTE(a, b) (strcoll(a, b) <= 0)
#define MICRO_OP_BINARY_STRGT(a, b) (strcoll(a, b) > 0)
#define MICRO_OP_BINARY_STRGTE(a, b) (strcoll(a, b) >= 0)

#define MICRO_OP_UNARY_BIN(a) (~a)
#define MICRO_OP_UNARY_LIN(a) (!a)

#define MICRO_OP_BOXN(dbl) (gab_number(dbl))
#define MICRO_OP_BOXI(i) (gab_safeinteger(i))
#define MICRO_OP_BOXB(t_or_f) (gab_bool(t_or_f))
#define MICRO_OP_BOXV(v) (v)

#define MICRO_OP_UNBOXF(v) (gab_valtof(v))
#define MICRO_OP_UNBOXI(v) (({ gab_valtoi(v); }))
#define MICRO_OP_UNBOXU(v) (({ gab_valtou(v); }))
#define MICRO_OP_UNBOXB(v) (gab_valintob(v))
#define MICRO_OP_UNBOXS(v) (gab_strdata(&v))
#define MICRO_OP_UNBOXV(v) (v)

#define MICRO_OP_UNBOXF2(v) (MICRO_OP_UNBOXF(v))
#define MICRO_OP_UNBOXI2(v) (MICRO_OP_UNBOXI(v))
#define MICRO_OP_UNBOXU2(v) (MICRO_OP_UNBOXU(v))
#define MICRO_OP_UNBOXB2(v) (MICRO_OP_UNBOXB(v))
#define MICRO_OP_UNBOXS2(v) (MICRO_OP_UNBOXS(v))
#define MICRO_OP_UNBOXV2(v) (MICRO_OP_UNBOXV(v))

#define MICRO_OP_UNBOXF_T gab_float
#define MICRO_OP_UNBOXU_T gab_uint
#define MICRO_OP_UNBOXI_T gab_int
#define MICRO_OP_UNBOXB_T bool
#define MICRO_OP_UNBOXS_T const char *
#define MICRO_OP_UNBOXV_T gab_value

#define GUARD_NOP(v)

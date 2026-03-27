#ifndef GAB_ENGINE_H
#define GAB_ENGINE_H

#include "core.h"
#include "gab.h"
#include <stdint.h>

#ifndef GAB_COLORS_IMPL
static const char *ANSI_COLORS[] = {
    GAB_GREEN, GAB_MAGENTA, GAB_RED, GAB_YELLOW, GAB_BLUE, GAB_CYAN,
};
#define GAB_COLORS_IMPL
#endif

#define GAB_COLORS_LEN (sizeof(ANSI_COLORS) / sizeof(ANSI_COLORS[0]))

enum gab_bcop_kind {
#define OP_CODE(name) OP_##name,
#include "bytecode.h"
#undef OP_CODE
};

/**
 * Structure used to actually execute bytecode
 *
 * This structure is pretty large (8kb).
 *
 * Since a lot of fibers are small and short lived, this is overkill.
 *
 * It might be a nice optimization to store a small number of frames
 * on the fiber itself, and then migrate the stack to a larger
 * one if this small stack overflows.
 *
 * TODO @cgab @runtime: Optimize fibers to begin with a small internal stack.
 * */
struct gab_vm {
  uint8_t *ip;

  gab_value *sp, *fp, *kb;

  gab_value sb[cGAB_STACK_MAX];
};

/**
 * @brief An immutable sequence of bytes.
 */
struct gab_ostring {
  struct gab_obj header;

  /**
   * A pre-computed hash of the bytes in 'data'.
   */
  uint64_t hash;

  /**
   * The number of utf8 (thus potentially multi-byte) characters.
   * -1 is used to denote strings which *are not valid utf8*. These can only
   *  represent kGAB_BINARY, not kGAB_MESSAGE, kGAB_STRING, or kGAB_MESSAGE.
   */
  uint64_t mb_len;

  /**
   * The number of bytes in 'data'
   */
  uint64_t len;

  /**
   * The data.
   */
  char data[];
};

/**
 * @brief A wrapper for a native c function.
 */
struct gab_onative {
  struct gab_obj header;

  /**
   * The underlying native function.
   */
  gab_native_f function;

  /**
   * A name, not often useful.
   */
  gab_value name;
};

/**
 * @brief A shape defines what keys are in a record
 *
 * This needs to be optimized. It:
 *  - Isn't space efficient, as it copies keys heavily.
 *  - Mutates transition vector kinda nastily
 *  - Lists, (kGAB_SHAPELIST), stores its shape and does
 *    everything O(n) when it could be constant time
 *
 * Desires from this data structure:
 *  - Constant time key->idx lookup (or scan through nearby array)
 *
 *           [s]
 *          /  \
 *      [...] [...]
 *     /
 *   [
 *
 *   big_int len;
 *
 *   gab_value edge;
 *
 *   The data array contains the keys, edges, and child values.
 *
 *   To add a transition, we have to traverse back *up* the tree to the root.
 *
 *   There is no O(log(n)) happening, its just O(n) back up bc each shape
 *   needs its own node.
 *
 *   Maybe I can introduce *shortcut edges* somehow?
 *
 *   Most code doesn't traverse down the whole tree, it puts from one shape to
 * another. That does still require traversing up the tree and copying nodes in
 * O(n)
 *
 *   gab_value data[]
 *
 */
struct gab_oshape {
  struct gab_obj header;

  uint64_t len;

  v_gab_value transitions;

  gab_value keys[];
};

/**
 * @brief A block - aka a prototype and it's captures.
 */
struct gab_oblock {
  struct gab_obj header;

  /**
   * The number of captured upvalues.
   */
  uint8_t nupvalues;

  /**
   * The prototype of the block.
   */
  gab_value p;

  /**
   * The captured values.
   */
  gab_value upvalues[];
};

/**
 * @brief A record node.
 */
struct gab_orecnode {
  struct gab_obj header;

  /**
   * @brief Length of data member. Each node has a maximum of 32 children, so
   * 1 byte is plenty.
   */
  uint8_t len;

  /**
   * @brief The children of this node. If this node is a leaf, then this will
   * hold values. Otherwise, it holds other recs or recnodes.
   */
  gab_value data[];
};

/**
 * @brief A record, gab's aggregate type. Implemented as a persistent vector,
 * with a shape for indexing.
 *
 * Records are trees of recs and recnodes.
 *  - The root of the record is guaranteed to be a gab_obj_rec. This is
 * necessary because it holds the shift and shape.
 *  - Any branches may be either gab_obj_recnode or gab_obj_rec.
 *  - The children of leaves are the values of the record itself.
 *  - A root can *also* be a leaf - this is the case when length <= 32.
 *
 * The implementation itself is based off of clojure's persistent vector. This
 * implementation is simpler than a HAMT. It also benefits from the fact that
 * hash-collisions are impossible (That is, they are the responsibility of the
 * shape)
 *
 * Benefits:
 *  - All records with len <= 32 are a *single* allocation.
 *  - Key -> Index lookup can be cached, so lookup is simple bit masking and
 * indexing.
 */
struct gab_orec {
  struct gab_obj header;

  /**
   * @brief length of data member. Nodes have a maximum width of 32 data
   * members, so 1 byte is plenty.
   */
  uint8_t len;

  /**
   * @brief shift value used to index tree as depth increases.
   */
  int32_t shift;

  /**
   * @brief The shape of this record. This determines the length of the record
   * as a whole, and the keys which are available.
   */
  gab_value shape;

  /**
   * @brief the children of this node. If this node is a leaf, then this will
   * hold the record's actual values.
   */
  gab_value data[];
};

/*
 * @brief A lightweight green-thread / coroutine / fiber.
 */
struct gab_ofiber {
  struct gab_obj header;

  /* Flags copied from the gab-triple when this fiber was created. */
  uint32_t flags;

  /* This value is managed by native-c functions that yield back to the
   * scheduler so that they don't block. It is what notifies said function that
   * it is re-entering.*/
  uintptr_t reentrant;

  /*
   * A simple bump-allocator for use by native-cfunctions.
   *
   * It resets when the frame is popped (ie: when the native c-func returns)
   */
  v_uint8_t allocator;

  /* When a user creates a fiber, A frame is setup on the stack using these
   * arrays as the module bytescode and constants.*/
  uint8_t virtual_frame_bc[4];
  gab_value virtual_frame_ks[7];

  /*
   * The vm structure which contains the data for executing bytecode.
   */
  struct gab_vm vm;

  /**
   * Result of execution
   */
  union gab_value_pair res_values;

  /**
   * The environment after execution finished
   */
  gab_value res_env;

  /**
   * Length of data array member
   */
  uint64_t len;

  /**
   * Holds the main block, and any arguments passed to it
   */
  gab_value data[];
};

/**
 * @brief A primitive for sending data between fibers.
 *
 * A channel *does not own* these values. They are usually on the c-stack or
 * gab-stack somewhere, and the thread/fiber blocks until a put/take
 * completes.
 */
struct gab_ochannel {
  struct gab_obj header;
  /* Number of values held at member *data* */
  _Atomic(uint64_t) len;
  /* Values held */
  _Atomic(gab_value *) data;
};

/**
 * @brief A container object, which holds arbitrary data.
 *
 * There are two callbacks:
 *  - one to do cleanup when the object is destroyed
 *  - one to visit children values when doing garbage collection.
 */
struct gab_obox {
  struct gab_obj header;

  /**
   * A callback called when the object is collected by the gc.
   *
   * This function should release all non-gab resources owned by the box.
   */
  gab_boxdestroy_f do_destroy;

  /**
   * A callback called when the object is visited during gc.
   *
   * This function should be used to visit all gab_values that are referenced
   * by the box.
   */
  gab_boxvisit_f do_visit;

  /**
   * The type of the box.
   */
  gab_value type;

  /**
   * The number of bytes in 'data'.
   */
  uint64_t len;

  /**
   * The data.
   */
  char data[];
};

/**
 * @brief The prototype of a block. Encapsulates everything known about a
 * block at compile time.
 */
struct gab_oprototype {
  struct gab_obj header;

  /**
   * The number of arguments, captures, slots (stack space) and locals.
   */
  uint8_t narguments, nupvalues, nslots, nlocals;

  /**
   * The source file this prototype is from.
   */
  struct gab_src *src;

  /**
   * The offset in the source's bytecode, and the length.
   */
  uint64_t offset, len;

  /**
   * The shape of the environment of the block
   */
  gab_value env;

  /**
   * Flags providing additional metadata about the prototype.
   */
  char data[];
};

#define NAME strings
#define K struct gab_ostring *
#define HASH(a) (a->hash)
#define EQUAL(a, b) (a == b)
#define LOAD cGAB_DICT_MAX_LOAD
#include "dict.h"

#define NAME gab_modules
#define K uint64_t
#define V a_gab_value *
#define DEF_V nullptr
#define HASH(a) (a)
#define EQUAL(a, b) (a == b)
#include "dict.h"

#define NAME gab_src
#define K gab_value
#define V struct gab_src *
#define DEF_V nullptr
#define HASH(a) (a)
#define EQUAL(a, b) (a == b)
#include "dict.h"

#define T struct gab_obj *
#define NAME gab_obj
#include "vector.h"

#define NAME gab_obj
#define K struct gab_obj *
#define V uint64_t
#define HASH(a) ((intptr_t)a)
#define EQUAL(a, b) (a == b)
#define DEF_V (UINT8_MAX)
#include "dict.h"

enum {
  kGAB_BUF_STK = 0,
  kGAB_BUF_INC = 1,
  kGAB_BUF_DEC = 2,
  kGAB_NBUF = 3,
};

#define GAB_GCNEPOCHS 3

struct gab_gc {
  d_gab_obj overflow_rc;
  v_gab_obj dead;
  gab_value msg[GAB_GCNEPOCHS];
};

typedef enum gab_token {
#define TOKEN(name) TOKEN##_##name,
#include "token.h"
#undef TOKEN
} gab_token;

#define T gab_token
#include "vector.h"

struct gab_src {
  gab_value name;

  a_char *source;

  v_s_char lines;

  v_gab_token tokens;

  v_s_char token_srcs;

  v_uint64_t token_lines;

  v_gab_value constants;
  v_uint8_t bytecode;
  v_uint64_t bytecode_toks;

  d_uint64_t node_begin_toks;
  d_uint64_t node_end_toks;

  uint64_t len;
  /**
   * Each OS thread needs its own copy of the bytecode and constants.
   * Both of these arrays are modified at runtime by the VM (for specializing
   * and inline cacheing)
   */
  struct src_bytecode {
    uint8_t *bytecode;
    gab_value *constants;
  } thread_bytecode[];
};

struct gab_src *gab_src(struct gab_triple gab, gab_value name, const char *src,
                        uint64_t len);

uint64_t gab_srcappend(struct gab_src *self, uint64_t len,
                       uint8_t bc[static len], uint64_t toks[static len]);

static inline void gab_srccomplete(struct gab_triple gab,
                                   struct gab_src *self) {
  for (int i = 0; i < self->len; i++) {
    uint8_t *bc = malloc(self->bytecode.len);
    memcpy(bc, self->bytecode.data, self->bytecode.len);

    gab_value *ks = malloc(self->constants.len * sizeof(gab_value));
    memcpy(ks, self->constants.data, self->constants.len * sizeof(gab_value));

    self->thread_bytecode[i] = (struct src_bytecode){bc, ks};
  }
}

void gab_srcdestroy(struct gab_src *self);

// Max locals per prototype - from gab_prototype_argt nslots field
#define GAB_JIT_MAX_LOCALS 255

enum gab_irtype_kind {
  kGAB_IRTYPE_STRING = kGAB_STRING,
  kGAB_IRTYPE_BINARY = kGAB_BINARY,
  kGAB_IRTYPE_MESSAGE = kGAB_MESSAGE,
  kGAB_IRTYPE_PRIMITIVE = kGAB_PRIMITIVE,
  kGAB_IRTYPE_NUMBER = kGAB_NUMBER,
  kGAB_IRTYPE_NATIVE = kGAB_NATIVE,
  kGAB_IRTYPE_PROTOTYPE = kGAB_PROTOTYPE,
  kGAB_IRTYPE_BLOCK = kGAB_BLOCK,
  kGAB_IRTYPE_BOX = kGAB_BOX,
  kGAB_IRTYPE_RECORD = kGAB_RECORD,
  kGAB_IRTYPE_RECORDNODE = kGAB_RECORDNODE,
  kGAB_IRTYPE_SHAPE = kGAB_SHAPE,
  kGAB_IRTYPE_SHAPELIST = kGAB_SHAPELIST,
  kGAB_IRTYPE_FIBER = kGAB_FIBER,
  kGAB_IRTYPE_FIBERDONE = kGAB_FIBERDONE,
  kGAB_IRTYPE_FIBERRUNNING = kGAB_FIBERRUNNING,
  kGAB_IRTYPE_CHANNEL = kGAB_CHANNEL,
  kGAB_IRTYPE_CHANNELCLOSED = kGAB_CHANNELCLOSED,
  kGAB_IRTYPE_UNKNOWN,
  kGAB_IRTYPE_MESSAGE_BOOL,
  kGAB_IRTYPE_UNBOXF,
  kGAB_IRTYPE_UNBOXI,
  kGAB_IRTYPE_UNBOXU,
  kGAB_IRTYPE_UNBOXB,
  kGAB_IRTYPE_UNBOXS,
};

struct gab_type_state {
  // The canonical gab_type for the local values.
  enum gab_irtype_kind slots[GAB_JIT_MAX_LOCALS];
  uint8_t nlocals, nupvalues;
};

struct gab_jtbb {
  // Identity
  struct gab_jtbbid {
    struct gab_oprototype *proto; // the prototype this block belongs to
    uint16_t block_offset;        // bytecode offset of first instruction
    struct gab_type_state
        entry_state; // the type state this version was compiled for
  } id;

  // Generated code
  void *native_code; // pointer into executable code buffer
  uint32_t code_size;

  // Lazily-computed result block.
  struct gab_blkver *successor;

  // For version chaining: multiple versions of same (proto, offset)
  struct gab_blkver *next_version;
};

static inline uint64_t hash_id(struct gab_jtbbid *id) {
  return (uintptr_t)id->proto;
}

static inline bool eq_id(struct gab_jtbbid *a, struct gab_jtbbid *b) {
  assert(a->proto->header.kind == kGAB_PROTOTYPE);
  assert(b->proto->header.kind == kGAB_PROTOTYPE);

  if (a->proto != b->proto)
    return false;

  if (a->block_offset != b->block_offset)
    return false;

  if (a->entry_state.nlocals != b->entry_state.nlocals)
    return false;
  if (a->entry_state.nupvalues != b->entry_state.nupvalues)
    return false;

  for (int i = 0; i < a->entry_state.nlocals; i++) {
    if (a->entry_state.slots[i] == kGAB_IRTYPE_UNKNOWN)
      continue;
    if (b->entry_state.slots[i] == kGAB_IRTYPE_UNKNOWN)
      continue;
    if (!gab_valeq(a->entry_state.slots[i], b->entry_state.slots[i]))
      return false;
  }

  for (int i = 0; i < a->entry_state.nupvalues; i++) {
    uint8_t slot = a->entry_state.nlocals + i;

    if (a->entry_state.slots[slot] == kGAB_IRTYPE_UNKNOWN)
      continue;
    if (b->entry_state.slots[slot] == kGAB_IRTYPE_UNKNOWN)
      continue;
    if (!gab_valeq(a->entry_state.slots[slot], b->entry_state.slots[slot]))
      return false;
  }

  return true;
}

#define K struct gab_jtbbid *
#define V struct gab_jtbb *
#define NAME bb
#define HASH(k) (hash_id(k))
#define EQUAL(a, b) (eq_id(a, b))
#define DEF_V (nullptr)
#include "dict.h"

#define HOTCOUNT_SIZE 4096
#define HOTCOUNT_MASK (HOTCOUNT_SIZE - 1)

#define HOTCOUNT_WARMUP 128

struct gab_jt {
  d_bb blocks;

  // Executable code buffer (mmap'd, PROT_READ|PROT_EXEC after fill)
  // uint8_t *code_buf;
  // uint32_t code_buf_len;
  // uint32_t code_buf_cap;

  uint8_t hotcount[HOTCOUNT_SIZE];
};

/**
 * @class The 'engine'. Stores the long-lived data
 * needed for the gab environment.
 */
struct gab_eg {
  uint64_t hash_seed;

  v_gab_value scratch;

  v_gab_value_thrd err;

  gab_value types[kGAB_NKINDS];

  // An atomic struct which tracks the state of
  // scheduling/aliveness in the workers.
  // These are swapped/compared as a whole.
  // Neat, all our state is atomic!
  _Atomic struct gab_sig {
    int32_t mask;
    int8_t schedule;
    int8_t signal;
  } sig;

  cnd_t gc_cnd;

  const char *resroots[cGAB_RESOURCE_MAX];

  struct gab_resource res[cGAB_RESOURCE_MAX];

  struct gab_gc gc;

  _Atomic gab_value messages;
  _Atomic uint64_t messages_epoch;
  gab_value work_channel;

  mtx_t shapes_mtx;
  gab_value shapes;

  mtx_t gc_mtx;
  d_strings strings;

  mtx_t sources_mtx;
  d_gab_src sources;

  mtx_t modules_mtx;
  d_gab_modules modules;

  uint32_t wait, len;

  struct gab_job {
    thrd_t td;

    uint32_t epoch;
    int32_t locked;
    v_gab_value lock_keep;

    gab_value work_channel;
    q_gab_value queue;

    struct gab_jt jt;

    struct gab_gcbuf {
      uint64_t len;
      struct gab_obj *data[cGAB_GC_MOD_BUFF_MAX];
    } buffers[kGAB_NBUF][GAB_GCNEPOCHS];
  } jobs[];
};

enum gab_irop_kind : uint8_t {

#define IR_CODE(name) kGAB_IR_##name,

#include "ir.h"

#undef IR_CODE

};

union gab_jtir {
  struct {
    enum gab_irop_kind kind;
    uint8_t type;
    union {
      struct {
        uint16_t a, b;
      };
      uint16_t args[2];
    };
    uint16_t slot;
  } op;
  gab_value value;
  uint64_t bits;
};

#define IR_SIZE 2048
#define IR_BIAS (IR_SIZE >> 1)

#define IR_KBIAS (0)
#define IR_VBIAS (IR_KBIAS + IR_BIAS)
#define IR_OBIAS (IR_VBIAS + IR_BIAS)

struct ir {
  union gab_jtir ir[IR_SIZE];
  uint32_t ks, os;

  // Maps: local index → current SSA ref (updated during block walk)
  uint16_t local_map[GAB_JIT_MAX_LOCALS];

  // Virtual stack
  uint16_t sb[IR_SIZE];
  uint16_t *sp;
};

union gab_value_pair gab_vmexec(struct gab_triple gab, gab_value fiber);

bool gab_wkspawn(struct gab_triple gab, gab_value fiber);

void gab_gccreate(struct gab_triple gab);

void gab_gcdestroy(struct gab_triple gab);

/*
 * Check if collection is necessary, and unblock the collection
 * thread if necessary
 */
bool gab_gctrigger(struct gab_triple gab);

void gab_gcdocollect(struct gab_triple gab);

void gab_gcloglen(struct gab_triple gab);
void gab_gcassertdone(struct gab_triple gab);

typedef void (*gab_gc_visitor)(struct gab_triple gab, struct gab_obj *obj);

enum variable_flag {
  fLOCAL_LOCAL = 1 << 0,
  fLOCAL_CAPTURED = 1 << 1,
  fLOCAL_INITIALIZED = 1 << 2,
  fLOCAL_REST = 1 << 3,
};

static inline void *gab_egalloc(struct gab_triple gab, struct gab_obj *obj,
                                uint64_t size) {
  if (size == 0) {
    assert(obj);

    free(obj);

    return nullptr;
  }

  assert(!obj);

  // Use 'calloc' to zero-initialize all the memory.
  return calloc(1, size);
}

struct gab_ostring *gab_egstrfind(struct gab_eg *gab, uint64_t hash,
                                  uint64_t len, const char *data);

struct gab_err_argt {
  enum gab_status status;
  const char *note_fmt;
  struct gab_src *src;
  uint64_t tok;
  int wkid;
};

#define T struct gab_err_argt
#define NAME err
#define CONCURRENT
#include "vector.h"

/*
 * @brief Construct a panic.
 */
gab_value gab_vspanicf(struct gab_triple gab, va_list vastruct,
                       struct gab_err_argt args);

/**
 * @brief Print the bytecode to the stream - useful for debugging.
 *
 * @param stream The stream to print to
 * @param proto The prototype to inspect
 * @return non-zero if an error occured.
 */
GAB_API int gab_fmodinspect(FILE *stream, gab_value prototype);

/**
 * @brief Inspect a gab_value out to stream, recursing depth times.
 */
int gab_fvalinspect(FILE *stream, gab_value self, int depth);

/* Helpers used by all the sprintf methods */
static inline int vsnprintf_through(char **dst, size_t *n, const char *fmt,
                                    va_list va) {
  int res = vsnprintf(*dst, *n, fmt, va);

  if (res > *n) {
    *dst += *n;
    *n = 0;
    return -1;
  }

  *dst += res;
  *n -= res;

  return res;
}

static inline int snprintf_through(char **dst, size_t *n, const char *fmt,
                                   ...) {
  va_list va;
  va_start(va, fmt);

  int res = vsnprintf_through(dst, n, fmt, va);
  va_end(va);

  return res;
}

/* Helpers used by all the sprintf methods */
static inline int gvsnprintf_through(char **dst, size_t *n, const char *fmt,
                                     va_list va) {
  int res = gab_vsprintf(*dst, *n, fmt, va);

  if (res < 0) {
    *dst += *n;
    *n = 0;
    return -1;
  }

  *dst += res;
  *n -= res;

  return res;
}

static inline int gsnprintf_through(char **dst, size_t *n, const char *fmt,
                                    ...) {
  va_list va;
  va_start(va, fmt);

  int res = gvsnprintf_through(dst, n, fmt, va);
  va_end(va);

  return res;
}

bool gab_wkspawn(struct gab_triple gab, gab_value fiber);

void gab_jbalive(struct gab_triple gab, int32_t wkid);

void gab_jbunalive(struct gab_triple gab, int32_t wkid);

gab_value __gab_shape(struct gab_triple gab, uint64_t len);

static inline uint8_t *proto_srcbegin(struct gab_triple gab,
                                      struct gab_oprototype *p) {
  return p->src->thread_bytecode[gab.wkid].bytecode;
}

static inline gab_value *proto_ks(struct gab_triple gab,
                                  struct gab_oprototype *p) {
  return p->src->thread_bytecode[gab.wkid].constants;
}

static inline uint8_t *proto_ip(struct gab_triple gab,
                                struct gab_oprototype *p) {
  return proto_srcbegin(gab, p) + p->offset;
}

struct gab_jtbb *gab_jttry(struct gab_triple gab, struct gab_jt *jt,
                           struct gab_oprototype *proto, uint8_t *ip,
                           gab_value *loc, gab_value *upv);

struct gab_jtbb *gab_jtchk(struct gab_triple gab, struct gab_jt *jt,
                           struct gab_oprototype *proto, uint8_t *ip,
                           gab_value *loc, gab_value *upv);

bool gab_jttick(struct gab_triple gab, struct gab_jt *jt, uint8_t *ip);

void gab_jtcreate(struct gab_jt *jt);

[[clang::preserve_none]]
union gab_value_pair gab_jtexit(struct gab_triple *__gab, struct gab_vm *__vm,
                                uint8_t *__ip, gab_value *__kb, gab_value *__fb,
                                gab_value *__sp);

#endif

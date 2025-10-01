#ifndef GAB_ENGINE_H
#define GAB_ENGINE_H

#include "core.h"
#include "gab.h"
#include <stdint.h>
#include <threads.h>

#ifdef GAB_STATUS_NAMES_IMPL
static const char *gab_status_names[] = {
#define STATUS(name, message) message,
#include "status_code.h"
#undef STATUS
};
#undef GAB_STATUS_NAMES_IMPL
#endif

#ifdef GAB_TOKEN_NAMES_IMPL
static const char *gab_token_names[] = {
#define TOKEN(message) #message,
#include "token.h"
#undef TOKEN
};
#undef GAB_TOKEN_NAMES_IMPL
#endif

typedef enum gab_opcode {
#define OP_CODE(name) OP_##name,
#include "bytecode.h"
#undef OP_CODE
} gab_opcode;

#ifdef GAB_OPCODE_NAMES_IMPL
static const char *gab_opcode_names[] = {
#define OP_CODE(name) #name,
#include "bytecode.h"
#undef OP_CODE
#undef GAB_OPCODE_NAMES_IMPL
};
#endif

/**
 * Structure used to actually execute bytecode
 */
struct gab_vm {
  uint8_t *ip;

  gab_value *sp, *fp, *kb;

  gab_value sb[cGAB_STACK_MAX];
};

/**
 * @class gab_obj
 * @brief This struct is the first member of all heap-allocated objects.
 *
 * All gab objects inherit (as far as c can do inheritance) from this struct.
 */
struct gab_obj {
  /**
   * @brief The number of live references to this object.
   *
   * If this number is overflowed, gab keeps track  of this object's
   * references in a separate, slower rec<gab_obj, uint64_t>. When the
   * reference count drops back under 255, rc returns to the fast path.
   */
  uint8_t references;
  /**
   * @brief Flags used by garbage collection and for debug information.
   */
  uint8_t flags;
  /**
   * @brief a flag denoting the kind of object referenced by this pointer -
   * defines how to interpret the remaining bytes of this allocation.
   */
  uint8_t kind;
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
  gab_value reentrant;

  /*
   * TODO: Give native c-functions an allocator here. This can be a very simple bumpallocator.
   * When a native fn returns, if it yielded we keep the bump,
   * if it fully returns we reset the bump allocator. This is safe because the
   * c-function can never nest and call into another c-native in the same fiber.
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
  unsigned char narguments, nupvalues, nslots, nlocals;

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

/**
 * @class The 'engine'. Stores the long-lived data
 * needed for the gab environment.
 */
struct gab_eg {
  _Atomic int8_t njobs;

  uint64_t nresources;

  uint64_t hash_seed;

  v_gab_value scratch;

  v_gab_value_thrd err;

  gab_value types[kGAB_NKINDS];

  struct gab_sig {
    _Atomic int8_t schedule;
    _Atomic int8_t signal;
  } sig;

  uint64_t nroots;
  const char *resroots[cGAB_RESOURCE_MAX];

  struct gab_resource res[cGAB_RESOURCE_MAX];

  struct gab_gc gc;

  _Atomic gab_value messages;
  gab_value work_channel;

  mtx_t shapes_mtx;
  gab_value shapes;

  mtx_t strings_mtx;
  d_strings strings;

  mtx_t sources_mtx;
  d_gab_src sources;

  mtx_t modules_mtx;
  d_gab_modules modules;

  uint64_t len;

  struct gab_job {
    thrd_t td;

    uint8_t alive;

    uint32_t epoch;
    int32_t locked;
    v_gab_value lock_keep;

    q_gab_value queue;

    struct gab_gcbuf {
      uint64_t len;
      struct gab_obj *data[cGAB_GC_MOD_BUFF_MAX];
    } buffers[kGAB_NBUF][GAB_GCNEPOCHS];
  } jobs[];
};

union gab_value_pair gab_vmexec(struct gab_triple gab, gab_value fiber);

bool gab_wkspawn(struct gab_triple gab);

void gab_gccreate(struct gab_triple gab);

void gab_gcdestroy(struct gab_triple gab);

/*
 * Check if collection is necessary, and unblock the collection
 * thread if necessary
 */
bool gab_gctrigger(struct gab_triple gab);

void gab_gcdocollect(struct gab_triple gab);

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

/*
 * @brief Construct a panic.
 */
gab_value gab_vspanicf(struct gab_triple gab, va_list vastruct,
                       struct gab_err_argt args);

/**
 * @brief Format the given string to the given stream.
 *
 * This format function does *not* respect the %-style formatters like printf.
 * The only supported formatter is $, which will use the next gab_value in the
 * var args.
 *
 * eg:
 * `gab_fprintf(stdout, "foo $", gab_string(gab, "bar"));`c
 *
 * @param stream The stream to print to
 * @param fmt The format string
 * @return the number of bytes written to the stream.
 */
GAB_API int gab_fprintf(FILE *stream, const char *fmt, ...);

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

gab_value __gab_shape(struct gab_triple gab, uint64_t len);

#endif

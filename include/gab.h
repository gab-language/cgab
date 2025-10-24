/**
 * @file
 * @brief A small, fast, and portable implementation of the gab programming
 * language
 */

#ifndef GAB_H
#define GAB_H

#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#include "core.h"

#define GAB_DYNLIB_MAIN "gab_lib"

#define GAB_API_INLINE static inline

#ifdef GAB_CORE
#define GAB_API [[gnu::used]]
#else
#define GAB_API extern
#endif

#define GAB_DYNLIB_MAIN_FN union gab_value_pair gab_lib(struct gab_triple gab)
#define GAB_DYNLIB_NATIVE_FN(module, name)                                     \
  union gab_value_pair gab_mod_##module##_##name(                              \
      struct gab_triple gab, uint64_t argc, gab_value *argv,                   \
      uintptr_t reentrant)

#ifdef __cplusplus
extern "C" {
#endif

#if cGAB_LIKELY
#define __gab_likely(x) (__builtin_expect(!!(x), 1))
#define __gab_unlikely(x) (__builtin_expect(!!(x), 0))
#else
#define __gab_likely(x) (x)
#define __gab_unlikely(x) (x)
#endif

/**
 * %-------------------------------%
 * |     Value Representation      |
 * %-------------------------------%
 *
 * Gab values are nan-boxed.
 *
 * An IEEE 754 double-precision float is a 64-bit value with bits laid out
 * like:
 *
 * 1 Sign bit
 * |   11 Exponent bits
 * |   |           52 Mantissa
 * |   |           |
 * [S][Exponent---][Mantissa------------------------------------------]
 *
 * The details of how these are used to represent numbers aren't really
 * relevant here as long we don't interfere with them. The important bit is
 * NaN.
 *
 * An IEEE double can represent a few magical values like NaN ("not a
 * number"), Infinity, and -Infinity. A NaN is any value where all exponent
 * bits are set:
 *
 *     NaN bits
 *     |
 * [-][11111111111][----------------------------------------------------]
 *
 * The bits set above are the only relevant ones. The rest of the bits are
 * unused.
 *
 * NaN values come in two flavors: "signalling" and "quiet". The former are
 * intended to halt execution, while the latter just flow through arithmetic
 * operations silently. We want the latter.
 *
 * Quiet NaNs are indicated by setting the highest mantissa bit:
 * We also need to set the *next* highest because of some intel shenanigans.
 *
 *                  Highest mantissa bit
 *                  |
 * [-][....NaN....][11--------------------------------------------------]
 *
 * This leaves the rest of the following bits to play with.
 *
 * Pointers to objects with data on the heap set the highest bit.
 *
 * We are left with 51 bits of mantissa to store an address.
 * Even 64-bit machines only actually use 48 bits for addresses.
 *
 *  Pointer bit set       Pointer data
 *  |                     |
 * [1][....NaN....11][--------------------------------------------------]
 *
 * Immediate values *don't* have the pointer bit set.
 * They also store a tag in the 2 bits just below the NaN.
 * This tag differentiates how to interpret the remaining 48 bits.
 *
 *      kGAB_BINARY, kGAB_STRING, kGAB_MESSAGE, kGAB_PRIMITIVE
 *                   |
 * [0][....NaN....11][--][------------------------------------------------]
 *
 * 'Primitives' are immediates which wrap further data in the lower 48 bits.
 * In most cases, this value is a literal bytecode instruction. This allows
 * the vm to implement certain message specializations as bytecode
 * instructions.
 *
 * There are some special cases which are not bytecode ops:
 *  - gab_valid
 *  - gab_cinvalid
 *  - gab_ctimeout
 *  - gab_cundefined
 *
 * These are constant values used throughout cgab.
 *
 *                    kGAB_PRIMITIVE                  Extra data
 *                    |                               |
 * [0][....NaN....11][--][------------------------------------------------]
 *
 * Gab also employs a short string optimization. Lots of strings in a gab
 * program are incredibly small, and incredibly common. values like
 *
 * none:
 * ok:
 * and even a send, like (1 + 1), stores a small string (for the message +:)
 *
 * We need to store the string's length, a null-terminator (for
 * c-compatibility), and the string's data.
 *
 * Instead of storing the length of the string, we store the amount of bytes
 * *not* used. Since there are a total of 5 bytes availble for storing string
 * data, the remaining length is computed as 5 - strlen(str).
 *
 * We do this for a special case - when the string has length 5, the remaining
 * length is 0. In this case, the byte which stores the remaining length
 * *also* serves as the null-terminator for the string.
 *
 * This layout sneakily gives us an extra byte of storage in our small
 * strings.
 *
 *             kGAB_STRING Remaining Length                             <-
 * Data |    |                                               |
 * [0][....NaN....11][--][--------][----------------------------------------]
 *                       [   0    ][   e       p       a       h       s    ]
 *                       [   3    ][----------------   0       k       o    ]
 *
 */

/*
 * A compact nan-boxed representatino of a gab value.
 */
typedef uint64_t gab_value;

/*
 * Number types for gab values
 */

/*
 * A double has 52 bits of mantissa, so the largest
 * integers that can be supported are 53-bits.
 *
 * This is the case for JS as well.
 * MaxSafeInteger is (2e53 - 1)
 *
 * C++ does not support these arbitrary bit-widths yet.
 */
#ifdef __cplusplus
typedef int64_t gab_int;
typedef uint64_t gab_uint;
#else
#define GAB_INTWIDTH 53
typedef signed _BitInt(GAB_INTWIDTH) gab_int;
typedef unsigned _BitInt(GAB_INTWIDTH) gab_uint;
#endif

// This is the MAXIMUM SAFE INTEGER, because anything greater
// cannot be exactly represented by a 64-bit floating point number.
// This is because they have 53 bits of mantissa.
#define GAB_INTMAX (2e53 - 1.0)

typedef double gab_float;

enum gab_kind {
  kGAB_STRING = 0, // MUST_STAY_ZERO
  kGAB_BINARY = 1,
  kGAB_MESSAGE = 2,
  kGAB_PRIMITIVE = 3,
  kGAB_NUMBER,
  kGAB_NATIVE,
  kGAB_PROTOTYPE,
  kGAB_BLOCK,
  kGAB_BOX,
  kGAB_RECORD,
  kGAB_RECORDNODE,
  kGAB_SHAPE,
  kGAB_SHAPELIST,
  kGAB_FIBER,
  kGAB_FIBERDONE,
  kGAB_FIBERRUNNING,
  kGAB_CHANNEL,
  kGAB_CHANNELCLOSED,
  kGAB_NKINDS,
};

#define __GAB_QNAN ((uint64_t)0x7ffc000000000000)

#define __GAB_SIGN_BIT ((uint64_t)1 << 63)

#define __GAB_TAGMASK (3)

#define __GAB_TAGOFFSET (48)

#define __GAB_TAGBITS ((uint64_t)__GAB_TAGMASK << __GAB_TAGOFFSET)

#define __GAB_VAL_TAG(val)                                                     \
  ((enum gab_kind)((__gab_valisn(val)                                          \
                        ? kGAB_NUMBER                                          \
                        : ((val) >> __GAB_TAGOFFSET) & __GAB_TAGMASK)))

// Sneakily use a union to get around the type system
GAB_API_INLINE gab_float __gab_valtod(gab_value value) {
  union {
    uint64_t bits;
    gab_float num;
  } data;
  data.bits = value;
  return data.num;
}

GAB_API_INLINE gab_value __gab_dtoval(gab_float value) {
  union {
    uint64_t bits;
    gab_float num;
  } data;
  data.num = value;
  return data.bits;
}

#define __gab_valisn(val) (((val) & __GAB_QNAN) != __GAB_QNAN)
#define gab_valisnum(val) (__gab_valisn(val))

#define __gab_valisb(val)                                                      \
  (gab_valeq(val, gab_true) || gab_valeq(val, gab_false))

#define __gab_obj(val)                                                         \
  (gab_value)(__GAB_SIGN_BIT | __GAB_QNAN | (uint64_t)(uintptr_t)(val))

#define gab_valiso(val)                                                        \
  (((val) & (__GAB_SIGN_BIT | __GAB_QNAN)) == (__GAB_SIGN_BIT | __GAB_QNAN))

#define gab_valisnew(val) (gab_valiso(val) && GAB_OBJ_IS_NEW(gab_valtoo(val)))

/*
 * The gab values true and false are implemented with sigils.
 *
 * These are simple gab strings, but serve as their own type, allowing
 * specialization on messages for sigils like true, false, ok, some, none,
 * etc.
 */

#define gab_nil                                                                \
  ((gab_value)(__GAB_QNAN | (uint64_t)kGAB_MESSAGE << __GAB_TAGOFFSET |        \
               (uint64_t)2 << 40 | (uint64_t)'n' | (uint64_t)'i' << 8 |        \
               (uint64_t)'l' << 16))

#define gab_false                                                              \
  ((gab_value)(__GAB_QNAN | (uint64_t)kGAB_MESSAGE << __GAB_TAGOFFSET |        \
               (uint64_t)0 << 40 | (uint64_t)'f' | (uint64_t)'a' << 8 |        \
               (uint64_t)'l' << 16 | (uint64_t)'s' << 24 |                     \
               (uint64_t)'e' << 32))

#define gab_true                                                               \
  ((gab_value)(__GAB_QNAN | (uint64_t)kGAB_MESSAGE << __GAB_TAGOFFSET |        \
               (uint64_t)1 << 40 | (uint64_t)'t' | (uint64_t)'r' << 8 |        \
               (uint64_t)'u' << 16 | (uint64_t)'e' << 24))

#define gab_ok                                                                 \
  ((gab_value)(__GAB_QNAN | (uint64_t)kGAB_MESSAGE << __GAB_TAGOFFSET |        \
               (uint64_t)3 << 40 | (uint64_t)'o' | (uint64_t)'k' << 8))

#define gab_none                                                               \
  ((gab_value)(__GAB_QNAN | (uint64_t)kGAB_MESSAGE << __GAB_TAGOFFSET |        \
               (uint64_t)1 << 40 | (uint64_t)'n' | (uint64_t)'o' << 8 |        \
               (uint64_t)'n' << 16 | (uint64_t)'e' << 24))

#define gab_err                                                                \
  ((gab_value)(__GAB_QNAN | (uint64_t)kGAB_MESSAGE << __GAB_TAGOFFSET |        \
               (uint64_t)2 << 40 | (uint64_t)'e' | (uint64_t)'r' << 8 |        \
               (uint64_t)'r' << 16))

/* Convert a c boolean into the corresponding gab value */
#define gab_bool(val) ((val) ? gab_true : gab_false)

/* Convert a c double into a gab value */
#define gab_number(val) (__gab_dtoval(val))

/* Create the gab value for a primitive operation */
#define gab_primitive(op)                                                      \
  ((gab_value)(__GAB_QNAN | ((uint64_t)kGAB_PRIMITIVE << __GAB_TAGOFFSET) |    \
               (op)))

/*
 * Primitives useful for the c-api. These should *never* be visible to real gab
 * code. If they are, its a bug that needs to be fixed.
 */
#define gab_cinvalid (gab_primitive(INT32_MAX))
#define gab_ctimeout (gab_primitive(INT32_MAX - 1))
#define gab_cundefined (gab_primitive(INT32_MAX - 2))
#define gab_cvalid (gab_primitive(INT32_MAX) - 3)

#define gab_union_cinvalid ((union gab_value_pair){{gab_cinvalid}})
#define gab_union_ctimeout(r) ((union gab_value_pair){{gab_ctimeout, r}})

#define gab_union_cvalid(v)                                                    \
  (union gab_value_pair) { .status = gab_cundefined, .vresult = v }

#define gab_union_ok(v)                                                        \
  (union gab_value_pair) { .status = gab_ok, .vresult = v }

/*
 * As Gab's number type is a double (64-bit floating point)
 *
 * The largest integer which can be *completely stored without any loss* is 32
 * bits.
 */
GAB_API_INLINE gab_int __gab_valtoi(gab_value v) {
  gab_float num = (__gab_valtod(v));

  if (num < -(gab_float)GAB_INTMAX)
    return 0;

  if (num >= (gab_float)GAB_INTMAX)
    return 0;

  return (gab_int)num;
}

GAB_API_INLINE gab_uint __gab_valtou(gab_value v) {
  gab_float num = (__gab_valtod(v));

  if (num >= GAB_INTMAX)
    return 0;

  return (gab_uint)(gab_int)num;
}

/* Cast a gab value to a number - a double-precsision floating point, signed, or
 * unsigned integer*/
#define gab_valtof(val) (__gab_valtod(val))
#define gab_valtoi(val) (__gab_valtoi(val))
#define gab_valtou(val) (__gab_valtou(val))

#if cGAB_LOG_GC
/* Cast a gab value to the generic object pointer */
#define gab_valtoo(val)                                                        \
  ({                                                                           \
    struct gab_obj *__o =                                                      \
        (struct gab_obj *)(uintptr_t)((val) & ~(__GAB_SIGN_BIT | __GAB_QNAN |  \
                                                __GAB_TAGBITS));               \
    if (GAB_OBJ_IS_FREED(__o)) {                                               \
      printf("UAF\t%p\t%s:%i", __o, __FUNCTION__, __LINE__);                   \
      exit(1);                                                                 \
    }                                                                          \
    __o;                                                                       \
  })
#else
/* Cast a gab value to the generic object pointer */
#define gab_valtoo(val)                                                        \
  ((struct gab_obj *)(uintptr_t)((val) & ~(__GAB_SIGN_BIT | __GAB_QNAN |       \
                                           __GAB_TAGBITS)))
#endif

/* Cast a gab value to a primitive operation */
#define gab_valtop(val) ((uint8_t)((val) & 0xff))

/* Convenience macro for getting arguments in builtins */
#define gab_arg(i) (i < argc ? argv[i] : gab_nil)

/* Convenience macro for comparing values */
#define gab_valeq(a, b) ((a) == (b))

/*
 * Gab uses a purely RC garbage collection approach.
 *
 * The algorithm is described in this paper:
 * https://researcher.watson.ibm.com/researcher/files/us-bacon/Bacon03Pure.pdf
 */
#define fGAB_OBJ_BUFFERED (1 << 6)
#define fGAB_OBJ_NEW (1 << 7)

#define GAB_OBJ_IS_BUFFERED(obj) ((obj)->flags & fGAB_OBJ_BUFFERED)
#define GAB_OBJ_IS_NEW(obj) ((obj)->flags & fGAB_OBJ_NEW)

#define GAB_OBJ_BUFFERED(obj) ((obj)->flags |= fGAB_OBJ_BUFFERED)
#define GAB_OBJ_NEW(obj) ((obj)->flags |= fGAB_OBJ_NEW)

#define GAB_OBJ_NOT_BUFFERED(obj) ((obj)->flags &= ~fGAB_OBJ_BUFFERED)
#define GAB_OBJ_NOT_NEW(obj) ((obj)->flags &= ~fGAB_OBJ_NEW)

// DEBUG purposes only
#define fGAB_OBJ_FREED (1 << 8)
#define GAB_OBJ_IS_FREED(obj) ((obj)->flags & fGAB_OBJ_FREED)
#define GAB_OBJ_FREED(obj) ((obj)->flags |= fGAB_OBJ_FREED)

#define T gab_value
#define DEF_T gab_cinvalid
#define SIZE cGAB_WORKER_LOCALQUEUE_MAX
#include "queue.h"

#define T gab_value
#include "array.h"

#define T gab_value
#include "vector.h"

#define T gab_value
#define NAME gab_value_thrd
#define V_CONCURRENT
#include "vector.h"

struct gab_gc;
struct gab_vm;
struct gab_eg;
struct gab_src;
struct gab_obj;

struct gab_triple {
  /* Pointer to the engine itself */
  struct gab_eg *eg;
  /* Flags specific to this triple */
  uint32_t flags;
  /* An index into the engine's jobs array, determining which os-thread this
   * triple is on. */
  int32_t wkid;
};

typedef void (*gab_gcvisit_f)(struct gab_triple, struct gab_obj *obj);

typedef union gab_value_pair (*gab_native_f)(struct gab_triple, uint64_t argc,
                                             gab_value *argv,
                                             uintptr_t reentrant);

typedef void (*gab_boxdestroy_f)(struct gab_triple gab, uint64_t len,
                                 char *data);

typedef void (*gab_boxvisit_f)(struct gab_triple gab, gab_gcvisit_f visitor,
                               uint64_t len, char *data);

typedef gab_value (*gab_atomswap_f)(struct gab_triple gab, gab_value current);

typedef gab_value (*gab_atomvswap_f)(struct gab_triple gab, gab_value current,
                                     va_list va);

/**
 * @brief INTERNAL: Free memory held by an object.
 *
 * @param eg The engine responsible for the object.
 * @param obj The object.
 */
GAB_API void gab_objdestroy(struct gab_triple gab, struct gab_obj *obj);

/**
 * @brief Return the size of the object's allocation, in bytes.
 *
 * @param obj The object.
 */
GAB_API uint64_t gab_obj_size(struct gab_obj *obj);

#define GAB_DYNAMIC_MODULE_SYMBOL "gab_lib"
typedef a_gab_value *(*gab_osdynmod_load)(struct gab_triple, const char *path);
typedef a_gab_value *(*gab_osdynmod)(struct gab_triple);

/*
 * @enum Gab Flags
 *
 * The 'flags' member of @see struct gab_triple may contain these flags.
 */
enum gab_flags {
  /*
   * Any AST's successfully parsed by @gab_parse will be dumped to stdout.
   * */
  fGAB_AST_DUMP = 1 << 0,
  /*
   * Any prototypes successfully compiled by @gab_cmpl will be dumped to
   * stdout.
   * */
  fGAB_BUILD_DUMP = 1 << 1,
  /*
   * Any calls to execute code will be skipped.
   * */
  fGAB_BUILD_CHECK = 1 << 2,
  /*
   * @see gab_errtocs will convert errors into structured
   * strings as opposed to pretty errors.
   */
  fGAB_ERR_STRUCTURED = 1 << 3,
};

/**
 * @class gab_create_argt
 */
struct gab_create_argt {
  /*
   * @see enum gab_flags
   */
  uint32_t flags, jobs, wait;

  /*
   * A list of resources. At each root, these
   * will be tested (in reverse order), until
   * they a module is found.
   *
   * This list should be terminated with an empty structure (specifically, an
   * empty prefix)
   */
  struct gab_resource {
    const char *prefix;
    const char *suffix;
    union gab_value_pair (*loader)(struct gab_triple, const char *, size_t len,
                                   const char *sargs[len],
                                   gab_value vargs[len]);

    bool (*exister)(const char *);
  } *resources;

  /*
   * A list of roots. Each resource is checked at
   * each root.
   *
   * This list should be terminated with a nullptr.
   */
  const char **roots;

  /* A list of modules to load automatically into the engine.
   * The name of the variable will match the name of the module.
   *
   * [ 'Messages', 'Ranges' ]
   *  -> Messages = 'Messages'.use
   *  -> Ranges = 'Ranges'.use
   *
   *  This list should be terminated with a nullptr.
   * */
  const char **modules;
};

/**
 * @brief Initialize a gab runtime.
 *
 * @param args Parameters for initialization.
 * @param gab_out The struct to initialize.
 * @return a result describing the outcome of initialization.
 */
GAB_API union gab_value_pair gab_create(struct gab_create_argt args,
                                        struct gab_triple *gab_out);

/**
 * @brief resolve a module with the engine's given loaders and roots.
 * The prefix and suffix that matched are written to prefix and suffix, if
 * provided.
 */
GAB_API const char *gab_resolve(struct gab_triple gab, const char *mod,
                                const char **prefix, const char **suffix);

GAB_API const char *gab_mresolve(const char **roots,
                                 const struct gab_resource *resources,
                                 const char *mod, const char **prefix,
                                 const char **suffix);

/**
 * @brief Free the memory owned by this triple.
 *
 * @param gab The triple to free.
 */
GAB_API void gab_destroy(struct gab_triple gab);

/**
 * @brief Print a gab_value into a buffer. including nested values as deep as
 * depth.
 *
 * The (char*) pointed to by dest will be used to traverse the buffer and
 * write no more than (*n) bytes to the buffer.
 *
 * Both *dest* and *n* will be mutated such that dest will point to the first
 * byte *after* the last written byte, and n will contain the amount of bytes
 * *remaining* after writing has completed.
 *
 * With this behavior, it is easy to make consecutive calls gab_svalinspect,
 * writing into the same buffer.
 *
 * Returns the number of bytes written, or -1 if the buffer was too small.
 *
 * If depth is negative, will print recursively without limit.
 *
 * @param stream The stream to print to
 * @param value The value to inspect
 * @param depth The depth to recurse to
 * @return the number of bytes written to the stream.
 */
GAB_API int gab_svalinspect(char **dest, size_t *n, gab_value value, int depth);
GAB_API int gab_psvalinspect(char **dest, size_t *n, gab_value value,
                             int depth);

/**
 * @brief Format the given string into the given buffer.
 *
 * This format function does *not* respect the %-style formatters like printf.
 * The only supported formatter is $, which will use the next gab_value in the
 * var args.
 *
 * Unlike libc's sprintf, will return -1 if the buffer was too small.
 *
 * eg:
 * `gab_sprintf(stdout, "foo $", gab_string(gab, "bar"));`c
 *
 * @param dst The destination buffer
 * @param n   The maximum number of bytes to write
 * @param fmt The format string
 * @return the number of bytes written to the buffer, or -1.
 */
GAB_API int gab_sprintf(char *dst, size_t n, const char *fmt, ...);
GAB_API int gab_psprintf(char *dst, size_t n, const char *fmt, ...);

/**
 * @brief Format the given string into the given buffer, with varargs. @see
 * gab_sprintf
 *
 * @param dst The destination buffer
 * @param n   The maximum number of bytes to write
 * @param fmt The format string
 * @param varargs The format arguments
 * @return the number of bytes written to the buffer, or -1.
 */
GAB_API int gab_vsprintf(char *dst, size_t n, const char *fmt, va_list varargs);
GAB_API int gab_vpsprintf(char *dst, size_t n, const char *fmt,
                          va_list varargs);

/**
 * @brief Format the given string into the given buffer, with n arguments.
 * @see gab_sprintf
 *
 * @param dst The destination buffer
 * @param n   The maximum number of bytes to write
 * @param fmt The format string
 * @param argc The number of format arguments
 * @param argv The format arguments
 * @return the number of bytes written to the buffer, or -1.
 */
GAB_API int gab_nsprintf(char *dst, size_t n, const char *fmt, uint64_t argc,
                         gab_value *argv);
GAB_API int gab_npsprintf(char *dst, size_t n, const char *fmt, uint64_t argc,
                          gab_value *argv);

/**
 * @brief Give the engine ownership of the values.
 *
 * When in c-code, it can be useful to create gab_objects which should be
 * global (ie, always kept alive). This function *does not* increment the rc
 * of the value. It instead gives the engine 'ownership', implying that when
 * the engine is freed it should *decrement* the rc of the value.
 *
 * @param eg The engine.
 * @param value The value to keep.
 * @return The number of values kept.
 */
GAB_API uint64_t gab_egkeep(struct gab_eg *eg, gab_value value);

/**
 * @brief Give the engine ownership of multiple values.
 * @see gab_egkeep.
 *
 * @param gab The engine.
 * @param len The number of values to keep.
 * @param values The values.
 * @return The total number of values kept by the engine.
 */
GAB_API uint64_t gab_negkeep(struct gab_eg *eg, uint64_t len, gab_value *argv);

/**
 * @brief As Gab code runs, errors encountered by fibers accumulate
 * in the engine. It is up to the user to choose *when* to drain
 * said errors and handle/present to a user.
 *
 * The errors are in a sentine-terminated array of gab\records.
 * It is the callers responsibility to free the pointer.
 * The array ends in gab_nil:.
 */
GAB_API gab_value *gab_egerrs(struct gab_eg *eg);

/**
 * @brief A convenient structure for returning results from c-code.
 */
union gab_value_pair {
  gab_value data[2];
  struct {
    gab_value status;
    union {
      /* A single result value. */
      gab_value vresult;
      /* An array of result values. */
      a_gab_value *aresult;
      /* An opaque bit blob. */
      uintptr_t bresult;
    };
  };
};

/**
 * @enum The possible result states of a call to gab_egimpl.
 * @see gab_egimpl_rest
 * @see gab_egimpl
 */
enum gab_impl_resk {
  /*
   * No implementation was found for this value and message.
   */
  kGAB_IMPL_NONE = 0,
  /*
   * The *type* of the receiver implements the message.
   * EG: The actual gab\shape of the record implements this message, not just
   * 'gab\record'
   */
  kGAB_IMPL_TYPE,
  /*
   * The *kind* of the receiver implements the message.
   * EG: gab\record as opposed to the record's actual gab\shape.
   */
  kGAB_IMPL_KIND,
  /*
   * There is a general/default implementation to fall back on.
   */
  kGAB_IMPL_GENERAL,
  /*
   * The receiver is a record with a matching property.
   */
  kGAB_IMPL_PROPERTY,
};

/**
 * @class gab_egimpl_rest
 * @brief The result of gab_egimpl
 * @see gab_egimpl
 */
struct gab_impl_rest {
  /**
   * @brief The type of the relevant type of the receiver.
   *
   * gab_value have multiple types - a gab_record has its shape, as well as
   * the gab.record type, as well as the general gab_cinvalid type.
   *
   * These are checked in a specific order, and the first match is used.
   */
  gab_value type;

  /**
   * @brief The specification found by the call to gab_impl. However , if
   * status is kGAB_IMPL_PROPERTY, will contain the *offset* into the record
   * for the corresponding property. @see gab_urecat
   */
  union {
    gab_value spec;
    uint64_t offset;
  } as;

  /**
   * @brief The status of this implementation resolution.
   * @see gab_impl_resk
   */
  enum gab_impl_resk status;
};

/**
 * @brief Find the implementation of a message for a given receiver.
 *
 * @param eg The engine to search for an implementation.
 * @param message The message to find.
 * @param receiver The receiver to find the implementation for.
 * @return The result of the implementation search.
 */
GAB_API struct gab_impl_rest gab_impl(struct gab_triple gab, gab_value message,
                                      gab_value receiver);

/**
 * @brief Push any number of value onto the vm's internal stack.
 *
 * This is how c-natives return values to the runtime.
 *
 * @param vm The vm to push the values onto.
 * @param value the value to push.
 * @return The number of values pushed (always one), 0 on err.
 */
#define gab_vmpush(vm, ...)                                                    \
  ({                                                                           \
    gab_value _args[] = {__VA_ARGS__};                                         \
    gab_nvmpush(vm, sizeof(_args) / sizeof(gab_value), _args);                 \
  })

/**
 * @brief Push multiple values onto the given vm's internal stack.
 * @see gab_vmpush.
 *
 * @param vm The vm that will receive the values.
 * @param len The number of values.
 * @param argv The array of values.
 */
GAB_API uint64_t gab_nvmpush(struct gab_vm *vm, uint64_t len, gab_value *argv);

/**
 * @brief Peek into the stack of the vm.
 */
GAB_API gab_value gab_vmpeek(struct gab_vm *vm, uint64_t dist);

GAB_API gab_value gab_vmpop(struct gab_vm *vm);

/**
 * @brief Inspect the frame in the vm at depth N in the callstack.
 *
 * @param stream The stream to print to.
 * @param vm The vm.
 * @param depth The depth of the callframe. '0' would be the topmost
 * callframe.
 */
GAB_API void gab_fvminspect(FILE *stream, struct gab_vm *vm, int depth);

/**
 * @brief Inspect the vm's callstack at the given depth, returning a gab_value
 * with relevant data.
 *
 * @param gab The triple to inspect.
 * @param depth The depth of the callframe. '0' would be the topmost
 * callframe.
 * @return The record.
 */
GAB_API gab_value gab_vmframe(struct gab_triple gab, uint64_t depth);

/**
 * @class gab_use_argt
 * @brief Arguments and options for executing a source string.
 */
struct gab_use_argt {
  /**
   * The name of the module to search for, as a c-string.
   */
  const char *sname;
  /**
   * The name of the module to search for, as a gab-value.
   */
  gab_value vname;
  /* Only one of sname and vname is required. */
  /**
   * @brief The number of arguments to the main block.
   */
  uint64_t len;
  /**
   * @brief The names of the arguments to the main block.
   */
  const char **sargv;
  /**
   * @brief The values of the arguments to the main block.
   */
  gab_value *argv;

  /**
   * Optional flags for compilation AND execution.
   */
  int flags;
};

/*
 * @param gab The triple
 * @param name The name of the module, as a gab_value
 * @return The values returned by the module, or nullptr if the module was not
 * found.
 */
GAB_API union gab_value_pair gab_use(struct gab_triple gab,
                                     struct gab_use_argt args);

/**
 * @brief Check if an engine has a module by name.
 *
 * @param eg The engine.
 * @param name The name of the import.
 * @returns The module if it exists, nullptr otherwise.
 */
GAB_API a_gab_value *gab_segmodat(struct gab_eg *eg, const char *name);

/**
 * @brief Put a module into the engine's import table.
 *
 * @param eg The engine.
 * @param name The name of the import. This is used to lookup the import.
 * @param mod The module to store. This is usually a block, but can be
 * anything
 * @param len The number of values returned by the module.
 * @param values The values returned by the module.
 * @returns The module if it was added, nullptr otherwise.
 */
GAB_API a_gab_value *gab_segmodput(struct gab_eg *eg, const char *name,
                                   a_gab_value *module);

/**
 * @class gab_parse_argt
 * @brief Arguments and options for compiling a string of source code.
 * @see gab_parse.
 * @see enum gab_flags.
 */
struct gab_parse_argt {
  /**
   * The name of the module, defaults to "__main__"
   */
  const char *name;
  /**
   * The number of bytes in source to consider as source code.
   * If 0, strlen(source) is used.
   */
  uint64_t source_len;
  /**
   * The source code to compile.
   */
  const char *source;
  /**
   * The number of arguments expected by the main block.
   */
  uint64_t len;
  /**
   * The names of the arguments expected by the main block.
   */
  const char **argv;
  /**
   * Optional flags for compilation.
   */
  int flags;
};

/**
 * @brief Compile a source string into an ast.
 * Flag options are defined in @link enum gab_flags.
 *
 * If a parse is successful, the pair result will be:
 *  - status:  gab_cvalid
 *  - vresult: a gab_record representing the AST.
 * Else, the pair result will be:
 *  - status:  gab_cinvalid
 *  - vresult: a gab_record representing tbe error.
 *
 * @see struct gab_build_argt.
 * @see enum gab_flags.
 *
 * @param gab The engine.
 * @param args The arguments.
 * @returns a pair of values describing the outcome of the parse.
 */
GAB_API union gab_value_pair gab_parse(struct gab_triple gab,
                                       struct gab_parse_argt args);

/**
 * @class gab_compile_argt
 * @brief Arguments and options for compiling an AST record into a block.
 * @see gab_parse.
 * @see enum gab_flags.
 */
struct gab_compile_argt {
  gab_value ast, env, bindings, mod;
  /**
   * Optional flags for compilation.
   */
  int flags;
};

/*
 * @brief Given an ast, env, bindings, and a module name, compile a prototype.
 *
 * If compilation is successful, the pair result will be:
 *  - status:  gab_cvalid
 *  - vresult: a gab_prototype
 * Else, the pair result will be:
 *  - status: gab_cinvalid
 *  - vresult: a gab_record representing tbe error.
 *
 * @param gab The engine.
 * @param args The arguments.
 * @returns a pair of values describin the outcome of compilation.
 */
GAB_API union gab_value_pair gab_compile(struct gab_triple gab,
                                         struct gab_compile_argt args);

/**
 * @brief Compile a source string into a block.
 * Flag options are defined in @link enum gab_flags.
 *
 * Building is essentially equivalent to calling gab_parse, and then
 * gab_compile.
 *
 * If compilation is successful, the pair result will be:
 *  - status:  gab_cvalid
 *  - vresult: a gab_prototype
 * Else, the pair result will be:
 *  - status: gab_cinvalid
 *  - vresult: a gab_record representing tbe error.
 *
 * @see struct gab_build_argt.
 * @see enum gab_flags.
 *
 * @param gab The engine.
 * @param args The arguments.
 * @returns a pair of values describing the outcome of the build.
 */
GAB_API union gab_value_pair gab_build(struct gab_triple gab,
                                       struct gab_parse_argt args);

/**
 * @class gab_run_argt
 * @brief Arguments and options for running a block.
 * @see gab_run
 */
struct gab_run_argt {
  /**
   * The main block to run.
   */
  gab_value main;
  /**
   * The number of arguments passed to the main block.
   */
  uint64_t len;
  /**
   * The arguments passed to the main block.
   */
  gab_value *argv;
  /**
   * Optional flags for the vm.
   */
  int flags;
};

/**
 * @brief Call a block. Under the hood, this creates and queues a fiber, then
 * block the caller until that fiber is completed.
 * @see struct gab_run_argt
 *
 * If run is without c-api level error
 *  - status:  gab_cvalid
 *  - aresult: a heap-allocated result returned
 *  - NOTE: The *gab* code may return an error. The heap-allocated result
 *    will begin with either the message gab_ok or gab_err to indicate result.
 * Else, the pair result will be:
 *  On yield (ie, a fiber queue or channel timeout)
 *     - status: gab_ctimeout
 *  On terminate (ie, a run is cancelled with sGAB_TERM)
 *     - status: gab_cinvalid
 *     - vresult: gab_record representing error (location *where* terminated)
 *
 * @param  gab The triple.
 * @param args The arguments.
 * @return A heap-allocated slice of values returned by the fiber.
 */
GAB_API union gab_value_pair gab_run(struct gab_triple gab,
                                     struct gab_run_argt args);

/**
 * @brief Asynchronously call a block. This will create and queue a fiber,
 * returning a handle to the fiber.
 * @see struct gab_run_argt
 *
 * @param  gab The triple.
 * @param args The arguments.
 * @return The fiber that was queued.
 */
GAB_API union gab_value_pair gab_arun(struct gab_triple gab,
                                      struct gab_run_argt args);

/**
 * @brief Asynchronously call a block. This will create and queue a fiber -
 * but may timeout. The tiemout only applies to the *queueing* of the fiber.
 * @see struct gab_run_argt
 *
 * @param  gab The triple.
 * @param args The arguments.
 * @return The fiber that was queued.
 */
GAB_API union gab_value_pair gab_tarun(struct gab_triple gab, size_t tries,
                                       struct gab_run_argt args);

/**
 * @class gab_send_argt
 * @brief Arguments and options for running a block.
 * @see gab_run
 */
struct gab_send_argt {
  /**
   * The message and receiver
   */
  gab_value message, receiver;
  /**
   * The number of arguments passed to the send.
   */
  uint64_t len;
  /**
   * The arguments passed to the send.
   */
  gab_value *argv;
  /**
   * Optional flags for the vm.
   */
  int flags;
};

/*
 * @brief Send a message to a receiving value.
 *
 * Performs this send in a new fiber, and will block until the fiber is done
 * running.
 *
 * If send is without c-api level error
 *  - status:  gab_cvalid
 *  - aresult: a heap-allocated result returned
 *  - NOTE: The *gab* code may return an error. The heap-allocated result
 *    will begin with either the message gab_ok or gab_err to indicate result.
 * Else, the pair result will be:
 *  On yield (ie, a fiber queue or channel timeout)
 *     - status: gab_ctimeout
 *  On terminate (ie, a run is cancelled with sGAB_TERM)
 *     - status: gab_cinvalid
 *     - vresult: gab_record representing error (location *where* terminated)
 *
 * @param gab The engine
 * @param args The arguments
 * @returns A pair of gab values describing the result
 */
GAB_API union gab_value_pair gab_send(struct gab_triple gab,
                                      struct gab_send_argt args);

/*
 * @brief Send a message to a receiving value.
 *
 * Performs this send in a new fiber, but will not block until the fiber is
 * done running. Because fibers are queued over a channel however, this will
 * block until the queued fiber is taken.
 */
GAB_API union gab_value_pair gab_asend(struct gab_triple gab,
                                       struct gab_send_argt args);

/**
 * @class gab_exec_argt
 * @brief Arguments and options for executing a source string.
 */
struct gab_exec_argt {
  /**
   * The name of the module - defaults to "gab\main".
   */
  const char *name;
  /**
   * The number of bytes in source to consider as source code.
   * If 0, strlen(source) is used.
   */
  uint64_t source_len;
  /**
   * The source code to execute.
   */
  const char *source;
  /**
   * @brief The number of arguments to the main block.
   */
  uint64_t len;
  /**
   * @brief The names of the arguments to the main block.
   */
  const char **sargv;
  /**
   * @brief The values of the arguments to the main block.
   */
  gab_value *argv;
  /**
   * Optional flags for compilation AND execution.
   */
  int flags;
};

/**
 * @brief Compile a source string to a block and run it.
 * This is equivalent to calling @link gab_build and then @link gab_run on the
 * result.
 *
 * @see struct gab_exec_argt
 *
 * @param gab The triple.
 * @param args The arguments.
 * @return A pair of gab_values describing the outcome.
 */
GAB_API union gab_value_pair gab_exec(struct gab_triple gab,
                                      struct gab_exec_argt args);

/**
 * @brief Compile a source string to a block and run it asynchronously.
 * This is equivalent to calling @link gab_cmpl and then @link gab_arun on the
 * result.
 *
 * @see struct gab_exec_argt
 *
 * @param gab The triple.
 * @param args The arguments.
 * @return A heap-allocated slice of values returned by the block.
 */
GAB_API union gab_value_pair gab_aexec(struct gab_triple gab,
                                       struct gab_exec_argt args);

/**
 * @brief Arguments and options for an interactive REPL.
 */
struct gab_repl_argt {
  /**
   * The prompt to display before each input of REPL.
   */
  const char *prompt_prefix;
  /**
   * The prompt shown when the user needs to enter additional lines.
   */
  const char *promptmore_prefix;
  /**
   * The prefix to display before each result of REPL.
   */
  const char *result_prefix;
  /**
   * The welcome message printed at the top of the REPL.
   */
  const char *welcome_message;
  /**
   * The name of the module - defaults to "__main__".
   */
  const char *name;
  /**
   * A callback function for reading a line of user input.
   */
  char *(*readline)(const char *prompt);
  /**
   * A callback function for adding lines of user input to history.
   */
  int (*add_hist)(const char *line);
  /**
   * Optional flags for compilation AND execution.
   */
  int flags;
  /**
   * The number of arguments to the main block.
   */
  uint64_t len;
  /**
   * The names of the arguments to the main block.
   */
  const char **sargv;
  /**
   * The values of the arguments to the main block.
   */
  gab_value *argv;
};

/**
 * @brief Begin an interactive REPL.
 *
 * @see struct gab_repl_argt
 *
 * @param gab The engine.
 * @param args The arguments.
 * @return A heap-allocated slice of values returned by the block.
 */
GAB_API void gab_repl(struct gab_triple gab, struct gab_repl_argt args);

/**
 * @brief Arguments for creating a specialization
 */
struct gab_def_argt {
  /**
   * The reciever and value of the specialization.
   */
  gab_value message, receiver, specialization;
};

/**
 * @brief Create a specialization on the given message for the given receiver
 * @see struct gab_spec_argt
 *
 * @param gab The triple.
 * @param args The arguments.
 * @return The message that was updated, or gab_cinvalid if specialization
 * failed.
 */
#define gab_def(gab, ...)                                                      \
  ({                                                                           \
    struct gab_def_argt defs[] = {__VA_ARGS__};                                \
    gab_ndef(gab, sizeof(defs) / sizeof(struct gab_def_argt), defs);           \
  })

/**
 * @brief Create a specialization on the given message for the given receiver
 * @see struct gab_spec_argt
 *
 * @param gab The triple.
 * @param len The number of specializations to set.
 * @param args The specializations.
 * @return true on a success
 */
GAB_API bool gab_ndef(struct gab_triple gab, uint64_t len,
                      struct gab_def_argt *args);

/**
 * @brief Get the runtime value that corresponds to the given kind.
 *
 * @param gab The engine
 * @param kind The type to retrieve the value for.
 * @return The runtime value corresponding to that type.
 */
GAB_API gab_value gab_type(struct gab_triple gab, enum gab_kind kind);

/**
 * @brief Construct an error for returning from a native message send.
 *
 * @param gab The triple.
 * @param fmt The format string.
 * @returns A pair of gab_values describing the error
 */
GAB_API union gab_value_pair gab_panicf(struct gab_triple gab, const char *fmt,
                                        ...);

/**
 * @brief Convert a gab_record error (as produced by a call to gab_build,
 * gab_compile, gab_run, etc). Into a c-string.
 */
GAB_API const char *gab_errtocs(struct gab_triple gab, gab_value err);

/**
 * @brief Construct an error for returning from a native message send.
 *
 * @param gab The triple.
 * @param fmt The format string.
 * @param va The list of var-args.
 * @returns A pair of gab_values describing the error
 */
GAB_API union gab_value_pair gab_vpanicf(struct gab_triple gab, const char *fmt,
                                         va_list va);

/**
 * @brief This wraps @see gab_panicf to give consistent error messages
 * when values don't match an expected type.
 *
 * @param gab The triple.
 * @param found The value with the mismatched type.
 * @param texpected The expected type.
 * @returns A pair of gab_values describing the error.
 */
GAB_API union gab_value_pair
gab_ptypemismatch(struct gab_triple gab, gab_value found, gab_value texpected);

/**
 * @brief Convenience function that wraps @see gab_ptypemismatch
 *
 * @param gab The triple.
 * @param found The value with the mismatched type.
 * @param texpected The expected type.
 * @returns false.
 */
GAB_API_INLINE union gab_value_pair
gab_pktypemismatch(struct gab_triple gab, gab_value found,
                   enum gab_kind texpected) {
  return gab_ptypemismatch(gab, found, gab_type(gab, texpected));
};

#if cGAB_LOG_GC

#define gab_iref(gab, val) (__gab_iref(gab, val, __FUNCTION__, __LINE__))

gab_value __gab_iref(struct gab_triple gab, gab_value val, const char *file,
                     int line);

#define gab_dref(gab, val) (__gab_dref(gab, val, __FUNCTION__, __LINE__))

gab_value __gab_dref(struct gab_triple gab, gab_value val, const char *file,
                     int line);

/**
 * @brief Increment the reference count of the value(s)
 *
 * @param vm The vm.
 * @param value The value.
 */
void __gab_niref(struct gab_triple gab, uint64_t stride, uint64_t len,
                 gab_value values[len], const char *file, int line);
#define gab_niref(gab, stride, len, values)                                    \
  (__gab_niref(gab, stride, len, values, __FUNCTION__, __LINE__))

/**
 * # Decrement the reference count of the value(s)
 *
 * @param vm The vm.
 * @param value The value.
 */
void __gab_ndref(struct gab_triple gab, uint64_t stride, uint64_t len,
                 gab_value values[len], const char *file, int line);
#define gab_ndref(gab, stride, len, values)                                    \
  (__gab_ndref(gab, stride, len, values, __FUNCTION__, __LINE__))

#else

/**
 * @brief Increment the reference count of the value(s)
 *
 * @param gab The gab triple.
 * @param value The value to increment.
 * @return the value incremented.
 */
GAB_API gab_value gab_iref(struct gab_triple gab, gab_value value);

/**
 * @brief Decrement the reference count of the value(s)
 *
 * @param vm The vm.
 * @param len The number of values.
 * @param values The values.
 * @return the value decremented.
 */
GAB_API gab_value gab_dref(struct gab_triple gab, gab_value value);

/**
 * @brief Increment the reference count of the value(s)
 *
 * @param vm The vm.
 * @param value The value.
 */
GAB_API void gab_niref(struct gab_triple gab, uint64_t stride, uint64_t len,
                       gab_value *values);

/**
 * @brief Decrement the reference count of the value(s)
 *
 * @param vm The vm.
 * @param value The value.
 */
GAB_API void gab_ndref(struct gab_triple gab, uint64_t stride, uint64_t len,
                       gab_value *values);

#endif

/*
 *
 */
#if cGAB_LOG_GC
#define gab_gcepochnext(gab) (__gab_gcepochnext(gab, __FUNCTION__, __LINE__))
void __gab_gcepochnext(struct gab_triple gab, const char *func, int line);
#else
void gab_gcepochnext(struct gab_triple gab);
#endif

/**
 * @brief Signal the engine to terminate.
 *
 * @param gab The triple.
 */
GAB_API bool gab_sigterm(struct gab_triple gab);

/**
 * @brief Trigger a garbage collection.
 * The collecting thread will begin a collection. Note that this is
 * asynchronous
 * - to synchronously trigger and wait for the completion of a collection,
 * @see gab_collect
 *
 * @param gab The triple.
 */
GAB_API bool gab_asigcoll(struct gab_triple gab);

/**
 * @brief Synchronously run the garbage collector.
 *
 * @param gab The triple.
 */
GAB_API bool gab_sigcoll(struct gab_triple gab);

/**
 * @brief Lock the garbage collector to prevent collection until gab_gcunlock
 * is called.
 * @see gab_gcunlock
 *
 * @param gc The gc to lock
 */
GAB_API void gab_gclock(struct gab_triple gab);

/**
 * @brief Unlock the given collector.
 *
 * @param gc The gc to unlock
 */
GAB_API void gab_gcunlock(struct gab_triple gab);

/**
 * @brief Get the name of a source file - aka the fully qualified path.
 *
 * @param src The source
 * @return The name
 */
GAB_API gab_value gab_srcname(struct gab_src *src);

/**
 * @brief Get the line in the source code corresponding to an offset in the
 * bytecode.
 *
 * @param src The source
 * @param offset The offset
 * @return The line in the source code
 */
GAB_API uint64_t gab_srcline(struct gab_src *src, uint64_t bytecode_offset);
GAB_API uint64_t gab_tsrcline(struct gab_src *src, uint64_t token_offset);

/**
 * @brief Get the kind of a value.
 * @see enum gab_kind
 *
 * This is not the **runtime type** of the value. For that, use
 * `gab_valtype`.
 *
 * @param value The value.
 * @return The kind of the value.
 */
GAB_API enum gab_kind gab_valkind(gab_value value);

GAB_API_INLINE bool gab_valisshp(gab_value value) {
  enum gab_kind k = gab_valkind(value);
  return k == kGAB_SHAPE | k == kGAB_SHAPELIST;
};

GAB_API_INLINE bool gab_valischn(gab_value value) {
  enum gab_kind k = gab_valkind(value);
  return k == kGAB_CHANNEL | k == kGAB_CHANNELCLOSED;
};

GAB_API_INLINE bool gab_valisfib(gab_value value) {
  enum gab_kind k = gab_valkind(value);
  return k == kGAB_FIBER | k == kGAB_FIBERDONE | k == kGAB_FIBERRUNNING;
};

/**
 * @brief Check if a value has a type *other than its kind* to check for
 * message sends.
 *
 * @param value The value to check.
 * @return true if the value supports types other than its kind.
 */
GAB_API_INLINE bool gab_valhast(gab_value value) {
  enum gab_kind k = gab_valkind(value);
  switch (k) {
  case kGAB_MESSAGE:
  case kGAB_BOX:
  case kGAB_RECORD:
    return true;
  default:
    return false;
  }
}

/* Cast a value to a (gab_ostring*) */
#define GAB_VAL_TO_STRING(value) ((struct gab_ostring *)gab_valtoo(value))

/**
 * @brief Create a gab_value from a bounded array of chars.
 *
 * @param gab The engine.
 * @param len The length of the string.
 * @param data The data.
 * @return The value.
 */
GAB_API gab_value gab_nstring(struct gab_triple gab, uint64_t len,
                              const char *data);

/**
 * @brief Create a gab_value from a c-string
 *
 * @param gab The engine.
 * @param data The data.
 * @return The value.
 */
GAB_API_INLINE gab_value gab_string(struct gab_triple gab, const char *data) {
  return gab_nstring(gab, strlen(data), data);
}

/**
 * @brief Concatenate two gab strings
 *
 * @param gab The engine.
 * @param a The first string.
 * @param b The second string.
 * @return The value.
 */
GAB_API gab_value gab_strcat(struct gab_triple gab, gab_value a, gab_value b);

GAB_API_INLINE gab_value gab_sstrcat(struct gab_triple gab, gab_value a,
                                     const char *b) {
  return gab_strcat(gab, a, gab_string(gab, b));
}

/**
 * @brief Get a pointer to the start of the string.
 *
 * This accepts a pointer because of the short string optimization, where
 * the gab_value itself embeds the string data.
 *
 * @param str The string
 * @return A pointer to the start of the string
 */
GAB_API const char *gab_strdata(gab_value *str);

/**
 * @brief Get the number of bytes in string. This is constant-time.
 *
 * @param str The string.
 * @return The length of the string.
 */
GAB_API uint64_t gab_strlen(gab_value str);

/**
 * @brief Get the byte at the given index in the binary.
 */
GAB_API int gab_binat(gab_value bin, size_t idx);

/**
 * @brief Get the number multi-byte codepoints in a string. This is
 * constant-time. (more or less)
 *
 * This should not be called on kGAB_BINARY. (As that might not be valid utf8)
 */
GAB_API uint64_t gab_strmblen(gab_value str);

/**
 * @brief Get a string's hash. This a constant-time.
 *
 * @param str The string
 * @return The hash
 */
GAB_API uint64_t gab_strhash(gab_value str);

/**
 * @brief Convert a string into it's corresponding message. This is
 * constant-time.
 *
 * @param str The string
 * @return The message
 */
GAB_API_INLINE gab_value gab_strtomsg(gab_value str) {
  assert(gab_valkind(str) == kGAB_STRING);
  return str | (uint64_t)kGAB_MESSAGE << __GAB_TAGOFFSET;
}

/**
 * @brief Convert a string into a binary. This is constant-time.
 */
GAB_API_INLINE gab_value gab_strtobin(gab_value str) {
  assert(gab_valkind(str) == kGAB_STRING);
  return str | (uint64_t)kGAB_BINARY << __GAB_TAGOFFSET;
}

/**
 * @brief Create a new message object.
 *
 * @param gab The gab engine.
 * @param len The number of bytes in data.
 * @param data The name of the message.
 * @return The new message object.
 */
GAB_API_INLINE gab_value gab_nmessage(struct gab_triple gab, uint64_t len,
                                      const char *data) {
  return gab_strtomsg(gab_nstring(gab, len, data));
}

/**
 * @brief Create a new message object.
 *
 * @param gab The gab engine.
 * @param data The name of the message.
 * @return The new message object.
 */
GAB_API_INLINE gab_value gab_message(struct gab_triple gab, const char *data) {
  return gab_strtomsg(gab_string(gab, data));
}

/**
 * @brief Convert a message into a string
 *
 * @param msg The message.
 * @return the name.
 */
GAB_API_INLINE gab_value gab_msgtostr(gab_value msg) {
  assert(gab_valkind(msg) == kGAB_MESSAGE);
  return msg & ~((uint64_t)kGAB_MESSAGE << __GAB_TAGOFFSET);
}

GAB_API_INLINE gab_value gab_ubintostr(gab_value bin) {
  assert(gab_valkind(bin) == kGAB_BINARY);
  return bin & ~((uint64_t)kGAB_BINARY << __GAB_TAGOFFSET);
}

/**
 * @brief Convert a binary into a string. This *can* fail, as a binary is not
 * guaranteed to be valid utf8.
 *
 * @param bin The binary to convert
 * @return The string if bin is valid utf8, otherwise gab_cinvalid.
 */
GAB_API_INLINE gab_value gab_bintostr(gab_value bin) {
  assert(gab_valkind(bin) == kGAB_BINARY);

  if (gab_strmblen(bin) == -1)
    return gab_cinvalid;

  return gab_ubintostr(bin);
}

GAB_API_INLINE gab_value gab_bincat(struct gab_triple gab, gab_value a,
                                    gab_value b) {
  assert(gab_valkind(a) == kGAB_BINARY);
  assert(gab_valkind(b) == kGAB_BINARY);
  assert(gab_valkind(gab_ubintostr(a)) == kGAB_STRING);
  assert(gab_valkind(gab_ubintostr(b)) == kGAB_STRING);

  gab_value astr = gab_ubintostr(a);
  gab_value bstr = gab_ubintostr(b);

  return gab_strtobin(gab_strcat(gab, astr, bstr));
}

/**
 * @brief Create a binary value from a cstring.
 *
 * @param data the cstring
 * @return the binary
 */
GAB_API_INLINE gab_value gab_binary(struct gab_triple gab, const char *data) {
  return gab_strtobin(gab_string(gab, data));
}

GAB_API_INLINE gab_value gab_nbinary(struct gab_triple gab, size_t len,
                                     const uint8_t *data) {
  return gab_strtobin(gab_nstring(gab, len, (const char *)data));
}

GAB_API_INLINE gab_value gab_nbincat(struct gab_triple gab, gab_value a,
                                     uint64_t len, const uint8_t *b) {
  assert(gab_valkind(a) == kGAB_BINARY);
  return gab_bincat(gab, a, gab_nbinary(gab, len, b));
}

/* Cast a value to a (gab_onative*) */
#define GAB_VAL_TO_NATIVE(value) ((struct gab_onative *)gab_valtoo(value))

/* Cast a value to a (gab_oblock*) */
#define GAB_VAL_TO_BLOCK(value) ((struct gab_oblock *)gab_valtoo(value))

/**
 * @brief Create a new block object, setting all captures to gab_nil.
 *
 * @param gab The gab engine.
 * @param prototype The prototype of the block.
 * @return The new block object.
 */
GAB_API gab_value gab_block(struct gab_triple gab, gab_value prototype);

#define GAB_VAL_TO_SHAPE(value) ((struct gab_oshape *)gab_valtoo(value))

#define gab_shapeof(gab, ...)                                                  \
  ({                                                                           \
    gab_value keys[] = {__VA_ARGS__};                                          \
    gab_shape(gab, 1, sizeof(keys) / sizeof(gab_value), keys, nullptr);        \
  })

#define gab_mshapeof(gab, ...)                                                 \
  ({                                                                           \
    const char *keys[] = {__VA_ARGS__};                                        \
    const uint64_t len = sizeof(keys) / sizeof(char *);                        \
    gab_value mkeys[len];                                                      \
    for (uint64_t i = 0; i < len; i++)                                         \
      mkeys[i] = gab_message(gab, keys[i]);                                    \
    gab_shape(gab, 1, sizeof(keys) / sizeof(gab_value), mkeys, nullptr);       \
  })

/*
 * @brief Create a shape.
 *
 * @param gab The engine
 * @param stride The stride between the keys in keys.
 * @param len The number of keys to traverse in keys.
 * @param keys The list of keys.
 * @param km_out The key-mask which marks repeat keys.
 * @return The new shape.
 */
GAB_API gab_value gab_shape(struct gab_triple gab, uint64_t stride,
                            uint64_t len, gab_value *keys, uint64_t *km_out);

/*
 * @brief Check if the given shape is a list.
 */
GAB_API_INLINE uint64_t gab_shpisl(gab_value shp) {
  assert(gab_valkind(shp) == kGAB_SHAPE || gab_valkind(shp) == kGAB_SHAPELIST);
  return gab_valkind(shp) == kGAB_SHAPELIST;
}

/*
 * @brief Get the number of keys in the shape.
 */
GAB_API uint64_t gab_shplen(gab_value shp);

/*
 * @brief Get a pointer to the underlying list of keys.
 */
GAB_API gab_value *gab_shpdata(gab_value shp);

/*
 * @brief Get the key at the given index, without bounds checking.
 */
GAB_API_INLINE gab_value gab_ushpat(gab_value shp, uint64_t idx) {
  assert(gab_valkind(shp) == kGAB_SHAPE || gab_valkind(shp) == kGAB_SHAPELIST);
  assert(idx < gab_shplen(shp));
  return gab_shpdata(shp)[idx];
}

GAB_API_INLINE gab_value gab_shpat(gab_value shp, uint64_t idx) {
  assert(gab_valkind(shp) == kGAB_SHAPE || gab_valkind(shp) == kGAB_SHAPELIST);

  if (idx >= gab_shplen(shp))
    return gab_cundefined;

  return gab_ushpat(shp, idx);
}

GAB_API_INLINE uint64_t gab_shpfind(gab_value shp, gab_value key) {
  assert(gab_valkind(shp) == kGAB_SHAPE || gab_valkind(shp) == kGAB_SHAPELIST);
  switch (gab_valkind(shp)) {
  case kGAB_SHAPELIST:
  case kGAB_SHAPE: {
    uint64_t len = gab_shplen(shp);
    gab_value *keys = gab_shpdata(shp);

    // TODO: SIMDIFY ME (or use trees, sure)
    for (uint64_t i = 0; i < len; i++) {
      if (gab_valeq(key, keys[i]))
        return i;
    }

    return -1;
  }
    // Fastpath for lists.
  // case kGAB_SHAPELIST: {
  //   if (gab_valkind(key) != kGAB_NUMBER)
  //     return -1;
  //
  //   uint64_t len = s->len;
  //
  //   int64_t i = gab_valtoi(key);
  //
  //   return i < len ? i : -1;
  // }
  default:
    assert(false && "UNREACHABLE");
    return -1;
  }
}

GAB_API_INLINE bool gab_shphas(gab_value shape, gab_value key) {
  return gab_shpfind(shape, key) != -1;
}

GAB_API_INLINE uint64_t gab_shptfind(gab_value shp, gab_value key);

GAB_API gab_value gab_shpwith(struct gab_triple gab, gab_value shp,
                              gab_value key);

#define gab_shpcat(gab, ...)                                                   \
  ({                                                                           \
    gab_value __shps[] = {__VA_ARGS__};                                        \
    gab_nshpcat(gab, sizeof(__shps) / sizeof(gab_value), __shps);              \
  })

/**
 * @brief Concatenate n shapes.
 *
 */
GAB_API gab_value gab_nshpcat(struct gab_triple gab, uint64_t len,
                              gab_value *shapes);

GAB_API gab_value gab_shptorec(struct gab_triple gab, gab_value shape);

GAB_API gab_value gab_shpwithout(struct gab_triple gab, gab_value shp,
                                 gab_value key);

#define GAB_VAL_TO_REC(value) ((struct gab_orec *)gab_valtoo(value))
#define GAB_VAL_TO_RECNODE(value) ((struct gab_orecnode *)gab_valtoo(value))

#define gab_recordof(gab, ...)                                                 \
  ({                                                                           \
    gab_value kvps[] = {__VA_ARGS__};                                          \
    gab_record(gab, 2, (sizeof(kvps) / sizeof(gab_value)) / 2, kvps,           \
               kvps + 1);                                                      \
  })

/**
 * @brief Create a record.
 *
 * @param gab The engine
 * @param stride Stride between key-value pairs in keys and vals.
 * @param len Number of key-value pairs.
 * @param keys The keys
 * @param vals The vals
 * @return The new record
 */
GAB_API gab_value gab_record(struct gab_triple gab, uint64_t stride,
                             uint64_t len, gab_value *keys, gab_value *vals);

/**
 * @brief Create a record.
 *
 * @param gab The engine
 * @param shape The shape that the record should have.
 * @param stride The stride between the values in vals
 * @param vals The vals
 * @param km A key-mask produced by @see gab_shape, for skipping repeat
 * values.
 * @return The new record
 */
GAB_API gab_value gab_recordfrom(struct gab_triple gab, gab_value shape,
                                 uint64_t stride, uint64_t len, gab_value *vals,
                                 uint64_t *km);

GAB_API_INLINE gab_value gab_erecord(struct gab_triple gab) {
  return gab_record(gab, 0, 0, nullptr, nullptr);
}

/**
 * @brief Create a record, with c-string keys..
 *
 * @param gab The engine
 * @param stride Stride between key-value pairs in keys and vals.
 * @param len Number of key-value pairs.
 * @param keys The keys
 * @param vals The vals
 * @return The new record
 */
GAB_API_INLINE gab_value gab_srecord(struct gab_triple gab, uint64_t len,
                                     const char **keys, gab_value *vals) {
  gab_value vkeys[len];

  gab_gclock(gab);

  for (uint64_t i = 0; i < len; i++)
    vkeys[i] = gab_message(gab, keys[i]);

  gab_value rec = gab_record(gab, 1, len, vkeys, vals);

  gab_gcunlock(gab);

  return rec;
}

/**
 * @brief Get the shape of a record
 *
 * @param rec The record
 * @return The shape of the record
 */
GAB_API gab_value gab_recshp(gab_value record);

/**
 * @brief Get the length of a record
 *
 * @param rec The record
 * @return The number of key-value pairs in the record
 */
GAB_API_INLINE uint64_t gab_reclen(gab_value record) {
  assert(gab_valkind(record) == kGAB_RECORD);
  return gab_shplen(gab_recshp(record));
}

/**
 * @brief Find the offset of a key in the record
 *
 * @param record The record to look in
 * @param key The key to search for
 * @return The offset, or -1 if not found
 */
GAB_API_INLINE uint64_t gab_recfind(gab_value record, gab_value key) {
  return gab_shpfind(gab_recshp(record), key);
}

/**
 * @brief Check if a record has a given key
 *
 * @param record The record to look in
 * @param key The key to check for
 * @return true if the key exists in the record.
 */
GAB_API_INLINE bool gab_rechas(gab_value record, gab_value key) {
  return gab_shphas(gab_recshp(record), key);
}

/**
 * @brief Check if an index is within a record
 *
 * @param record The record to look in
 * @param idx The index to check
 * @return true if the index is valid within the record
 */
GAB_API_INLINE bool gab_urechas(gab_value record, uint64_t index) {
  assert(gab_valkind(record) == kGAB_RECORD);
  return index < gab_reclen(record);
}

/**
 * @brief Get the key at a given index in the record. Does no bounds checking.
 *
 * @param record The record to look in
 * @param index The index of the key
 *
 * @return The key at that index
 */
GAB_API_INLINE gab_value gab_ukrecat(gab_value record, uint64_t index) {
  assert(gab_valkind(record) == kGAB_RECORD);
  assert(gab_urechas(record, index));
  return gab_ushpat(gab_recshp(record), index);
}

/**
 * @brief Get the value at a given index in the record. Does no bounds
 * checking.
 *
 * @param record The record to look in
 * @param index The index of the value to get
 * @return the value at index
 */
GAB_API gab_value gab_uvrecat(gab_value record, uint64_t index);

/**
 * @brief Return a new record with the new value at the given index. Does no
 * bounds checking.
 *
 * @param record The record to begin with
 * @param index The index to replace
 * @param value The new value
 * @return a new record, with value at index.
 */
GAB_API gab_value gab_urecput(struct gab_triple gab, gab_value record,
                              uint64_t index, gab_value value);

/**
 * @brief Get the value at a given key in the record. If the key doesn't
 * exist, returns undefined.
 *
 * @param record The record to check
 * @param key The key to look for
 * @return the value associated with key, or undefined.
 */
GAB_API_INLINE gab_value gab_recat(gab_value record, gab_value key) {
  assert(gab_valkind(record) == kGAB_RECORD);
  uint64_t i = gab_recfind(record, key);

  if (i == -1)
    return gab_cundefined;

  return gab_uvrecat(record, i);
}

/**
 * @brief Get the value at a given string in the record.
 *
 * @param gab The engine
 * @param record The record to look in
 * @param key A c-string to convert into a gab string
 * @return the value at gab_string(key)
 */
GAB_API_INLINE gab_value gab_srecat(struct gab_triple gab, gab_value record,
                                    const char *key) {
  return gab_recat(record, gab_string(gab, key));
}

/**
 * @brief Get the value at a given message in the record.
 *
 * @param gab The engine
 * @param record The record to look in
 * @param key A c-string to convert into a gab message
 * @return the value at gab_message(key)
 */
GAB_API_INLINE gab_value gab_mrecat(struct gab_triple gab, gab_value rec,
                                    const char *key) {
  return gab_recat(rec, gab_message(gab, key));
}

/**
 * @brief Get a new record with value put at key.
 *
 * @param gab The engine
 * @param record The record to start with
 * @param key The key
 * @param value The value
 * @return a new record with value at key
 */
GAB_API gab_value gab_recput(struct gab_triple gab, gab_value record,
                             gab_value key, gab_value value);

/**
 * @brief Remove a key from a record.
 *
 * @param gab The engine
 * @param record The record to start with
 * @param key The key
 * @param value_out If provided, will be filled with the value at they removed
 * key.
 * @return a new record without key.
 */
GAB_API gab_value gab_rectake(struct gab_triple gab, gab_value record,
                              gab_value key, gab_value *value_out);

/**
 * @brief Concatenate two maps together. The last value of a given key will
 * prevail.
 *
 * @param gab The engine
 * @param record_over The record to merge *over&
 * @param record_under The record to merge *under*
 * @return a new record with all keys from both records.
 */
GAB_API gab_value gab_nreccat(struct gab_triple gab, uint64_t len,
                              gab_value *records);

#define gab_reccat(gab, ...)                                                   \
  ({                                                                           \
    gab_value __recs[] = {__VA_ARGS__};                                        \
    gab_nreccat(gab, sizeof(__recs) / sizeof(gab_value), __recs);              \
  })

#define gab_lstcat(gab, ...)                                                   \
  ({                                                                           \
    gab_value __recs[] = {__VA_ARGS__};                                        \
    gab_nlstcat(gab, sizeof(__recs) / sizeof(gab_value), __recs);              \
  })

/**
 * @brief Concatenate n records, left to rate
 *
 */
GAB_API gab_value gab_nlstcat(struct gab_triple gab, uint64_t len,
                              gab_value *records);

#define gab_lstpush(gab, list, ...)                                            \
  ({                                                                           \
    gab_value __vals[] = {__VA_ARGS__};                                        \
    gab_nlstpush(gab, list, sizeof(__vals) / sizeof(gab_value), __vals);       \
  })

/**
 * @brief Push n values onto the end of a list-record
 */
GAB_API gab_value gab_nlstpush(struct gab_triple gab, gab_value list,
                               uint64_t len, gab_value *values);

/**
 * @brief Pop the last value from a record, returning the new record.
 *
 * The popped value will be written to out_val, if it is not nullptr.
 */
GAB_API_INLINE gab_value gab_recpop(struct gab_triple gab, gab_value rec,
                                    gab_value *out_val, gab_value *out_key) {
  gab_value lastkey = gab_ushpat(gab_recshp(rec), gab_reclen(rec) - 1);

  if (out_key)
    *out_key = lastkey;

  return gab_rectake(gab, rec, lastkey, out_val);
}

GAB_API_INLINE uint64_t gab_recisl(gab_value rec) {
  return gab_shpisl(gab_recshp(rec));
}

/**
 * @brief Get the nth value in a list.
 *
 * Assumes that 'rec' is a gab_record list. Bound-checks n.
 */
GAB_API_INLINE gab_value gab_lstat(gab_value lst, uint64_t n) {
  assert(gab_recisl(lst));

  if (n > gab_reclen(lst))
    return gab_cundefined;

  return gab_uvrecat(lst, n);
}

#define GAB_VAL_TO_FIBER(value) ((struct gab_ofiber *)gab_valtoo(value))

struct gab_fiber_argt {
  uint64_t argc;

  gab_value receiver, message, *argv;

  int flags;
};

/**
 * @brief Create a fiber.
 *
 * @param gab The engine
 * @param receiver The receiver to send message to
 * @param message The message to send
 * @param argc The number of arguments to the block
 * @param argv The arguments to the block
 * @return A fiber, ready to be run
 */
GAB_API gab_value gab_fiber(struct gab_triple gab, struct gab_fiber_argt args);

/*
 * @brief Get a pointer to the vm within the fiber.
 */
GAB_API struct gab_vm *gab_fibvm(gab_value fiber);

/**
 * @brief Block the caller until this fiber is completed.
 *
 * @param fiber The fiber
 */
GAB_API union gab_value_pair gab_fibawait(struct gab_triple gab,
                                          gab_value fiber);

GAB_API union gab_value_pair gab_tfibawait(struct gab_triple gab,
                                           gab_value fiber, size_t tries);

GAB_API gab_value gab_fibawaite(struct gab_triple gab, gab_value fiber);

GAB_API gab_value gab_fibstacktrace(struct gab_triple gab, gab_value fiber);

/*
 * @brief Using the fiber's bump allocator, allocate n bytes and return a
 * pointer to it
 */
GAB_API void *gab_fibmalloc(gab_value fiber, uint64_t n);

/*
 * @brief Push a byte onto the fiber's bump allocator.
 */
GAB_API void gab_fibpush(gab_value fiber, uint8_t b);

/*
 * @brief Get a pointer into the bump allocator, at offset n.
 */
GAB_API void *gab_fibat(gab_value fiber, uint64_t n);

/*
 * @brief Get the size of the arena.
 */
GAB_API uint64_t gab_fibsize(gab_value fiber);

GAB_API_INLINE bool gab_fibisrunning(gab_value fiber) {
  return gab_valkind(fiber) == kGAB_FIBERRUNNING;
}

GAB_API_INLINE bool gab_fibisdone(gab_value fiber) {
  return gab_valkind(fiber) == kGAB_FIBERDONE;
}

/*
 * @brief Return the fiber running in this thread.
 */
GAB_API gab_value gab_thisfiber(struct gab_triple gab);

/*
 * @brief Return the specialization for a given message and receiver.
 *
 * @param gab The gab triple.
 * @param message The message.
 * @param receiver The receiver.
 *
 * @return The spec
 */
GAB_API_INLINE gab_value gab_thisfibmsgat(struct gab_triple gab,
                                          gab_value message,
                                          gab_value receiver);

GAB_API_INLINE gab_value gab_thisfibmsgrec(struct gab_triple gab,
                                           gab_value message);

/**
 * @brief Create a gab\channel.
 *
 * @param gab The engine
 * @return The channel
 */
GAB_API gab_value gab_channel(struct gab_triple gab);

/**
 * @brief Put a value on the given channel.
 *
 * @param gab The engine
 * @param channel The channel
 * @param value The value to put
 */
GAB_API gab_value gab_chnput(struct gab_triple gab, gab_value channel,
                             gab_value value);

GAB_API gab_value gab_tchnput(struct gab_triple gab, gab_value channel,
                              gab_value value, uint64_t tries);

GAB_API gab_value gab_nchnput(struct gab_triple gab, gab_value channel,
                              uint64_t len, gab_value *value);

GAB_API gab_value gab_ntchnput(struct gab_triple gab, gab_value channel,
                               uint64_t len, gab_value *value, uint64_t tries);

/*
 * This is an unsafe version of channel put.
 * It is unsafe because it is *not atomic*. It will block for up to tries tries,
 * and try to put the slice of values in the channel.
 * It *will not* block until a taker arrives.
 * It *will not* atomically **undo** the put if a taker never arrives.
 * Because of this, it may mutate the channel (leave it with values inside).
 */
GAB_API gab_value gab_untchnput(struct gab_triple gab, gab_value channel,
                                uint64_t len, gab_value *value, uint64_t tries);

GAB_API gab_value gab_untchntake(struct gab_triple gab, gab_value channel,
                                 uint64_t len, gab_value *value,
                                 uint64_t tries);

/**
 * @brief Take a value from the given channel. This will block the caller
 * until a value is available to take.
 *
 * @param gab The engine
 * @param channel The channel
 * @return The value taken
 */
GAB_API gab_value gab_chntake(struct gab_triple gab, gab_value channel);

GAB_API gab_value gab_nchntake(struct gab_triple gab, gab_value channel,
                               uint64_t len, gab_value *data);

GAB_API gab_value gab_tchntake(struct gab_triple gab, gab_value channel,
                               uint64_t tries);

GAB_API gab_value gab_ntchntake(struct gab_triple gab, gab_value channel,
                                uint64_t len, gab_value *data, uint64_t tries);

/**
 * @brief Close the given channel. A closed channel cannot receive new values.
 *
 * @param channel The channel
 */
GAB_API void gab_chnclose(gab_value channel);

/**
 * @brief Return true if the given channel is closed
 *
 * @param channel The channel
 * @return whetehr or not the channel is closed
 */
GAB_API bool gab_chnisclosed(gab_value channel);

/**
 * @brief Return true if the given channel is full
 *
 * @param channel The channel
 * @return whetehr or not the channel is full
 */
GAB_API bool gab_chnisfull(gab_value channel);

/**
 * @brief Return true if the given channel is empty
 *
 * @param channel The channel
 * @return whetehr or not the channel is empty
 */
GAB_API bool gab_chnisempty(gab_value channel);

GAB_API bool gab_chnmatches(gab_value channel, gab_value *ptr);

/* Cast a value to a (gab_ochannel*) */
#define GAB_VAL_TO_CHANNEL(value) ((struct gab_ochannel *)gab_valtoo(value))

#define GAB_VAL_TO_BOX(value) ((struct gab_obox *)gab_valtoo(value))

/**
 * The arguments for creating a box.
 */
struct gab_box_argt {
  /**
   * The number of bytes in 'data'.
   */
  uint64_t size;
  /**
   * The user data.
   */
  void *data;
  /**
   * The type of the box.
   */
  gab_value type;
  /**
   * A callback called when the object is collected by the gc.
   */
  gab_boxdestroy_f destructor;
  /**
   * A callback called when the object is visited during gc.
   */
  gab_boxvisit_f visitor;
};

/**
 * @brief Create a gab value which wraps some user data.
 * @see struct gab_box_argt
 *
 * @param gab The engine.
 * @param args The arguments.
 *
 * @return The value.
 */
GAB_API gab_value gab_box(struct gab_triple gab, struct gab_box_argt args);

/**
 * @brief Get the length of the user data from a boxed value.
 *
 * @param box The box.
 * @return The user data.
 */
GAB_API inline uint64_t gab_boxlen(gab_value box);

/**
 * @brief Get the user data from a boxed value.
 *
 * @param box The box.
 * @return The user data.
 */
GAB_API void *gab_boxdata(gab_value box);

/**
 * @brief Get the user type from a boxed value.
 *
 * @param box The box.
 * @return The user type.
 */
GAB_API gab_value gab_boxtype(gab_value value);

/* Cast a value to a (gab_obj_bprototype*) */
#define GAB_VAL_TO_PROTOTYPE(value) ((struct gab_oprototype *)gab_valtoo(value))

/**
 * @brief Arguments for creating a prototype.
 */
struct gab_prototype_argt {
  char narguments, nslots, nlocals, nupvalues, *flags, *indexes, *data;
  gab_value env;
};

/**
 * @brief Create a new prototype object.
 *
 * @param gab The gab engine.
 * @param src The source file.
 * @param begin The start of the bytecode.
 * @param end The end of the bytecode.
 * @param args The arguments.
 * @see struct gab_blkproto_argt
 * @return The new block prototype object.
 */
GAB_API gab_value gab_prototype(struct gab_triple gab, struct gab_src *src,
                                uint64_t begin, uint64_t len,
                                struct gab_prototype_argt args);

GAB_API gab_value gab_prtenv(gab_value prt);

GAB_API_INLINE gab_value gab_prtshp(gab_value prt) {
  gab_value env = gab_prtenv(prt);
  uint64_t len = gab_reclen(env);
  assert(len > 0);
  return gab_recshp(gab_uvrecat(env, len - 1));
}

GAB_API gab_value gab_prtparams(struct gab_triple gab, gab_value prt);

GAB_API gab_value gab_blkproto(gab_value block);

GAB_API_INLINE gab_value gab_blkparams(struct gab_triple gab, gab_value block) {
  return gab_prtparams(gab, gab_blkproto(block));
}

GAB_API_INLINE gab_value gab_blkenv(gab_value block) {
  return gab_prtenv(gab_blkproto(block));
}

GAB_API_INLINE gab_value gab_blkshp(gab_value block) {
  return gab_prtshp(gab_blkproto(block));
}

/**
 * @brief Create a native wrapper to a c function with a gab_string name.
 *
 * @param gab The triple.
 * @param name The name of the native.
 * @param f The c function.
 *
 * @return The value.
 */
GAB_API gab_value gab_native(struct gab_triple gab, gab_value name,
                             gab_native_f f);

/**
 * @brief Create a native wrapper to a c function with a c-string name.
 *
 * @param gab The triple.
 * @param name The name of the native.
 * @param f The c function.
 * @return The value.
 */
GAB_API gab_value gab_snative(struct gab_triple gab, const char *name,
                              gab_native_f f);

#define gab_listof(gab, ...)                                                   \
  ({                                                                           \
    gab_value items[] = {__VA_ARGS__};                                         \
    gab_list(gab, sizeof(items) / sizeof(gab_value), items);                   \
  })

/**
 * @brief Bundle a list of values into a tuple.
 *
 * @param gab The engine
 * @param len The length of values array.
 * @param values The values of the record to bundle.
 * @return The new record.
 */
GAB_API gab_value gab_list(struct gab_triple gab, uint64_t len,
                           gab_value *values);

/**
 * @brief Get the practical runtime type of a value.
 *
 * @param gab The engine
 * @param value The value
 * @return The runtime value corresponding to the type of the given value
 */
GAB_API_INLINE gab_value gab_valtype(struct gab_triple gab, gab_value value) {
  enum gab_kind k = gab_valkind(value);
  switch (k) {
  /* These values have a runtime type of themselves */
  case kGAB_MESSAGE:
    return value;
  /* These are special cases for the practical type */
  case kGAB_BOX:
    return gab_boxtype(value);
  case kGAB_RECORD:
    return gab_recshp(value);
  /* Otherwise, return the value for that kind */
  default:
    return gab_type(gab, k);
  }
}

GAB_API struct gab_gc *gab_gc(struct gab_triple gab);

GAB_API_INLINE gab_value gab_valintos(struct gab_triple gab, gab_value value);

/**
 * @brief Get the running fiber of the current job.
 *
 * @param gab The engine
 * @return The fiber
 */
GAB_API gab_value gab_thisfiber(struct gab_triple gab);

GAB_API_INLINE struct gab_vm *gab_thisvm(struct gab_triple gab) {
  gab_value fiber = gab_thisfiber(gab);
  assert(fiber != gab_cinvalid);
  return gab_fibvm(fiber);
}

GAB_API gab_value gab_vmmsg(struct gab_vm *vm);

/**
 * @brief In native functions, it can be useful to check what the current
 * message is that is being sent. This function returns that message.
 *
 * @return The message being sent in the current vm.
 */
GAB_API_INLINE gab_value gab_thisvmmsg(struct gab_triple gab) {
  return gab_vmmsg(gab_thisvm(gab));
};

enum gab_signal {
  sGAB_IGN,
  sGAB_COLL,
  sGAB_TERM,
};

/**
 *
 * @brief Yield control of this thread briefly.
 *
 * When a thread needs to wait (for example, when taking from a channel), it
 * should repeatedly call gab_yield as part of its loop. Each call may return
 * a signal (for example, sGAB_COLL). Each kind of signal needs to be handled
 * appropriately at each callsite of gab_yield.
 *
 * @param gab The engine.
 * @return A signal to handle.
 */
[[nodiscard]] GAB_API enum gab_signal gab_yield(struct gab_triple gab);

/**
 *
 * @brief 'busy-wait', as configured by the engine.
 *
 * In several places in the engine, a thread must spin in a loop,
 * checking and waiting for work to appear.
 *
 * This 'busy-wait' can cause an 'idle' thread (from the perspective of the gab
 * engine, this thread isn't doing any useful work) to have close to 100% CPU
 * usage (the loop can spin very fast).
 *
 * This function is called in every iteration of these loops. It sleeps the
 * thread for an amount of time, as specified in the 'wait' call to gab_create.
 *
 * If your user code includes a loop like this (for instance, you're
 * implementing an IO event loop) be sure to call this function periodically in
 * order to comply with the engine's behavior.
 */
GAB_API void gab_busywait(struct gab_triple gab);

/**
 * @brief Check if a value's runtime id matches a given value.
 *
 * @param eg The engine
 * @param value The value
 * @param type The type
 * @return true if the value's type matches the given type.
 */
GAB_API_INLINE bool gab_valisa(struct gab_triple gab, gab_value value,
                               gab_value type) {
  return gab_valeq(gab_valtype(gab, value), type);
}

GAB_API gab_value gab_thisfibmsg(struct gab_triple gab);

GAB_API_INLINE gab_value gab_thisfibmsgrec(struct gab_triple gab,
                                           gab_value message) {
  return gab_recat(gab_thisfibmsg(gab), message);
}

GAB_API_INLINE gab_value gab_thisfibmsgat(struct gab_triple gab,
                                          gab_value message,
                                          gab_value receiver) {
  gab_value spec_rec = gab_thisfibmsgrec(gab, message);

  if (spec_rec == gab_cundefined)
    return gab_cundefined;

  return gab_recat(spec_rec, receiver);
}

/**
 * @brief Coerce a value *into* a boolean.
 *
 * @param value The value to coerce.
 * @return False if the value is gab_false or gab_nil. Otherwise true.
 */
GAB_API_INLINE bool gab_valintob(gab_value value) {
  return !(gab_valeq(value, gab_false) || gab_valeq(value, gab_nil));
}

/**
 * @brief Coerce the given value to a string.
 *
 * @param gab The engine
 * @param value The value to convert
 * @return The string representation of the value.
 */
GAB_API_INLINE gab_value gab_valintos(struct gab_triple gab, gab_value value) {
  switch (gab_valkind(value)) {
  case kGAB_MESSAGE:
    return gab_msgtostr(value);
  case kGAB_STRING:
    return value;
  default:
    for (size_t len = 4096;; len *= 2) {
      char *buffer = malloc(len);
      assert(buffer);

      char *cursor = buffer;
      size_t remaining = len;

      if (gab_svalinspect(&cursor, &remaining, value, -1) < 0)
        continue;

      return gab_string(gab, buffer);
    }
  }
}

/**
 * @brief Coerce the given value to a string. Format said string to be pretty!
 *
 * @param gab The engine
 * @param value The value to convert
 * @return The string representation of the value.
 */
GAB_API_INLINE gab_value gab_pvalintos(struct gab_triple gab, gab_value value) {
  for (size_t len = 4096;; len *= 2) {
    char *buffer = malloc(len);
    assert(buffer);

    char *cursor = buffer;
    size_t remaining = len;

    if (gab_psvalinspect(&cursor, &remaining, value, -1) < 0)
      continue;

    return gab_string(gab, buffer);
  }
}

/**
 * @brief Returns true if the engine is currently processing/propagating a
 * signal.
 */
GAB_API bool gab_is_signaling(struct gab_triple gab);

/**
 * @brief Returns true if there is a signal waiting for the worker
 * given in by gab_triple.
 */
GAB_API bool gab_sigwaiting(struct gab_triple gab);

/**
 * @brief most likely you want @see gab_sigpropagate
 */
GAB_API bool gab_signext(struct gab_triple gab, int wkid);

/**
 * @brief Propagate the current signal to the next worker. Skips dead workers.
 * Wraps around to worker 0 last. (It is improper to signal worker 0 first)
 */
GAB_API_INLINE void gab_sigpropagate(struct gab_triple gab) {
  if (gab.wkid <= 0)
    return;

  int wkid = gab.wkid + 1;
  gab_signext(gab, wkid);
};

/**
 * @brief Clear the current signal as resolved.
 */
GAB_API bool gab_sigclear(struct gab_triple gab);

/**
 * @brief Send signal s to worker wkid. An example usage of this system is to
 * propagate garbage collections, via sGAB_COLL
 *
 * This should be made atomic.
 */
GAB_API bool gab_signal(struct gab_triple gab, enum gab_signal s, int wkid);

#ifdef __cplusplus
}
#endif

#endif

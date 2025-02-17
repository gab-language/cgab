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
#include "platform.h"

#if cGAB_LIKELY
#define __gab_likely(x) (__builtin_expect(!!(x), 1))
#define __gab_unlikely(x) (__builtin_expect(!!(x), 0))
#else
#define __gab_likely(x) (x)
#define __gab_unlikely(x) (x)
#endif

/**
 *===============================*
 |                               |
 |     Value Representation      |
 |                               |
 *===============================*

 Gab values are nan-boxed.

 An IEEE 754 double-precision float is a 64-bit value with bits laid out like:

 1 Sign bit
 |   11 Exponent bits
 |   |          52 Mantissa
 |   |          |
 [S][Exponent---][Mantissa------------------------------------------]

 The details of how these are used to represent numbers aren't really
 relevant here as long we don't interfere with them. The important bit is NaN.

 An IEEE double can represent a few magical values like NaN ("not a number"),
 Infinity, and -Infinity. A NaN is any value where all exponent bits are set:

     NaN bits
     |
 [-][11111111111][----------------------------------------------------]

 The bits set above are the only relevant ones. The rest of the bits are unused.

 NaN values come in two flavors: "signalling" and "quiet". The former are
 intended to halt execution, while the latter just flow through arithmetic
 operations silently. We want the latter.

 Quiet NaNs are indicated by setting the highest mantissa bit:

                  Highest mantissa bit
                  |
 [-][....NaN....][1---------------------------------------------------]

 This leaves the rest of the following bits to play with.

 Pointers to objects with data on the heap set the highest bit.

 We are left with 51 bits of mantissa to store an address.
 Even 64-bit machines only actually use 48 bits for addresses.

  Pointer bit set       Pointer data
  |                     |
 [1][....NaN....1][---------------------------------------------------]

 Immediate values *don't* have the pointer bit set.
 They also store a tag in the 3 bits just below the NaN.

      kGAB_SYMBOL, kGAB_STRING, kGAB_MESSAGE, kGAB_MESSAGE, kGAB_UNDEFINED
                   |
 [0][....NaN....1][---][------------------------------------------------]

 'Primitives' are message specializtions which are implemented as an opcode in
 the vm. This covers things like '+' on numbers and strings, etc.

 The opcode is stored in the lowest byte

                   kGAB_PRIMITIVE                               Opcode
                   |                                            |
 [0][....NaN....1][---]----------------------------------------[--------]

 Gab also employs a short string optimization. Lots of strings in a gab program
 are incredibly small, and incredibly common. values like '.some', '.none',
 '.ok', and even messages like '+' store a small string.

 We need to store the string's length, a null-terminator (for c-compatibility),
 and the string's data.

 Instead of storing the length of the string, we store the amount of bytes *not*
 used. Since there are a total of 5 bytes availble for storing string data, the
 remaining length is computed as 5 - strlen(str).

 We do this for a special case - when the string has length 5, the remaining
 length is 0. In this case, the byte which stores the remaining length *also*
 serves as the null-terminator for the string.

 This layout sneakily gives us an extra byte of storage in our small strings.

            kGAB_STRING Remaining Length                             <- Data
                   |    |                                               |
 [0][....NaN....1][---][--------][----------------------------------------]
                       [...0....][...e.......p.......a........h......s....]
                       [...3....][-------------------------...k......o....]

*/

typedef uint64_t gab_value;

#define T gab_value
#include "array.h"

#define T gab_value
#include "vector.h"

enum gab_kind {
  kGAB_STRING = 0, // MUST_STAY_ZERO
  kGAB_BINARY = 1,
  kGAB_MESSAGE = 2,
  kGAB_SYMBOL = 3, // Only relevant for parsing.
  kGAB_PRIMITIVE = 4,
  kGAB_UNDEFINED = 5,
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

#define __GAB_QNAN ((uint64_t)0x7ff8000000000000)

#define __GAB_SIGN_BIT ((uint64_t)1 << 63)

#define __GAB_TAGMASK (7)

#define __GAB_TAGOFFSET (48)

#define __GAB_TAGBITS ((uint64_t)__GAB_TAGMASK << __GAB_TAGOFFSET)

#define __GAB_VAL_TAG(val)                                                     \
  ((enum gab_kind)((__gab_valisn(val)                                          \
                        ? kGAB_NUMBER                                          \
                        : ((val) >> __GAB_TAGOFFSET) & __GAB_TAGMASK)))

// Sneakily use a union to get around the type system
static inline double __gab_valtod(gab_value value) {
  union {
    uint64_t bits;
    double num;
  } data;
  data.bits = value;
  return data.num;
}

static inline gab_value __gab_dtoval(double value) {
  union {
    uint64_t bits;
    double num;
  } data;
  data.num = value;
  return data.bits;
}

#define __gab_valisn(val) (((val) & __GAB_QNAN) != __GAB_QNAN)

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
 * specialization on messages for sigils like true, false, ok, some, none, etc.
 */

#define gab_nil                                                                \
  ((gab_value)(__GAB_QNAN | (uint64_t)kGAB_MESSAGE << __GAB_TAGOFFSET |          \
               (uint64_t)2 << 40 | (uint64_t)'n' | (uint64_t)'i' << 8 |        \
               (uint64_t)'l' << 16))

#define gab_undefined                                                          \
  ((gab_value)(__GAB_QNAN | (uint64_t)kGAB_UNDEFINED << __GAB_TAGOFFSET))

#define gab_false                                                              \
  ((gab_value)(__GAB_QNAN | (uint64_t)kGAB_MESSAGE << __GAB_TAGOFFSET |          \
               (uint64_t)0 << 40 | (uint64_t)'f' | (uint64_t)'a' << 8 |        \
               (uint64_t)'l' << 16 | (uint64_t)'s' << 24 |                     \
               (uint64_t)'e' << 32))

#define gab_true                                                               \
  ((gab_value)(__GAB_QNAN | (uint64_t)kGAB_MESSAGE << __GAB_TAGOFFSET |          \
               (uint64_t)1 << 40 | (uint64_t)'t' | (uint64_t)'r' << 8 |        \
               (uint64_t)'u' << 16 | (uint64_t)'e' << 24))

#define gab_ok                                                                 \
  ((gab_value)(__GAB_QNAN | (uint64_t)kGAB_MESSAGE << __GAB_TAGOFFSET |          \
               (uint64_t)3 << 40 | (uint64_t)'o' | (uint64_t)'k' << 8))

#define gab_none                                                               \
  ((gab_value)(__GAB_QNAN | (uint64_t)kGAB_MESSAGE << __GAB_TAGOFFSET |          \
               (uint64_t)1 << 40 | (uint64_t)'n' | (uint64_t)'o' << 8 |        \
               (uint64_t)'n' << 16 | (uint64_t)'e' << 24))

#define gab_err                                                                \
  ((gab_value)(__GAB_QNAN | (uint64_t)kGAB_MESSAGE << __GAB_TAGOFFSET |          \
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

/* Cast a gab value to a number */
#define gab_valton(val) (__gab_valtod(val))

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
#define fGAB_OBJ_FREED (1 << 8) // Used for debug purposes

#define GAB_OBJ_IS_BUFFERED(obj) ((obj)->flags & fGAB_OBJ_BUFFERED)
#define GAB_OBJ_IS_NEW(obj) ((obj)->flags & fGAB_OBJ_NEW)
#define GAB_OBJ_IS_FREED(obj) ((obj)->flags & fGAB_OBJ_FREED)

#define GAB_OBJ_BUFFERED(obj) ((obj)->flags |= fGAB_OBJ_BUFFERED)
#define GAB_OBJ_NEW(obj) ((obj)->flags |= fGAB_OBJ_NEW)
#define GAB_OBJ_FREED(obj) ((obj)->flags |= fGAB_OBJ_FREED)

#define __KEEP_FLAGS (fGAB_OBJ_BUFFERED | fGAB_OBJ_NEW | fGAB_OBJ_FREED)

#define GAB_OBJ_NOT_BUFFERED(obj) ((obj)->flags &= ~fGAB_OBJ_BUFFERED)
#define GAB_OBJ_NOT_NEW(obj) ((obj)->flags &= ~fGAB_OBJ_NEW)

typedef enum gab_opcode {
#define OP_CODE(name) OP_##name,
#include "bytecode.h"
#undef OP_CODE
} gab_opcode;

static const char *gab_opcode_names[] = {
#define OP_CODE(name) #name,
#include "bytecode.h"
#undef OP_CODE
};

struct gab_gc;
struct gab_vm;
struct gab_eg;
struct gab_src;

struct gab_triple {
  struct gab_eg *eg;
  int32_t flags;
  int32_t wkid;
};

struct gab_obj;

struct gab_obj_string;
struct gab_obj_prototype;
struct gab_obj_native;
struct gab_obj_block;
struct gab_obj_message;
struct gab_obj_box;
struct gab_obj_fiber;

typedef void (*gab_gcvisit_f)(struct gab_triple, struct gab_obj *obj);

typedef a_gab_value *(*gab_native_f)(struct gab_triple, uint64_t argc,
                                     gab_value *argv);

typedef void (*gab_boxdestroy_f)(uint64_t len, char *data);

typedef void (*gab_boxcopy_f)(uint64_t len, char *data);

typedef void (*gab_boxvisit_f)(struct gab_triple gab, gab_gcvisit_f visitor,
                               uint64_t len, char *data);

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
   * If this number is overflowed, gab keeps track  of this object's references
   * in a separate, slower rec<gab_obj, uint64_t>. When the reference count
   * drops back under 255, rc returns to the fast path.
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
 * @brief INTERNAL: Free memory held by an object.
 *
 * @param eg The engine responsible for the object.
 * @param obj The object.
 */
void gab_obj_destroy(struct gab_eg *eg, struct gab_obj *obj);

/**
 * @brief Return the size of the object's allocation, in bytes.
 *
 * @param obj The object.
 */
uint64_t gab_obj_size(struct gab_obj *obj);

#define GAB_DYNAMIC_MODULE_SYMBOL "gab_lib"
typedef a_gab_value *(*gab_osdynmod_load)(struct gab_triple, const char *path);
typedef a_gab_value *(*gab_osdynmod)(struct gab_triple);

/**
 * @class gab_create_argt
 */
struct gab_create_argt {
  uint64_t flags, jobs;

  FILE *sin, *sout, *serr;
  /**
   * @brief A hook for loading dynamic libraries.
   * This is used to load native-c modules.
   */
  gab_osdynmod_load os_dynmod;
};

/**
 * @brief Allocate data necessary for a runtime. This struct is lightweight and
 * should be passed by value.
 */
struct gab_triple gab_create(struct gab_create_argt args);

/**
 * @brief Free the memory owned by this triple.
 *
 * @param gab The triple to free.
 */
void gab_destroy(struct gab_triple gab);

/**
 * @brief Print the bytecode to the stream - useful for debugging.
 *
 * @param stream The stream to print to
 * @param proto The prototype to inspect
 * @return non-zero if an error occured.
 */
int gab_fmodinspect(FILE *stream, struct gab_obj_prototype *proto);

/**
 * @brief Print a gab_value to the given stream. Will prent nested values as
 * deep as depth.
 *
 * If depth is negative, will print recursively without limit.
 *
 * @param stream The stream to print to
 * @param value The value to inspect
 * @param depth The depth to recurse to
 * @return the number of bytes written to the stream.
 */
int gab_fvalinspect(FILE *stream, gab_value value, int depth);

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
int gab_fprintf(FILE *stream, const char *fmt, ...);

/**
 * @brief Format the given string to the given stream.
 * @see gab_fprintf
 *
 * @param stream The stream to print to
 * @param fmt The format string
 * @param varargs The arguments
 * @return the number of bytes written to the stream.
 */
int gab_vfprintf(FILE *stream, const char *fmt, va_list varargs);

/**
 * @brief Format the given string to the given stream.
 * @see gab_fprintf
 *
 * @param stream The stream to print to
 * @param fmt The format string
 * @param argc The number of arguments
 * @param argv The arguments
 * @return the number of bytes written to the stream.
 */
int gab_nfprintf(FILE *stream, const char *fmt, uint64_t argc,
                 gab_value argv[argc]);

/**
 * @brief Give the engine ownership of the values.
 *
 * When in c-code, it can be useful to create gab_objects which should be global
 * (ie, always kept alive).
 * Since gab_objects are allocated with one reference, this function does not
 * increment the rc of the value. It instead gives the engine 'ownership',
 * implying that when the engine is freed it should *decrement* the rc of the
 * value. The object will then be freed in the final collection, if its rc is <=
 * 0.
 *
 * @param eg The engine.
 * @param value The value to keep.
 * @return The number of values kept.
 */
uint64_t gab_egkeep(struct gab_eg *eg, gab_value value);

/**
 * @brief Give the engine ownership of multiple values.
 * @see gab_egkeep.
 *
 * @param gab The engine.
 * @param len The number of values to keep.
 * @param values The values.
 * @return The total number of values kept by the engine.
 */
uint64_t gab_negkeep(struct gab_eg *eg, uint64_t len, gab_value *argv);

/**
 * @enum The possible result states of a call to gab_egimpl.
 * @see gab_egimpl_rest
 * @see gab_egimpl
 */
enum gab_impl_resk {
  kGAB_IMPL_NONE = 0,
  kGAB_IMPL_TYPE,
  kGAB_IMPL_KIND,
  kGAB_IMPL_GENERAL,
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
   * the gab.record type, as well as the general gab_undefined type.
   *
   * These are checked in a specific order, and the first match is used.
   */
  gab_value type;

  /**
   * @brief The offset within the message of the implementation.
   *
   * For properties (see sGAB_IMPL_PROPERTY), this will return the offset within
   * the record.
   */
  union {
    gab_value spec;
    uint64_t offset;
  } as;

  /**
   * @brief The status of this implementation resolution.
   * @see gab_egimpl_resk
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
static inline struct gab_impl_rest
gab_impl(struct gab_triple gab, gab_value message, gab_value receiver);

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
 * @brief Push multiple values onto the vm's internal stack.
 * @see gab_vmpush.
 *
 * @param vm The vm that will receive the values.
 * @param len The number of values.
 * @param argv The array of values.
 */
uint64_t gab_nvmpush(struct gab_vm *vm, uint64_t len, gab_value *argv);

/**
 * @brief Inspect the vm at depth N in the callstack.
 *
 * @param stream The stream to print to.
 * @param vm The vm.
 * @param depth The depth of the callframe. '0' would be the topmost callframe.
 */
void gab_fvminspect(FILE *stream, struct gab_vm *vm, int depth);

/**
 * @brief Inspect the vm's callstack at the given depth, returning a value with
 * the relevant data.
 *
 * @param gab The triple to inspect.
 * @param depth The depth of the callframe. '0' would be the topmost callframe.
 * @return The record.
 */
gab_value gab_vmframe(struct gab_triple gab, uint64_t depth);

/*
 * @brief Attempt to load the module named 'name', with the default semantics of
 * \use
 *
 * @param gab The triple
 * @param name The name of the module
 * @return The values returned by the module, or nullptr if the module was not
 * found.
 */
a_gab_value *gab_suse(struct gab_triple gab, const char *name);

/*
 * @param gab The triple
 * @param name The name of the module, as a gab_value
 * @return The values returned by the module, or nullptr if the module was not
 * found.
 */
a_gab_value *gab_use(struct gab_triple gab, gab_value name);

/**
 * @brief Put a module into the engine's import table.
 *
 * @param eg The engine.
 * @param name The name of the import. This is used to lookup the import.
 * @param mod The module to store. This is usually a block, but can be anything
 * @param len The number of values returned by the module.
 * @param values The values returned by the module.
 * @returns The module if it was added, nullptr otherwise.
 */
a_gab_value *gab_segmodput(struct gab_eg *eg, const char *name, a_gab_value* module);

/**
 * @brief Check if an engine has a module by name.
 *
 * @param eg The engine.
 * @param name The name of the import.
 * @returns The module if it exists, nullptr otherwise.
 */
a_gab_value *gab_segmodat(struct gab_eg *eg, const char *name);

/**
 * @class gab_cmpl_argt
 * @brief Arguments and options for compiling a string of source code.
 * @see gab_cmpl.
 * @see enum gab_flags.
 */
struct gab_build_argt {
  /**
   * The name of the module, defaults to "__main__"
   */
  const char *name;
  /**
   * The source code to compile.
   */
  const char *source;
  /**
   * Optional flags for compilation.
   */
  int flags;
  /**
   * The number of arguments expected by the main block.
   */
  uint64_t len;
  /**
   * The names of the arguments expected by the main block.
   */
  const char **argv;
};

/**
 * @brief Compile a source string into an ast.
 * Flag options are defined in @link enum gab_flags.
 *
 * @see struct gab_build_argt.
 * @see enum gab_flags.
 *
 * @param gab The engine.
 * @param args The arguments.
 * @returns A gab_value containing the compiled block, which can be called.
 */
gab_value gab_build(struct gab_triple gab, struct gab_build_argt args);

gab_value gab_parse(struct gab_triple gab, struct gab_build_argt args);

/**
 * @brief Compile an AST into a block.
 */
union gab_value_pair {
  gab_value data[2];

  struct {
    gab_value prototype;
    gab_value environment;
  };
};

union gab_value_pair gab_compile(struct gab_triple gab, gab_value ast,
                                 gab_value env, gab_value bindings,
                                 gab_value mod);

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
   * Optional flags for the vm.
   */
  int flags;
  /**
   * The number of arguments passed to the main block.
   */
  uint64_t len;
  /**
   * The arguments passed to the main block.
   */
  gab_value *argv;
};

/**
 * @brief Call a block. Under the hood, this creates and queues a fiber, then
 * block the caller until that fiber is completed.
 * @see struct gab_run_argt
 *
 * @param  gab The triple.
 * @param args The arguments.
 * @return A heap-allocated slice of values returned by the fiber.
 */
a_gab_value *gab_run(struct gab_triple gab, struct gab_run_argt args);

/**
 * @brief Asynchronously call a block. This will create and queue a fiber,
 * returning a handle to the fiber.
 * @see struct gab_run_argt
 *
 * @param  gab The triple.
 * @param args The arguments.
 * @return The fiber that was queued.
 */
gab_value gab_arun(struct gab_triple gab, struct gab_run_argt args);

struct gab_send_argt {
  /**
   * The message and receiver
   */
  gab_value message, receiver;
  /**
   * Optional flags for the vm.
   */
  int flags;
  /**
   * The number of arguments passed to the send.
   */
  uint64_t len;
  /**
   * The arguments passed to the send.
   */
  gab_value *argv;
};

/*
 * @brief Send a message to a receiving value.
 *
 * Performs this send in a new fiber - thus is potentially blocking.
 */
a_gab_value *gab_send(struct gab_triple gab, struct gab_send_argt args);

/**
 * @class gab_exec_argt
 * @brief Arguments and options for executing a source string.
 */
struct gab_exec_argt {
  /**
   * The name of the module - defaults to "__main__".
   */
  const char *name;
  /**
   * The source code to execute.
   */
  const char *source;
  /**
   * Optional flags for compilation AND execution.
   */
  int flags;
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
};

/**
 * @brief Compile a source string to a block and run it.
 * This is equivalent to calling @link gab_cmpl and then @link gab_run on the
 * result.
 *
 * @see struct gab_exec_argt
 *
 * @param gab The triple.
 * @param args The arguments.
 * @return A heap-allocated slice of values returned by the block.
 */
a_gab_value *gab_exec(struct gab_triple gab, struct gab_exec_argt args);

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
gab_value gab_aexec(struct gab_triple gab, struct gab_exec_argt args);

/**
 * @brief Arguments and options for an interactive REPL.
 */
struct gab_repl_argt {
  /**
   * The prompt to display before each input of REPL.
   */
  const char *prompt_prefix;
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
void gab_repl(struct gab_triple gab, struct gab_repl_argt args);

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
 * @return The message that was updated, or gab_undefined if specialization
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
 * @return -1 on a success. Otherwise, returns the index in args of the first
 * specialization that failed.
 */
int gab_ndef(struct gab_triple gab, uint64_t len, struct gab_def_argt *args);

/**
 * @brief Get the runtime value that corresponds to the given kind.
 *
 * @param gab The engine
 * @param kind The type to retrieve the value for.
 * @return The runtime value corresponding to that type.
 */
static inline gab_value gab_type(struct gab_triple gab, enum gab_kind kind);

/**
 * @brief If fGAB_SILENT is not set, print an error message to the engine's
 * stderr. If fGAB_EXIT_ON_PANIC is set, then exit the program.
 *
 * @param gab The triple.
 * @param fmt The format string.
 * @returns An array of gab_values.
 */
a_gab_value *gab_fpanic(struct gab_triple gab, const char *fmt, ...);

/**
 * @brief If fGAB_SILENT is not set, print an error message to the engine's
 * stderr. If fGAB_EXIT_ON_PANIC is set, then exit the program.
 *
 * @param gab The triple.
 * @param fmt The format string.
 * @returns An array of gab_values.
 */
/*a_gab_value *gab_nfpanic(struct gab_triple gab, const char *fmt, uint64_t
 * argc, gab_value argv[static argc]);*/

/**
 * @brief Panic the triple with an 'unexpected type' message.
 * @see gab_panic
 *
 * @param gab The triple.
 * @param found The value with the mismatched type.
 * @param texpected The expected type.
 * @returns An array of gab_values.
 */
a_gab_value *gab_ptypemismatch(struct gab_triple gab, gab_value found,
                               gab_value texpected);

/**
 * @brief Panic the triple with an 'unexpected type' message.
 * @see gab_panic
 *
 * @param gab The triple.
 * @param found The value with the mismatched type.
 * @param texpected The expected type.
 * @returns false.
 */
static inline a_gab_value *gab_pktypemismatch(struct gab_triple gab,
                                              gab_value found,
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
 * # Increment the reference count of the value(s)
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
gab_value gab_iref(struct gab_triple gab, gab_value value);

/**
 * @brief Decrement the reference count of the value(s)
 *
 * @param vm The vm.
 * @param len The number of values.
 * @param values The values.
 * @return the value decremented.
 */
gab_value gab_dref(struct gab_triple gab, gab_value value);

/**
 * @brief Increment the reference count of the value(s)
 *
 * @param vm The vm.
 * @param value The value.
 */
void gab_niref(struct gab_triple gab, uint64_t stride, uint64_t len,
               gab_value *values);

/**
 * @brief Decrement the reference count of the value(s)
 *
 * @param vm The vm.
 * @param value The value.
 */
void gab_ndref(struct gab_triple gab, uint64_t stride, uint64_t len,
               gab_value *values);

#endif

/**
 * @brief Run the garbage collector.
 *
 * @param gab The engine.
 * @param gc The garbage collector.
 * @param vm The vm.
 */
void gab_collect(struct gab_triple gab);

/**
 * @brief Lock the garbage collector to prevent collection until gab_gcunlock is
 * called.
 * @see gab_gcunlock
 *
 * @param gc The gc to lock
 */
void gab_gclock(struct gab_triple gab);

/**
 * @brief Unlock the given collector.
 *
 * @param gc The gc to unlock
 */
void gab_gcunlock(struct gab_triple gab);

/**
 * @brief Get the name of a source file - aka the fully qualified path.
 *
 * @param src The source
 * @return The name
 */
gab_value gab_srcname(struct gab_src *src);

/**
 * @brief Get the line in the source code corresponding to an offset in the
 * bytecode.
 *
 * @param src The source
 * @param offset The offset
 * @return The line in the source code
 */
uint64_t gab_srcline(struct gab_src *src, uint64_t offset);

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
static inline enum gab_kind gab_valkind(gab_value value) {
  if (gab_valiso(value))
    return gab_valtoo(value)->kind + __GAB_VAL_TAG(value);

  return __GAB_VAL_TAG(value);
}

/**
 * @brief Check if a value has a type *other than its kind* to check for message
 * sends.
 *
 * @param value The value to check.
 * @return true if the value supports types other than its kind.
 */
static inline bool gab_valhast(gab_value value) {
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

/**
 * @brief An immutable sequence of bytes.
 */
struct gab_obj_string {
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

/* Cast a value to a (gab_obj_string*) */
#define GAB_VAL_TO_STRING(value) ((struct gab_obj_string *)gab_valtoo(value))

/**
 * @brief Create a gab_value from a bounded array of chars.
 *
 * @param gab The engine.
 * @param len The length of the string.
 * @param data The data.
 * @return The value.
 */
gab_value gab_nstring(struct gab_triple gab, uint64_t len, const char *data);

/**
 * @brief Create a gab_value from a c-string
 *
 * @param gab The engine.
 * @param data The data.
 * @return The value.
 */
static inline gab_value gab_string(struct gab_triple gab, const char *data) {
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
gab_value gab_strcat(struct gab_triple gab, gab_value a, gab_value b);

/**
 * @brief Get a pointer to the start of the string.
 *
 * This accepts a pointer because of the short string optimization, where
 * the gab_value itself embeds the string data.
 *
 * @param str The string
 * @return A pointer to the start of the string
 */
static inline const char *gab_strdata(gab_value *str) {
  assert(gab_valkind(*str) == kGAB_STRING ||
         gab_valkind(*str) == kGAB_MESSAGE ||
         gab_valkind(*str) == kGAB_SYMBOL || gab_valkind(*str) == kGAB_BINARY);

  if (gab_valiso(*str))
    return GAB_VAL_TO_STRING(*str)->data;

  return ((const char *)str);
}

/**
 * @brief Get the number of bytes in string. This is constant-time.
 *
 * @param str The string.
 * @return The length of the string.
 */
static inline uint64_t gab_strlen(gab_value str) {
  assert(gab_valkind(str) == kGAB_STRING || gab_valkind(str) == kGAB_SYMBOL || gab_valkind(str) == kGAB_MESSAGE);

  if (gab_valiso(str))
    return GAB_VAL_TO_STRING(str)->len;

  return 5 - ((str >> 40) & 0xFF);
};

/**
 * @brief Get the number multi-byte codepoints in a string. This is
 * constant-time. (more or less)
 *
 * This should not be called on kGAB_BINARY. (As that might not be valid utf8)
 */
static inline uint64_t gab_strmblen(gab_value str) {
  assert(gab_valkind(str) == kGAB_STRING || gab_valkind(str) == kGAB_SYMBOL);

  if (gab_valiso(str))
    return GAB_VAL_TO_STRING(str)->mb_len;

  // This is a small string. No space to store mb_len, so just recompute.
  mbstate_t state = {0};
  const char *cursor = gab_strdata(&str);
  return mbsrtowcs(NULL, &cursor, 0, &state);
};

/**
 * @brief Get a string's hash. This a constant-time.
 *
 * @param str The string
 * @return The hash
 */
static inline uint64_t gab_strhash(gab_value str) {
  assert(gab_valkind(str) == kGAB_STRING);

  if (gab_valiso(str))
    return GAB_VAL_TO_STRING(str)->hash;

  return str;
}

static inline uint64_t gab_sstrendswith(gab_value str, const char *pat,
                                        uint64_t offset) {
  assert(gab_valkind(str) == kGAB_STRING);

  const char *cstr = gab_strdata(&str);
  uint64_t cstrlen = gab_strlen(str);

  uint64_t len = strlen(pat);

  if (cstrlen < offset + len)
    return false;

  return !memcmp(cstr + cstrlen - offset - len, pat, len);
}

static inline gab_value gab_strslice(struct gab_triple gab, gab_value str,
                                     uint64_t offset, uint64_t len) {
  assert(gab_valkind(str) == kGAB_STRING);

  const char *cstr = gab_strdata(&str);
  uint64_t cstrlen = gab_strlen(str);

  offset = offset >= cstrlen ? cstrlen : offset;
  len = offset + len > cstrlen ? cstrlen - offset : len;

  return gab_nstring(gab, len, cstr + offset);
};

/**
 * @brief Convert a string into it's corresponding message. This is
 * constant-time.
 *
 * @param str The string
 * @return The message
 */
static inline gab_value gab_strtomsg(gab_value str) {
  assert(gab_valkind(str) == kGAB_STRING);
  return str | (uint64_t)kGAB_MESSAGE << __GAB_TAGOFFSET;
}

/**
 * @brief Convert a string into a symbol. This is constant-time.
 */
static inline gab_value gab_strtosym(gab_value str) {
  assert(gab_valkind(str) == kGAB_STRING);
  return str | (uint64_t)kGAB_SYMBOL << __GAB_TAGOFFSET;
}

/**
 * @brief Convert a string into a binary. This is constant-time.
 */
static inline gab_value gab_strtobin(gab_value str) {
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
static inline gab_value gab_nmessage(struct gab_triple gab, uint64_t len,
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
static inline gab_value gab_message(struct gab_triple gab, const char *data) {
  return gab_strtomsg(gab_string(gab, data));
}

/**
 * @brief Convert a message into a string
 *
 * @param msg The message.
 * @return the name.
 */
static inline gab_value gab_msgtostr(gab_value msg) {
  assert(gab_valkind(msg) == kGAB_MESSAGE);
  return msg & ~((uint64_t)kGAB_MESSAGE << __GAB_TAGOFFSET);
}

static inline gab_value gab_symtostr(gab_value sym) {
  assert(gab_valkind(sym) == kGAB_SYMBOL);
  return sym & ~((uint64_t)kGAB_SYMBOL << __GAB_TAGOFFSET);
}

/**
 * @brief Convert a binary into a string. This *can* fail, as a binary is not
 * guaranteed to be valid utf8.
 *
 * @param bin The binary to convert
 * @return The string if bin is valid utf8, otherwise gab_undefined.
 */
static inline gab_value gab_bintostr(gab_value bin) {
  assert(gab_valkind(bin) == kGAB_BINARY);

  if (gab_strmblen(bin) == -1)
    return gab_undefined;

  return bin & ~((uint64_t)kGAB_BINARY << __GAB_TAGOFFSET);
}

/**
 * @brief Create a symbol value from a cstring.
 *
 * @param data the cstring
 * @return The symbol
 */
static inline gab_value gab_symbol(struct gab_triple gab, const char *data) {
  return gab_strtosym(gab_string(gab, data));
}

/**
 * @brief Create a binary value from a cstring.
 *
 * @param data the cstring
 * @return the binary
 */
static inline gab_value gab_binary(struct gab_triple gab, const char *data) {
  return gab_strtobin(gab_string(gab, data));
}

static inline gab_value gab_nbinary(struct gab_triple gab, size_t len,
                                    const char *data) {
  return gab_strtobin(gab_nstring(gab, len, data));
}

/**
 * @brief A wrapper for a native c function.
 */
struct gab_obj_native {
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

/* Cast a value to a (gab_obj_native*) */
#define GAB_VAL_TO_NATIVE(value) ((struct gab_obj_native *)gab_valtoo(value))

/**
 * @brief A block - aka a prototype and it's captures.
 */
struct gab_obj_block {
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

/* Cast a value to a (gab_obj_block*) */
#define GAB_VAL_TO_BLOCK(value) ((struct gab_obj_block *)gab_valtoo(value))

/**
 * @brief Create a new block object, setting all captures to gab_nil.
 *
 * @param gab The gab engine.
 * @param prototype The prototype of the block.
 * @return The new block object.
 */
gab_value gab_block(struct gab_triple gab, gab_value prototype);

struct gab_obj_shape {
  struct gab_obj header;

  uint64_t len;

  v_gab_value transitions;

  gab_value keys[];
};

#define GAB_VAL_TO_SHAPE(value) ((struct gab_obj_shape *)gab_valtoo(value))

#define gab_shapeof(gab, ...)                                                  \
  ({                                                                           \
    gab_value keys[] = {__VA_ARGS__};                                          \
    gab_shape(gab, 1, sizeof(keys) / sizeof(gab_value), keys);                 \
  })

gab_value gab_shape(struct gab_triple gab, uint64_t stride, uint64_t len,
                    gab_value *keys);

gab_value __gab_shape(struct gab_triple gab, uint64_t len);

static inline uint64_t gab_shpislist(gab_value shp) {
  assert(gab_valkind(shp) == kGAB_SHAPE || gab_valkind(shp) == kGAB_SHAPELIST);
  struct gab_obj_shape *s = GAB_VAL_TO_SHAPE(shp);
  return s->header.kind == kGAB_SHAPELIST;
}

static inline uint64_t gab_shplen(gab_value shp) {
  assert(gab_valkind(shp) == kGAB_SHAPE || gab_valkind(shp) == kGAB_SHAPELIST);
  struct gab_obj_shape *s = GAB_VAL_TO_SHAPE(shp);
  return s->len;
}

static inline gab_value gab_ushpat(gab_value shp, uint64_t idx) {
  assert(gab_valkind(shp) == kGAB_SHAPE || gab_valkind(shp) == kGAB_SHAPELIST);
  struct gab_obj_shape *s = GAB_VAL_TO_SHAPE(shp);
  return s->keys[idx];
}

static inline uint64_t gab_shpfind(gab_value shp, gab_value key) {
  assert(gab_valkind(shp) == kGAB_SHAPE || gab_valkind(shp) == kGAB_SHAPELIST);
  struct gab_obj_shape *s = GAB_VAL_TO_SHAPE(shp);

  uint64_t len = s->len;

  for (uint64_t i = 0; i < len; i++) {
    if (gab_valeq(key, s->keys[i]))
      return i;
  }

  return -1;
}

static inline bool gab_shphas(gab_value shape, gab_value key) {
  return gab_shpfind(shape, key) != -1;
}

static inline uint64_t gab_shptfind(gab_value shp, gab_value key) {
  assert(gab_valkind(shp) == kGAB_SHAPE || gab_valkind(shp) == kGAB_SHAPELIST);
  struct gab_obj_shape *s = GAB_VAL_TO_SHAPE(shp);

  uint64_t len = s->transitions.len / 2;

  for (uint64_t i = 0; i < len; i++) {
    if (gab_valeq(key, v_gab_value_val_at(&s->transitions, i * 2)))
      return i;
  }

  return -1;
};

gab_value gab_shpwith(struct gab_triple gab, gab_value shp, gab_value key);

#define gab_shpcat(gab, ...)                                                   \
  ({                                                                           \
    gab_value __shps[] = {__VA_ARGS__};                                        \
    gab_nshpcat(gab, sizeof(__shps) / sizeof(gab_value), __shps);              \
  })

/**
 * @brief Concatenate n shpords, left to rate
 *
 */
gab_value gab_nshpcat(struct gab_triple gab, uint64_t len, gab_value *shapes);

gab_value gab_shptorec(struct gab_triple gab, gab_value shape);

gab_value gab_shpwithout(struct gab_triple gab, gab_value shp, gab_value key);

/**
 * @brief A record node.
 */
struct gab_obj_recnode {
  struct gab_obj header;

  /**
   * @brief Length of data member. Each node has a maximum of 32 children, so 1
   * byte is plenty.
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
struct gab_obj_rec {
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

#define GAB_VAL_TO_REC(value) ((struct gab_obj_rec *)gab_valtoo(value))
#define GAB_VAL_TO_RECNODE(value) ((struct gab_obj_recnode *)gab_valtoo(value))

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
gab_value gab_record(struct gab_triple gab, uint64_t stride, uint64_t len,
                     gab_value *keys, gab_value *vals);

gab_value gab_recordfrom(struct gab_triple gab, gab_value shape,
                         uint64_t stride, uint64_t len, gab_value *vals);

static inline gab_value gab_erecord(struct gab_triple gab) {
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
static inline gab_value gab_srecord(struct gab_triple gab, uint64_t len,
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
static inline gab_value gab_recshp(gab_value record) {
  assert(gab_valkind(record) == kGAB_RECORD);
  return GAB_VAL_TO_REC(record)->shape;
};

/**
 * @brief Get the length of a record
 *
 * @param rec The record
 * @return The number of key-value pairs in the record
 */
static inline uint64_t gab_reclen(gab_value record) {
  assert(gab_valkind(record) == kGAB_RECORD);
  struct gab_obj_rec *r = GAB_VAL_TO_REC(record);
  return gab_shplen(r->shape);
}

/**
 * @brief Find the offset of a key in the record
 *
 * @param record The record to look in
 * @param key The key to search for
 * @return The offset, or -1 if not found
 */
static inline uint64_t gab_recfind(gab_value record, gab_value key) {
  return gab_shpfind(gab_recshp(record), key);
}

/**
 * @brief Check if a record has a given key
 *
 * @param record The record to look in
 * @param key The key to check for
 * @return true if the key exists in the record.
 */
static inline bool gab_rechas(gab_value record, gab_value key) {
  return gab_shphas(gab_recshp(record), key);
}

/**
 * @brief Check if an index is within a record
 *
 * @param record The record to look in
 * @param idx The index to check
 * @return true if the index is valid within the record
 */
static inline bool gab_urechas(gab_value record, uint64_t index) {
  assert(gab_valkind(record) == kGAB_RECORD);
  struct gab_obj_rec *m = GAB_VAL_TO_REC(record);
  return index < gab_shplen(m->shape);
}

/**
 * @brief Get the key at a given index in the record. Does no bounds checking.
 *
 * @param record The record to look in
 * @param index The index of the key
 *
 * @return The key at that index
 */
static inline gab_value gab_ukrecat(gab_value record, uint64_t index) {
  assert(gab_valkind(record) == kGAB_RECORD);
  assert(gab_urechas(record, index));
  return gab_ushpat(gab_recshp(record), index);
}

/**
 * @brief Get the value at a given index in the record. Does no bounds checking.
 *
 * @param record The record to look in
 * @param index The index of the value to get
 * @return the value at index
 */
gab_value gab_uvrecat(gab_value record, uint64_t index);

/**
 * @brief Return a new record with the new value at the given index. Does no
 * bounds checking.
 *
 * @param record The record to begin with
 * @param index The index to replace
 * @param value The new value
 * @return a new record, with value at index.
 */
gab_value gab_urecput(struct gab_triple gab, gab_value record, uint64_t index,
                      gab_value value);

/**
 * @brief Get the value at a given key in the record. If the key doesn't exist,
 * returns undefined.
 *
 * @param record The record to check
 * @param key The key to look for
 * @return the value associated with key, or undefined.
 */
static inline gab_value gab_recat(gab_value record, gab_value key) {
  assert(gab_valkind(record) == kGAB_RECORD);
  uint64_t i = gab_recfind(record, key);

  if (i == -1)
    return gab_undefined;

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
static inline gab_value gab_srecat(struct gab_triple gab, gab_value record,
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
static inline gab_value gab_mrecat(struct gab_triple gab, gab_value rec,
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
gab_value gab_recput(struct gab_triple gab, gab_value record, gab_value key,
                     gab_value value);

/**
 * @brief Pop a value from a record
 */
gab_value gab_rectake(struct gab_triple gab, gab_value record, gab_value key,
                      gab_value *value);

#define gab_lstcat(gab, ...)                                                   \
  ({                                                                           \
    gab_value __recs[] = {__VA_ARGS__};                                        \
    gab_nlstcat(gab, sizeof(__recs) / sizeof(gab_value), __recs);              \
  })

/**
 * @brief Concatenate n records, left to rate
 *
 */
gab_value gab_nlstcat(struct gab_triple gab, uint64_t len, gab_value *records);

#define gab_lstpush(gab, list, ...)                                            \
  ({                                                                           \
    gab_value __vals[] = {__VA_ARGS__};                                        \
    gab_nlstpush(gab, list, sizeof(__vals) / sizeof(gab_value), __vals);       \
  })

/**
 * @brief Push n values onto the end of a list-record
 */
gab_value gab_nlstpush(struct gab_triple gab, gab_value list, uint64_t len,
                       gab_value *values);

/**
 * @brief Pop the last value from a list, returning the new list.
 *
 * The popped value will be written to out_val, if it is not nullptr.
 */
gab_value gab_lstpop(struct gab_triple gab, gab_value list, gab_value *out_val);

static inline uint64_t gab_recisl(gab_value rec) {
  gab_value shp = gab_recshp(rec);
  assert(gab_valkind(shp) == kGAB_SHAPE || gab_valkind(shp) == kGAB_SHAPELIST);
  struct gab_obj_shape *s = GAB_VAL_TO_SHAPE(shp);
  return s->header.kind == kGAB_SHAPELIST;
}

/**
 * @brief Return a new record without key. TODO: Not Implemented
 *
 * @param gab The engine
 * @param record The record
 * @param key The key
 * @return a record without key
 */
gab_value gab_recdel(struct gab_triple gab, gab_value record, gab_value key);

/*
 * @brief A lightweight green-thread / coroutine / fiber.
 */
struct gab_obj_fiber {
  struct gab_obj header;

  /**
   * Structure used to actually execute bytecode
   */
  struct gab_vm {
    uint8_t *ip;

    gab_value *sp, *fp;

    gab_value sb[cGAB_STACK_MAX];
  } vm;

  /**
   * The messages available to the fiber
   */
  gab_value messages;

  /**
   * Result of execution
   */
  a_gab_value *res;

  /**
   * Length of data array member
   */
  uint64_t len;

  /**
   * Holds the main block, and any arguments passed to it
   */
  gab_value data[];
};

#define GAB_VAL_TO_FIBER(value) ((struct gab_obj_fiber *)gab_valtoo(value))

struct gab_fiber_argt {
  uint64_t argc;

  gab_value receiver, message, *argv;
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
gab_value gab_fiber(struct gab_triple gab, struct gab_fiber_argt args);

/**
 * @brief Block the caller until this fiber is completed.
 *
 * @param fiber The fiber
 */
a_gab_value *gab_fibawait(struct gab_triple gab, gab_value fiber);

static inline bool gab_fibisrunning(gab_value fiber) {
  return gab_valkind(fiber) == kGAB_FIBERRUNNING;
}

static inline bool gab_fibisdone(gab_value fiber) {
  return gab_valkind(fiber) == kGAB_FIBERDONE;
}

static inline gab_value gab_thisfiber(struct gab_triple gab);

/*
 * @brief Return the specialization for a given message and receiver.
 *
 * @param gab The gab triple.
 * @param message The message.
 * @param receiver The receiver.
 *
 * @return The spec
 */
static inline gab_value gab_thisfibmsgat(struct gab_triple gab,
                                         gab_value message, gab_value receiver);

static inline gab_value gab_thisfibmsgrec(struct gab_triple gab,
                                          gab_value message);

/**
 * @brief A primitive for sending data between fibers.
 */
struct gab_obj_channel {
  struct gab_obj header;

  /**
   * The atomic channel for communicating data betwixt fibers
   */
  _Atomic gab_value data;
};

/**
 * @brief Create a channel with the given buffer capacity.
 *
 * @param gab The engine
 * @param len The length of the channel's buffer
 * @return The channel
 */
gab_value gab_channel(struct gab_triple gab);

/**
 * @brief Put a value on the given channel.
 *  In unbuffered channels, this will block the caller until the value is taken
 * by another fiber.
 *
 * @param gab The engine
 * @param channel The channel
 * @param value The value to put
 */
bool gab_chnput(struct gab_triple gab, gab_value channel, gab_value value);

/**
 * @brief Take a value from the given channel. This will block the caller until
 * a value is available to take.
 *
 * @param gab The engine
 * @param channel The channel
 * @return The value taken
 */
gab_value gab_chntake(struct gab_triple gab, gab_value channel);

/**
 * @brief Take a value from the given channel, with a timeout.
 *
 * @param gab The engine
 * @param channel The channel
 * @param nms The number of milliseconds to wait before closing
 * @return The value taken
 */
gab_value gab_tchntake(struct gab_triple gab, gab_value channel, uint64_t nms);

/**
 * @brief Close the given channel. A closed channel cannot receive new values.
 *
 * @param channel The channel
 */
void gab_chnclose(gab_value channel);

/**
 * @brief Return true if the given channel is closed
 *
 * @param channel The channel
 * @return whetehr or not the channel is closed
 */
bool gab_chnisclosed(gab_value channel);

/**
 * @brief Return true if the given channel is full
 *
 * @param channel The channel
 * @return whetehr or not the channel is full
 */
bool gab_chnisfull(gab_value channel);

/**
 * @brief Return true if the given channel is empty
 *
 * @param channel The channel
 * @return whetehr or not the channel is empty
 */
bool gab_chnisempty(gab_value channel);

/* Cast a value to a (gab_obj_channel*) */
#define GAB_VAL_TO_CHANNEL(value) ((struct gab_obj_channel *)gab_valtoo(value))

/**
 * @brief A container object, which holds arbitrary data.
 *
 * There are two callbacks:
 *  - one to do cleanup when the object is destroyed
 *  - one to visit children values when doing garbage collection.
 */
struct gab_obj_box {
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
   * This function should be used to visit all gab_values that are referenced by
   * the box.
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

#define GAB_VAL_TO_BOX(value) ((struct gab_obj_box *)gab_valtoo(value))

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
gab_value gab_box(struct gab_triple gab, struct gab_box_argt args);

/**
 * @brief Get the length of the user data from a boxed value.
 *
 * @param box The box.
 * @return The user data.
 */
static inline uint64_t gab_boxlen(gab_value box) {
  assert(gab_valkind(box) == kGAB_BOX);
  return GAB_VAL_TO_BOX(box)->len;
}

/**
 * @brief Get the user data from a boxed value.
 *
 * @param box The box.
 * @return The user data.
 */
static inline void *gab_boxdata(gab_value box) {
  assert(gab_valkind(box) == kGAB_BOX);
  return GAB_VAL_TO_BOX(box)->data;
}

/**
 * @brief Get the user type from a boxed value.
 *
 * @param box The box.
 * @return The user type.
 */
static inline gab_value gab_boxtype(gab_value value) {
  assert(gab_valkind(value) == kGAB_BOX);
  return GAB_VAL_TO_BOX(value)->type;
}

/**
 * @brief The prototype of a block. Encapsulates everything known about a block
 * at compile time.
 */
struct gab_obj_prototype {
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
   * Flags providing additional metadata about the prototype.
   */
  char data[];
};

/* Cast a value to a (gab_obj_bprototype*) */
#define GAB_VAL_TO_PROTOTYPE(value)                                            \
  ((struct gab_obj_prototype *)gab_valtoo(value))

/**
 * @brief Arguments for creating a prototype.
 */
struct gab_prototype_argt {
  char narguments, nslots, nlocals, nupvalues, *flags, *indexes, *data;
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
gab_value gab_prototype(struct gab_triple gab, struct gab_src *src,
                        uint64_t begin, uint64_t len,
                        struct gab_prototype_argt args);

/**
 * @brief Create a native wrapper to a c function with a gab_string name.
 *
 * @param gab The triple.
 * @param name The name of the native.
 * @param f The c function.
 *
 * @return The value.
 */
gab_value gab_native(struct gab_triple gab, gab_value name, gab_native_f f);

/**
 * @brief Create a native wrapper to a c function with a c-string name.
 *
 * @param gab The triple.
 * @param name The name of the native.
 * @param f The c function.
 * @return The value.
 */
gab_value gab_snative(struct gab_triple gab, const char *name, gab_native_f f);

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
gab_value gab_list(struct gab_triple gab, uint64_t len,
                   gab_value values[static len]);

/**
 * @brief Get the practical runtime type of a value.
 *
 * @param gab The engine
 * @param value The value
 * @return The runtime value corresponding to the type of the given value
 */
static inline gab_value gab_valtype(struct gab_triple gab, gab_value value) {
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

#define NAME strings
#define K struct gab_obj_string *
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
/**
 * @class The 'engine'. Stores the long-lived data
 * needed for the gab environment.
 */
struct gab_eg {
  uint64_t hash_seed;

  v_gab_value scratch;

  gab_value types[kGAB_NKINDS];

  _Atomic int8_t njobs;

  struct gab_gc {
    _Atomic int8_t schedule;
    d_gab_obj overflow_rc;
    v_gab_obj dead;

    struct gab_gcbuf {
      uint64_t len;
      struct gab_obj *data[cGAB_GC_MOD_BUFF_MAX];
    } buffers[][kGAB_NBUF][GAB_GCNEPOCHS];
  } *gc;

  gab_value messages, work_channel;

  mtx_t shapes_mtx;
  gab_value shapes;

  mtx_t strings_mtx;
  d_strings strings;

  mtx_t sources_mtx;
  d_gab_src sources;

  mtx_t modules_mtx;
  d_gab_modules modules;

  FILE *sin, *sout, *serr;

  gab_osdynmod_load os_dynmod;

  struct gab_obj *(*os_objalloc)(struct gab_triple gab, struct gab_obj *,
                                 uint64_t new_size);

  uint64_t len;
  struct gab_jb {
    thrd_t td;

    bool alive;

    gab_value fiber;

    _Atomic uint32_t epoch;
    _Atomic int32_t locked;
    v_gab_value lock_keep;
  } jobs[];
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

static inline gab_value gab_type(struct gab_triple gab, enum gab_kind k) {
  assert(k < kGAB_NKINDS);
  return gab.eg->types[k];
}

static inline struct gab_gc *gab_gc(struct gab_triple gab) {
  return gab.eg->gc;
}

static inline gab_value gab_fb(struct gab_triple gab) {
  return gab.eg->jobs[gab.wkid].fiber;
}

static inline struct gab_vm *gab_vm(struct gab_triple gab) {
  gab_value fiber = gab.eg->jobs[gab.wkid].fiber;

  if (fiber == gab_undefined)
    return nullptr;

  return &GAB_VAL_TO_FIBER(fiber)->vm;
}

void gab_yield(struct gab_triple gab);

/**
 * @brief Check if a value's runtime id matches a given value.
 *
 * @param eg The engine
 * @param value The value
 * @param type The type
 * @return true if the value's type matches the given type.
 */
static inline bool gab_valisa(struct gab_triple gab, gab_value value,
                              gab_value type) {
  return gab_valeq(gab_valtype(gab, value), type);
}

/**
 * @brief Get the running fiber of the current job.
 *
 * @param gab The engine
 * @return The fiber
 */
static inline gab_value gab_thisfiber(struct gab_triple gab) {
  return gab.eg->jobs[gab.wkid].fiber;
}

/**
 * @brief Add a specialization to a message.
 *
 * @param gab The gab triple.
 * @param msg The message.
 * @param receiver The receiver.
 * @param spec The specialization.
 * @return The message, or gab_undefined if it already had the spec.
 */
static inline struct gab_impl_rest
gab_impl(struct gab_triple gab, gab_value message, gab_value receiver) {
  gab_value spec = gab_undefined;
  gab_value type = receiver;

  /* Check if the receiver has a supertype, and if that supertype implments the
   * message. ie <gab.shape 0 1>*/
  if (gab_valhast(receiver)) {
    type = gab_valtype(gab, receiver);
    spec = gab_thisfibmsgat(gab, message, type);
    if (spec != gab_undefined)
      return (struct gab_impl_rest){
          type,
          .as.spec = spec,
          kGAB_IMPL_TYPE,
      };
  }

  /* Check for the kind of the receiver. ie 'gab.record' */
  type = gab_type(gab, gab_valkind(receiver));
  spec = gab_thisfibmsgat(gab, message, type);
  if (spec != gab_undefined)
    return (struct gab_impl_rest){
        type,
        .as.spec = spec,
        kGAB_IMPL_KIND,
    };

  /* Check if the receiver is a record and has a matching property */
  if (gab_valkind(receiver) == kGAB_RECORD) {
    type = gab_recshp(receiver);
    if (gab_rechas(receiver, message))
      return (struct gab_impl_rest){
          type,
          .as.offset = gab_recfind(receiver, message),
          kGAB_IMPL_PROPERTY,
      };
  }

  /* Check for a default, generic implementation */
  /* Previously, this had a higher priority than
   * record properties - I don't remember why I made that change.
   *
   * Ahh, I remember the issue. The `Messages.specializations` record
   * is impossible to do anything with, because it is a record with a key
   * for every message in the system.
   *
   * .
   */
  type = gab_undefined;
  spec = gab_thisfibmsgat(gab, message, type);
  if (spec != gab_undefined)
    return (struct gab_impl_rest){
        .as.spec = spec,
        kGAB_IMPL_GENERAL,
    };

  return (struct gab_impl_rest){.status = kGAB_IMPL_NONE};
}

static inline gab_value gab_thisfibmsg(struct gab_triple gab) {
  gab_value fiber = gab_thisfiber(gab);

  if (fiber == gab_undefined)
    return gab.eg->messages;

  struct gab_obj_fiber *f = GAB_VAL_TO_FIBER(fiber);
  return f->messages;
}

static inline gab_value gab_thisfibmsgrec(struct gab_triple gab,
                                          gab_value message) {
  return gab_recat(gab_thisfibmsg(gab), message);
}

static inline gab_value
gab_thisfibmsgat(struct gab_triple gab, gab_value message, gab_value receiver) {
  gab_value spec_rec = gab_thisfibmsgrec(gab, message);

  if (spec_rec == gab_undefined)
    return gab_undefined;

  return gab_recat(spec_rec, receiver);
}

/**
 * @brief Coerce a value *into* a boolean.
 *
 * @param value The value to coerce.
 * @return False if the value is gab_false or gab_nil. Otherwise true.
 */
static inline bool gab_valintob(gab_value value) {
  return !(gab_valeq(value, gab_false) || gab_valeq(value, gab_nil));
}

/**
 * @brief Coerce the given value to a string.
 *
 * @param gab The engine
 * @param value The value to convert
 * @return The string representation of the value.
 */
static inline gab_value gab_valintos(struct gab_triple gab, gab_value value) {
  char buffer[128];

  switch (gab_valkind(value)) {
  case kGAB_STRING:
    return value;
  case kGAB_MESSAGE:
    return gab_msgtostr(value);
  case kGAB_SYMBOL:
    return gab_symtostr(value);
  case kGAB_BINARY:
    return gab_bintostr(value);
  case kGAB_PRIMITIVE:
    return gab_string(gab, gab_opcode_names[gab_valtop(value)]);
  case kGAB_NUMBER: {
    snprintf(buffer, 128, "%lg", gab_valton(value));
    return gab_string(gab, buffer);
  }
  case kGAB_UNDEFINED: {
    snprintf(buffer, 128, "undefined");
    return gab_string(gab, buffer);
  }
  case kGAB_FIBER:
  case kGAB_FIBERRUNNING:
  case kGAB_FIBERDONE: {
    struct gab_obj_fiber *m = GAB_VAL_TO_FIBER(value);
    snprintf(buffer, 128, "<" tGAB_FIBER " %p>", m);
    return gab_string(gab, buffer);
  }
  case kGAB_CHANNEL:
  case kGAB_CHANNELCLOSED: {
    struct gab_obj_channel *m = GAB_VAL_TO_CHANNEL(value);
    snprintf(buffer, 128, "<" tGAB_CHANNEL " %p>", m);
    return gab_string(gab, buffer);
  }
  case kGAB_SHAPE:
  case kGAB_SHAPELIST: {
    struct gab_obj_shape *m = GAB_VAL_TO_SHAPE(value);
    snprintf(buffer, 128, "<" tGAB_SHAPE " %p>", m);
    return gab_string(gab, buffer);
  }
  case kGAB_RECORD: {
    struct gab_obj_rec *m = GAB_VAL_TO_REC(value);
    snprintf(buffer, 128, "<" tGAB_RECORD " %p>", m);
    return gab_string(gab, buffer);
  }
  case kGAB_BLOCK: {
    struct gab_obj_block *o = GAB_VAL_TO_BLOCK(value);
    struct gab_obj_prototype *p = GAB_VAL_TO_PROTOTYPE(o->p);
    gab_value str = gab_srcname(p->src);
    snprintf(buffer, 128, "<" tGAB_BLOCK " %s:%" PRIu64 ">", gab_strdata(&str),
             gab_srcline(p->src, p->offset));
    return gab_string(gab, buffer);
  }
  case kGAB_PROTOTYPE: {
    struct gab_obj_prototype *o = GAB_VAL_TO_PROTOTYPE(value);
    gab_value str = gab_srcname(o->src);
    snprintf(buffer, 128, "<" tGAB_PROTOTYPE " %s:%" PRIu64 ">",
             gab_strdata(&str), gab_srcline(o->src, o->offset));

    return gab_string(gab, buffer);
  }
  case kGAB_NATIVE: {
    struct gab_obj_native *o = GAB_VAL_TO_NATIVE(value);
    gab_value str = o->name;
    snprintf(buffer, 128, "<" tGAB_NATIVE " %s>", gab_strdata(&str));

    return gab_string(gab, buffer);
  }
  case kGAB_BOX: {
    struct gab_obj_box *o = GAB_VAL_TO_BOX(value);
    gab_value str = o->type;
    snprintf(buffer, 128, "<%s %p>", gab_strdata(&str), o);

    return gab_string(gab, buffer);
  }
  default:
    assert(false && "Unhandled type in gab_valtos");
    return gab_undefined;
  }
}

#endif

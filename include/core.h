#ifndef GAB_COMMON_H
#define GAB_COMMON_H

// The number of cache slots, mostly used for
#ifndef cGAB_SEND_CACHE_LEN
#define cGAB_SEND_CACHE_LEN 4
#endif

#if cGAB_SEND_CACHE_LEN % 2 != 0
#error "cGAB_SEND_CACHE_LEN must be a power of 2"
#endif

#if cGAB_SEND_CACHE_LEN < 4
#error "cGAB_SEND_CACHE_LEN must be at least 4"
#endif

// Use __builtin_expect to aid the compiler
// in choosing hot/cold code paths in the interpreter.
#ifndef cGAB_LIKELY
#define cGAB_LIKELY 1
#endif

// Limit the prefix length of a string that gab will hash.
// This means that strings with an identical prefix of length
// cGAB_STRING_HASHLEN will hash to the same value - even if they are different
// *after* that prefix. This puts a cap on how *much* time gab will spend
// hashing strings - but it is a tradeoff, because all strings that share that
// prefix will basically be in a linked-list. (hash-bucket-chaining, etc) If
// this macro is 0, then gab will not limit hashing.
#ifndef cGAB_STRING_HASHLEN
#define cGAB_STRING_HASHLEN 0
#endif

// Combine common consecutive instruction patterns into one superinstruction
#ifndef cGAB_SUPERINSTRUCTIONS
#define cGAB_SUPERINSTRUCTIONS 1
#endif

// Emit tailcalls where possible
#ifndef cGAB_TAILCALL
#define cGAB_TAILCALL 1
#endif

// Workers (os threads that can actually run gab code)
// will wait this long before exiting, if they haven't received work.
// New workers are spawned as needed up until a maximum is reached (specified at
// runtime) Worker threads wait about half a second before spinning down.
#ifndef cGAB_WORKER_IDLE_TRIES
#define cGAB_WORKER_IDLE_TRIES ((size_t)1)
#endif

#ifndef cGAB_VM_CHANNEL_PUT_TRIES
#define cGAB_VM_CHANNEL_PUT_TRIES (0)
#endif

#ifndef cGAB_VM_CHANNEL_TAKE_TRIES
#define cGAB_VM_CHANNEL_TAKE_TRIES (0)
#endif

// A worker (os thread) may need to yield at an arbitrary point.
// This is done using the gab_yield function, which handles
// sleeping, context switching, and checking if the worker needs
// to participate in a garbage collection.
// A sleeptime of 0ms will result in *a lot* of context switching,
// which is undesirable for the OS Scheduler. To help this, a small
// amount of sleeping in the yield function is useful
#define GAB_YIELD_SLEEPTIME_NS (0)

// Collect as frequently as possible (on every RC push)
#ifndef cGAB_DEBUG_GC
#define cGAB_DEBUG_GC 0
#endif

// Log what is happening during collection.
#ifndef cGAB_LOG_GC
#define cGAB_LOG_GC 0
#endif

// Make sure functions don't break out of their frame
#ifndef cGAB_DEBUG_VM
#define cGAB_DEBUG_VM 0
#endif

#ifndef cGAB_DEBUG_BC
#define cGAB_DEBUG_BC 0
#endif

#ifndef cGAB_LOG_EG
#define cGAB_LOG_EG 0
#endif

// Log what is happening during execution.
#ifndef cGAB_LOG_VM
#define cGAB_LOG_VM 0
#endif

// Define how many jobs should be used, default to 8.
#ifndef cGAB_DEFAULT_NJOBS
#define cGAB_DEFAULT_NJOBS 8
#endif

// Capacity at which point dictionaries are resized
#ifndef cGAB_DICT_MAX_LOAD
#define cGAB_DICT_MAX_LOAD 0.6
#endif

// Maximum number of call frames that can be on the call stack
#ifndef cGAB_FRAMES_MAX
#define cGAB_FRAMES_MAX 32
#endif

// Maximum number of function defintions that can be nested.
#ifndef cGAB_FUNCTION_DEF_NESTING_MAX
#define cGAB_FUNCTION_DEF_NESTING_MAX 64
#endif

// Initial capacity of interned table
#ifndef cGAB_INTERN_INITIAL_CAP
#define cGAB_INTERN_INITIAL_CAP 256
#endif

// Initial capacity of module constant table
#ifndef cGAB_CONSTANTS_INITIAL_CAP
#define cGAB_CONSTANTS_INITIAL_CAP 64
#endif

#ifndef cGAB_WORKER_LOCALQUEUE_MAX
#define cGAB_WORKER_LOCALQUEUE_MAX 32
#endif

#ifndef cGAB_ERR_SPRINTF_BUF_MAX
#define cGAB_ERR_SPRINTF_BUF_MAX 4096
#endif

#ifndef cGAB_STACK_MAX
#define cGAB_STACK_MAX (cGAB_FRAMES_MAX * 32)
#endif

#ifndef cGAB_RESOURCE_MAX
#define cGAB_RESOURCE_MAX 64
#endif

// Garbage collection increment/decrement buffer size
// I don't love having these just be static buffers, its very possible
// for them to overflow
#ifndef cGAB_GC_MOD_BUFF_MAX
#define cGAB_GC_MOD_BUFF_MAX (cGAB_STACK_MAX * cGAB_WORKER_LOCALQUEUE_MAX)
#endif

#if cGAB_GC_MOD_BUFF_MAX <= cGAB_STACK_MAX
#error "cGAB_GC_MOD_BUFF_MAX must be greater than to cGAB_STACK_MAX"
#endif

#define cGAB_BINARY_LEN_CUTOFF 16

// Not configurable, just constants
#define GAB_CONSTANTS_MAX (UINT16_MAX + 1)
// Maximum value of a local.
#define GAB_LOCAL_MAX 256
// Maximum value of an upvalues.
#define GAB_UPVALUE_MAX (256 >> 1)
// Maximum number of function arguments.
#define GAB_ARG_MAX (256 >> 2)
// Maximum number of function return values.
#define GAB_RET_MAX 128

// The size of a single line of cache.
// This is enough for:
//  - The type of the value
//  - The specialization
//  - The offset of the spec in the record/message
#define GAB_SEND_CACHE_SIZE 3

// These ks are always the first two elements, and are
// necessary for tracking what message is sent
// and if it has been changed
#define GAB_SEND_KMESSAGE 0
#define GAB_SEND_KSPECS 1

#define GAB_SEND_KTYPE 2
#define GAB_SEND_KSPEC 3
#define GAB_SEND_KOFFSET 4

#define GAB_SEND_KGENERIC_CALL_SPECS 5
#define GAB_SEND_KGENERIC_CALL_MESSAGE 6

#define GAB_SEND_KNATIVE_REENTRANT_USERDATA 7

#define GAB_SEND_HASH(t) (t & (cGAB_SEND_CACHE_LEN - 1))

#define GAB_PVEC_BITS (5)
#define GAB_PVEC_SIZE (1 << GAB_PVEC_BITS)
#define GAB_PVEC_MASK (GAB_PVEC_SIZE - 1)

#if GAB_PVEC_SIZE > 64
#error "HAMT_SIZE is larger than is indexable by a size_t"
#endif

#define VAR_EXP 255
#define fHAVE_VAR (1 << 0)
#define fHAVE_TAIL (1 << 7)

enum gab_status {
#define STATUS(name, message) GAB_##name,
#include "status_code.h"
#undef STATUS
};

// VERSION
#define GAB_VERSION_MAJOR "0"
#define GAB_VERSION_MINOR "0"
#define GAB_VERSION_PATCH "4"
#define GAB_VERSION_TAG                                                        \
  GAB_VERSION_MAJOR "." GAB_VERSION_MINOR "." GAB_VERSION_PATCH

// Message constants
#define mGAB_LT "<"
#define mGAB_GT ">"
#define mGAB_EQ "=="
#define mGAB_ADD "+"
#define mGAB_SUB "-"
#define mGAB_MUL "*"
#define mGAB_DIV "/"
#define mGAB_MOD "%"
#define mGAB_BOR "|"
#define mGAB_BND "&"
#define mGAB_LOR "|"
#define mGAB_LND "&"
#define mGAB_LSH "<<"
#define mGAB_RSH ">>"
#define mGAB_LTE "<="
#define mGAB_GTE ">="
#define mGAB_SPLATLIST "*"
#define mGAB_SPLATDICT "**"
#define mGAB_CONS "cons"
#define mGAB_TYPE "?"
#define mGAB_BIN "~"
#define mGAB_LIN "!"
#define mGAB_CALL ""
#define mGAB_USE "use"
#define mGAB_TAKE ">!"
#define mGAB_PUT "<!"
#define mGAB_ASSIGN "="
#define mGAB_BLOCK "=>"
#define mGAB_MAKE "make"

#define tGAB_STRING "gab\\string"
#define tGAB_BINARY "gab\\binary"
#define tGAB_MESSAGE "gab\\message"
#define tGAB_PRIMITIVE "gab\\primitive"
#define tGAB_NUMBER "gab\\number"
#define tGAB_NATIVE "gab\\native"
#define tGAB_PROTOTYPE "gab\\prototype"
#define tGAB_BLOCK "gab\\block"
#define tGAB_RECORD "gab\\record"
#define tGAB_LIST "gab\\list"
#define tGAB_SHAPE "gab\\shape"
#define tGAB_BOX "gab\\box"
#define tGAB_FIBER "gab\\fiber"
#define tGAB_CHANNEL "gab\\channel"

#define tGAB_STRING_NAME "Strings"
#define tGAB_BINARY_NAME "Binaries"
#define tGAB_MESSAGE_NAME "Messages"
#define tGAB_PRIMITIVE_NAME "Primitives"
#define tGAB_NUMBER_NAME "Numbers"
#define tGAB_NATIVE_NAME "Natives"
#define tGAB_PROTOTYPE_NAME "Prototypes"
#define tGAB_BLOCK_NAME "Blocks"
#define tGAB_RECORD_NAME "Records"
#define tGAB_LIST_NAME "Lists"
#define tGAB_SHAPE_NAME "Shapes"
#define tGAB_BOX_NAME "Boxes"
#define tGAB_FIBER_NAME "Fibers"
#define tGAB_CHANNEL_NAME "Channels"

#define tGAB_IO "io"
#define tGAB_IOFILE "io\\file"
#define tGAB_IOSOCK "io\\sock"

#define mGAB_AST_NODE_SEND_LHS "gab\\lhs"
#define mGAB_AST_NODE_SEND_MSG "gab\\msg"
#define mGAB_AST_NODE_SEND_RHS "gab\\rhs"

#define T char
#include "slice.h"

#define T char
#include "array.h"

#define T char
#include "vector.h"

#define T uint8_t
#include "vector.h"

#define T uint32_t
#include "vector.h"

#define T uint64_t
#include "vector.h"

#define T s_char
#include "vector.h"

#define T int8_t
#include "vector.h"

#define T s_char
#include "array.h"

#define T a_char *
#define NAME a_char
#include "vector.h"

#define T uint64_t
#include "array.h"

#define K uint64_t
#define V uint64_t
#define DEF_V 0
#define HASH(a) a
#define EQUAL(a, b) (a == b)
#define LOAD cGAB_DICT_MAX_LOAD
#include "dict.h"

static inline s_char s_char_cstr(const char *str) {
  return (s_char){.data = str, .len = strlen(str)};
}

static inline s_char s_char_arr(const a_char *str) {
  return (s_char){.data = str->data, .len = str->len};
}

static inline s_char s_char_tok(s_char str, uint64_t start, char ch) {
  if (start >= str.len)
    return (s_char){.data = str.data + start, .len = 0};

  uint64_t cursor = start;

  while (cursor < str.len && str.data[cursor] != ch)
    cursor++;

  return (s_char){.data = str.data + start, .len = cursor - start};
}

static inline void v_char_spush(v_char *self, s_char slice) {
  for (size_t i = 0; i < slice.len; i++) {
    v_char_push(self, slice.data[i]);
  }
}

static inline void v_uint8_t_npush(v_uint8_t *self, size_t n, uint8_t *buff) {
  for (size_t i = 0; i < n; i++) {
    v_uint8_t_push(self, buff[i]);
  }
}

#define LEN_CARRAY(a) (sizeof(a) / sizeof(a[0]))

#endif

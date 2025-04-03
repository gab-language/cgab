#ifndef GAB_ENGINE_H
#define GAB_ENGINE_H

#include "core.h"
#include "gab.h"
#include <stdint.h>

#ifdef GAB_STATUS_NAMES_IMPL
static const char *gab_status_names[] = {
#define STATUS(name, message) message,
#include "status_code.h"
#undef STATUS
};
#undef STATUS_NAMES
#endif

#ifdef GAB_TOKEN_NAMES_IMPL
static const char *gab_token_names[] = {
#define TOKEN(message) #message,
#include "token.h"
#undef TOKEN
};
#undef TOKEN_NAMES
#endif

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

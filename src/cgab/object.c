#include "colors.h"
#include "core.h"
#include <stdatomic.h>
#include <stdint.h>
#define GAB_COLORS_IMPL
#include "colors.h"
#define GAB_OPCODE_NAMES_IMPL
#include "engine.h"
#include "gab.h"
#include "lexer.h"
#include <stdlib.h>
#include <wchar.h>

#define GAB_CREATE_OBJ(obj_type, kind)                                         \
  ((struct obj_type *)gab_obj_create(gab, sizeof(struct obj_type), kind))

#define GAB_CREATE_FLEX_OBJ(obj_type, flex_type, flex_count, kind)             \
  ((struct obj_type *)gab_obj_create(                                          \
      gab, sizeof(struct obj_type) + sizeof(flex_type) * (flex_count),         \
      (kind)))

struct gab_obj *gab_obj_create(struct gab_triple gab, uint64_t sz,
                               enum gab_kind k) {
  struct gab_obj *self = gab_egalloc(gab, nullptr, sz);

  self->kind = k;
  self->references = 1;
  self->flags = fGAB_OBJ_NEW;

#if cGAB_LOG_GC
  printf("CREATE\t%p\t%lu\t%d\n", (void *)self, sz, k);
#endif

  struct gab_job *wk = gab.eg->jobs + gab.wkid;
  if (wk->locked) {
    v_gab_value_push(&wk->lock_keep, __gab_obj(self));
    GAB_OBJ_BUFFERED(self);
#if cGAB_LOG_GC
    printf("QLOCK\t%p\n", (void *)self);
#endif
  } else {
    gab_dref(gab, __gab_obj(self));
  }

  return self;
}

uint64_t gab_obj_size(struct gab_obj *obj) {
  switch (obj->kind) {
  case kGAB_CHANNEL:
    return sizeof(struct gab_ochannel);
  case kGAB_BOX: {
    struct gab_obox *o = (struct gab_obox *)obj;
    return sizeof(struct gab_obox) + o->len * sizeof(char);
  }
  case kGAB_RECORDNODE: {
    struct gab_orecnode *o = (struct gab_orecnode *)obj;
    return sizeof(struct gab_orecnode) + o->len * sizeof(gab_value);
  }
  case kGAB_RECORD: {
    struct gab_orec *o = (struct gab_orec *)obj;
    return sizeof(struct gab_orec) + o->len * sizeof(gab_value);
  }
  case kGAB_BLOCK: {
    struct gab_oblock *o = (struct gab_oblock *)obj;
    return sizeof(struct gab_oblock) + o->nupvalues * sizeof(gab_value);
  }
  case kGAB_PROTOTYPE: {
    struct gab_oprototype *o = (struct gab_oprototype *)obj;
    return sizeof(struct gab_oprototype) + o->nupvalues * sizeof(char);
  }
  case kGAB_SHAPE:
  case kGAB_SHAPELIST: {
    struct gab_oshape *o = (struct gab_oshape *)obj;
    return sizeof(struct gab_oshape) + o->len * sizeof(gab_value);
  }
  case kGAB_STRING: {
    struct gab_ostring *o = (struct gab_ostring *)obj;
    return sizeof(struct gab_ostring) + (o->len + 1) * sizeof(char);
  }
  case kGAB_FIBER:
    return sizeof(struct gab_ofiber);
  case kGAB_NATIVE:
    return sizeof(struct gab_onative);
  default:
    break;
  }
  assert(false && "unreachable");
  return 0;
}

int sshape_dumpkeys(char **dest, size_t *n, gab_value shape, int depth) {
  struct gab_oshape *shp = GAB_VAL_TO_SHAPE(shape);
  uint64_t len = shp->len;

  if (len == 0)
    return 0;

  if (len > 16 && depth >= 0)
    return snprintf_through(dest, n, "... ");

  if (snprintf_through(dest, n, " ") < 0)
    return -1;

  for (uint64_t i = 0; i < len; i++) {
    if (gab_svalinspect(dest, n, shp->keys[i], depth - 1) < 0)
      return -1;

    if (i + 1 < len)
      if (snprintf_through(dest, n, " ") < 0)
        return -1;
  }

  return snprintf_through(dest, n, " ");
}
int srec_dumpvalues(char **dest, size_t *n, gab_value rec, int depth) {
  assert(gab_valkind(rec) == kGAB_RECORD);
  uint64_t len = gab_reclen(rec);

  if (len == 0)
    return 0;

  if (len > 16 && depth >= 0)
    return snprintf_through(dest, n, " ... ");

  if (snprintf_through(dest, n, " ") < 0)
    return -1;

  for (uint64_t i = 0; i < len; i++) {
    if (gab_svalinspect(dest, n, gab_uvrecat(rec, i), depth - 1) < 0)
      return -1;

    if (i + 1 < len)
      if (snprintf_through(dest, n, ", ") < 0)
        return -1;
  }

  return snprintf_through(dest, n, " ");
}

int srec_dumpproperties(char **dest, size_t *n, gab_value rec, int depth) {
  switch (gab_valkind(rec)) {
  case kGAB_RECORD: {
    uint64_t len = gab_reclen(rec);

    if (len == 0)
      return 0;

    if (len > 16 && depth >= 0)
      return snprintf_through(dest, n, " ... ");

    if (snprintf_through(dest, n, " ") < 0)
      return -1;

    for (uint64_t i = 0; i < len; i++) {
      if (gab_svalinspect(dest, n, gab_ukrecat(rec, i), depth - 1) < 0)
        return -1;

      if (snprintf_through(dest, n, " ") < 0)
        return -1;

      if (gab_svalinspect(dest, n, gab_uvrecat(rec, i), depth - 1) < 0)
        return -1;

      if (i + 1 < len)
        if (snprintf_through(dest, n, ", ") < 0)
          return -1;
    }

    return snprintf_through(dest, n, " ");
  }
  case kGAB_RECORDNODE: {
    struct gab_orecnode *m = GAB_VAL_TO_RECNODE(rec);
    uint64_t len = m->len;

    if (len == 0)
      return snprintf_through(dest, n, "~ ");

    if (len > 16)
      return snprintf_through(dest, n, "... ");

    if (snprintf_through(dest, n, " ") < 0)
      return -1;

    for (uint64_t i = 0; i < len; i++) {
      if (gab_svalinspect(dest, n, m->data[i], depth - 1) < 0)
        return -1;

      if (i + 1 < len)
        if (snprintf_through(dest, n, ", ") < 0)
          return -1;
    }

    return snprintf_through(dest, n, " ");
  }
  default:
    break;
  }
  assert(false && "NOT A REC");
  return 0;
}

int sinspectval(char **dest, size_t *n, gab_value self, int depth) {
  switch (gab_valkind(self)) {
  case kGAB_PRIMITIVE: {
    switch (self) {
    case gab_cundefined:
      return snprintf_through(dest, n, "cundefined");
    case gab_cinvalid:
      return snprintf_through(dest, n, "cinvalid");
    case gab_ctimeout:
      return snprintf_through(dest, n, "ctimeout");
    case gab_cvalid:
      return snprintf_through(dest, n, "cvalid");
    default:
      return snprintf_through(dest, n, "<" tGAB_PRIMITIVE " %s>",
                              gab_opcode_names[gab_valtop(self)]);
    }
  }
  case kGAB_NUMBER:
    return snprintf_through(dest, n, "%lg", gab_valtof(self));
  case kGAB_STRING:
    return snprintf_through(dest, n, "%s", gab_strdata(&self));
  case kGAB_BINARY: {
    const char *s = gab_strdata(&self);

    if (snprintf_through(dest, n, "<" tGAB_BINARY " 0x") < 0)
      return -1;

    uint64_t len = gab_strlen(self);

    if (len < cGAB_BINARY_LEN_CUTOFF) {
      while (len--)
        if (snprintf_through(dest, n, "%02x", (unsigned char)*s++) < 0)
          return -1;
    } else {
      uint64_t preview = cGAB_BINARY_LEN_CUTOFF;
      while (preview--)
        if (snprintf_through(dest, n, "%02x", (unsigned char)*s++) < 0)
          return -1;

      if (snprintf_through(dest, n, "...") < 0)
        return -1;
    }

    if (snprintf_through(dest, n, ">") < 0)
      return -1;

    return 0;
  }
  case kGAB_MESSAGE:
    return snprintf_through(dest, n, "%s:", gab_strdata(&self));
  case kGAB_SHAPE:
  case kGAB_SHAPELIST:
    return snprintf_through(dest, n, "<" tGAB_SHAPE " ") +
           sshape_dumpkeys(dest, n, self, depth) +
           snprintf_through(dest, n, ">");
  case kGAB_CHANNEL:
  case kGAB_CHANNELCLOSED:
    return snprintf_through(dest, n, "<" tGAB_CHANNEL ">");
  case kGAB_FIBER:
  case kGAB_FIBERRUNNING:
  case kGAB_FIBERDONE:
    return snprintf_through(dest, n, "<" tGAB_FIBER ">");
  case kGAB_RECORD: {
    if (gab_valkind(gab_recshp(self)) == kGAB_SHAPELIST)
      return snprintf_through(dest, n, "[") +
             srec_dumpvalues(dest, n, self, depth) +
             snprintf_through(dest, n, "]");
    else
      return snprintf_through(dest, n, "{") +
             srec_dumpproperties(dest, n, self, depth) +
             snprintf_through(dest, n, "}");
  }
  case kGAB_RECORDNODE:
    return srec_dumpproperties(dest, n, self, depth);
  case kGAB_BOX: {
    struct gab_obox *con = GAB_VAL_TO_BOX(self);
    return snprintf_through(dest, n, "<" tGAB_BOX " ") +
           gab_svalinspect(dest, n, con->type, depth) +
           snprintf_through(dest, n, ">");
  }
  case kGAB_BLOCK: {
    struct gab_oblock *blk = GAB_VAL_TO_BLOCK(self);
    struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(blk->p);
    uint64_t line = gab_srcline(p->src, p->offset);
    return snprintf_through(dest, n, "<" tGAB_BLOCK " ") +
           gab_svalinspect(dest, n, gab_srcname(p->src), depth) +
           snprintf_through(dest, n, ":%lu>", line);
  }
  case kGAB_NATIVE: {
    struct gab_onative *native = GAB_VAL_TO_NATIVE(self);
    return snprintf_through(dest, n, "<" tGAB_NATIVE " ") +
           gab_svalinspect(dest, n, native->name, depth) +
           snprintf_through(dest, n, ">");
  }
  case kGAB_PROTOTYPE: {
    struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(self);
    uint64_t line = gab_srcline(p->src, p->offset);
    return snprintf_through(dest, n, "<" tGAB_PROTOTYPE " ") +
           gab_svalinspect(dest, n, gab_srcname(p->src), depth) +
           snprintf_through(dest, n, ":%lu>", line);
  }
  default:
    break;
  }
  assert(false && "NOT AN OBJECT");
  return 0;
}

int gab_svalinspect(char **dest, size_t *n, gab_value value, int depth) {
  return sinspectval(dest, n, value, depth);
}

int gab_fvalinspect(FILE *stream, gab_value self, int depth) {
  for (size_t i = 128;; i <<= 1) {
    size_t n = i;
    char buf[n];
    char *cursor = buf;
    if (gab_svalinspect(&cursor, &n, self, depth) >= 0) {
      fprintf(stream, "%s", buf);
      return 1;
    };
  }

  return 0;
}

void gab_objdestroy(struct gab_triple gab, struct gab_obj *self) {
  switch (self->kind) {
  case kGAB_FIBER:
  case kGAB_FIBERRUNNING:
  case kGAB_FIBERDONE: {
    /*struct gab_ofiber *fib = (struct gab_ofiber *)self;*/
    /*if (fib->res_values != nullptr)*/
    /*  a_gab_value_destroy(fib->res_values);*/

    break;
  };
  case kGAB_SHAPE:
  case kGAB_SHAPELIST: {
    struct gab_oshape *shp = (struct gab_oshape *)self;
    v_gab_value_destroy(&shp->transitions);
    break;
  }
  case kGAB_BOX: {
    struct gab_obox *box = (struct gab_obox *)self;
    if (box->do_destroy)
      box->do_destroy(gab, box->len, box->data);
    break;
  }
  case kGAB_STRING:
    mtx_lock(&gab.eg->strings_mtx);
    /*
     * ASYNC ISSUE: Because collections happen asynchronously (and the strings
     * intern table *doesn't hold references) Strings that are queued for
     * removal *can* be re-used *right* before they are deleted. This requires a
     * better, long-term solution.
     *
     * TO RESOLVE THIS ISSUE: Strings are incremented as they are created.
     * This means strings are never deallocated (not an ideal solution)
     */
    d_strings_remove(&gab.eg->strings, (struct gab_ostring *)self);
    mtx_unlock(&gab.eg->strings_mtx);
    break;
  default:
    break;
  }
}

gab_value gab_shorstr(uint64_t len, const char *data) {
  assert(len <= 5);

  gab_value v = 0;
  v |= (__GAB_QNAN | (uint64_t)kGAB_STRING << __GAB_TAGOFFSET |
        (((uint64_t)5 - len) << 40));

  for (uint64_t i = 0; i < len; i++) {
    v |= (uint64_t)(0xff & data[i]) << (i * 8);
  }

  return v;
}

gab_value gab_shortstrcat(gab_value _a, gab_value _b) {
  assert(gab_valkind(_a) == kGAB_STRING || gab_valkind(_a) == kGAB_MESSAGE);
  assert(gab_valkind(_b) == kGAB_STRING || gab_valkind(_b) == kGAB_MESSAGE);

  uint64_t alen = gab_strlen(_a);
  uint64_t blen = gab_strlen(_b);

  assert(alen + blen <= 5);

  uint8_t len = alen + blen;

  gab_value v = 0;
  v |= (__GAB_QNAN | (uint64_t)kGAB_STRING << __GAB_TAGOFFSET |
        (((uint64_t)5 - len) << 40));

  for (uint64_t i = 0; i < alen; i++) {
    v |= (uint64_t)(0xff & gab_strdata(&_a)[i]) << (i * 8);
  }

  for (uint64_t i = 0; i < blen; i++) {
    v |= (uint64_t)(0xff & gab_strdata(&_b)[i]) << ((i + alen) * 8);
  }

  assert(gab_valkind(v) == kGAB_STRING);

  return v;
}

gab_value nstring(struct gab_triple gab, uint64_t hash, uint64_t len,
                  const char *data) {
  s_char str = s_char_create(data, len);

  struct gab_ostring *self =
      GAB_CREATE_FLEX_OBJ(gab_ostring, char, str.len + 1, kGAB_STRING);

  memcpy(self->data, str.data, str.len);
  self->len = str.len;
  self->hash = hash;

  mbstate_t state = {0};
  const char *cursor = self->data;
  self->mb_len = mbsrtowcs(NULL, &cursor, 0, &state);

  /* The strings table should hold a reference to this string */
  d_strings_insert(&gab.eg->strings, self, 0);
  return gab_iref(gab, __gab_obj(self));
}

gab_value gab_nstring(struct gab_triple gab, uint64_t len, const char *data) {
  if (len <= 5)
    return gab_shorstr(len, data);

  mtx_lock(&gab.eg->strings_mtx);

#if cGAB_STRING_HASHLEN > 0
  uint64_t hash =
      hash_bytes(len < cGAB_STRING_HASHLEN ? len : cGAB_STRING_HASHLEN,
                 (unsigned char *)data);
#else
  uint64_t hash = hash_bytes(len, (unsigned char *)data);
#endif

  struct gab_ostring *interned = gab_egstrfind(gab.eg, hash, len, data);

  if (interned)
    return mtx_unlock(&gab.eg->strings_mtx), __gab_obj(interned);

  gab_value s = nstring(gab, hash, len, data);

  return mtx_unlock(&gab.eg->strings_mtx), s;
};

GAB_API inline const char *gab_strdata(gab_value *str) {
  assert(gab_valkind(*str) == kGAB_STRING ||
         gab_valkind(*str) == kGAB_MESSAGE || gab_valkind(*str) == kGAB_BINARY);

  if (gab_valiso(*str))
    return GAB_VAL_TO_STRING(*str)->data;

  return ((const char *)str);
}

GAB_API inline uint64_t gab_strlen(gab_value str) {
  assert(gab_valkind(str) == kGAB_STRING || gab_valkind(str) == kGAB_MESSAGE ||
         gab_valkind(str) == kGAB_BINARY);

  if (gab_valiso(str))
    return GAB_VAL_TO_STRING(str)->len;

  return 5 - ((str >> 40) & 0xFF);
};

GAB_API inline uint64_t gab_strmblen(gab_value str) {
  assert(gab_valkind(str) == kGAB_STRING || gab_valkind(str) == kGAB_BINARY ||
         gab_valkind(str) == kGAB_MESSAGE);

  if (gab_valiso(str))
    return GAB_VAL_TO_STRING(str)->mb_len;

  // This is a small string. No space to store mb_len, so just recompute.
  mbstate_t state = {0};
  const char *cursor = gab_strdata(&str);
  return mbsrtowcs(NULL, &cursor, 0, &state);
};

GAB_API inline uint64_t gab_strhash(gab_value str) {
  assert(gab_valkind(str) == kGAB_STRING);

  if (gab_valiso(str))
    return GAB_VAL_TO_STRING(str)->hash;

  // TODO: Propertly hash the contents of short strings.
  return str;
}

GAB_API inline int gab_binat(gab_value str, size_t idx) {
  assert(gab_valkind(str) == kGAB_BINARY);

  size_t len = gab_strlen(str);

  if (idx >= len)
    return -1;

  return gab_strdata(&str)[idx];
}

/*
  Given two strings, create a third which is the concatenation a+b
*/
gab_value gab_strcat(struct gab_triple gab, gab_value _a, gab_value _b) {
  assert(gab_valkind(_a) == kGAB_STRING || gab_valkind(_a) == kGAB_MESSAGE);
  assert(gab_valkind(_b) == kGAB_STRING || gab_valkind(_b) == kGAB_MESSAGE);

  uint64_t alen = gab_strlen(_a);
  uint64_t blen = gab_strlen(_b);

  if (alen == 0)
    return _b;

  if (blen == 0)
    return _a;

  uint64_t len = alen + blen;

  if (len <= 5)
    return gab_shortstrcat(_a, _b);

  a_char *buff = a_char_empty(len + 1);

  // Copy the data into the string obj.
  memcpy(buff->data, gab_strdata(&_a), alen);
  memcpy(buff->data + alen, gab_strdata(&_b), blen);

// Pre compute the hash
#if cGAB_STRING_HASHLEN > 0
  uint64_t hash =
      hash_bytes(len < cGAB_STRING_HASHLEN ? len : cGAB_STRING_HASHLEN,
                 (unsigned char *)buff->data);
#else
  uint64_t hash = hash_bytes(len, (unsigned char *)buff->data);
#endif

  /*
    If this string was interned already, return.

    Unfortunately, we can't check for this before copying and computing the
    hash.
  */
  mtx_lock(&gab.eg->strings_mtx);

  struct gab_ostring *interned = gab_egstrfind(gab.eg, hash, len, buff->data);

  if (interned)
    return a_char_destroy(buff), mtx_unlock(&gab.eg->strings_mtx),
           __gab_obj(interned);

  gab_value result = nstring(gab, hash, len, buff->data);

  assert(gab_valkind(result) == kGAB_STRING);
  assert(gab_strlen(result) == len);

  return a_char_destroy(buff), mtx_unlock(&gab.eg->strings_mtx), result;
};

gab_value gab_prototype(struct gab_triple gab, struct gab_src *src,
                        uint64_t offset, uint64_t len,
                        struct gab_prototype_argt args) {

  struct gab_oprototype *self = GAB_CREATE_FLEX_OBJ(
      gab_oprototype, uint8_t, args.nupvalues, kGAB_PROTOTYPE);

  self->src = src;
  self->offset = offset;
  self->len = args.nupvalues;
  self->len = len;
  self->nslots = args.nslots;
  self->nlocals = args.nlocals;
  self->nupvalues = args.nupvalues;
  self->narguments = args.narguments;
  self->env = args.env;

  if (args.nupvalues > 0) {
    if (args.data) {
      memcpy(self->data, args.data, args.nupvalues * sizeof(uint8_t));
    } else if (args.flags && args.indexes) {
      for (uint8_t i = 0; i < args.nupvalues; i++) {
        bool is_local = args.flags[i] & fLOCAL_LOCAL;
        self->data[i] = (args.indexes[i] << 1) | is_local;
      }
    } else {
      assert(0 && "Invalid arguments to gab_bprototype");
    }
  }

  return __gab_obj(self);
}

gab_value gab_prtenv(gab_value prt) {
  assert(gab_valkind(prt) == kGAB_PROTOTYPE);
  return GAB_VAL_TO_PROTOTYPE(prt)->env;
}

gab_value gab_native(struct gab_triple gab, gab_value name, gab_native_f f) {
  assert(gab_valkind(name) == kGAB_STRING || gab_valkind(name) == kGAB_MESSAGE);

  struct gab_onative *self = GAB_CREATE_OBJ(gab_onative, kGAB_NATIVE);

  self->name = name;
  self->function = f;

  return __gab_obj(self);
}

gab_value gab_snative(struct gab_triple gab, const char *name, gab_native_f f) {
  return gab_native(gab, gab_string(gab, name), f);
}

gab_value gab_block(struct gab_triple gab, gab_value prototype) {
  assert(gab_valkind(prototype) == kGAB_PROTOTYPE);
  struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(prototype);

  struct gab_oblock *self =
      GAB_CREATE_FLEX_OBJ(gab_oblock, gab_value, p->nupvalues, kGAB_BLOCK);

  self->p = prototype;
  self->nupvalues = p->nupvalues;

  for (uint8_t i = 0; i < self->nupvalues; i++) {
    self->upvalues[i] = gab_cinvalid;
  }

  return __gab_obj(self);
}

gab_value gab_box(struct gab_triple gab, struct gab_box_argt args) {
  struct gab_obox *self =
      GAB_CREATE_FLEX_OBJ(gab_obox, unsigned char, args.size, kGAB_BOX);

  self->do_destroy = args.destructor;
  self->do_visit = args.visitor;
  self->type = args.type;
  self->len = args.size;

  if (args.data) {
    memcpy(self->data, args.data, args.size);
  } else {
    memset(self->data, 0, args.size);
  }

  return __gab_obj(self);
}

GAB_API uint64_t gab_boxlen(gab_value box) {
  assert(gab_valkind(box) == kGAB_BOX);
  return GAB_VAL_TO_BOX(box)->len;
}

GAB_API void *gab_boxdata(gab_value box) {
  assert(gab_valkind(box) == kGAB_BOX);
  return GAB_VAL_TO_BOX(box)->data;
}

GAB_API gab_value gab_boxtype(gab_value value) {
  assert(gab_valkind(value) == kGAB_BOX);
  return GAB_VAL_TO_BOX(value)->type;
}

gab_value __gab_record(struct gab_triple gab, uint64_t len, uint64_t space,
                       gab_value *data) {
  struct gab_orec *self =
      GAB_CREATE_FLEX_OBJ(gab_orec, gab_value, space + len, kGAB_RECORD);

  self->len = len + space;
  self->shape = gab_cinvalid;
  self->shift = GAB_PVEC_BITS;

  if (len) {
    assert(data);
    memcpy(self->data, data, sizeof(gab_value) * len);
  }

  for (uint64_t i = len; i < self->len; i++)
    self->data[i] = gab_cinvalid;

  return __gab_obj(self);
}

gab_value __gab_recordnode(struct gab_triple gab, uint64_t len, uint64_t adjust,
                           gab_value *data) {
  assert(adjust + len > 0);
  struct gab_orecnode *self = GAB_CREATE_FLEX_OBJ(
      gab_orecnode, gab_value, adjust + len, kGAB_RECORDNODE);

  self->len = len + adjust;

  if (len) {
    assert(data);
    memcpy(self->data, data, sizeof(gab_value) * len);
  }

  for (uint64_t i = len; i < self->len; i++)
    self->data[i] = gab_cinvalid;

  return __gab_obj(self);
}

gab_value reccpy(struct gab_triple gab, gab_value r, int64_t adjust) {
  switch (gab_valkind(r)) {
  case kGAB_RECORD: {
    struct gab_orec *n = GAB_VAL_TO_REC(r);

    struct gab_orec *nm =
        GAB_VAL_TO_REC(__gab_record(gab, n->len, adjust, n->data));

    nm->shift = n->shift;
    nm->shape = n->shape;

    return __gab_obj(nm);
  }
  case kGAB_RECORDNODE: {
    struct gab_orecnode *n = GAB_VAL_TO_RECNODE(r);
    return __gab_recordnode(gab, n->len, adjust, n->data);
  }
    // Saw invalid
  case kGAB_PRIMITIVE:
    assert(r == gab_cinvalid);
    return __gab_recordnode(gab, 0, 1, nullptr);
  default:
    break;
  }

  assert(0 && "Only rec and recnodebranch cpy");
  return gab_cinvalid;
}
void recpop(gab_value rec) {
  switch (gab_valkind(rec)) {
  case kGAB_RECORDNODE: {
    struct gab_orecnode *r = GAB_VAL_TO_RECNODE(rec);
    assert(r->len > 0);
    r->len--;
    return;
  }
  case kGAB_RECORD: {
    struct gab_orec *r = GAB_VAL_TO_REC(rec);
    assert(r->len > 0);
    r->len--;
    return;
  }
  default:
    break;
  }
  assert(false && "UNREACHABLE");
}

void recassoc(gab_value rec, gab_value v, uint64_t i) {
  switch (gab_valkind(rec)) {
  case kGAB_RECORDNODE: {
    struct gab_orecnode *r = GAB_VAL_TO_RECNODE(rec);
    assert(i < r->len);
    r->data[i] = v;
    return;
  }
  case kGAB_RECORD: {
    struct gab_orec *r = GAB_VAL_TO_REC(rec);
    assert(i < r->len);
    r->data[i] = v;
    return;
  }
  default:
    break;
  }
  assert(false && "UNREACHABLE");
}

gab_value recnth(gab_value rec, uint64_t n) {
  switch (gab_valkind(rec)) {
  case kGAB_RECORDNODE: {
    struct gab_orecnode *r = GAB_VAL_TO_RECNODE(rec);
    assert(n < r->len);
    return r->data[n];
  }
  case kGAB_RECORD: {
    struct gab_orec *r = GAB_VAL_TO_REC(rec);
    assert(n < r->len);
    return r->data[n];
  }
  default:
    break;
  }

  assert(false && "UNREACHABLE");
  return gab_cinvalid;
}

uint64_t reclen(gab_value rec) {
  switch (gab_valkind(rec)) {
  case kGAB_RECORDNODE:
    return GAB_VAL_TO_RECNODE(rec)->len;
  case kGAB_RECORD:
    return GAB_VAL_TO_REC(rec)->len;
  case kGAB_PRIMITIVE:
    assert(rec == gab_cinvalid);
    return 0;
  default:
    assert(false && "UNREACHABLE");
    return 0;
  }
}

gab_value gab_uvrecat(gab_value rec, uint64_t i) {
  assert(gab_valkind(rec) == kGAB_RECORD);

  struct gab_orec *r = GAB_VAL_TO_REC(rec);

  gab_value node = rec;

  for (int64_t level = r->shift; level > 0; level -= GAB_PVEC_BITS) {
    uint64_t idx = (i >> level) & GAB_PVEC_MASK;

    gab_value next_node = recnth(node, idx);

    assert(gab_valkind(next_node) == kGAB_RECORDNODE ||
           gab_valkind(next_node) == kGAB_RECORD);

    node = next_node;
  }

  node = recnth(node, i & GAB_PVEC_MASK);

  assert(gab_valkind(node) != kGAB_RECORDNODE);

  return node;
}

bool recneedsspace(gab_value rec, uint64_t i) {
  assert(gab_valkind(rec) == kGAB_RECORD);
  struct gab_orec *r = GAB_VAL_TO_REC(rec);
  uint64_t idx = (i >> r->shift) & GAB_PVEC_MASK;
  return idx >= r->len;
}

gab_value recsetshp(gab_value rec, gab_value shp) {
  assert(gab_valkind(rec) == kGAB_RECORD);
  struct gab_orec *r = GAB_VAL_TO_REC(rec);
  r->shape = shp;
  return rec;
}

/*
 * Since order is always dictated by shape, we can do a cheeky optimization for
 * dissoc.
 *
 * The bit-partitioned vector trie data structure used for records only supports
 * push-and-pop operations.
 *
 * To do a dissoc from anywhere within the record, we need to swap the value at
 * the end and of the record with the chosen value, and then perform the pop.
 * This means we need to clone two paths through the trie -
 *  1. one down to the chosen value
 *  2. one down to the last node
 *
 * then, perform the swap and pop
 *
 * we can do this because shapes dictate order, not records themselves.
 * this will create a new shape. (to account for the swapped value, not seen
 * here)
 *
 * There is a fast case, where the value popped *is* the last value.
 */
gab_value dissoc(struct gab_triple gab, gab_value rec, uint64_t i) {
  assert(gab_valkind(rec) == kGAB_RECORD);
  struct gab_orec *r = GAB_VAL_TO_REC(rec);

  gab_value chosen_node = rec;
  gab_value root = chosen_node;
  gab_value chosen_path = root;
  gab_value rightmost_node = rec;
  gab_value rightmost_path = root;

  /*
   * Keep track of if the path to chosen elem has diverged
   * from the path to the last elem.
   *
   * While they converge, we only have to copy one path.
   */
  bool diverged = false;

  for (int64_t level = r->shift; level > 0; level -= GAB_PVEC_BITS) {
    uint64_t idx = (i >> level) & GAB_PVEC_MASK;

    assert(idx < reclen(chosen_node));

    chosen_node = reccpy(gab, recnth(chosen_node, idx), 0);

    recassoc(chosen_path, chosen_node, idx);
    chosen_path = chosen_node;

    uint64_t rightmost_idx = reclen(rightmost_node) - 1;

    if (!diverged && idx == rightmost_idx) {
      rightmost_node = chosen_node;
      rightmost_path = chosen_path;
      continue;
    }

    diverged = true;

    assert(rightmost_idx < reclen(rightmost_node));

    // Improve this to trim this copy with negative space when necessary
    // TODO: Account for popping out empty nodes
    rightmost_node = reccpy(gab, recnth(rightmost_node, rightmost_idx), 0);

    recassoc(rightmost_path, rightmost_node, rightmost_idx);
    rightmost_path = rightmost_node;
  }

  assert(chosen_node != gab_cinvalid);
  // Update the chosen node with the value we're popping
  recassoc(chosen_node, recnth(rightmost_node, reclen(rightmost_node) - 1),
           i & GAB_PVEC_MASK);

  // the rightmost node should have one less value. This can be done more
  // effieciently above.
  recpop(rightmost_node);
  return root;
}

gab_value assoc(struct gab_triple gab, gab_value rec, gab_value v, uint64_t i) {
  assert(gab_valkind(rec) == kGAB_RECORD);
  struct gab_orec *r = GAB_VAL_TO_REC(rec);

  gab_value node = rec;
  gab_value root = node;
  gab_value path = root;

  for (int64_t level = r->shift; level > 0; level -= GAB_PVEC_BITS) {
    uint64_t idx = (i >> level) & GAB_PVEC_MASK;
    uint64_t nidx = (i >> (level - GAB_PVEC_BITS)) & GAB_PVEC_MASK;

    if (idx < reclen(node))
      node = reccpy(gab, recnth(node, idx), nidx >= reclen(recnth(node, idx)));
    else
      node = __gab_recordnode(gab, 0, 1, nullptr);

    recassoc(path, node, idx);
    path = node;
  }

  assert(node != gab_cinvalid);
  recassoc(node, v, i & GAB_PVEC_MASK);
  return root;
}

void massoc(struct gab_triple gab, gab_value rec, gab_value v, uint64_t i) {
  assert(gab_valkind(rec) == kGAB_RECORD);
  struct gab_orec *r = GAB_VAL_TO_REC(rec);

  assert(i < gab_reclen(rec));

  gab_value node = rec;

  for (int64_t level = r->shift; level > 0; level -= GAB_PVEC_BITS) {
    uint64_t idx = (i >> level) & GAB_PVEC_MASK;

    assert(idx < reclen(node));
    node = recnth(node, idx);
  }

  assert(node != gab_cinvalid);
  recassoc(node, v, i & GAB_PVEC_MASK);

  return;
}

gab_value cons(struct gab_triple gab, gab_value rec, gab_value v,
               gab_value shp) {
  assert(gab_valkind(rec) == kGAB_RECORD);
  struct gab_orec *r = GAB_VAL_TO_REC(rec);

  uint64_t i = gab_reclen(rec);

  // overflow root
  if ((i >> GAB_PVEC_BITS) >= ((uint64_t)1 << r->shift)) {
    gab_value new_root = __gab_record(gab, 1, 1, &rec);

    struct gab_orec *new_r = GAB_VAL_TO_REC(new_root);

    new_r->shape = shp;
    new_r->shift = r->shift + 5;

#ifndef NDEBUG
    for (size_t j = 0; j < i; j++)
      gab_uvrecat(new_root, j);
#endif

    assoc(gab, new_root, v, i);

#ifndef NDEBUG
    for (size_t j = 0; j < i; j++)
      gab_uvrecat(new_root, j);
#endif

    return new_root;
  }

  gab_value record =
      recsetshp(assoc(gab, reccpy(gab, rec, recneedsspace(rec, i)), v, i), shp);

  // ASSERT ALL leaves are valid
  for (size_t j = 0; j < i; j++)
    gab_uvrecat(record, j);

  return record;
}

gab_value gab_recput(struct gab_triple gab, gab_value rec, gab_value key,
                     gab_value val) {
  assert(gab_valkind(rec) == kGAB_RECORD);

  uint64_t idx = gab_recfind(rec, key);

  gab_gclock(gab);

  if (idx == -1) {
    gab_value result =
        cons(gab, rec, val, gab_shpwith(gab, gab_recshp(rec), key));

    return gab_gcunlock(gab), result;
  }

  gab_value result =
      assoc(gab, reccpy(gab, rec, recneedsspace(rec, idx)), val, idx);

  return gab_gcunlock(gab), result;
}

gab_value gab_rectake(struct gab_triple gab, gab_value rec, gab_value key,
                      gab_value *value) {
  assert(gab_valkind(rec) == kGAB_RECORD);

  uint64_t idx = gab_recfind(rec, key);

  if (idx == -1) {
    if (value)
      *value = gab_nil;

    return rec;
  }

  gab_gclock(gab);

  if (value)
    *value = gab_uvrecat(rec, idx);

  gab_value result = recsetshp(dissoc(gab, reccpy(gab, rec, 0), idx),
                               gab_shpwithout(gab, gab_recshp(rec), key));

  return gab_gcunlock(gab), result;
}

gab_value gab_nlstpush(struct gab_triple gab, gab_value list, uint64_t len,
                       gab_value *values) {
  assert(gab_valkind(list) == kGAB_RECORD);

  uint64_t start = gab_reclen(list);

  gab_gclock(gab);

  for (uint64_t i = 0; i < len; i++) {
    gab_value key = gab_number(start + i);
    gab_value val = values[i];
    list = gab_recput(gab, list, key, val);
  }

  return gab_gcunlock(gab), list;
}

gab_value gab_lstpop(struct gab_triple gab, gab_value list, gab_value *popped) {
  return gab_rectake(gab, list, gab_number(gab_reclen(list) - 1), popped);
}

gab_value gab_urecput(struct gab_triple gab, gab_value rec, uint64_t i,
                      gab_value v) {
  assert(gab_valkind(rec) == kGAB_RECORD);
  assert(i < gab_reclen(rec));

  gab_gclock(gab);

  gab_value result = assoc(gab, reccpy(gab, rec, 0), v, i);

  return gab_gcunlock(gab), result;
}

uint64_t getlen(uint64_t n, uint64_t shift) {
  if (n)
    n--;

  return ((n >> shift) & GAB_PVEC_MASK) + 1;
}

void recfillchildren(struct gab_triple gab, gab_value rec, uint64_t shift,
                     uint64_t n, uint64_t len) {
  assert(len > 0);

  if (shift == 0)
    return;

  for (uint64_t l = 0; l < len - 1; l++) {
    gab_value lhs_child = __gab_recordnode(gab, 0, GAB_PVEC_SIZE, nullptr);

    recfillchildren(gab, lhs_child, shift - GAB_PVEC_BITS, n, GAB_PVEC_SIZE);

    recassoc(rec, lhs_child, l);
  }

  uint64_t rhs_childlen = getlen(n, shift - GAB_PVEC_BITS);

  gab_value rhs_child = __gab_recordnode(gab, 0, rhs_childlen, nullptr);

  recfillchildren(gab, rhs_child, shift - GAB_PVEC_BITS, n, rhs_childlen);

  recassoc(rec, rhs_child, len - 1);
}

uint64_t getshift(uint64_t n) {
  uint64_t shift = 0;

  if (n)
    n--;

  while ((n >> GAB_PVEC_BITS) >= (1 << shift)) {
    shift += 5;
  }

  return shift;
}

gab_value gab_shptorec(struct gab_triple gab, gab_value shp) {
  assert(gab_valkind(shp) == kGAB_SHAPE || gab_valkind(shp) == kGAB_SHAPELIST);

  uint64_t len = gab_shplen(shp);

  gab_gclock(gab);

  uint64_t shift = getshift(len);

  uint64_t rootlen = getlen(len, shift);

  struct gab_orec *self =
      GAB_CREATE_FLEX_OBJ(gab_orec, gab_value, rootlen, kGAB_RECORD);

  self->shape = shp;
  self->shift = shift;
  self->len = rootlen;

  gab_value res = __gab_obj(self);

  if (len) {
    recfillchildren(gab, res, shift, len, rootlen);

    for (uint64_t i = 0; i < len; i++)
      massoc(gab, res, gab_nil, i);
  }

  return gab_gcunlock(gab), res;
}

gab_value gab_recordfrom(struct gab_triple gab, gab_value shape,
                         uint64_t stride, uint64_t len, gab_value *vals,
                         uint64_t *km) {
  gab_gclock(gab);

  uint64_t real_len = gab_shplen(shape);
  assert(real_len <= len);

  uint64_t shift = getshift(real_len);

  uint64_t rootlen = getlen(real_len, shift);

  struct gab_orec *self =
      GAB_CREATE_FLEX_OBJ(gab_orec, gab_value, rootlen, kGAB_RECORD);

  self->shape = shape;
  self->shift = shift;
  self->len = rootlen;

  gab_value res = __gab_obj(self);

  if (real_len) {
    recfillchildren(gab, res, shift, real_len, rootlen);

    assert(real_len == gab_shplen(self->shape));

    uint64_t real_i = 0;
    for (uint64_t i = 0; i < len; i++) {
      uint64_t km_idx = i / 64;
      uint64_t in_idx = i % 64;
      assert(in_idx < 64);
      if (!km || !(km[km_idx] & ((uint64_t)1 << in_idx)))
        massoc(gab, res, vals[i * stride], real_i++);
    }
  }

  return gab_gcunlock(gab), res;
}

gab_value gab_record(struct gab_triple gab, uint64_t stride, uint64_t len,
                     gab_value *keys, gab_value *vals) {
  gab_gclock(gab);

  uint64_t km_size = 1 + (len / 64);
  uint64_t km[km_size];
  memset(km, 0, km_size * sizeof(uint64_t));

  gab_value shp = gab_shape(gab, stride, len, keys, km);
  gab_value rec = gab_recordfrom(gab, shp, stride, len, vals, km);
  return gab_gcunlock(gab), rec;
}

gab_value gab_recshp(gab_value record) {
  assert(gab_valkind(record) == kGAB_RECORD);
  return GAB_VAL_TO_REC(record)->shape;
};

gab_value nth_amongst(uint64_t n, uint64_t len, gab_value records[static len]) {
  assert(len > 0);

  uint64_t r = 0;
  uint64_t i = 0;

  while (r < len && n >= i + gab_reclen(records[r]))
    i += gab_reclen(records[r++]);

  return gab_uvrecat(records[r], n - i);
}

gab_value gab_nlstcat(struct gab_triple gab, uint64_t len,
                      gab_value records[static len]) {
  if (len == 0)
    return gab_erecord(gab);

  uint64_t total_len = 0;

  for (uint64_t i = 0; i < len; i++)
    total_len += gab_reclen(records[i]);

  if (total_len == 0)
    return gab_erecord(gab);

  gab_value total_keys[total_len];
  for (uint64_t i = 0; i < total_len; i++)
    total_keys[i] = gab_number(i);

  gab_gclock(gab);

  uint64_t shift = getshift(total_len);

  uint64_t rootlen = getlen(total_len, shift);

  struct gab_orec *self =
      GAB_CREATE_FLEX_OBJ(gab_orec, gab_value, rootlen, kGAB_RECORD);

  self->shape = gab_shape(gab, 1, total_len, total_keys, nullptr);
  self->shift = shift;
  self->len = rootlen;

  gab_value res = __gab_obj(self);

  if (total_len) {
    recfillchildren(gab, res, shift, total_len, rootlen);

    assert(total_len == gab_shplen(self->shape));

    for (uint64_t i = 0; i < total_len; i++)
      massoc(gab, res, nth_amongst(i, total_len, records), i);
  }

  return gab_gcunlock(gab), res;
}

gab_value gab_nreccat(struct gab_triple gab, uint64_t len, gab_value *records) {
  gab_gclock(gab);

  if (len == 0)
    return gab_recordof(gab);

  gab_value shapes[len];
  for (uint64_t i = 0; i < len; i++)
    shapes[i] = gab_recshp(records[i]);

  gab_value new_shp = gab_nshpcat(gab, len, shapes);

  uint64_t total_len = gab_shplen(new_shp);
  uint64_t shift = getshift(total_len);
  uint64_t rootlen = getlen(total_len, shift);

  struct gab_orec *self =
      GAB_CREATE_FLEX_OBJ(gab_orec, gab_value, rootlen, kGAB_RECORD);

  self->shape = new_shp;
  self->shift = shift;
  self->len = rootlen;

  gab_value res = __gab_obj(self);

  if (total_len) {
    recfillchildren(gab, res, shift, total_len, rootlen);
    assert(total_len == gab_shplen(self->shape));

    for (uint64_t i = 0; i < total_len; i++) {
      gab_value key = gab_ushpat(new_shp, i);
      for (uint64_t j = 0; j < len; j++) {
        gab_value rec = records[j];
        gab_value val = gab_recat(rec, key);
        if (val != gab_cundefined)
          massoc(gab, res, val, i);
      }
    }
  }

  return gab_gcunlock(gab), res;
}

gab_value gab_list(struct gab_triple gab, uint64_t size, gab_value *values) {
  gab_gclock(gab);

  if (!size)
    return gab_gcunlock(gab), gab_record(gab, 0, 0, nullptr, nullptr);

  gab_value keys[size];
  for (uint64_t i = 0; i < size; i++) {
    keys[i] = gab_number(i);
  }

  gab_value v = gab_record(gab, 1, size, keys, values);
  return gab_gcunlock(gab), v;
}

gab_value gab_shape(struct gab_triple gab, uint64_t stride, uint64_t len,
                    gab_value *keys, uint64_t *km_out) {
  gab_value shp = gab.eg->shapes;

  gab_gclock(gab);

  for (uint64_t i = 0; i < len; i++) {
    gab_value new_shp = gab_shpwith(gab, shp, keys[i * stride]);

    if (km_out && new_shp == shp) {
      uint64_t km_idx = i / 64;
      uint64_t in_idx = i % 64;
      km_out[km_idx] |= ((uint64_t)1 << in_idx);
    }

    shp = new_shp;
  }

  gab_gcunlock(gab);

  /*assert(len == gab_shplen(shp));*/
  return shp;
}

uint64_t gab_shplen(gab_value shp) {
  assert(gab_valkind(shp) == kGAB_SHAPE || gab_valkind(shp) == kGAB_SHAPELIST);
  struct gab_oshape *s = GAB_VAL_TO_SHAPE(shp);
  return s->len;
}

gab_value *gab_shpdata(gab_value shp) {
  assert(gab_valkind(shp) == kGAB_SHAPE || gab_valkind(shp) == kGAB_SHAPELIST);
  struct gab_oshape *s = GAB_VAL_TO_SHAPE(shp);
  return s->keys;
};

uint64_t gab_shptfind(gab_value shp, gab_value key) {
  assert(gab_valkind(shp) == kGAB_SHAPE || gab_valkind(shp) == kGAB_SHAPELIST);
  struct gab_oshape *s = GAB_VAL_TO_SHAPE(shp);

  uint64_t len = s->transitions.len / 2;

  for (uint64_t i = 0; i < len; i++) {
    if (gab_valeq(key, v_gab_value_val_at(&s->transitions, i * 2)))
      return i;
  }

  return -1;
};

gab_value gab_nshpcat(struct gab_triple gab, uint64_t len,
                      gab_value shapes[static len]) {
  assert(len > 0);
  gab_value shp = shapes[0];

  for (uint64_t i = 1; i < len; i++)
    for (uint64_t k = 0; k < gab_shplen(shapes[i]); k++)
      shp = gab_shpwith(gab, shp, gab_ushpat(shapes[i], k));

  return shp;
}

gab_value __gab_shape(struct gab_triple gab, uint64_t len) {
  struct gab_oshape *self =
      GAB_CREATE_FLEX_OBJ(gab_oshape, gab_value, len, kGAB_SHAPELIST);

  self->len = len;

  v_gab_value_create(&self->transitions, 16);

  return gab_iref(gab, __gab_obj(self));
}

/*
 * This needs to mimic the swap-and-pop that records do to actually pop values
 */
gab_value gab_shpwithout(struct gab_triple gab, gab_value shape,
                         gab_value key) {
  gab_value shp = gab.eg->shapes;

  assert(gab_shpfind(shape, key) != UINT64_MAX);

  gab_gclock(gab);

  uint64_t len = gab_shplen(shape);

  gab_value last_key = gab_ushpat(shape, gab_shplen(shape) - 1);

  // Iterate through n - 1 keys
  for (uint64_t i = 0; i < len - 1; i++) {
    gab_value thiskey = gab_ushpat(shape, i);

    if (key == thiskey) // This performs the swap
      shp = gab_shpwith(gab, shp, last_key);
    else
      shp = gab_shpwith(gab, shp, thiskey);
  }

  assert(len - 1 == gab_shplen(shp));
  return gab_gcunlock(gab), shp;
}

gab_value gab_shpwith(struct gab_triple gab, gab_value shp, gab_value key) {
  mtx_lock(&gab.eg->shapes_mtx);

  assert(gab_valkind(shp) == kGAB_SHAPE || gab_valkind(shp) == kGAB_SHAPELIST);
  struct gab_oshape *s = GAB_VAL_TO_SHAPE(shp);

  uint64_t idx = gab_shpfind(shp, key);
  if (idx != -1) {
    mtx_unlock(&gab.eg->shapes_mtx);
    return shp;
  }

  idx = gab_shptfind(shp, key);
  if (idx != -1) {
    mtx_unlock(&gab.eg->shapes_mtx);
    return v_gab_value_val_at(&s->transitions, idx * 2 + 1);
  }

  gab_value new_shape = __gab_shape(gab, s->len + 1);
  struct gab_oshape *self = GAB_VAL_TO_SHAPE(new_shape);

  if (gab_valkind(shp) != kGAB_SHAPELIST || key != gab_number(s->len))
    self->header.kind = kGAB_SHAPE;

  // Set the keys on the new shape
  memcpy(self->keys, s->keys, sizeof(gab_value) * s->len);
  self->keys[s->len] = key;

  // Push transition into parent shape
  v_gab_value_push(&s->transitions, key);
  v_gab_value_push(&s->transitions, new_shape);

  mtx_unlock(&gab.eg->shapes_mtx);
  return new_shape;
}

gab_value gab_shpwithout(struct gab_triple gab, gab_value shp, gab_value key);

gab_value setup_fibersend(struct gab_triple gab, struct gab_ofiber *self) {
  struct gab_vm *vm = &self->vm;

  memcpy(self->virtual_frame_bc,
         &(uint8_t[]){
             OP_SEND,
             fHAVE_TAIL,
             0,
             OP_RETURN,
         },
         sizeof(self->virtual_frame_bc));

  memcpy(self->virtual_frame_ks,
         &(gab_value[]){
             self->data[0],
             gab_cundefined,
             gab_cundefined,
             gab_cundefined,
             gab_cundefined,
             gab_cundefined,
             gab_cundefined,
         },
         sizeof(self->virtual_frame_ks));

  vm->ip = self->virtual_frame_bc;
  vm->kb = self->virtual_frame_ks;

  return __gab_obj(self);
}

gab_value gab_fiber(struct gab_triple gab, struct gab_fiber_argt args) {
  assert(gab_valkind(args.message) == kGAB_MESSAGE);

  struct gab_ofiber *self =
      GAB_CREATE_FLEX_OBJ(gab_ofiber, gab_value, args.argc + 2, kGAB_FIBER);

  self->len = args.argc + 2;

  if (args.argc) {
    assert(args.argv);
    memcpy(self->data + 2, args.argv, args.argc * sizeof(gab_value));
  }

  self->data[0] = args.message;
  self->data[1] = args.receiver;

  // self->vm.sb = self->vm.stk;
  self->vm.fp = self->vm.sb + 3;
  self->vm.sp = self->vm.sb + 3;
  self->flags = gab.flags | args.flags;

  // Setup main and args
  *self->vm.sp++ = args.receiver;
  for (uint8_t i = 0; i < args.argc; i++)
    *self->vm.sp++ = args.argv[i];

  *self->vm.sp = args.argc + 1;

  // Setup the return frame
  self->vm.fp[-1] = 0;
  self->vm.fp[-2] = 0;
  self->vm.fp[-3] = 0;

  self->vm.ip = nullptr;
  self->res_env = gab_cinvalid;

  return setup_fibersend(gab, self);
}

GAB_API inline struct gab_vm *gab_fibvm(gab_value fiber) {
  assert(gab_valkind(fiber) >= kGAB_FIBER &&
         gab_valkind(fiber) <= kGAB_FIBERRUNNING);
  return &GAB_VAL_TO_FIBER(fiber)->vm;
}

union gab_value_pair gab_fibawait(struct gab_triple gab, gab_value f) {
  assert(gab_valkind(f) >= kGAB_FIBER && gab_valkind(f) <= kGAB_FIBERRUNNING);

  struct gab_ofiber *fiber = GAB_VAL_TO_FIBER(f);

  assert(gab_valkind(f) == fiber->header.kind);
  while (fiber->header.kind != kGAB_FIBERDONE)
    switch (gab_yield(gab)) {
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    case sGAB_TERM:
      return gab_union_cinvalid;
    default:
      break;
    }

  return fiber->res_values;
}

gab_value gab_fibawaite(struct gab_triple gab, gab_value f) {
  assert(gab_valkind(f) >= kGAB_FIBER && gab_valkind(f) <= kGAB_FIBERRUNNING);

  struct gab_ofiber *fiber = GAB_VAL_TO_FIBER(f);

  while (fiber->header.kind != kGAB_FIBERDONE)
    switch (gab_yield(gab)) {
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    case sGAB_TERM:
      return gab_cinvalid;
    default:
      break;
    }

  return fiber->res_env;
}

gab_value gab_channel(struct gab_triple gab) {
  struct gab_ochannel *self = GAB_CREATE_OBJ(gab_ochannel, kGAB_CHANNEL);

  atomic_init(&self->data, nullptr);
  return __gab_obj(self);
}

void gab_chnclose(gab_value c) {
  assert(gab_valkind(c) >= kGAB_CHANNEL &&
         gab_valkind(c) <= kGAB_CHANNELCLOSED);

  struct gab_ochannel *channel = GAB_VAL_TO_CHANNEL(c);

  channel->header.kind = kGAB_CHANNELCLOSED;
}

bool gab_chnisclosed(gab_value c) {
  assert(gab_valkind(c) >= kGAB_CHANNEL &&
         gab_valkind(c) <= kGAB_CHANNELCLOSED);

  struct gab_ochannel *channel = GAB_VAL_TO_CHANNEL(c);

  return channel->header.kind == kGAB_CHANNELCLOSED;
};

bool gab_chnisempty(gab_value c) {
  assert(gab_valkind(c) >= kGAB_CHANNEL &&
         gab_valkind(c) <= kGAB_CHANNELCLOSED);

  struct gab_ochannel *channel = GAB_VAL_TO_CHANNEL(c);
  switch (channel->header.kind) {
  case kGAB_CHANNELCLOSED:
    return true;
  case kGAB_CHANNEL:
    return atomic_load(&channel->data) == nullptr;
  }
  assert(false && "unreachable");
  return false;
};

bool gab_chnisfull(gab_value c) {
  assert(gab_valkind(c) >= kGAB_CHANNEL &&
         gab_valkind(c) <= kGAB_CHANNELCLOSED);

  struct gab_ochannel *channel = GAB_VAL_TO_CHANNEL(c);
  switch (channel->header.kind) {
  case kGAB_CHANNELCLOSED:
    return false;
  case kGAB_CHANNEL:
    return atomic_load(&channel->data) != nullptr;
  }
  assert(false && "unreachable");
  return false;
};

/*
 * Try to put a slice into a channel. Uses weak atomic exchange, so
 *  must be used in loops.
 *
 * If the compare-exchange succeeds, also writes the len into the channel.
 */
bool channel_put(struct gab_ochannel *channel, uint64_t len, gab_value *vs) {
  static gab_value *null = nullptr;

  if (atomic_compare_exchange_weak(&channel->data, &null, vs))
    return channel->len = len, true;

  return false;
}

/*
 * Abandon a put by storing nullptr into data.
 */
void channel_abandon(struct gab_ochannel *channel) {
  atomic_store(&channel->data, nullptr);
}

/*
 * Try to load up to n values from the channel into dest.
 * If successful, return a gab_number of the number of values actually loaded.
 * Else return gab_cundefined.
 */
gab_value channel_take(struct gab_ochannel *channel, uint64_t n,
                       gab_value *dest) {
  // This load of the len scares me. However, we only ever write to channel->len
  // when we have succeeded with our atomic exchange against nullptr. So I think
  // it should be fine.
  gab_value *src = atomic_load(&channel->data);
  uint64_t avail = channel->len;

  if (!src)
    return gab_cundefined;

  // No space to complete this take.
  // if (n < avail)
  //   // return gab_cundefined;
  //   return gab_number(-avail);
  uint64_t len = n < avail ? n : avail;

  // Try the take - perform our copy.
  memcpy(dest, src, sizeof(gab_value) * len);

  // If this exchange fails:
  //  The data ptr in the channel no longer matches the src ptr
  //  that we just memcpy'd from. That means someone got to the data first
  //  (and either the channel is now empty, or a new src ptr is there).
  //  In either case, we should return 0 (failure) on a mismatch.
  if (atomic_compare_exchange_weak(&channel->data, &src, nullptr))
    return gab_number(len);
  else
    return gab_cundefined;
}

/* Identical to put, except the memcpy goes the other way. */
gab_value channel_block_while_full(struct gab_triple gab,
                                   struct gab_ochannel *channel, gab_value c,
                                   uint64_t timeout_ns, uint64_t *timer_ns) {
  while (gab_chnisfull(c)) {
    switch (gab_yield(gab)) {
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    case sGAB_TERM:
      return gab_cinvalid;
    default:
      break;
    }

    *timer_ns += GAB_YIELD_SLEEPTIME_NS;

    if (gab_chnisclosed(c))
      return gab_cundefined;

    if (*timer_ns > timeout_ns)
      return gab_ctimeout;
  }

  return gab_cvalid;
}

gab_value channel_block_while_empty(struct gab_triple gab,
                                    struct gab_ochannel *channel, gab_value c,
                                    uint64_t timeout_ns, uint64_t *timer_ns) {
  while (gab_chnisempty(c)) {
    switch (gab_yield(gab)) {
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    case sGAB_TERM:
      return gab_cinvalid;
    default:
      break;
    }

    *timer_ns += GAB_YIELD_SLEEPTIME_NS;

    if (gab_chnisclosed(c))
      return gab_cundefined;

    if (*timer_ns > timeout_ns)
      return gab_ctimeout;
  }

  return gab_cvalid;
}

/*
 * Returns
 * gab_ctimeout on timeout
 * gab_cundefined on close!
 * gab_cinvalid on terminate
 * gab_cvalid on success
 */
gab_value channel_blocking_put(struct gab_triple gab,
                               struct gab_ochannel *channel, gab_value c,
                               uint64_t len, gab_value *vs, size_t nms) {
  gab_value res = gab_cundefined;

  const uint64_t timeout_ns = nms * 1000000;
  uint64_t timer_ns = 0;

  while (!gab_chnisclosed(c)) {
    res = channel_block_while_full(gab, channel, c, timeout_ns, &timer_ns);

    if (res != gab_cvalid)
      return res;

    if (channel_put(channel, len, vs))
      break;
  }

  // If a taker never arrives, we should remove our value as if our put
  // failed and return a timeout.
  res = channel_block_while_full(gab, channel, c, timeout_ns, &timer_ns);

  switch (res) {
  // We were interrupted, timed out, or the channel closed.
  // Take the value we put in out, and return.
  case gab_ctimeout:
  case gab_cinvalid:
  case gab_cundefined:
    return channel_abandon(channel), res;
  // A taker arrived.
  default:
    return gab_cvalid;
  }
}

/*
 * Returns
 * gab_ctimeout on timeout
 * gab_cundefined on close!
 * gab_cinvalid on terminate
 * positive gab_number containing number of values written on success
 * negative gab_number containing number of values *would* have written, but
 * didn't have space. (failure).
 */
gab_value channel_blocking_take(struct gab_triple gab,
                                struct gab_ochannel *channel, gab_value c,
                                uint64_t len, gab_value *vs, size_t nms) {
  gab_value res = gab_cundefined;

  const uint64_t timeout_ns = nms * 1000000;
  uint64_t timer_ns = 0;

  while (!gab_chnisclosed(c) && res == gab_cundefined) {
    res = channel_block_while_empty(gab, channel, c, timeout_ns, &timer_ns);

    if (res != gab_cvalid)
      return res;

    res = channel_take(channel, len, vs);
  }

  return res;
}

/*
 * Returns
 * gab_ctimeout on timeout
 * gab_cundefined on close!
 * gab_cinvalid on terminate
 * gab_cvalid on success
 */
gab_value gab_ntchnput(struct gab_triple gab, gab_value c, uint64_t len,
                       gab_value *vs, uint64_t nms) {
  assert(gab_valkind(c) >= kGAB_CHANNEL &&
         gab_valkind(c) <= kGAB_CHANNELCLOSED);

  struct gab_ochannel *channel = GAB_VAL_TO_CHANNEL(c);

  switch (channel->header.kind) {
  case kGAB_CHANNEL:
    return channel_blocking_put(gab, channel, c, len, vs, nms);
  case kGAB_CHANNELCLOSED:
    return gab_cundefined;
  default:
    assert(false && "UNREACHABLE");
    return gab_cinvalid;
  }
}

gab_value gab_tchnput(struct gab_triple gab, gab_value c, gab_value value,
                      uint64_t nms) {
  return gab_ntchnput(gab, c, 1, &value, nms);
}

gab_value gab_nchnput(struct gab_triple gab, gab_value channel, uint64_t len,
                      gab_value *vs) {
  gab_value v = gab_ntchnput(gab, channel, len, vs, -1);
  assert(v != gab_ctimeout);
  return v;
}

gab_value gab_chnput(struct gab_triple gab, gab_value c, gab_value value) {
  gab_value v = gab_tchnput(gab, c, value, -1);
  assert(v != gab_ctimeout);
  return v;
}

/*
 * Returns
 * gab_ctimeout on timeout
 * gab_cundefined on close!
 * gab_cinvalid on terminate
 * gab_number corresponding to number of taken values on success, or negative
 * number if space wasn't available.
 */
gab_value gab_ntchntake(struct gab_triple gab, gab_value c, uint64_t len,
                        gab_value *data, uint64_t nms) {
  assert(gab_valkind(c) >= kGAB_CHANNEL &&
         gab_valkind(c) <= kGAB_CHANNELCLOSED);

  struct gab_ochannel *channel = GAB_VAL_TO_CHANNEL(c);

  switch (channel->header.kind) {
  case kGAB_CHANNEL:
    return channel_blocking_take(gab, channel, c, len, data, nms);
  case kGAB_CHANNELCLOSED:
    return gab_cundefined;
  default:
    assert(false && "Unreachable");
    return gab_cinvalid;
  }
};

gab_value gab_tchntake(struct gab_triple gab, gab_value channel, uint64_t nms) {
  gab_value out;
  gab_value res = gab_ntchntake(gab, channel, 1, &out, nms);

  if (gab_valkind(res) != kGAB_NUMBER)
    return res;

  gab_int n = gab_valtoi(res);
  if (n < 0)
    return gab_cundefined;

  // We should have one written value.
  assert(n == 1);
  return out;
};

gab_value gab_nchntake(struct gab_triple gab, gab_value channel, uint64_t len,
                       gab_value *data) {
  return gab_ntchntake(gab, channel, len, data, -1);
}

gab_value gab_chntake(struct gab_triple gab, gab_value c) {
  gab_value v = gab_tchntake(gab, c, -1);
  assert(v != gab_ctimeout);
  return v;
}

static uint64_t dumpInstruction(FILE *stream, struct gab_oprototype *self,
                                uint64_t offset);

static uint64_t dumpSimpleInstruction(FILE *stream, struct gab_oprototype *self,
                                      uint64_t offset) {
  const char *name =
      gab_opcode_names[v_uint8_t_val_at(&self->src->bytecode, offset)];
  fprintf(stream, "%-25s\n", name);
  return offset + 1;
}

static uint64_t dumpSendInstruction(FILE *stream, struct gab_oprototype *self,
                                    uint64_t offset) {
  const char *name =
      gab_opcode_names[v_uint8_t_val_at(&self->src->bytecode, offset)];

  uint16_t constant =
      ((uint16_t)v_uint8_t_val_at(&self->src->bytecode, offset + 1)) << 8 |
      v_uint8_t_val_at(&self->src->bytecode, offset + 2);

  gab_value msg = v_gab_value_val_at(&self->src->constants,
                                     constant & (~(fHAVE_TAIL << 8)));

  bool tail = ((constant & (fHAVE_TAIL << 8)) != 0);

  fprintf(stream, "%-25s" GAB_BLUE, name);
  gab_fvalinspect(stream, msg, 0);
  fprintf(stream, GAB_RESET " %s\n", tail ? " [TAILCALL]" : "");

  return offset + 3;
}

static uint64_t dumpByteInstruction(FILE *stream, struct gab_oprototype *self,
                                    uint64_t offset) {
  uint8_t operand = v_uint8_t_val_at(&self->src->bytecode, offset + 1);
  const char *name =
      gab_opcode_names[v_uint8_t_val_at(&self->src->bytecode, offset)];
  fprintf(stream, "%-25s%hhx\n", name, operand);
  return offset + 2;
}

static uint64_t dumpTrimInstruction(FILE *stream, struct gab_oprototype *self,
                                    uint64_t offset) {
  uint8_t wantbyte = v_uint8_t_val_at(&self->src->bytecode, offset + 1);
  const char *name =
      gab_opcode_names[v_uint8_t_val_at(&self->src->bytecode, offset)];
  fprintf(stream, "%-25s%hhx\n", name, wantbyte);
  return offset + 2;
}

static uint64_t dumpReturnInstruction(FILE *stream, struct gab_oprototype *self,
                                      uint64_t offset) {
  fprintf(stream, "%-25s\n", "RETURN");
  return offset + 1;
}

static uint64_t dumpPackInstruction(FILE *stream, struct gab_oprototype *self,
                                    uint64_t offset) {
  uint8_t operandA = v_uint8_t_val_at(&self->src->bytecode, offset + 1);
  uint8_t operandB = v_uint8_t_val_at(&self->src->bytecode, offset + 2);
  const char *name =
      gab_opcode_names[v_uint8_t_val_at(&self->src->bytecode, offset)];
  fprintf(stream, "%-25s -> %hhx %hhx\n", name, operandA, operandB);
  return offset + 3;
}

static uint64_t dumpConstantInstruction(FILE *stream,
                                        struct gab_oprototype *self,
                                        uint64_t offset) {
  uint16_t constant =
      ((uint16_t)v_uint8_t_val_at(&self->src->bytecode, offset + 1)) << 8 |
      v_uint8_t_val_at(&self->src->bytecode, offset + 2);
  const char *name =
      gab_opcode_names[v_uint8_t_val_at(&self->src->bytecode, offset)];
  fprintf(stream, "%-25s", name);
  gab_fvalinspect(stdout, v_gab_value_val_at(&self->src->constants, constant),
                  0);
  fprintf(stream, "\n");
  return offset + 3;
}

static uint64_t dumpNConstantInstruction(FILE *stream,
                                         struct gab_oprototype *self,
                                         uint64_t offset) {
  const char *name =
      gab_opcode_names[v_uint8_t_val_at(&self->src->bytecode, offset)];
  fprintf(stream, "%-25s", name);

  uint8_t n = v_uint8_t_val_at(&self->src->bytecode, offset + 1);

  for (int i = 0; i < n; i++) {
    uint16_t constant =
        ((uint16_t)v_uint8_t_val_at(&self->src->bytecode, offset + 2 + (2 * i)))
            << 8 |
        v_uint8_t_val_at(&self->src->bytecode, offset + 3 + (2 * i));

    gab_fvalinspect(stdout, v_gab_value_val_at(&self->src->constants, constant),
                    0);

    if (i < n - 1)
      fprintf(stream, ", ");
  }

  fprintf(stream, "\n");
  return offset + 2 + (2 * n);
}

static uint64_t dumpInstruction(FILE *stream, struct gab_oprototype *self,
                                uint64_t offset) {
  uint8_t op = v_uint8_t_val_at(&self->src->bytecode, offset);
  switch (op) {
  case OP_POP:
  case OP_TUPLE:
  case OP_CONS:
  case OP_NOP:
    return dumpSimpleInstruction(stream, self, offset);
  case OP_PACK_RECORD:
  case OP_PACK_LIST:
    return dumpPackInstruction(stream, self, offset);
  case OP_NCONSTANT:
    return dumpNConstantInstruction(stream, self, offset);
  case OP_CONSTANT:
    return dumpConstantInstruction(stream, self, offset);
  case OP_SEND:
  case OP_SEND_BLOCK:
  case OP_SEND_NATIVE:
  case OP_SEND_PROPERTY:
  case OP_SEND_PRIMITIVE_CONCAT:
  case OP_SEND_PRIMITIVE_SPLATLIST:
  case OP_SEND_PRIMITIVE_SPLATDICT:
  case OP_SEND_PRIMITIVE_ADD:
  case OP_SEND_PRIMITIVE_SUB:
  case OP_SEND_PRIMITIVE_MUL:
  case OP_SEND_PRIMITIVE_DIV:
  case OP_SEND_PRIMITIVE_MOD:
  case OP_SEND_PRIMITIVE_EQ:
  case OP_SEND_PRIMITIVE_LT:
  case OP_SEND_PRIMITIVE_LTE:
  case OP_SEND_PRIMITIVE_GT:
  case OP_SEND_PRIMITIVE_GTE:
  case OP_SEND_PRIMITIVE_CALL_BLOCK:
  case OP_SEND_PRIMITIVE_CALL_NATIVE:
  case OP_SEND_PRIMITIVE_CALL_MESSAGE:
  case OP_SEND_PRIMITIVE_CALL_MESSAGE_PRIMITIVE:
  case OP_SEND_PRIMITIVE_CALL_MESSAGE_NATIVE:
  case OP_SEND_PRIMITIVE_CALL_MESSAGE_CONSTANT:
  case OP_SEND_PRIMITIVE_CALL_MESSAGE_BLOCK:
  case OP_TAILSEND_PRIMITIVE_CALL_MESSAGE_BLOCK:
  case OP_SEND_PRIMITIVE_CALL_MESSAGE_PROPERTY:
  case OP_TAILSEND_BLOCK:
  case OP_TAILSEND_PRIMITIVE_CALL_BLOCK:
  case OP_LOCALSEND_BLOCK:
  case OP_LOCALTAILSEND_BLOCK:
  case OP_MATCHSEND_BLOCK:
  case OP_MATCHTAILSEND_BLOCK:
    return dumpSendInstruction(stream, self, offset);
  case OP_POP_N:
  case OP_STORE_LOCAL:
  case OP_POPSTORE_LOCAL:
  case OP_LOAD_UPVALUE:
  case OP_LOAD_LOCAL:
    return dumpByteInstruction(stream, self, offset);
  case OP_NPOPSTORE_STORE_LOCAL:
  case OP_NPOPSTORE_LOCAL:
  case OP_NLOAD_UPVALUE:
  case OP_NLOAD_LOCAL: {
    const char *name =
        gab_opcode_names[v_uint8_t_val_at(&self->src->bytecode, offset)];

    uint8_t operand = v_uint8_t_val_at(&self->src->bytecode, offset + 1);

    fprintf(stream, "%-25s%hhx: ", name, operand);

    for (int i = 0; i < operand - 1; i++) {
      fprintf(stream, "%hhx, ",
              v_uint8_t_val_at(&self->src->bytecode, offset + 2 + i));
    }

    fprintf(stream, "%hhx\n",
            v_uint8_t_val_at(&self->src->bytecode, offset + 1 + operand));

    return offset + 2 + operand;
  }
  case OP_RETURN:
    return dumpReturnInstruction(stream, self, offset);
  case OP_BLOCK: {
    offset++;

    uint16_t proto_constant =
        (((uint16_t)self->src->bytecode.data[offset] << 8) |
         self->src->bytecode.data[offset + 1]);

    offset += 2;

    gab_value pval = v_gab_value_val_at(&self->src->constants, proto_constant);

    struct gab_oprototype *p = GAB_VAL_TO_PROTOTYPE(pval);

    printf("%-25s" GAB_CYAN "%-20s\n" GAB_RESET, "OP_BLOCK",
           gab_strdata(&p->src->name));

    for (int j = 0; j < p->nupvalues; j++) {
      int isLocal = p->data[j] & fLOCAL_LOCAL;
      uint8_t index = p->data[j] >> 1;
      printf("      |                   %d %s\n", index,
             isLocal ? "local" : "upvalue");
    }
    return offset;
  }
  case OP_TRIM_UP1:
  case OP_TRIM_UP2:
  case OP_TRIM_UP3:
  case OP_TRIM_UP4:
  case OP_TRIM_UP5:
  case OP_TRIM_UP6:
  case OP_TRIM_UP7:
  case OP_TRIM_UP8:
  case OP_TRIM_UP9:
  case OP_TRIM_DOWN1:
  case OP_TRIM_DOWN2:
  case OP_TRIM_DOWN3:
  case OP_TRIM_DOWN4:
  case OP_TRIM_DOWN5:
  case OP_TRIM_DOWN6:
  case OP_TRIM_DOWN7:
  case OP_TRIM_DOWN8:
  case OP_TRIM_DOWN9:
  case OP_TRIM_EXACTLY0:
  case OP_TRIM_EXACTLY1:
  case OP_TRIM_EXACTLY2:
  case OP_TRIM_EXACTLY3:
  case OP_TRIM_EXACTLY4:
  case OP_TRIM_EXACTLY5:
  case OP_TRIM_EXACTLY6:
  case OP_TRIM_EXACTLY7:
  case OP_TRIM_EXACTLY8:
  case OP_TRIM_EXACTLY9:
  case OP_TRIM: {
    return dumpTrimInstruction(stream, self, offset);
  }
  default: {
    uint8_t code = v_uint8_t_val_at(&self->src->bytecode, offset);
    printf("Unknown opcode %d (%s?)\n", code, gab_opcode_names[code]);
    return offset + 1;
  }
  }
}

int gab_fmodinspect(FILE *stream, gab_value module) {
  struct gab_oprototype *proto = nullptr;

  switch (gab_valkind(module)) {
  case kGAB_BLOCK:
    proto = GAB_VAL_TO_PROTOTYPE(GAB_VAL_TO_BLOCK(module)->p);
    break;
  case kGAB_PROTOTYPE:
    proto = GAB_VAL_TO_PROTOTYPE(module);
    break;
  default:
    return -1;
  }

  uint64_t offset = proto->offset;

  uint64_t end = proto->offset + proto->len;

  printf("     ");
  gab_fvalinspect(stream, proto->src->name, 0);
  printf("\n");

  while (offset < end) {
    fprintf(stream, GAB_YELLOW "%04" PRIu64 " " GAB_RESET, offset);
    offset = dumpInstruction(stream, proto, offset);
  }

  return 0;
}

#undef CREATE_GAB_FLEX_OBJ
#undef CREATE_GAB_OBJ

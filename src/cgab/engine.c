#include <stdint.h>
#define GAB_STATUS_NAMES_IMPL
#define GAB_TOKEN_NAMES_IMPL
#include "engine.h"

#include "colors.h"

#include "core.h"
#include "gab.h"
#include "lexer.h"

a_char *gab_fosread(FILE *fd) {
  v_char buffer = {0};

  while (1) {
    int c = fgetc(fd);

    if (c == EOF)
      break;

    v_char_push(&buffer, c);
  }

  v_char_push(&buffer, '\0');

  a_char *data = a_char_create(buffer.data, buffer.len);

  v_char_destroy(&buffer);

  return data;
}

a_char *gab_osread(const char *path) {
  FILE *file = fopen(path, "rb");

  if (file == nullptr)
    return nullptr;

  a_char *data = gab_fosread(file);

  fclose(file);
  return data;
}

a_char *gab_fosreadl(FILE *fd) {
  v_char buffer;
  v_char_create(&buffer, 1024);

  for (;;) {
    int c = fgetc(fd);

    v_char_push(&buffer, c);

    if (c == '\n' || c == EOF)
      break;
  }

  v_char_push(&buffer, '\0');

  a_char *data = a_char_create(buffer.data, buffer.len);

  v_char_destroy(&buffer);

  return data;
}

struct primitive {
  const char *name;
  union {
    gab_value val;
    enum gab_kind kind;
    const char *message;
  };
  gab_value primitive;
};

struct primitive all_primitives[] = {
    {
        .name = mGAB_TYPE,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_TYPE),
    },
};

struct primitive val_primitives[] = {
    {
        .name = mGAB_EQ,
        .val = gab_invalid,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_EQ),
    },
    {
        .name = mGAB_CONS,
        .val = gab_invalid,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_CONS),
    },
};

struct primitive msg_primitives[] = {
    {
        .name = mGAB_MAKE,
        .message = tGAB_LIST,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_LIST),
    },
    {
        .name = mGAB_MAKE,
        .message = tGAB_FIBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_FIBER),
    },
    {
        .name = mGAB_MAKE,
        .message = tGAB_RECORD,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_RECORD),
    },
    {
        .name = mGAB_MAKE,
        .message = tGAB_SHAPE,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_SHAPE),
    },
    {
        .name = mGAB_MAKE,
        .message = tGAB_CHANNEL,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_CHANNEL),
    },
    {
        .name = mGAB_BND,
        .message = "false",
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_LND),
    },
    {
        .name = mGAB_BOR,
        .message = "false",
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_LOR),
    },
    {
        .name = mGAB_LIN,
        .message = "false",
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_LIN),
    },
    {
        .name = mGAB_BND,
        .message = "true",
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_LND),
    },
    {
        .name = mGAB_BOR,
        .message = "true",
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_LOR),
    },
    {
        .name = mGAB_LIN,
        .message = "true",
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_LIN),
    },
};

struct primitive kind_primitives[] = {
    {
        .name = mGAB_BIN,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_BIN),
    },
    {
        .name = mGAB_BIN,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_BIN),
    },
    {
        .name = mGAB_BOR,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_BOR),
    },
    {
        .name = mGAB_BND,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_BND),
    },
    {
        .name = mGAB_LSH,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_LSH),
    },
    {
        .name = mGAB_RSH,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_RSH),
    },
    {
        .name = mGAB_ADD,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_ADD),
    },
    {
        .name = mGAB_SUB,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_SUB),
    },
    {
        .name = mGAB_MUL,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_MUL),
    },
    {
        .name = mGAB_DIV,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_DIV),
    },
    {
        .name = mGAB_MOD,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_MOD),
    },
    {
        .name = mGAB_LT,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_LT),
    },
    {
        .name = mGAB_LTE,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_LTE),
    },
    {
        .name = mGAB_GT,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_GT),
    },
    {
        .name = mGAB_GTE,
        .kind = kGAB_NUMBER,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_GTE),
    },
    {
        .name = mGAB_ADD,
        .kind = kGAB_STRING,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_CONCAT),
    },
    {
        .name = mGAB_MAKE,
        .kind = kGAB_SHAPE,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_MAKE_SHAPE),
    },
    {
        .name = mGAB_SPLAT,
        .kind = kGAB_RECORD,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_SPLAT),
    },
    {
        .name = mGAB_SPLATKEYS,
        .kind = kGAB_RECORD,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_SPLATKEYS),
    },
    {
        .name = mGAB_CONS,
        .kind = kGAB_RECORD,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_CONS_RECORD),
    },
    {
        .name = mGAB_USE,
        .kind = kGAB_STRING,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_USE),
    },
    {
        .name = mGAB_CALL,
        .kind = kGAB_NATIVE,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_CALL_NATIVE),
    },
    {
        .name = mGAB_CALL,
        .kind = kGAB_BLOCK,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_CALL_BLOCK),
    },
    {
        .name = mGAB_CALL,
        .kind = kGAB_MESSAGE,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_CALL_MESSAGE),
    },
    {
        .name = mGAB_PUT,
        .kind = kGAB_CHANNEL,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_PUT),
    },
    {
        .name = mGAB_TAKE,
        .kind = kGAB_CHANNEL,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_TAKE),
    },
};

struct native {
  const char *name;
  union {
    enum gab_kind kind;
    const char *message;
    const char *box_type;
  };
  gab_native_f native;
};

static const struct timespec t = {.tv_nsec = GAB_YIELD_SLEEPTIME_NS};

enum gab_signal gab_yield(struct gab_triple gab) {
  if (gab_sigwaiting(gab)) {
#if cGAB_LOG_EG
    printf("[WORKER %i] RECV SIG: %i\n", gab.wkid, gab.eg->sig.signal);
#endif
    return gab.eg->sig.signal;
  }

  // Previously, this would perform a thrd_yield() as well as a sleep.
  // This was causing *a lot* of context switching and was not a good idea.
  // As far as I know
  thrd_sleep(&t, nullptr);
  /*thrd_yield();*/
  return sGAB_IGN;
}

int32_t gc_job(void *data) {
  struct gab_triple *g = data;
  struct gab_triple gab = *g;
  assert(gab.wkid == 0);

  while (gab.eg->njobs >= 0) {
    switch (gab_yield(gab)) {
    case sGAB_TERM:
      gab_sigclear(gab);
      continue;
    case sGAB_COLL:
      gab_gcdocollect(gab);
      gab_sigclear(gab);
      continue;
    default:
      break;
    }

    /*
     * Coordinate work stealing here, where we are guaranteed to *not*
     * be collecting
     *
     * if we have spare jobs:
     *  look for the first worker
     */
  }

  free(g);
  return 0;
}

int32_t worker_job(void *data) {
  struct gab_triple *g = data;
  struct gab_triple gab = *g;

  assert(gab.wkid != 0);
  gab.eg->njobs++;

  struct gab_jb *self = gab.eg->jobs + gab.wkid;

#if cGAB_LOG_EG
  fprintf(stdout, "[WORKER %i] SPAWNED\n", gab.wkid);
#endif

  while (!gab_chnisclosed(gab.eg->work_channel) ||
         !gab_chnisempty(gab.eg->work_channel) ||
         !q_gab_value_is_empty(&self->queue)) {
    switch (gab_yield(gab)) {
    case sGAB_TERM:
      // Clear the work queue - we're terminated
      q_gab_value_create(&self->queue);
      goto fin;
    default:
      break;
    }

#if cGAB_LOG_EG
    gab_fprintf(stdout, "[WORKER $] TAKING WITH TIMEOUT $ms\n",
                gab_number(gab.wkid), gab_number(cGAB_WORKER_IDLEWAIT_MS));
#endif

#if cGAB_LOG_EG
    gab_fprintf(stdout, "[WORKER $] chntake succeeded: $\n",
                gab_number(gab.wkid), fiber);
#endif
    if (q_gab_value_is_empty(&self->queue)) {
      gab_value fiber =
          gab_tchntake(gab, gab.eg->work_channel, cGAB_WORKER_IDLEWAIT_MS);

      if (fiber == gab_invalid || fiber == gab_timeout)
        goto fin;

      if (!q_gab_value_push(&self->queue, fiber))
        assert(false && "PUSH FAILED");
    }

    gab_value fiber = q_gab_value_peek(&self->queue);

    assert(gab_valkind(fiber) != kGAB_FIBERDONE);

    a_gab_value *res = gab_vmexec(gab, fiber);

    q_gab_value_pop(&self->queue);

    if (res == nullptr)
      if (!q_gab_value_push(&self->queue, fiber))
        assert(false && "PUSH FAILED");
  }

fin:
  assert(q_gab_value_is_empty(&self->queue));

#if cGAB_LOG_EG
  fprintf(stdout, "[WORKER %i] CLOSING\n", gab.wkid);
#endif

  gab.eg->jobs[gab.wkid].alive = false;

  switch (gab_yield(gab)) {
  case sGAB_TERM:
    gab_sigpropagate(gab);
    goto fin;
  default:
    break;
  }

  gab.eg->njobs--;

  assert(gab.eg->jobs[gab.wkid].locked == 0);

  free(g);

  return 0;
}

struct gab_jb *next_available_job(struct gab_triple gab) {

  // Try to reuse an existing job, thats exited after idling
  for (uint64_t i = 1; i < gab.eg->len; i++) {
    // If we have a dead thread, revive it
    if (!gab.eg->jobs[i].alive)
      return gab.eg->jobs + i;
  }

  // No room for new jobs
  return nullptr;
}

bool gab_jbcreate(struct gab_triple gab, struct gab_jb *job, int(fn)(void *)) {
  if (!job)
    return false;

#if cGAB_LOG_EG
  fprintf(stdout, "[WORKER %i] spawning %lu\n", gab.wkid, job - gab.eg->jobs);
#endif

  job->locked = 0;
  job->alive = true;
  v_gab_value_create(&job->lock_keep, 8);
  q_gab_value_create(&job->queue);

  struct gab_triple *gabcpy = malloc(sizeof(struct gab_triple));
  memcpy(gabcpy, &gab, sizeof(struct gab_triple));
  gabcpy->wkid = job - gab.eg->jobs;

  return thrd_create(&job->td, fn, gabcpy) == thrd_success;
}

bool gab_wkspawn(struct gab_triple gab) {
  return gab_jbcreate(gab, next_available_job(gab), worker_job);
}

struct gab_triple gab_create(struct gab_create_argt args) {
  uint64_t njobs = args.jobs ? args.jobs : 8;

  uint64_t egsize = sizeof(struct gab_eg) + sizeof(struct gab_jb) * (njobs + 1);

  args.sin = args.sin != nullptr ? args.sin : stdin;
  args.sout = args.sout != nullptr ? args.sout : stdout;
  args.serr = args.serr != nullptr ? args.serr : stderr;

  struct gab_eg *eg = malloc(egsize);
  memset(eg, 0, egsize);

  eg->len = njobs + 1;
  eg->njobs = 0;
  eg->os_dynmod = args.os_dynmod;
  eg->hash_seed = time(nullptr);
  eg->sin = args.sin;
  eg->sout = args.sout;
  eg->serr = args.serr;
  eg->sig.schedule = -1;

  // The only non-zero initialization that jobs need is epoch = 1
  for (uint64_t i = 0; i < eg->len; i++)
    eg->jobs[i].epoch = 1;

  assert(eg->sin);
  assert(eg->sout);
  assert(eg->serr);

  mtx_init(&eg->shapes_mtx, mtx_plain);
  mtx_init(&eg->sources_mtx, mtx_plain);
  mtx_init(&eg->strings_mtx, mtx_plain);
  mtx_init(&eg->modules_mtx, mtx_plain);

  d_gab_src_create(&eg->sources, 8);

  struct gab_triple gab = {.eg = eg, .flags = args.flags};

  uint64_t gcsize =
      sizeof(struct gab_gc) +
      sizeof(struct gab_gcbuf[kGAB_NBUF][GAB_GCNEPOCHS]) * (eg->len);

  eg->gc = malloc(gcsize);
  gab_gccreate(gab);

  gab_jbcreate(gab, gab.eg->jobs, gc_job);

  gab_gclock(gab);

  eg->types[kGAB_NUMBER] = gab_string(gab, tGAB_NUMBER);
  eg->types[kGAB_BINARY] = gab_string(gab, tGAB_BINARY);
  eg->types[kGAB_STRING] = gab_string(gab, tGAB_STRING);
  eg->types[kGAB_MESSAGE] = gab_string(gab, tGAB_MESSAGE);
  eg->types[kGAB_PROTOTYPE] = gab_string(gab, tGAB_PROTOTYPE);
  eg->types[kGAB_NATIVE] = gab_string(gab, tGAB_NATIVE);
  eg->types[kGAB_BLOCK] = gab_string(gab, tGAB_BLOCK);
  eg->types[kGAB_SHAPE] = gab_string(gab, tGAB_SHAPE);
  eg->types[kGAB_SHAPELIST] = gab_string(gab, tGAB_SHAPE);
  eg->types[kGAB_RECORD] = gab_string(gab, tGAB_RECORD);
  eg->types[kGAB_RECORDNODE] = gab_string(gab, tGAB_RECORD);
  eg->types[kGAB_BOX] = gab_string(gab, tGAB_BOX);
  eg->types[kGAB_FIBER] = gab_string(gab, tGAB_FIBER);
  eg->types[kGAB_FIBERDONE] = gab_string(gab, tGAB_FIBER);
  eg->types[kGAB_FIBERRUNNING] = gab_string(gab, tGAB_FIBER);
  eg->types[kGAB_CHANNEL] = gab_string(gab, tGAB_CHANNEL);
  eg->types[kGAB_CHANNELCLOSED] = gab_string(gab, tGAB_CHANNEL);
  eg->types[kGAB_PRIMITIVE] = gab_string(gab, tGAB_PRIMITIVE);

  gab_niref(gab, 1, kGAB_NKINDS, eg->types);
  gab_negkeep(gab.eg, kGAB_NKINDS, eg->types);

  eg->shapes = __gab_shape(gab, 0);
  eg->messages = gab_erecord(gab);

  eg->work_channel = gab_channel(gab);
  gab_iref(gab, eg->work_channel);

  for (int i = 0; i < LEN_CARRAY(kind_primitives); i++) {
    gab_egkeep(
        gab.eg,
        gab_iref(gab,
                 gab_def(gab, (struct gab_def_argt){
                                  gab_message(gab, kind_primitives[i].name),
                                  gab_type(gab, kind_primitives[i].kind),
                                  kind_primitives[i].primitive,
                              })));
  }

  for (int i = 0; i < LEN_CARRAY(val_primitives); i++) {
    gab_egkeep(
        gab.eg,
        gab_iref(gab, gab_def(gab, (struct gab_def_argt){
                                       gab_message(gab, val_primitives[i].name),
                                       val_primitives[i].val,
                                       val_primitives[i].primitive,
                                   })));
  }

  for (int i = 0; i < LEN_CARRAY(msg_primitives); i++) {
    gab_egkeep(
        gab.eg,
        gab_iref(gab,
                 gab_def(gab, (struct gab_def_argt){
                                  gab_message(gab, msg_primitives[i].name),
                                  gab_message(gab, msg_primitives[i].message),
                                  msg_primitives[i].primitive,
                              })));
  }

  for (int i = 0; i < LEN_CARRAY(all_primitives); i++) {
    for (int t = 0; t < kGAB_NKINDS; t++) {
      gab_egkeep(
          gab.eg,
          gab_iref(gab,
                   gab_def(gab, (struct gab_def_argt){
                                    gab_message(gab, all_primitives[i].name),
                                    gab_type(gab, t),
                                    all_primitives[i].primitive,
                                })));
    }
  }

  if (!(gab.flags & fGAB_ENV_EMPTY)) {
    if (gab_suse(gab, "core") == nullptr)
      printf("[Error]: Failed to find core library\n");
  }

  return gab_gcunlock(gab), gab;
}

void dec_child_shapes(struct gab_triple gab, gab_value shp) {
  assert(gab_valkind(shp) == kGAB_SHAPE || gab_valkind(shp) == kGAB_SHAPELIST);
  struct gab_obj_shape *shape = GAB_VAL_TO_SHAPE(shp);

  uint64_t len = shape->transitions.len / 2;

  for (size_t i = 0; i < len; i++)
    dec_child_shapes(gab, v_gab_value_val_at(&shape->transitions, i * 2 + 1));

  gab_dref(gab, shp);
}

void gab_destroy(struct gab_triple gab) {
  // Wait until there is no work to be done
  while (!gab_chnisempty(gab.eg->work_channel))
    ;

  gab_chnclose(gab.eg->work_channel);

  while (gab.eg->njobs > 0)
    continue;

  gab_dref(gab, gab.eg->work_channel);
  gab_ndref(gab, 1, gab.eg->scratch.len, gab.eg->scratch.data);

  for (uint64_t i = 0; i < gab.eg->strings.cap; i++)
    if (d_strings_iexists(&gab.eg->strings, i))
      gab_dref(gab, __gab_obj(d_strings_ikey(&gab.eg->strings, i)));

  if (gab_valkind(gab.eg->shapes) == kGAB_SHAPELIST)
    dec_child_shapes(gab, gab.eg->shapes);

  gab.eg->messages = gab_invalid;
  gab.eg->shapes = gab_invalid;

  assert(gab.eg->njobs == 0);

  /**
   * Three consececutive collections are needed here because
   * of the delayed nature of the RC algorithm.
   *
   * Decrements are process an epoch *after* they are queued.
   *
   * There are three epochs tracked, so we need three collections
   * to ensure that all rc events are processed.
   */
  gab_sigcoll(gab);

  gab_sigcoll(gab);

  gab_sigcoll(gab);

  gab_sigcoll(gab);

  gab_gcassertdone(gab);

  /*assert(gab.eg->bytes_allocated == 0);*/
  assert(gab.eg->njobs == 0);
  gab.eg->njobs = -1;

  thrd_join(gab.eg->jobs[0].td, nullptr);
  gab_gcdestroy(gab);
  free(gab.eg->gc);

  for (uint64_t i = 0; i < gab.eg->sources.cap; i++) {
    if (d_gab_src_iexists(&gab.eg->sources, i)) {
      struct gab_src *s = d_gab_src_ival(&gab.eg->sources, i);
      gab_srcdestroy(s);
    }
  }

  for (int i = 0; i < gab.eg->len; i++) {
    struct gab_jb *wk = &gab.eg->jobs[i];
    v_gab_value_destroy(&wk->lock_keep);
  }

  d_strings_destroy(&gab.eg->strings);
  d_gab_modules_destroy(&gab.eg->modules);
  d_gab_src_destroy(&gab.eg->sources);

  v_gab_value_destroy(&gab.eg->scratch);

  mtx_destroy(&gab.eg->shapes_mtx);
  mtx_destroy(&gab.eg->strings_mtx);
  mtx_destroy(&gab.eg->sources_mtx);
  mtx_destroy(&gab.eg->modules_mtx);

  free(gab.eg);
}

void gab_repl(struct gab_triple gab, struct gab_repl_argt args) {
  uint64_t iterations = 0;
  gab_value env = gab_invalid;
  gab_value fiber = gab_invalid;

  args.welcome_message = args.welcome_message ? args.welcome_message : "";
  args.prompt_prefix = args.prompt_prefix ? args.prompt_prefix : "";
  args.result_prefix = args.result_prefix ? args.result_prefix : "";

  printf("%s\n", args.welcome_message);

  for (;;) {
    printf("%s", args.prompt_prefix);
    fflush(stdout);
    a_char *src = gab_fosreadl(stdin);

    if ((int8_t)src->data[0] == EOF) {
      a_char_destroy(src);
      return;
    }

    if (src->data[1] == '\0') {
      a_char_destroy(src);
      continue;
    }

    // Append the iterations number to the end of the given name
    char unique_name[strlen(args.name) + 16];
    snprintf(unique_name, sizeof(unique_name), "%s:%" PRIu64 "", args.name,
             iterations);

    iterations++;

    if (env == gab_invalid) {
      fiber =
          gab_aexec(gab, (struct gab_exec_argt){
                             .name = unique_name,
                             .source = (char *)src->data,
                             .flags = args.flags | fGAB_RUN_INCLUDEDEFAULTARGS,
                         });
    } else {
      size_t len = gab_reclen(env) - 1;
      const char *keys[len];
      gab_value keyvals[len];
      gab_value vals[len];

      for (size_t i = 0; i < len; i++) {
        size_t index = i + 1;
        keyvals[i] = gab_valintos(gab, gab_ukrecat(env, index));
        vals[i] = gab_uvrecat(env, index);
        keys[i] = gab_strdata(keyvals + i);
      }

      fiber = gab_aexec(gab, (struct gab_exec_argt){
                                 .name = unique_name,
                                 .source = (char *)src->data,
                                 .flags = args.flags,
                                 .len = len,
                                 .sargv = keys,
                                 .argv = vals,
                             });
    }

    if (fiber == gab_invalid)
      continue;

    a_gab_value *result = gab_fibawait(gab, fiber);
    env = gab_fibawaite(gab, fiber);

    if (result == nullptr)
      continue;

    printf("%s", args.result_prefix);
    for (int32_t i = 0; i < result->len; i++) {
      gab_value arg = result->data[i];

      if (i == result->len - 1) {
        gab_fvalinspect(stdout, arg, -1);
      } else {
        gab_fvalinspect(stdout, arg, -1);
        printf(" ");
      }
    }

    putc('\n', stdout);

    a_char_destroy(src);
  }
}

static const char *default_argvals[] = {
    tGAB_STRING, tGAB_BINARY, tGAB_MESSAGE, tGAB_RECORD, tGAB_LIST,
    tGAB_SHAPE,  tGAB_FIBER,  tGAB_CHANNEL, tGAB_NUMBER, tGAB_BLOCK,
};
static const char *default_argnames[] = {
    tGAB_STRING_NAME, tGAB_BINARY_NAME, tGAB_MESSAGE_NAME, tGAB_RECORD_NAME,
    tGAB_LIST_NAME,   tGAB_SHAPE_NAME,  tGAB_FIBER_NAME,   tGAB_CHANNEL_NAME,
    tGAB_NUMBER_NAME, tGAB_BLOCK_NAME,
};
static const size_t default_arglen =
    sizeof(default_argvals) / sizeof(const char *);

gab_value gab_aexec(struct gab_triple gab, struct gab_exec_argt args) {
  const char *sargv[default_arglen + args.len];
  gab_value vargv[default_arglen + args.len];

  gab.flags |= args.flags;

  if (gab.flags & fGAB_RUN_INCLUDEDEFAULTARGS) {
    // Copy given args in.
    if (args.len && args.sargv && args.argv) {
      memcpy(sargv, args.sargv, args.len * sizeof(const char *));
      memcpy(vargv, args.argv, args.len * sizeof(gab_value));
    }

    // Append default args.
    for (size_t i = 0; i < default_arglen; i++) {
      sargv[args.len + i] = default_argnames[i];
      vargv[args.len + i] = gab_message(gab, default_argvals[i]);
    }

    // Update args to point to appended buffers
    args.len = args.len + default_arglen;
    args.argv = vargv;
    args.sargv = sargv;
  }

  gab_value main = gab_build(gab, (struct gab_build_argt){
                                      .name = args.name,
                                      .source = args.source,
                                      .len = args.len,
                                      .argv = args.sargv,
                                  });

  if (main == gab_invalid || gab.flags & fGAB_BUILD_CHECK)
    return main;

  return gab_arun(gab, (struct gab_run_argt){
                           .main = main,
                           .len = args.len,
                           .argv = args.argv,
                       });
}

a_gab_value *gab_exec(struct gab_triple gab, struct gab_exec_argt args) {
  gab_value fib = gab_aexec(gab, args);

  if (fib == gab_invalid)
    return nullptr;

  return gab_fibawait(gab, fib);
}

gab_value dodef(struct gab_triple gab, gab_value messages, uint64_t len,
                struct gab_def_argt args[static len]) {

  gab_gclock(gab);

  for (uint64_t i = 0; i < len; i++) {
    struct gab_def_argt arg = args[i];

    gab_value specs = gab_recat(messages, arg.message);

    if (specs == gab_invalid)
      specs = gab_record(gab, 0, 0, nullptr, nullptr);

    gab_value newspecs =
        gab_recput(gab, specs, arg.receiver, arg.specialization);

    messages = gab_recput(gab, messages, arg.message, newspecs);
  }

  return gab_gcunlock(gab), messages;
}

gab_value swapdef(struct gab_triple gab, gab_value messages, va_list va) {
  uint64_t len = va_arg(va, uint64_t);
  struct gab_def_argt *args = va_arg(va, struct gab_def_argt *);

  return dodef(gab, messages, len, args);
}

bool gab_ndef(struct gab_triple gab, uint64_t len,
              struct gab_def_argt args[static len]) {
  gab_value messages = gab.eg->messages;

  gab_value m = dodef(gab, messages, len, args);

  gab.eg->messages = m;

  return m != gab_invalid;
}

struct gab_ostring *gab_egstrfind(struct gab_eg *self, uint64_t hash,
                                  uint64_t len, const char *data) {
  if (self->strings.len == 0)
    return nullptr;

  uint64_t index = hash & (self->strings.cap - 1);

  for (;;) {
    d_status status = d_strings_istatus(&self->strings, index);
    struct gab_ostring *key = d_strings_ikey(&self->strings, index);

    switch (status) {
    case D_TOMBSTONE:
      break;
    case D_EMPTY:
      return nullptr;
    case D_FULL:
      if (key->len == len && key->hash == hash && !memcmp(key->data, data, len))
        return key;
    }

    index = (index + 1) & (self->strings.cap - 1);
  }
}

a_gab_value *gab_segmodat(struct gab_eg *eg, const char *name) {
  uint64_t hash = s_char_hash(s_char_cstr(name));

  mtx_lock(&eg->modules_mtx);

  a_gab_value *module = d_gab_modules_read(&eg->modules, hash);

  mtx_unlock(&eg->modules_mtx);

  return module;
}

a_gab_value *gab_segmodput(struct gab_eg *eg, const char *name,
                           a_gab_value *module) {
  uint64_t hash = s_char_hash(s_char_cstr(name));

  mtx_lock(&eg->modules_mtx);

  if (d_gab_modules_read(&eg->modules, hash) != nullptr)
    return mtx_unlock(&eg->modules_mtx), nullptr;

  d_gab_modules_insert(&eg->modules, hash, module);
  return mtx_unlock(&eg->modules_mtx), module;
}

uint64_t gab_egkeep(struct gab_eg *gab, gab_value v) {
  return gab_negkeep(gab, 1, &v);
}

uint64_t gab_negkeep(struct gab_eg *gab, uint64_t len,
                     gab_value values[static len]) {
  mtx_lock(&gab->modules_mtx);

  for (uint64_t i = 0; i < len; i++)
    if (gab_valiso(values[i]))
      v_gab_value_push(&gab->scratch, values[i]);

  mtx_unlock(&gab->modules_mtx);

  return len;
}

int gab_sprintf(char *dest, size_t n, const char *fmt, ...) {
  va_list va;
  va_start(va, fmt);

  int res = gab_vsprintf(dest, n, fmt, va);

  va_end(va);

  return res;
}

int gab_fprintf(FILE *stream, const char *fmt, ...) {
  va_list va;
  va_start(va, fmt);

  int res = gab_vfprintf(stream, fmt, va);

  va_end(va);

  return res;
}

int gab_nsprintf(char *dest, size_t n, const char *fmt, uint64_t argc,
                 gab_value argv[argc]) {
  const char *c = fmt;
  char *cursor = dest;
  size_t remaining = n;
  int bytes = 0;
  uint64_t i = 0;

  while (*c != '\0') {
    if (remaining == 1)
      return *cursor = '\0', bytes;

    switch (*c) {
    case '$': {
      if (i >= argc)
        return -1;

      gab_value arg = argv[i++];

      int res = gab_svalinspect(&cursor, &remaining, arg, 1);

      if (res < 0)
        return res;

      bytes += res;

      break;
    }
    default:
      *cursor++ = *c;
      bytes += 1;
      remaining -= 1;
    }
    c++;
  }

  if (i != argc)
    return -1;

  return *cursor = '\0', bytes;
}

int gab_nfprintf(FILE *stream, const char *fmt, uint64_t argc,
                 gab_value argv[argc]) {
  const char *c = fmt;
  int bytes = 0;
  uint64_t i = 0;

  while (*c != '\0') {
    switch (*c) {
    case '$': {
      if (i >= argc)
        return -1;

      gab_value arg = argv[i++];

      bytes += gab_fvalinspect(stream, arg, 1);

      break;
    }
    default:
      bytes += fputc(*c, stream);
    }
    c++;
  }

  if (i != argc)
    return -1;

  return bytes;
}

int gab_vsprintf(char *dest, size_t n, const char *fmt, va_list varargs) {
  const char *c = fmt;
  int bytes = 0;
  char *cursor = dest;
  size_t remaining = n;

  while (*c != '\0') {
    switch (*c) {
    case '$': {
      gab_value arg = va_arg(varargs, gab_value);

      int res = gab_svalinspect(&cursor, &remaining, arg, 1);

      if (res < 0)
        return res;

      bytes += res;

      break;
    }
    default:
      if (remaining == 0)
        return -1;

      *cursor++ = *c;
      bytes += 1;
      remaining -= 1;
    }
    c++;
  }

  return bytes;
}

int gab_vfprintf(FILE *stream, const char *fmt, va_list varargs) {
  const char *c = fmt;
  int bytes = 0;

  while (*c != '\0') {
    switch (*c) {
    case '$': {
      gab_value arg = va_arg(varargs, gab_value);

      bytes += gab_fvalinspect(stream, arg, 1);
      break;
    }
    default:
      bytes += fputc(*c, stream);
    }
    c++;
  }

  return bytes;
}

void dump_pretty_err(struct gab_triple gab, FILE *stream, va_list varargs,
                     struct gab_err_argt args) {
  gab_value tok_name = gab_string(
      gab,
      args.src
          ? gab_token_names[v_gab_token_val_at(&args.src->tokens, args.tok)]
          : "C");

  gab_value src_name = args.src ? args.src->name : gab_string(gab, "C");

  gab_fprintf(stream, "\n[$] $ panicked near $", src_name, args.message,
              tok_name);

  if (args.status != GAB_NONE) {
    gab_value status_name = gab_string(gab, gab_status_names[args.status]);
    gab_fprintf(stream, ": $.", status_name);
  }

  if (args.src) {
    s_char tok_src = v_s_char_val_at(&args.src->token_srcs, args.tok);
    const char *tok_start = tok_src.data;
    const char *tok_end = tok_src.data + tok_src.len;

    uint64_t line = v_uint64_t_val_at(&args.src->token_lines, args.tok);

    s_char line_src = v_s_char_val_at(&args.src->lines, line - 1);

    while (*line_src.data == ' ' || *line_src.data == '\t') {
      line_src.data++;
      line_src.len--;
      if (line_src.len == 0)
        break;
    }

    a_char *under_src = a_char_empty(line_src.len);

    for (uint8_t i = 0; i < under_src->len; i++) {
      if (line_src.data + i >= tok_start && line_src.data + i < tok_end)
        under_src->data[i] = '^';
      else
        under_src->data[i] = ' ';
    }

    fprintf(stream,
            "\n\n" GAB_RED "%.4" PRIu64 "" GAB_RESET "| %.*s"
            "\n      " GAB_YELLOW "%.*s" GAB_RESET "",
            line, (int)line_src.len, line_src.data, (int)under_src->len,
            under_src->data);

    a_char_destroy(under_src);
  }

  if (args.note_fmt && strlen(args.note_fmt) > 0) {
    fprintf(stream, "\n\n");
    gab_vfprintf(stream, args.note_fmt, varargs);
  }

  fprintf(stream, "\n\n");
};

void dump_structured_err(struct gab_triple gab, FILE *stream, va_list varargs,
                         struct gab_err_argt args) {
  fprintf(stream, "%s:%s:%s:%s", args.err_out->status_name,
          args.err_out->src_name, args.err_out->tok_name,
          args.err_out->msg_name);

  if (args.src) {
    fprintf(stream,
            ":%" PRIu64 ":%" PRIu64 ":%" PRIu64 ":%" PRIu64 ":%" PRIu64 "",
            args.err_out->row, args.err_out->col_begin, args.err_out->col_end,
            args.err_out->byte_begin, args.err_out->byte_end);
  }

  fputc('\n', stream);
}

void gab_vfpanic(struct gab_triple gab, FILE *stream, va_list varargs,
                 struct gab_err_argt args) {
  struct gab_errdetails err = {0};
  args.err_out = args.err_out ? args.err_out : &err;

  args.err_out->tok_name =
      args.src
          ? gab_token_names[v_gab_token_val_at(&args.src->tokens, args.tok)]
          : "C";

  if (args.src) {
    args.err_out->row = v_uint64_t_val_at(&args.src->token_lines, args.tok);

    s_char line_src = v_s_char_val_at(&args.src->lines, args.err_out->row - 1);
    s_char tok_src = v_s_char_val_at(&args.src->token_srcs, args.tok);

    assert(tok_src.data >= line_src.data);

    args.err_out->col_begin = tok_src.data - line_src.data;
    args.err_out->col_end = tok_src.data + tok_src.len - line_src.data;

    args.err_out->byte_begin = tok_src.data - args.src->source->data;
    args.err_out->byte_end =
        tok_src.data + tok_src.len - args.src->source->data;
  }

  args.err_out->src_name = args.src ? gab_strdata(&args.src->name) : "C";

  args.err_out->msg_name = gab_strdata(&args.message);

  args.err_out->status_name = gab_status_names[args.status];

  if (gab.flags & fGAB_ERR_QUIET)
    return;

  if (gab.flags & fGAB_ERR_STRUCTURED)
    dump_structured_err(gab, stream, varargs, args);
  else
    dump_pretty_err(gab, stream, varargs, args);
}

/*int gab_val_printf_handler(FILE *stream, const struct printf_info *info,*/
/*                           const void *const *args) {*/
/*  const gab_value value = *(const gab_value *const)args[0];*/
/*  return gab_fvalinspect(stream, value, -1);*/
/*}*/
/**/
/*int gab_val_printf_arginfo(const struct printf_info *i, uint64_t n, int
 * *argtypes,*/
/*                           int *sizes) {*/
/*  if (n > 0) {*/
/*    argtypes[0] = PA_INT | PA_FLAG_LONG;*/
/*    sizes[0] = sizeof(gab_value);*/
/*  }*/
/**/
/*  return 1;*/
/*}*/

#define MODULE_SYMBOL "gab_lib"

typedef a_gab_value *(*handler_f)(struct gab_triple, const char *);

typedef a_gab_value *(*module_f)(struct gab_triple);

typedef struct {
  const char *prefix;
  const char *suffix;

  const handler_f handler;
} resource;

a_gab_value *gab_use_dynlib(struct gab_triple gab, const char *path) {
  gab_osdynlib lib = gab_oslibopen(path);

  if (lib == nullptr) {
#ifdef GAB_PLATFORM_UNIX
    return gab_fpanic(gab, "Failed to load module '$': $",
                      gab_string(gab, path), gab_string(gab, dlerror()));
#else
    {
      int error = GetLastError();
      char buffer[128];
      if (FormatMessageA(
              FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM, NULL,
              error, 0, buffer, sizeof(buffer) / sizeof(char), NULL))
        return gab_fpanic(gab, "Failed to load module '$': $",
                          gab_string(gab, path), gab_string(gab, buffer));

      return gab_fpanic(gab, "Failed to load module '$'",
                        gab_string(gab, path));
    }
#endif
  }

  module_f mod = (module_f)gab_oslibfind(lib, GAB_DYNLIB_MAIN);

  if (mod == nullptr)
#ifdef GAB_PLATFORM_UNIX
    return gab_fpanic(gab, "Failed to load module '$': $",
                      gab_string(gab, path), gab_string(gab, dlerror()));
#else
  {
    int error = GetLastError();
    char buffer[128];
    if (FormatMessageA(
            FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM, NULL,
            error, 0, buffer, sizeof(buffer) / sizeof(char), NULL))
      return gab_fpanic(gab, "Failed to load module '$': $",
                        gab_string(gab, path), gab_string(gab, buffer));

    return gab_fpanic(gab, "Failed to load module '$'", gab_string(gab, path));
  }
#endif

  a_gab_value *res = mod(gab);

  // At this point, mod should have reported any errors.
  /*gab.flags |= fGAB_ERR_QUIET;*/

  if (res == nullptr)
    return gab_fpanic(gab, "Failed to load c module.");

  if (res->data[0] != gab_ok)
    return gab_fpanic(gab,
                      "Failed to load module: module returned $, expected $",
                      res->data[0], gab_ok);

  a_gab_value *final = gab_segmodput(gab.eg, path, res);

  return final;
}

a_gab_value *gab_use_source(struct gab_triple gab, const char *path) {
  a_char *src = gab_osread(path);

  if (src == nullptr) {
    gab_value reason = gab_string(gab, strerror(errno));
    return gab_fpanic(gab, "Failed to load module: $", reason);
  }

  gab_value fiber =
      gab_aexec(gab, (struct gab_exec_argt){
                         .name = path,
                         .source = (const char *)src->data,
                         .flags = gab.flags | fGAB_RUN_INCLUDEDEFAULTARGS,
                     });

  if (fiber == gab_invalid)
    return nullptr;

  a_gab_value *res = gab_fibawait(gab, fiber);

  a_char_destroy(src);

  // At this point, the fiber should have reported its own errors;
  /*gab.flags |= fGAB_ERR_QUIET;*/

  if (res == nullptr)
    return gab_fpanic(gab, "Failed to load source module.");

  if (res->data[0] != gab_ok)
    return gab_fpanic(gab,
                      "Failed to load module: module returned $, expected $",
                      res->data[0], gab_ok);

  a_gab_value *final = gab_segmodput(gab.eg, path, res);

  return final;
}

resource resources[] = {
    {
        // [PREFIX]/<module>.gab
        .prefix = "/",
        .suffix = ".gab",
        .handler = gab_use_source,
    },
    {
        // [PREFIX]/mod/<module>.gab
        .prefix = "/mod/",
        .suffix = ".gab",
        .handler = gab_use_source,
    },
    {
        // [PREFIX]/mod/<module>/mod.gab
        .prefix = "/",
        .suffix = "/mod.gab",
        .handler = gab_use_source,
    },
    {
        // [PREFIX]/<module>.[so | dylib | dll]
        .prefix = "/",
        .suffix = GAB_DYNLIB_FILEENDING,
        .handler = gab_use_dynlib,
    },
    {
        // [PREFIX]/mod/<module>.[so | dylib | dll]
        .prefix = "/mod/",
        .suffix = GAB_DYNLIB_FILEENDING,
        .handler = gab_use_dynlib,
    },
};

a_char *match_resource(resource *res, const char *name, uint64_t len) {
  const char *prefix = gab_osprefix(GAB_VERSION_TAG);

  const char *roots[] = {".", prefix};

  for (int i = 0; i < LEN_CARRAY(roots); i++) {
    if (roots[i] == nullptr)
      continue;

    const uint64_t r_len = strlen(roots[i]);
    const uint64_t p_len = strlen(res->prefix);
    const uint64_t s_len = strlen(res->suffix);
    const uint64_t total_len = r_len + p_len + len + s_len + 1;

    char buffer[total_len];

    memcpy(buffer, roots[i], r_len);
    memcpy(buffer + r_len, res->prefix, p_len);
    memcpy(buffer + r_len + p_len, name, len);
    memcpy(buffer + r_len + p_len + len, res->suffix, s_len + 1);

    FILE *f = fopen(buffer, "r");

    if (!f)
      continue;

    fclose(f);
    free((void *)prefix);
    return a_char_create(buffer, total_len);
  }

  free((void *)prefix);
  return nullptr;
}

a_gab_value *gab_suse(struct gab_triple gab, const char *name) {
  return gab_use(gab, gab_string(gab, name));
}

a_gab_value *gab_use(struct gab_triple gab, gab_value path) {
  assert(gab_valkind(path) == kGAB_STRING);

  const char *name = gab_strdata(&path);

  for (int j = 0; j < sizeof(resources) / sizeof(resource); j++) {
    resource *res = resources + j;
    a_char *path = match_resource(res, name, strlen(name));

    if (path) {
      a_gab_value *cached = gab_segmodat(gab.eg, (char *)path->data);

      if (cached != nullptr) {
        /* Skip the first argument, which is the module's data */
        a_char_destroy(path);
        return cached;
      }

      a_gab_value *result = res->handler(gab, path->data);

      if (result != nullptr) {
        /* Skip the first argument, which is the module's data */
        a_char_destroy(path);
        return result;
      }

      a_char_destroy(path);
    }
  }

  return nullptr;
}

a_gab_value *gab_run(struct gab_triple gab, struct gab_run_argt args) {
  gab_value fb = gab_arun(gab, args);

  if (fb == gab_invalid)
    return nullptr;

  a_gab_value *res = gab_fibawait(gab, fb);
  assert(res != nullptr);

  return res;
}

gab_value gab_arun(struct gab_triple gab, struct gab_run_argt args) {
  return gab_tarun(gab, -1, args);
}

gab_value gab_tarun(struct gab_triple gab, size_t nms,
                    struct gab_run_argt args) {
  gab.flags |= args.flags;

  if (gab.flags & fGAB_BUILD_CHECK)
    return gab_invalid;

  gab_value fb = gab_fiber(gab, (struct gab_fiber_argt){
                                    .message = gab_message(gab, mGAB_CALL),
                                    .receiver = args.main,
                                    .flags = gab.flags,
                                    .argv = args.argv,
                                    .argc = args.len,
                                });

  gab_iref(gab, fb);
  gab_egkeep(gab.eg, fb); // Not the best solution

#if cGAB_LOG_EG
  fprintf(stdout, "[WORKER %i] chnput ", gab.wkid);
  gab_fprintf(stdout, "$\n", fb);
#endif

  // Somehow check if the put will block, and create a job in that case.
  // Should check to see if the channel has takers waiting already.
  gab_wkspawn(gab);

  if (gab_tchnput(gab, gab.eg->work_channel, fb, nms) == gab_timeout)
    return gab_timeout;

  return fb;
}

a_gab_value *gab_send(struct gab_triple gab, struct gab_send_argt args) {
  gab_value fb = gab_asend(gab, args);

  if (fb == gab_invalid)
    return nullptr;

  a_gab_value *res = gab_fibawait(gab, fb);

  gab_dref(gab, fb);

  return res;
};

gab_value gab_asend(struct gab_triple gab, struct gab_send_argt args) {
  gab.flags |= args.flags;

  gab_value fb = gab_fiber(gab, (struct gab_fiber_argt){
                                    .message = args.message,
                                    .receiver = args.receiver,
                                    .argv = args.argv,
                                    .argc = args.len,
                                    .flags = gab.flags,
                                });

  if (fb == gab_invalid)
    return gab_invalid;

  gab_iref(gab, fb);

  gab_jbcreate(gab, next_available_job(gab), worker_job);

  gab_chnput(gab, gab.eg->work_channel, fb);

  return fb;
};

GAB_API void gab_sigterm(struct gab_triple gab) {
  bool succeeded = gab_signal(gab, sGAB_TERM, 1);
  assert(succeeded);
}

void gab_asigcoll(struct gab_triple gab) {
  bool succeeded = gab_signal(gab, sGAB_COLL, 1);
  assert(succeeded);
}

void gab_sigcoll(struct gab_triple gab) {
  gab_asigcoll(gab);

  while (gab_is_signaling(gab))
    ;
}

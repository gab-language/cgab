#define GAB_STATUS_NAMES_IMPL
#define GAB_TOKEN_NAMES_IMPL
#include "engine.h"

#include "colors.h"

#include "core.h"
#include "gab.h"
#include "lexer.h"

struct errdetails {
  const char *status_name, *src_name, *tok_name, *msg_name;
  uint64_t token, row, col_begin, col_end, byte_begin, byte_end;
};

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
        .val = gab_cundefined,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_EQ),
    },
    {
        .name = mGAB_CONS,
        .val = gab_cundefined,
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
        .name = mGAB_SPLATLIST,
        .kind = kGAB_RECORD,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_SPLATLIST),
    },
    {
        .name = mGAB_SPLATLIST,
        .kind = kGAB_SHAPE,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_SPLATSHAPE),
    },
    {
        .name = mGAB_SPLATDICT,
        .kind = kGAB_RECORD,
        .primitive = gab_primitive(OP_SEND_PRIMITIVE_SPLATDICT),
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

  struct gab_job *job = gab.eg->jobs + gab.wkid;

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
  v_gab_value_destroy(&job->lock_keep);
  return 0;
}

int32_t worker_job(void *data) {
  struct gab_triple *g = data;
  struct gab_triple gab = *g;

  assert(gab.wkid != 0);
  atomic_fetch_add(&gab.eg->njobs, 1);

  struct gab_job *job = gab.eg->jobs + gab.wkid;

#if cGAB_LOG_EG
  fprintf(stdout, "[WORKER %i] SPAWNED\n", gab.wkid);
#endif

  while (!gab_chnisclosed(gab.eg->work_channel) ||
         !q_gab_value_is_empty(&job->queue)) {
#if cGAB_LOG_EG
    gab_fprintf(stdout, "[WORKER $] TAKING WITH TIMEOUT $ms\n",
                gab_number(gab.wkid), gab_number(cGAB_WORKER_IDLEWAIT_MS));
#endif

    gab_value fiber =
        gab_tchntake(gab, gab.eg->work_channel, cGAB_WORKER_IDLEWAIT_MS);

#if cGAB_LOG_EG
    gab_fprintf(stdout, "[WORKER $] chntake succeeded: $\n",
                gab_number(gab.wkid), fiber);
#endif

    if (fiber == gab_cinvalid || fiber == gab_ctimeout ||
        fiber == gab_cundefined) {

      // Our global take timed out and our internal queue is empty.
      // our work is done.
      if (q_gab_value_is_empty(&job->queue))
        goto fin;
    } else {
      // Our global take succeeded - append to our local queue.
      if (!q_gab_value_push(&job->queue, fiber))
        assert(false && "PUSH FAILED");
    }

    // Peek at job to do on the queue.
    fiber = q_gab_value_peek(&job->queue);

    assert(gab_valkind(fiber) != kGAB_FIBERDONE);

    // Run our job.
    union gab_value_pair res = gab_vmexec(gab, fiber);

    assert(q_gab_value_peek(&job->queue) == fiber);

    // We did work - pop it off the queue now.
    q_gab_value_pop(&job->queue);

    switch (res.status) {
    case gab_ctimeout:
      // We did not complete the work. Push back onto our queue.
      if (!q_gab_value_push(&job->queue, fiber))
        assert(false && "PUSH FAILED");
      break;
    // We completed the work. Nothing else to do.
    case gab_cvalid:
      break;
    // We were interruppted by sGAB_TERM. Signal will be handled below.
    case gab_cinvalid:
      break;
    default:
      assert(false && "UNREACHABLE");
    }

    /* Yield for signals here before taking a new job */
    switch (gab_yield(gab)) {
    case sGAB_TERM:
      // Clear the work queue - we're terminated
      q_gab_value_create(&job->queue);
      goto fin;
    default:
      break;
    }
  }

fin:
  assert(q_gab_value_is_empty(&job->queue));

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

  atomic_fetch_sub(&gab.eg->njobs, 1);

  assert(job->locked == 0);

  free(g);
  v_gab_value_destroy(&job->lock_keep);

  return 0;
}

struct gab_job *next_available_job(struct gab_triple gab) {

  // Try to reuse an existing job, thats exited after idling
  for (uint64_t i = 1; i < gab.eg->len; i++) {
    // If we have a dead thread, revive it
    if (!gab.eg->jobs[i].alive)
      return gab.eg->jobs + i;
  }

  // No room for new jobs
  return nullptr;
}

bool gab_jbcreate(struct gab_triple gab, struct gab_job *job, int(fn)(void *)) {
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

union gab_value_pair gab_create(struct gab_create_argt args,
                                struct gab_triple gab_out[static 1]) {
  uint64_t njobs = args.jobs ? args.jobs : 8;

  uint64_t egsize =
      sizeof(struct gab_eg) + sizeof(struct gab_job) * (njobs + 1);

  struct gab_eg *eg = malloc(egsize);
  memset(eg, 0, egsize);

  eg->len = njobs + 1;
  eg->njobs = 0;
  eg->hash_seed = time(nullptr);
  eg->sig.schedule = -1;
  eg->joberr_handler = args.joberr_handler;

  // The only non-zero initialization that jobs need is epoch = 1
  for (uint64_t i = 0; i < eg->len; i++)
    eg->jobs[i].epoch = 1;

  mtx_init(&eg->shapes_mtx, mtx_plain);
  mtx_init(&eg->sources_mtx, mtx_plain);
  mtx_init(&eg->strings_mtx, mtx_plain);
  mtx_init(&eg->modules_mtx, mtx_plain);

  d_gab_src_create(&eg->sources, 8);

  gab_out->eg = eg;
  gab_out->flags = args.flags;
  gab_out->wkid = 0;

  struct gab_triple gab = *gab_out;

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

  size_t nargs = 0;
  gab_value vargs[args.len + 1];
  const char *sargs[args.len + 1];

  vargs[nargs] = gab_ok;
  sargs[nargs] = "";
  nargs++;

  // Use each module that's asked for, in order.
  // Build up an array of names and values.
  for (int i = 0; i < args.len; i++) {
    const char *module = args.modules[i];
    union gab_value_pair res = gab_use(gab, (struct gab_use_argt){
                                                .sname = module,
                                                .len = nargs,
                                                .sargv = sargs,
                                                .argv = vargs,
                                            });

    // If any of these uses fail, return the failure.
    if (res.status != gab_cvalid)
      return gab_gcunlock(gab), res;

    if (res.aresult->data[0] != gab_ok)
      return gab_gcunlock(gab), res;

    vargs[nargs] = res.aresult->data[1];
    sargs[nargs] = module;
    nargs++;
  }

  return gab_gcunlock(gab), (union gab_value_pair){
                                .status = gab_cvalid,
                                .aresult = a_gab_value_create(vargs, nargs),
                            };
}

void dec_child_shapes(struct gab_triple gab, gab_value shp) {
  assert(gab_valkind(shp) == kGAB_SHAPE || gab_valkind(shp) == kGAB_SHAPELIST);
  struct gab_oshape *shape = GAB_VAL_TO_SHAPE(shp);

  uint64_t len = shape->transitions.len / 2;

  for (size_t i = 0; i < len; i++)
    dec_child_shapes(gab, v_gab_value_val_at(&shape->transitions, i * 2 + 1));

  gab_dref(gab, shp);
}

void gab_destroy(struct gab_triple gab) {
  // Wait until there is no work to be done
  // This doesn't quite work anymore as now
  // Workers have local queues of work to do.
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

  gab.eg->messages = gab_cinvalid;
  gab.eg->shapes = gab_cinvalid;

  assert(gab.eg->njobs == 0);

  /**
   * Four consececutive collections are needed here because
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

  for (uint64_t i = 0; i < gab.eg->sources.cap; i++) {
    if (d_gab_src_iexists(&gab.eg->sources, i)) {
      struct gab_src *s = d_gab_src_ival(&gab.eg->sources, i);
      gab_srcdestroy(s);
    }
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
  gab_value env = gab_cinvalid;
  union gab_value_pair fiber;

  args.welcome_message = args.welcome_message ? args.welcome_message : "";
  args.prompt_prefix = args.prompt_prefix ? args.prompt_prefix : "";
  args.result_prefix = args.result_prefix ? args.result_prefix : "";

  printf("%s\n", args.welcome_message);

  for (;;) {
    printf("%s", args.prompt_prefix);
    fflush(stdout);
    a_char *src = gab_fosreadl(stdin);

    if (src->len <= 1) {
      a_char_destroy(src);
      return;
    }

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

    if (env == gab_cinvalid) {
      fiber = gab_aexec(gab, (struct gab_exec_argt){
                                 .name = unique_name,
                                 .source = (char *)src->data,
                                 .flags = args.flags,
                                 .len = args.len,
                                 .sargv = args.sargv,
                                 .argv = args.argv,
                             });
    } else {
      size_t len = gab_reclen(env) - 1;
      const char *keys[len + 1];
      gab_value keyvals[len + 1];
      gab_value vals[len + 1];

      for (size_t i = 0; i < len; i++) {
        size_t index = i + 1;
        keyvals[i] = gab_ukrecat(env, index);
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

    a_char_destroy(src);
    src = nullptr;

    if (fiber.status != gab_cvalid) {
      const char *errstr = gab_errtocs(gab, fiber.vresult);
      assert(errstr != nullptr);
      puts(errstr);
      continue;
    }

    union gab_value_pair res = gab_fibawait(gab, fiber.vresult);

    /* Setup env regardless of run failing/succeeding */
    gab_value new_env = gab_fibawaite(gab, fiber.vresult);
    if (env == gab_cinvalid || new_env == gab_cinvalid)
      env = new_env;
    else
      env = gab_reccat(gab, env, new_env);

    assert(env != gab_cinvalid);

    if (res.status != gab_cvalid) {
      continue;
    }

    if (res.aresult->data[0] != gab_ok) {
      continue;
    }

    printf("%s", args.result_prefix);
    for (int32_t i = 0; i < res.aresult->len; i++) {
      gab_value arg = res.aresult->data[i];

      if (i == res.aresult->len - 1) {
        gab_fvalinspect(stdout, arg, -1);
      } else {
        gab_fvalinspect(stdout, arg, -1);
        printf(" ");
      }
    }

    putc('\n', stdout);
  }
}

union gab_value_pair gab_aexec(struct gab_triple gab,
                               struct gab_exec_argt args) {
  gab.flags |= args.flags;

  union gab_value_pair main = gab_build(gab, (struct gab_parse_argt){
                                                 .name = args.name,
                                                 .source = args.source,
                                                 .len = args.len,
                                                 .argv = args.sargv,
                                             });

  if (main.status != gab_cvalid || gab.flags & fGAB_BUILD_CHECK)
    return main;

  return gab_arun(gab, (struct gab_run_argt){
                           .main = main.vresult,
                           .len = args.len,
                           .argv = args.argv,
                       });
}

union gab_value_pair gab_exec(struct gab_triple gab,
                              struct gab_exec_argt args) {
  union gab_value_pair fib = gab_aexec(gab, args);

  if (fib.status != gab_cvalid)
    return fib;

  return gab_fibawait(gab, fib.vresult);
}

gab_value dodef(struct gab_triple gab, gab_value messages, uint64_t len,
                struct gab_def_argt args[static len]) {

  gab_gclock(gab);

  for (uint64_t i = 0; i < len; i++) {
    struct gab_def_argt arg = args[i];

    gab_value specs = gab_recat(messages, arg.message);

    if (specs == gab_cundefined)
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

  return m != gab_cinvalid;
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

  for (size_t i = 128;; i <<= 1) {
    va_start(va, fmt);

    char buf[i];
    if (gab_vsprintf(buf, i, fmt, va) >= 0)
      return va_end(va), fputs(buf, stream);

    va_end(va);
  }

  return -1;
}

int gab_nsprintf(char *dest, size_t n, const char *fmt, uint64_t argc,
                 gab_value argv[argc]) {
  const char *c = fmt;
  char *cursor = dest;
  size_t remaining = n;
  uint64_t i = 0;

  while (*c != '\0') {
    switch (*c) {
    case '$': {
      if (i >= argc)
        return -1;

      gab_value arg = argv[i++];

      int res = gab_svalinspect(&cursor, &remaining, arg, 1);

      if (res < 0)
        return res;

      break;
    }
    default:
      if (remaining == 0)
        return -1;

      *cursor++ = *c;
      remaining -= 1;
    }

    c++;
  }

  if (remaining == 0)
    return -1;

  *cursor++ = *c;
  remaining -= 1;

  if (i != argc)
    return -1;

  return n - remaining;
}

int gab_vsprintf(char *dest, size_t n, const char *fmt, va_list varargs) {
  const char *c = fmt;
  char *cursor = dest;
  size_t remaining = n;

  while (*c != '\0') {
    switch (*c) {
    case '$': {
      gab_value arg = va_arg(varargs, gab_value);

      int res = gab_svalinspect(&cursor, &remaining, arg, 1);

      if (res < 0)
        return -1;

      break;
    }
    default:
      if (remaining == 0)
        return -1;

      *cursor++ = *c;
      remaining -= 1;
    }
    c++;
  }

  if (remaining == 0)
    return -1;

  *cursor++ = *c;
  remaining -= 1;

  return n - remaining;
}

int sprint_pretty_err(struct gab_triple gab, char **buf, size_t *len,
                      struct errdetails *args, const char *hint) {
  struct gab_src *src =
      d_gab_src_read(&gab.eg->sources, gab_string(gab, args->src_name));

  const char *tok_name =
      src ? gab_token_names[v_gab_token_val_at(&src->tokens, args->token)]
          : "C";

  const char *src_name = src ? gab_strdata(&src->name) : "C";

  if (snprintf_through(buf, len,
                       "[" GAB_GREEN "gab@%i" GAB_RESET
                       "] panicked in " GAB_GREEN "%s" GAB_RESET
                       " near " GAB_YELLOW "%s.\n\n" GAB_RESET,
                       gab.wkid, src_name, tok_name) < 0)
    return -1;

  if (args->status_name)
    if (snprintf_through(buf, len, "    " GAB_RED "%s" GAB_RESET "\n",
                         args->status_name) < 0)
      return -1;

  if (src) {
    s_char tok_src = v_s_char_val_at(&src->token_srcs, args->token);

    uint64_t line_num = v_uint64_t_val_at(&src->token_lines, args->token);

    s_char line_src = v_s_char_val_at(&src->lines, line_num - 1);

    // Skip preceding whitespace for this line.
    size_t whitespace_skipped = 0;
    while (line_src.data[whitespace_skipped] == ' ' ||
           line_src.data[whitespace_skipped] == '\t') {
      whitespace_skipped++;
      assert(line_src.len > whitespace_skipped);
    }

    line_src.data += whitespace_skipped;
    line_src.len -= whitespace_skipped;

    if (line_num > 1) {
      size_t prev_line_num = line_num - 2;
      s_char prev_line_src = v_s_char_val_at(&src->lines, prev_line_num);
      if (prev_line_src.len > whitespace_skipped)
        if (snprintf_through(buf, len, "\n      %.*s",
                             (int)(prev_line_src.len - whitespace_skipped),
                             prev_line_src.data + whitespace_skipped) < 0) {
          return -1;
        }
    }

    int leftpad = (int)(tok_src.data - line_src.data);
    // int tokpad = (int)tok_src.len - 2;
    int rhs_width = (int)tok_src.len - 1;

    const char *lhs = "^";
    const char *rhs = "^^^^^^^^^^^^^^^^^^^^^^";

    if (snprintf_through(buf, len,
                         "\n" GAB_RED "%.4" PRIu64 "" GAB_RESET "| %.*s"
                         "\n      " GAB_YELLOW "%*s%s%.*s" GAB_RESET "",
                         line_num, (int)line_src.len, line_src.data, leftpad,
                         "", lhs, rhs_width, rhs) < 0) {
      return -1;
    }

    if (line_num < src->lines.len) {
      size_t next_line_num = line_num;
      s_char next_line_src = v_s_char_val_at(&src->lines, next_line_num);

      if (next_line_src.len > whitespace_skipped)
        if (snprintf_through(buf, len, "\n      %.*s",
                             (int)(next_line_src.len - whitespace_skipped),
                             next_line_src.data + whitespace_skipped) < 0) {
          return -1;
        }
    }
  }

  if (hint > 0)
    if (snprintf_through(buf, len, "\n\n%s", hint) < 0)
      return -1;

  return snprintf_through(buf, len, "\n");
};

int sprint_structured_err(struct gab_triple gab, char **buf, size_t *len,
                          struct errdetails *d, const char *hint) {
  snprintf_through(buf, len, "%s:%s:%s:%s", d->status_name, d->src_name,
                   d->tok_name, d->msg_name);

  snprintf_through(
      buf, len, ":%" PRIu64 ":%" PRIu64 ":%" PRIu64 ":%" PRIu64 ":%" PRIu64 "",
      d->row, d->col_begin, d->col_end, d->byte_begin, d->byte_end);

  return snprintf_through(buf, len, "\n");
}

// This should take an array (a stacktrace) and produce an array (stacktrace)
gab_value gab_vspanicf(struct gab_triple gab, va_list va,
                       struct gab_err_argt args) {
  struct errdetails err = {
      .tok_name =
          args.src
              ? gab_token_names[v_gab_token_val_at(&args.src->tokens, args.tok)]
              : "C",
  };

  if (args.src) {
    err.row = v_uint64_t_val_at(&args.src->token_lines, args.tok);

    s_char line_src = v_s_char_val_at(&args.src->lines, err.row - 1);
    s_char tok_src = v_s_char_val_at(&args.src->token_srcs, args.tok);

    assert(tok_src.data >= line_src.data);

    err.col_begin = tok_src.data - line_src.data;
    err.col_end = tok_src.data + tok_src.len - line_src.data;

    err.byte_begin = tok_src.data - args.src->source->data;
    err.byte_end = tok_src.data + tok_src.len - args.src->source->data;
  }

  err.src_name = args.src ? gab_strdata(&args.src->name) : "C";

  err.status_name = gab_status_names[args.status];

  gab_gclock(gab);

  char hint[cGAB_ERR_SPRINTF_BUF_MAX] = {0};
  if (args.note_fmt) {
    int res = gab_vsprintf(hint, sizeof(hint), args.note_fmt, va);
    assert(res >= 0);
  }

  gab_value rec = gab_recordof(
      gab, gab_message(gab, "status"), gab_string(gab, err.status_name),
      gab_message(gab, "src"), gab_string(gab, err.src_name),
      gab_message(gab, "tok\\offset"), gab_number(args.tok),
      gab_message(gab, "tok\\t"), gab_string(gab, err.tok_name),
      gab_message(gab, "hint"), gab_string(gab, hint), gab_message(gab, "row"),
      gab_number(err.row), gab_message(gab, "col\\begin"),
      gab_number(err.col_begin), gab_message(gab, "col\\end"),
      gab_number(err.col_end), gab_message(gab, "byte\\begin"),
      gab_number(err.byte_begin), gab_message(gab, "byte\\end"),
      gab_number(err.byte_end), );

  gab_gcunlock(gab);

  return rec;
}

gab_value single_errtos(struct gab_triple gab, gab_value err) {
  gab_value token_type = gab_mrecat(gab, err, "tok\\t");
  gab_value srcname = gab_mrecat(gab, err, "src");
  gab_value status = gab_mrecat(gab, err, "status");
  gab_value hint = gab_mrecat(gab, err, "hint");

  gab_value token = gab_valtou(gab_mrecat(gab, err, "tok\\offset"));
  uint64_t row = gab_valtou(gab_mrecat(gab, err, "row"));
  uint64_t col_begin = gab_valtou(gab_mrecat(gab, err, "col\\begin"));
  uint64_t col_end = gab_valtou(gab_mrecat(gab, err, "col\\end"));
  uint64_t byte_begin = gab_valtou(gab_mrecat(gab, err, "byte\\begin"));
  uint64_t byte_end = gab_valtou(gab_mrecat(gab, err, "byte\\end"));

  struct errdetails e = {
      .token = token,
      .src_name = gab_strdata(&srcname),
      .status_name = gab_strdata(&status),
      .tok_name = gab_strdata(&token_type),
      .byte_begin = byte_begin,
      .byte_end = byte_end,
      .col_begin = col_begin,
      .col_end = col_end,
      .row = row,
  };

  const char *cstrhint = gab_strdata(&hint);

  int (*print_fn)(struct gab_triple, char **, size_t *, struct errdetails *,
                  const char *) = gab.flags & fGAB_ERR_STRUCTURED
                                      ? sprint_structured_err
                                      : sprint_pretty_err;

  for (size_t i = 128;; i <<= 1) {
    char buf[i];
    size_t n = i;
    char *cursor = buf;
    if (print_fn(gab, &cursor, &n, &e, cstrhint) >= 0) {
      gab_value newestr = gab_string(gab, buf);
      return newestr;
    }
  }

  return gab_nil;
}

const char *gab_errtocs(struct gab_triple gab, gab_value err) {
  assert(gab_valkind(err) == kGAB_RECORD);

  if (gab_valkind(err) != kGAB_RECORD)
    return nullptr;

  if (!gab_recisl(err)) {
    // Only works because this will never be a shortstr.
    // This is dangerous typically though.
    gab_value str = single_errtos(gab, err);
    assert(str != gab_nil);
    assert(gab_strlen(str) > 5);
    return gab_strdata(&str);
  }

  gab_value total_str = gab_string(gab, "");

  int len = gab_reclen(err);
  for (int i = len - 1; i >= 0; --i) {
    gab_value next_err = gab_uvrecat(err, i);
    assert(next_err != gab_nil);
    gab_value next_str = single_errtos(gab, next_err);
    total_str = gab_strcat(gab, total_str, next_str);
  }

  assert(gab_strlen(total_str) > 5);
  return gab_strdata(&total_str);
}

#define MODULE_SYMBOL "gab_lib"

typedef union gab_value_pair (*handler_f)(struct gab_triple, const char *,
                                          size_t len, const char *sargs[len],
                                          gab_value vargs[len]);

typedef union gab_value_pair (*module_f)(struct gab_triple);

typedef struct {
  const char *prefix;
  const char *suffix;

  const handler_f handler;
} resource;

union gab_value_pair gab_use_dynlib(struct gab_triple gab, const char *path,
                                    size_t len, const char **sargs,
                                    gab_value *vargs) {
  gab_osdynlib lib = gab_oslibopen(path);

  if (lib == nullptr) {
#ifdef GAB_PLATFORM_UNIX
    return gab_panicf(gab, "Failed to load module '$': $",
                      gab_string(gab, path), gab_string(gab, dlerror()));
#else
    {
      int error = GetLastError();
      char buffer[128];
      if (FormatMessageA(
              FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM, NULL,
              error, 0, buffer, sizeof(buffer) / sizeof(char), NULL))
        return gab_panicf(gab, "Failed to load module '$': $",
                          gab_string(gab, path), gab_string(gab, buffer));

      return gab_panicf(gab, "Failed to load module '$'",
                        gab_string(gab, path));
    }
#endif
  }

  module_f mod = (module_f)gab_oslibfind(lib, GAB_DYNLIB_MAIN);

  if (mod == nullptr)
#ifdef GAB_PLATFORM_UNIX
    return gab_panicf(gab, "Failed to load module '$': $",
                      gab_string(gab, path), gab_string(gab, dlerror()));
#else
  {
    int error = GetLastError();
    char buffer[128];
    if (FormatMessageA(
            FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM, NULL,
            error, 0, buffer, sizeof(buffer) / sizeof(char), NULL))
      return gab_panicf(gab, "Failed to load module '$': $",
                        gab_string(gab, path), gab_string(gab, buffer));

    return gab_panicf(gab, "Failed to load module '$'", gab_string(gab, path));
  }
#endif

  union gab_value_pair res = mod(gab);

  // At this point, mod should have reported any errors.
  /*gab.flags |= fGAB_ERR_QUIET;*/

  if (res.status != gab_cvalid)
    return gab_panicf(gab, "Failed to load c module.");

  if (res.aresult->data[0] != gab_ok)
    return gab_panicf(gab,
                      "Failed to load module: module returned $, expected $",
                      res.aresult->data[0], gab_ok);

  if (gab_segmodput(gab.eg, path, res.aresult) == nullptr)
    return gab_panicf(gab, "Failed to cache c module.");

  return res;
}

union gab_value_pair gab_use_source(struct gab_triple gab, const char *path,
                                    size_t len, const char **sargs,
                                    gab_value *vargs) {
  a_char *src = gab_osread(path);

  if (src == nullptr) {
    gab_value reason = gab_string(gab, strerror(errno));
    return gab_panicf(gab, "Failed to load module: $", reason);
  }

  union gab_value_pair fiber =
      gab_aexec(gab, (struct gab_exec_argt){.name = path,
                                            .source = (const char *)src->data,
                                            .flags = gab.flags,
                                            .len = len,
                                            .sargv = sargs,
                                            .argv = vargs});

  a_char_destroy(src);

  if (fiber.status != gab_cvalid)
    return fiber;

  union gab_value_pair res = gab_fibawait(gab, fiber.vresult);

  // At this point, the fiber should have reported its own errors;
  /*gab.flags |= fGAB_ERR_QUIET;*/

  if (res.status != gab_cvalid)
    return res;

  if (res.aresult->data[0] != gab_ok)
    return res;

  if (gab_segmodput(gab.eg, path, res.aresult) == nullptr)
    return gab_panicf(gab, "Failed to cache source module.");

  return res;
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

union gab_value_pair gab_use(struct gab_triple gab, struct gab_use_argt args) {
  gab_value path = args.sname ? gab_string(gab, args.sname) : args.vname;
  assert(gab_valkind(path) == kGAB_STRING);

  const char *name = gab_strdata(&path);

  for (int j = 0; j < sizeof(resources) / sizeof(resource); j++) {
    resource *res = resources + j;
    a_char *module_path = match_resource(res, name, strlen(name));

    if (module_path) {
      a_gab_value *cached = gab_segmodat(gab.eg, (char *)module_path->data);

      if (cached != nullptr) {
        /* Skip the first argument, which is the module's data */
        a_char_destroy(module_path);
        return (union gab_value_pair){
            .status = gab_cvalid,
            .aresult = cached,
        };
      }

      union gab_value_pair result =
          res->handler(gab, module_path->data, args.len, args.sargv, args.argv);

      return a_char_destroy(module_path), result;
    }
  }

  return gab_panicf(gab, "Module $ could not be found", path);
}

union gab_value_pair gab_run(struct gab_triple gab, struct gab_run_argt args) {
  union gab_value_pair fb = gab_arun(gab, args);

  if (fb.status != gab_cvalid)
    return fb;

  return gab_fibawait(gab, fb.vresult);
}

union gab_value_pair gab_arun(struct gab_triple gab, struct gab_run_argt args) {
  return gab_tarun(gab, -1, args);
}

union gab_value_pair gab_tarun(struct gab_triple gab, size_t nms,
                               struct gab_run_argt args) {
  gab.flags |= args.flags;

  if (gab.flags & fGAB_BUILD_CHECK)
    return (union gab_value_pair){.status = gab_cinvalid};

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

  if (gab_tchnput(gab, gab.eg->work_channel, fb, nms) == gab_ctimeout)
    return (union gab_value_pair){.status = gab_ctimeout};

  return (union gab_value_pair){{gab_cvalid, fb}};
}

union gab_value_pair gab_send(struct gab_triple gab,
                              struct gab_send_argt args) {
  union gab_value_pair fb = gab_asend(gab, args);

  if (fb.status != gab_cvalid)
    return fb;

  union gab_value_pair res = gab_fibawait(gab, fb.vresult);

  if (res.status != gab_cvalid)
    return res;

  gab_dref(gab, fb.vresult);

  return (union gab_value_pair){
      .status = gab_cvalid,
      .aresult = res.aresult,
  };
};

union gab_value_pair gab_asend(struct gab_triple gab,
                               struct gab_send_argt args) {
  gab.flags |= args.flags;

  gab_value fb = gab_fiber(gab, (struct gab_fiber_argt){
                                    .message = args.message,
                                    .receiver = args.receiver,
                                    .argv = args.argv,
                                    .argc = args.len,
                                    .flags = gab.flags,
                                });

  if (fb == gab_cinvalid)
    return (union gab_value_pair){{gab_cinvalid}};

  gab_iref(gab, fb);

  gab_jbcreate(gab, next_available_job(gab), worker_job);

  gab_chnput(gab, gab.eg->work_channel, fb);

  return (union gab_value_pair){{gab_cvalid, fb}};
};

bool gab_sigterm(struct gab_triple gab) {
  bool succeeded = gab_signal(gab, sGAB_TERM, 1);

  if (succeeded)
    while (gab_is_signaling(gab))
      switch (gab_yield(gab)) {
      case sGAB_COLL:
        gab_gcepochnext(gab);
        gab_sigpropagate(gab);
        break;
      case sGAB_TERM:
        return false;
      default:
        break;
      };

  return succeeded;
}

bool gab_asigcoll(struct gab_triple gab) {
  bool succeeded = gab_signal(gab, sGAB_COLL, 1);
  return succeeded;
}

bool gab_sigcoll(struct gab_triple gab) {
  bool succeeded = gab_asigcoll(gab);

  if (succeeded)
    while (gab_is_signaling(gab))
      switch (gab_yield(gab)) {
      case sGAB_COLL:
        gab_gcepochnext(gab);
        gab_sigpropagate(gab);
        break;
      case sGAB_TERM:
        return false;
      default:
        break;
      };

  return succeeded;
}

struct gab_impl_rest
gab_impl(struct gab_triple gab, gab_value message, gab_value receiver) {
  gab_value spec = gab_cundefined;
  gab_value type = receiver;

  /* Check if the receiver has a supertype, and if that supertype implments
   * the message. ie <gab.shape 0 1>*/
  if (gab_valhast(receiver)) {
    type = gab_valtype(gab, receiver);
    spec = gab_thisfibmsgat(gab, message, type);
    if (spec != gab_cundefined)
      return (struct gab_impl_rest){
          type,
          .as.spec = spec,
          kGAB_IMPL_TYPE,
      };
  }

  /* Check for the kind of the receiver. ie 'gab\record' */
  type = gab_type(gab, gab_valkind(receiver));
  spec = gab_thisfibmsgat(gab, message, type);
  if (spec != gab_cundefined)
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
   */
  type = gab_cundefined;
  spec = gab_thisfibmsgat(gab, message, type);
  if (spec != gab_cundefined)
    return (struct gab_impl_rest){
        .as.spec = spec,
        kGAB_IMPL_GENERAL,
    };

  return (struct gab_impl_rest){.status = kGAB_IMPL_NONE};
}

GAB_API enum gab_kind gab_valkind(gab_value value) {
  if (gab_valiso(value))
    return gab_valtoo(value)->kind + __GAB_VAL_TAG(value);

  return __GAB_VAL_TAG(value);
}

GAB_API gab_value gab_type(struct gab_triple gab, enum gab_kind k) {
  assert(k < kGAB_NKINDS);
  return gab.eg->types[k];
}

GAB_API struct gab_gc *gab_gc(struct gab_triple gab) {
  return &gab.eg->gc;
}

GAB_API gab_value gab_thisfiber(struct gab_triple gab) {
  return q_gab_value_peek(&gab.eg->jobs[gab.wkid].queue);
}

GAB_API gab_value gab_thisfibmsg(struct gab_triple gab) {
  return gab.eg->messages;
  /*gab_value fiber = gab_thisfiber(gab);*/
  /**/
  /*if (fiber == gab_cinvalid)*/
  /*  return gab_atmat(gab, gab.eg->messages);*/
  /**/
  /*struct gab_ofiber *f = GAB_VAL_TO_FIBER(fiber);*/
  /*return gab_atmat(gab, f->messages);*/
}

GAB_API inline bool gab_is_signaling(struct gab_triple gab) {
  /*printf("SCHEDULE: %i, SIGNALING: %d\n", gab.eg->sig.schedule,
   * gab.eg->sig.schedule >= 0);*/
  return gab.eg->sig.schedule >= 0;
}

GAB_API inline bool gab_sigwaiting(struct gab_triple gab) {
  return gab.eg->sig.schedule == gab.wkid;
}

GAB_API inline void gab_signext(struct gab_triple gab, int wkid) {
#if cGAB_LOG_EG
  printf("[WORKER %i] TRY NEXT %i\n", gab.wkid, wkid);
#endif

  // Wrap around the number of jobs. Since
  // The 0th job is the GC job, we will wrap around
  // and begin the gc last.
  if (wkid >= gab.eg->len) {
#if cGAB_LOG_EG
    printf("[WORKER %i] WRAP NEXT %i\n", gab.wkid, 0);
#endif

    gab.eg->sig.schedule = 0;
    return;
  }

  // If the worker we're scheduling for isn't alive, skip it
  if (!gab.eg->jobs[wkid].alive) {

    if (gab.eg->sig.signal == sGAB_COLL)
      gab.eg->jobs[wkid].epoch++;

#if cGAB_LOG_EG
    printf("[WORKER %i] SKIP NEXT %i\n", gab.wkid, wkid);
#endif

    assert(!gab.eg->jobs[wkid].alive);
    gab_signext(gab, wkid + 1);
    return;
  }

  if (gab.eg->sig.schedule < (int8_t)wkid) {
#if cGAB_LOG_EG
    printf("[WORKER %i] DO NEXT %i\n", gab.wkid, wkid);
#endif
    gab.eg->sig.schedule = wkid;
  }
}

GAB_API inline void gab_sigclear(struct gab_triple gab) {
  assert(gab_is_signaling(gab));
  gab.eg->sig.signal = sGAB_IGN;
  gab.eg->sig.schedule = -1;
}

GAB_API inline bool gab_signal(struct gab_triple gab, enum gab_signal s,
                               int wkid) {
  if (wkid == 0)
    return false;

  if (gab_is_signaling(gab))
    if (gab.eg->sig.signal == s)
      return true;

  while (gab_is_signaling(gab))
    switch (gab_yield(gab)) {
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    case sGAB_TERM:
      return false;
    default:
      break;
    };

  if (gab.eg->sig.schedule >= 0)
    return gab.eg->sig.signal == s;

#if cGAB_LOG_EG
  printf("[WORKER %i] SIGNALLING %i\n", gab.wkid, s);
#endif

  gab.eg->sig.signal = s;
  gab_signext(gab, wkid);
  return true;
};


#include "include/core.h"
#include "include/gab.h"
#include <stdatomic.h>
#include <stdio.h>
#include <threads.h>

enum {
  fDONE,
  fWAITING,
  fRUNNING,
};

typedef struct {
  atomic_char status;
  atomic_int rc;
  v_a_char in_queue;
  v_a_char out_queue;
  mtx_t mutex;
  thrd_t thrd;
  thrd_t parent;

  gab_value init;
  struct gab_eg *gab;
} fiber;

fiber *fiber_create(gab_value init) {
  fiber *self = NEW(fiber);

  mtx_init(&self->mutex, mtx_plain);
  v_a_char_create(&self->in_queue, 8);
  v_a_char_create(&self->out_queue, 8);

  struct gab_eg *gab = gab_create();
  gab_argpush(gab, gab_string(gab, ""));

  self->rc = 1;
  self->gab = gab;
  self->init = gab_valcpy(gab, NULL, init);
  self->parent = thrd_current();

  gab_egkeep(self->gab, self->init);

  self->status = fRUNNING;

  return self;
}

void fiber_destroy(fiber *self) {
  mtx_destroy(&self->mutex);
  v_a_char_destroy(&self->in_queue);
  v_a_char_destroy(&self->out_queue);
}

void fiber_dref(fiber *f) {
  f->rc--;
  if (f->rc <= 0) {
    fiber_destroy(f);
    free(f);
  }
}

void fiber_destructor_cb(void *d) {
  fiber *f = (fiber *)d;
  fiber_dref(f);
}

bool callable(gab_value v) {
  return gab_valknd(v) == kGAB_BLOCK || gab_valknd(v) == kGAB_SUSPENSE;
}

gab_value out_queue_pop(struct gab_eg *gab, fiber *f) {
  if (f->out_queue.len < 1)
    return gab_nil;

  a_char *ref = v_a_char_pop(&f->out_queue);

  gab_value v = gab_nstring(gab, ref->len, (char *)ref->data);

  free(ref);

  return v;
}

void out_queue_push(fiber *f, gab_value v) {
  gab_egkeep(f->gab, v);

  s_char ref = gab_valintocs(f->gab, v);

  v_a_char_push(&f->out_queue, a_char_create(ref.data, ref.len));
}

gab_value run(fiber *f, gab_value runnable) {
  // Run the runnable in the engine for the fiber
  a_gab_value *result =
      gab_run(f->gab, runnable, fGAB_DUMP_ERROR | fGAB_EXIT_ON_PANIC);

  mtx_lock(&f->mutex);
  // Push all the values returned into the out_queue
  // Except for the last value, which may be a callable
  // That we need to loop with
  for (int i = 0; i < result->len - 1; i++) {
    out_queue_push(f, result->data[i]);
  }

  gab_value runner = result->data[result->len - 1];

  if (!callable(runner)) {
    out_queue_push(f, runner);
    f->status = fDONE;

    free(result);
    mtx_unlock(&f->mutex);
    return gab_undefined;
  }

  if (result->len == 1) {
    f->status = fWAITING;
  }

  free(result);
  mtx_unlock(&f->mutex);
  return runner;
}

int fiber_launch(void *d) {
  fiber *self = (fiber *)d;
  self->rc++;

  gab_value runner = self->init;

  runner = run(self, runner);

  if (runner == gab_undefined)
    goto fin;

  for (;;) {
    mtx_lock(&self->mutex);

    if (self->in_queue.len == 0) {
      self->status = fWAITING;

      mtx_unlock(&self->mutex);
      thrd_yield();
      continue;
    }

    self->status = fRUNNING;
    a_char *msg = v_a_char_pop(&self->in_queue);

    mtx_unlock(&self->mutex);

    gab_value arg = gab_nstring(self->gab, msg->len, (char *)msg->data);
    free(msg);

    gab_argput(self->gab, arg, 0);

    runner = run(self, runner);

    if (runner == gab_undefined)
      break;

    gab_egkeep(self->gab, runner);
  }

fin:
  mtx_lock(&self->mutex);
  self->status = fDONE;
  mtx_unlock(&self->mutex);

  fiber_dref(self);
  gab_destroy(self->gab);
  return 0;
}

void gab_lib_fiber(struct gab_eg *gab, struct gab_gc *gc, struct gab_vm *vm,
                   size_t argc, gab_value argv[argc]) {
  switch (argc) {
  case 2: {
    if (!callable(argv[1])) {
      gab_panic(gab, vm, "Invalid call to gab_lib_go");
      return;
    }

    fiber *f = fiber_create(argv[1]);

    if (thrd_create(&f->thrd, fiber_launch, f) != thrd_success) {
      gab_panic(gab, vm, "Invalid call to gab_lib_go");
      return;
    }

    thrd_detach(f->thrd); // The fiber will clean itself up

    gab_value fiber = gab_box(
        gab, (struct gab_box_argt){
                 .data = f,
                 .type = gab_gciref(gab, gc, vm, gab_string(gab, "Fiber")),
                 .destructor = fiber_destructor_cb,
             });

    gab_vmpush(vm, fiber);

    gab_gcdref(gab, gc, vm, fiber);
    break;
  }
  default:
    gab_panic(gab, vm, "Invalid call to gab_lib_go");
    return;
  }
}

void gab_lib_send(struct gab_eg *gab, struct gab_gc *gc, struct gab_vm *vm,
                  size_t argc, gab_value argv[argc]) {
  if (argc < 2) {
    gab_panic(gab, vm, "invalid_arguments");
    return;
  }

  fiber *f = (fiber *)gab_boxdata(argv[0]);

  if (f->status == fDONE) {
    gab_panic(gab, vm, "fiber_done");
    return;
  }

  mtx_lock(&f->mutex);
  for (int i = 1; i < argc; i++) {
    gab_value msg = gab_valintos(gab, argv[i]);

    s_char ref = gab_valintocs(gab, msg);

    v_a_char_push(&f->in_queue, a_char_create(ref.data, ref.len));
  }
  mtx_unlock(&f->mutex);

  gab_vmpush(vm, gab_nil);
}

void gab_lib_await(struct gab_eg *gab, struct gab_gc *gc, struct gab_vm *vm,
                   size_t argc, gab_value argv[argc]) {
  fiber *f = gab_boxdata(argv[0]);

  for (;;) {
    mtx_lock(&f->mutex);

    if (f->status == fDONE) {
      gab_vmpush(vm, out_queue_pop(gab, f));
      mtx_unlock(&f->mutex);
      return;
    }

    if (f->status == fWAITING) {
      break;
    }

    if (f->out_queue.len < 1) {
      mtx_unlock(&f->mutex);

      thrd_yield();
      continue;
    }

    break;
  }

  gab_value result = out_queue_pop(gab, f);
  mtx_unlock(&f->mutex);

  gab_vmpush(vm, result);
}

a_gab_value *gab_lib(struct gab_eg *gab, struct gab_gc *gc, struct gab_vm *vm) {
  const char *names[] = {
      "fiber",
      mGAB_CALL,
      "await",
  };

  gab_value receivers[] = {
      gab_nil,
      gab_string(gab, "Fiber"),
      gab_string(gab, "Fiber"),
  };

  gab_value specs[] = {
      gab_sbuiltin(gab, "fiber", gab_lib_fiber),
      gab_sbuiltin(gab, "send", gab_lib_send),
      gab_sbuiltin(gab, "await", gab_lib_await),
  };

  static_assert(LEN_CARRAY(names) == LEN_CARRAY(receivers));
  static_assert(LEN_CARRAY(names) == LEN_CARRAY(specs));

  for (int i = 0; i < LEN_CARRAY(specs); i++) {
    gab_spec(gab, (struct gab_spec_argt){
                      .name = names[i],
                      .receiver = receivers[i],
                      .specialization = specs[i],
                  });
  }

  gab_ngciref(gab, gc, vm, 1, LEN_CARRAY(receivers), receivers);

  return NULL;
}

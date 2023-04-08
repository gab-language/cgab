#ifndef GAB_VM_H
#define GAB_VM_H

#include "gc.h"
#include "object.h"

/*
 * A run-time representation of a callframe.
 */
typedef struct gab_vm_frame {
  gab_obj_block *c;

  /*
   *The instruction pointer.
   *This is stored and loaded by the macros STORE_FRAME and LOAD_FRAME.
   */
  u8 *ip;

  /*
   * The value on the stack where this callframe begins.
   * Locals are offset from this.
   */
  gab_value *slots;
  /*
   * Every call expects a certain number of results.
   * This is set during when the values are called.
   */
  u8 want;
} gab_vm_frame;

/*
 * The gab virtual machine. This has all the state needed for executing bytecode.
 */
typedef struct gab_vm {
  /*
   * The flags passed in to the vm
   */
  u8 flags;

  /*
   * The garbage collector used to cleanup the memory as the vm runs
   */
  gab_gc gc;

  gab_vm_frame *fp;

  gab_value *sp;

  gab_value sb[STACK_MAX];

  gab_vm_frame fb[FRAMES_MAX];
} gab_vm;

void gab_vm_create(gab_vm *vm, u8 flags, u8 argc, gab_value argv[argc]);

void gab_vm_destroy(gab_vm *vm);

gab_value gab_vm_run(gab_engine *gab, gab_value main, u8 flags, u8 argc,
                     gab_value argv[argc]);

gab_value gab_vm_panic(gab_engine *gab, gab_vm *vm, const char *msg);

i32 gab_vm_push(gab_vm* vm, u64 argc, gab_value argv[argc]);

#endif

#include "include/gab.h"
#include <stdio.h>

void file_cb(void *data) { fclose(data); }

void gab_lib_open(gab_engine *gab, gab_vm *vm, u8 argc, gab_value argv[argc]) {
  if (argc != 3 || !GAB_VAL_IS_STRING(argv[1]) || !GAB_VAL_IS_STRING(argv[2])) {
    gab_panic(gab, vm, "&:open expects a path and permissions string");
    return;
  }

  gab_obj_string *path_obj = GAB_VAL_TO_STRING(argv[1]);
  char path[path_obj->len + 1];
  memcpy(path, path_obj->data, sizeof(i8) * path_obj->len);
  path[path_obj->len] = '\0';

  gab_obj_string *perms_obj = GAB_VAL_TO_STRING(argv[2]);
  char perms[perms_obj->len + 1];
  memcpy(perms, perms_obj->data, sizeof(i8) * perms_obj->len);
  perms[perms_obj->len] = '\0';

  FILE *file = fopen(path, perms);
  if (file == NULL) {
    gab_value r = GAB_STRING("FILE_COULD_NOT_OPEN");
    gab_push(vm, r);
    return;
  }

  gab_value result[2] = {
      GAB_STRING("ok"),
      GAB_CONTAINER(GAB_STRING("File"), file_cb, NULL, file),
  };

  gab_vpush(vm, 2, result);

  gab_val_dref(vm, result[1]);
}

void gab_lib_read(gab_engine *gab, gab_vm *vm, u8 argc, gab_value argv[argc]) {
  if (argc != 1 || !GAB_VAL_TO_CONTAINER(argv[0])) {
    gab_panic(gab, vm, "&:read expects a file handle");
    return;
  }

  gab_obj_container *file_obj = GAB_VAL_TO_CONTAINER(argv[0]);

  FILE *file = file_obj->data;

  fseek(file, 0L, SEEK_END);
  size_t fileSize = ftell(file);
  rewind(file);

  char buffer[fileSize];

  size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);

  if (bytesRead < fileSize) {
    gab_push(vm, GAB_STRING("FILE_COULD_NOT_READ"));
    return;
  }

  gab_obj_string *result =
      gab_obj_string_create(gab, s_i8_create((i8 *)buffer + 0, bytesRead));

  gab_value res[2] = {
      GAB_STRING("ok"),
      GAB_VAL_OBJ(result),
  };

  gab_vpush(vm, 2, res);

  gab_val_dref(vm, res[1]);
}

void gab_lib_write(gab_engine *gab, gab_vm *vm, u8 argc, gab_value argv[argc]) {
  if (argc != 2 || !GAB_VAL_IS_CONTAINER(argv[0]) ||
      !GAB_VAL_IS_STRING(argv[1])) {
    gab_panic(gab, vm, "&:write expects a file handle and data string");
  }

  gab_obj_container *handle = GAB_VAL_TO_CONTAINER(argv[0]);

  gab_obj_string *data_obj = GAB_VAL_TO_STRING(argv[1]);
  char data[data_obj->len + 1];
  memcpy(data, data_obj->data, sizeof(i8) * data_obj->len);
  data[data_obj->len] = '\0';

  i32 result = fputs(data, handle->data);

  if (result > 0) {
    gab_push(vm, GAB_STRING("ok"));
    return;
  }

  gab_push(vm, GAB_STRING("FILE_COULD_NOT_WRITE"));
  return;
}

gab_value gab_mod(gab_engine *gab, gab_vm *vm) {
  gab_value keys[] = {
      GAB_STRING("open"),
      GAB_STRING("read"),
      GAB_STRING("write"),
  };

  gab_value type = GAB_STRING("File");

  gab_value receiver_types[] = {
      GAB_VAL_NIL(),
      type,
      type,
  };

  gab_value values[] = {
      GAB_BUILTIN(open),
      GAB_BUILTIN(read),
      GAB_BUILTIN(write),
  };

  for (u8 i = 0; i < 3; i++) {
    gab_specialize(gab, vm, keys[i], receiver_types[i], values[i]);
    gab_val_dref(vm, values[i]);
  }

  return GAB_VAL_NIL();
}

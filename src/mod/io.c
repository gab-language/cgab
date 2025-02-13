#include "core.h"
#include "gab.h"

void file_cb(uint64_t len, char data[static len]) { fclose(*(FILE **)data); }

gab_value iostream(struct gab_triple gab, FILE *stream, bool owning) {
  return gab_box(gab, (struct gab_box_argt){
                          .type = gab_string(gab, tGAB_IOSTREAM),
                          .data = &stream,
                          .size = sizeof(FILE *),
                          .destructor = owning ? file_cb : nullptr,
                          .visitor = nullptr,
                      });
}

a_gab_value *gab_iolib_open(struct gab_triple gab, uint64_t argc,
                            gab_value argv[argc]) {
  gab_value path = gab_arg(0);
  gab_value perm = gab_arg(1);

  if (gab_valkind(path) != kGAB_STRING)
    return gab_pktypemismatch(gab, path, kGAB_STRING);

  if (gab_valkind(perm) != kGAB_STRING)
    return gab_pktypemismatch(gab, perm, kGAB_STRING);

  const char *cpath = gab_strdata(&path);
  const char *cperm = gab_strdata(&perm);

  FILE *stream = fopen(cpath, cperm);

  if (stream == nullptr) {
    gab_vmpush(gab_vm(gab), gab_err, gab_string(gab, strerror(errno)));
    return nullptr;
  }

  gab_vmpush(gab_vm(gab), gab_ok, iostream(gab, stream, true));

  return nullptr;
}

a_gab_value *gab_iolib_until(struct gab_triple gab, uint64_t argc,
                             gab_value argv[argc]) {
  gab_value iostream = gab_arg(0);
  gab_value delim = gab_arg(1);

  if (gab_valkind(iostream) != kGAB_BOX)
    return gab_ptypemismatch(gab, iostream, gab_string(gab, tGAB_IOSTREAM));

  if (delim == gab_nil)
    delim = gab_string(gab, "\n");

  v_char buffer;
  v_char_create(&buffer, 1024);

  FILE *stream = *(FILE **)gab_boxdata(argv[0]);

  for (;;) {
    int c = fgetc(stream);

    if (c == EOF)
      break;

    v_char_push(&buffer, c);

    if (c == '\n')
      break;
  }

  gab_vmpush(gab_vm(gab), gab_ok, gab_nstring(gab, buffer.len, buffer.data));

  return nullptr;
}

a_gab_value *gab_iolib_scan(struct gab_triple gab, uint64_t argc,
                            gab_value argv[argc]) {
  gab_value iostream = gab_arg(0);
  gab_value bytesToRead = gab_arg(1);

  if (gab_valkind(iostream) != kGAB_BOX)
    return gab_ptypemismatch(gab, iostream, gab_string(gab, tGAB_IOSTREAM));

  if (gab_valkind(bytesToRead) != kGAB_NUMBER)
    return gab_pktypemismatch(gab, bytesToRead, kGAB_NUMBER);

  uint64_t bytes = gab_valton(bytesToRead);

  if (bytes == 0) {
    gab_vmpush(gab_vm(gab), gab_string(gab, ""));
    return nullptr;
  }

  char buffer[bytes];

  FILE *stream = *(FILE **)gab_boxdata(argv[0]);

  // Try to read bytes number of bytes
  int bytes_read = fread(buffer, 1, sizeof(buffer), stream);

  if (bytes_read < bytes)
    gab_vmpush(gab_vm(gab), gab_err, gab_string(gab, strerror(errno)));
  else
    gab_vmpush(gab_vm(gab), gab_ok, gab_nstring(gab, bytes_read, buffer));

  return nullptr;
}

a_gab_value *gab_iolib_read(struct gab_triple gab, uint64_t argc,
                            gab_value argv[argc]) {
  if (argc != 1 || gab_valkind(argv[0]) != kGAB_BOX)
    return gab_fpanic(gab, "&:read expects a file handle");

  FILE *file = *(FILE **)gab_boxdata(argv[0]);

  fseek(file, 0L, SEEK_END);
  uint64_t fileSize = ftell(file);
  rewind(file);

  char buffer[fileSize];

  uint64_t bytesRead = fread(buffer, sizeof(char), fileSize, file);

  if (bytesRead < fileSize) {
    gab_vmpush(gab_vm(gab), gab_string(gab, "FILE_COULD_NOT_READ"));
    return nullptr;
  }

  gab_vmpush(gab_vm(gab), gab_ok, gab_nstring(gab, bytesRead, buffer));

  return nullptr;
}

a_gab_value *gab_iolib_write(struct gab_triple gab, uint64_t argc,
                             gab_value argv[argc]) {
  gab_value stream = gab_arg(0);

  if (gab_valkind(stream) != kGAB_BOX)
    return gab_ptypemismatch(gab, stream, gab_string(gab, tGAB_IOSTREAM));

  FILE *fs = *(FILE **)gab_boxdata(stream);

  gab_value str = gab_arg(1);

  if (gab_valkind(str) != kGAB_STRING)
    return gab_pktypemismatch(gab, str, kGAB_STRING);

  const char *data = gab_strdata(&str);

  int32_t result = fputs(data, fs);

  if (result <= 0 || fflush(fs))
    gab_vmpush(gab_vm(gab), gab_err, gab_string(gab, strerror(errno)));
  else
    gab_vmpush(gab_vm(gab), gab_ok);

  return nullptr;
}

GAB_DYNLIB_MAIN_FN {
  gab_value t = gab_string(gab, tGAB_IOSTREAM);

  gab_def(gab,
          {
              gab_message(gab, "open"),
              gab_message(gab, "io"),
              gab_snative(gab, "printf", gab_iolib_open),
          },
          {
              gab_message(gab, "until"),
              t,
              gab_snative(gab, "until", gab_iolib_until),
          },
          {
              gab_message(gab, "read"),
              t,
              gab_snative(gab, "read", gab_iolib_read),
          },
          {
              gab_message(gab, "scan"),
              t,
              gab_snative(gab, "scan", gab_iolib_scan),
          },
          {
              gab_message(gab, "write"),
              t,
              gab_snative(gab, "write", gab_iolib_write),
          });

  return a_gab_value_one(gab_ok);
}

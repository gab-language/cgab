#include "core.h"
#include "gab.h"

void file_cb(struct gab_triple, uint64_t len, char data[static len]) {
  fclose(*(FILE **)data);
}

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
  gab_value path = gab_arg(1);
  gab_value perm = gab_arg(2);

  if (gab_valkind(path) != kGAB_STRING)
    return gab_pktypemismatch(gab, path, kGAB_STRING);

  if (gab_valkind(perm) != kGAB_STRING)
    return gab_pktypemismatch(gab, perm, kGAB_STRING);

  const char *cpath = gab_strdata(&path);
  const char *cperm = gab_strdata(&perm);

  FILE *stream = fopen(cpath, cperm);

  if (stream == nullptr) {
    gab_vmpush(gab_thisvm(gab), gab_err, gab_string(gab, strerror(errno)));
    return nullptr;
  }

  gab_vmpush(gab_thisvm(gab), gab_ok, iostream(gab, stream, true));

  return nullptr;
}

bool osfgetc(struct gab_triple gab, FILE *stream, int *c) {
  for (;;) {
    switch (gab_yield(gab)) {
    case sGAB_TERM:
      return false;
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    default:
      break;
    }

    if (!gab_osfisready(stream))
      continue;

    *c = fgetc(stream);
    return true;
  }
}

int osfread(struct gab_triple gab, FILE *stream, v_char *sb) {
  for (;;) {
    int c = -1;

    if (!osfgetc(gab, stream, &c))
      return -1;

    if (c == EOF)
      return sb->len;

    v_char_push(sb, c);
  }
}

int osnfread(struct gab_triple gab, FILE *stream, size_t n, char *s) {
  int bytes_read = 0;
  for (;;) {
    int c = -1;

    if (!osfgetc(gab, stream, &c))
      return -1;

    if (c == EOF)
      return bytes_read;

    if (bytes_read == n)
      return bytes_read;

    *s++ = c;
    bytes_read++;
  }
}

a_gab_value *gab_iolib_until(struct gab_triple gab, uint64_t argc,
                             gab_value argv[argc]) {
  gab_value iostream = gab_arg(0);
  gab_value delim = gab_arg(1);

  if (gab_valkind(iostream) != kGAB_BOX)
    return gab_ptypemismatch(gab, iostream, gab_string(gab, tGAB_IOSTREAM));

  if (delim == gab_nil)
    delim = gab_binary(gab, "\n");

  if (gab_valkind(delim) != kGAB_BINARY)
    return gab_pktypemismatch(gab, delim, kGAB_BINARY);

  if (gab_strlen(delim) > 1)
    return gab_fpanic(gab, "Expected delimiter '$' to be one byte long", delim);

  char delim_byte = gab_binat(delim, 0);

  v_char buffer;
  v_char_create(&buffer, 1024);

  int c = 0;
  FILE *stream = *(FILE **)gab_boxdata(argv[0]);

  for (;;) {
    if (!osfgetc(gab, stream, &c))
      return nullptr;

    if (c == EOF)
      break;

    v_char_push(&buffer, c);

    if (c == delim_byte)
      break;
  }

  gab_vmpush(gab_thisvm(gab), gab_ok,
             gab_nstring(gab, buffer.len, buffer.data));

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
    gab_vmpush(gab_thisvm(gab), gab_string(gab, ""));
    return nullptr;
  }

  char buffer[bytes];

  FILE *stream = *(FILE **)gab_boxdata(argv[0]);

  // Try to read bytes number of bytes into buffer
  int bytes_read = osnfread(gab, stream, bytes, buffer);

  if (bytes_read < bytes)
    gab_vmpush(gab_thisvm(gab), gab_err, gab_string(gab, strerror(errno)));
  else
    gab_vmpush(gab_thisvm(gab), gab_ok, gab_nstring(gab, bytes_read, buffer));

  return nullptr;
}

a_gab_value *gab_iolib_read(struct gab_triple gab, uint64_t argc,
                            gab_value argv[argc]) {
  if (argc != 1 || gab_valkind(argv[0]) != kGAB_BOX)
    return gab_fpanic(gab, "&:read expects a file handle");

  FILE *stream = *(FILE **)gab_boxdata(argv[0]);

  if (!gab_osfisready(stream)) {
    gab_vmpush(gab_thisvm(gab), gab_err,
               gab_string(gab, "File not ready for reading"));
    return nullptr;
  }

  v_char sb = {0};
  int bytes_read = osfread(gab, stream, &sb);

  if (bytes_read < sb.len) {
    gab_vmpush(gab_thisvm(gab), gab_string(gab, "File was not fully read"));
    return nullptr;
  }

  gab_vmpush(gab_thisvm(gab), gab_ok, gab_nstring(gab, sb.len, sb.data));

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
    gab_vmpush(gab_thisvm(gab), gab_err, gab_string(gab, strerror(errno)));
  else
    gab_vmpush(gab_thisvm(gab), gab_ok);

  return nullptr;
}

GAB_DYNLIB_MAIN_FN {
  gab_value t = gab_string(gab, tGAB_IOSTREAM);

  gab_def(gab,
          {
              gab_message(gab, "t"),
              gab_strtomsg(t),
              t,
          },
          {
              gab_message(gab, "stdin"),
              gab_strtomsg(t),
              iostream(gab, stdin, false),
          },
          {
              gab_message(gab, "stdout"),
              gab_strtomsg(t),
              iostream(gab, stdout, false),
          },
          {
              gab_message(gab, "stderr"),
              gab_strtomsg(t),
              iostream(gab, stderr, false),
          },
          {
              gab_message(gab, "open"),
              gab_strtomsg(t),
              gab_snative(gab, "open", gab_iolib_open),
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

  gab_value res[] = {gab_ok, gab_strtomsg(t)};

  return a_gab_value_create(res, 2);
}

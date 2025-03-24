#include "core.h"
#include "gab.h"
#include <stdint.h>

#ifdef GAB_PLATFORM_UNIX
#define QIO_LINUX
#else
#error Unsupported QIO Platform
#endif
#include "qio/qio.h"

int io_loop_cb(void *initialized) {
  if (qio_init(256) < 0)
    return 1;

  *(bool *)initialized = true;

  if (qio_loop() < 0)
    return qio_destroy(), 1;

  return qio_destroy(), 0;
}

void file_cb(struct gab_triple, uint64_t len, char data[static len]) {
  qfd_t qfd = *(qfd_t *)data;
  // Block for the file to close
  qd_result(qclose(qfd));
}

gab_value wrap_qfd(struct gab_triple gab, qfd_t qd, bool owning) {
  return gab_box(gab, (struct gab_box_argt){
                          .type = gab_string(gab, tGAB_IOSTREAM),
                          .data = &qd,
                          .size = sizeof(qfd_t),
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
  /*const char *cperm = gab_strdata(&perm);*/

  qd_t qd = qopen(cpath);

  while (!qd_status(qd))
    switch (gab_yield(gab)) {
    case sGAB_TERM:
      return nullptr;
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    default:
      break;
    }

  qfd_t qfd = qd_result(qd);

  if (qfd < 0) {
    gab_vmpush(gab_thisvm(gab), gab_err, gab_string(gab, strerror(-qfd)));
    return nullptr;
  }

  gab_vmpush(gab_thisvm(gab), gab_ok, wrap_qfd(gab, qfd, true));

  return nullptr;
}

int64_t osfgetc(struct gab_triple gab, qfd_t qfd, int *c) {
  qd_t qid = qread(qfd, -1, 1, (uint8_t *)c);

  while (!qd_status(qid))
    switch (gab_yield(gab)) {
    case sGAB_TERM:
      return -1;
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    default:
      break;
    }

  return qd_result(qid);
}

/* Awful - read one byte at a time */
int osfread(struct gab_triple gab, qfd_t qfd, v_char *sb) {
  for (;;) {
    int c = -1;

    int64_t res = osfgetc(gab, qfd, &c);

    if (res < 0)
      return res;

    if (c == EOF)
      return sb->len;

    v_char_push(sb, c);
  }
}

int osnfread(struct gab_triple gab, qfd_t qfd, size_t n, uint8_t buf[n]) {
  qd_t qid = qread(qfd, -1, n, buf);

  while (!qd_status(qid))
    switch (gab_yield(gab)) {
    case sGAB_TERM:
      return -1;
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    default:
      break;
    }

  int bytes_read = qd_result(qid);

  if (bytes_read < 0)
    return bytes_read;

  if (bytes_read == 0)
    return strlen((const char *)buf);

  return bytes_read;
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
  qfd_t stream = *(qfd_t *)gab_boxdata(argv[0]);

  for (;;) {
    if (osfgetc(gab, stream, &c) < 0)
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

  uint64_t bytes = gab_valtou(bytesToRead);

  if (bytes == 0) {
    gab_vmpush(gab_thisvm(gab), gab_string(gab, ""));
    return nullptr;
  }

  char buffer[bytes];

  qfd_t stream = *(qfd_t *)gab_boxdata(iostream);

  // Try to read bytes number of bytes into buffer
  int result = osnfread(gab, stream, bytes, (uint8_t *)buffer);

  if (result < bytes)
    gab_vmpush(gab_thisvm(gab), gab_err, gab_string(gab, strerror(-result)));
  else
    gab_vmpush(gab_thisvm(gab), gab_ok, gab_nstring(gab, bytes, buffer));

  return nullptr;
}

a_gab_value *gab_iolib_read(struct gab_triple gab, uint64_t argc,
                            gab_value argv[argc]) {
  if (argc != 1 || gab_valkind(argv[0]) != kGAB_BOX)
    return gab_fpanic(gab, "&:read expects a file handle");

  qfd_t stream = *(qfd_t *)gab_boxdata(argv[0]);

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

  qfd_t fs = *(qfd_t *)gab_boxdata(stream);

  gab_value str = gab_arg(1);

  if (gab_valkind(str) != kGAB_STRING)
    return gab_pktypemismatch(gab, str, kGAB_STRING);

  const char *data = gab_strdata(&str);
  size_t len = gab_strlen(str);

  qd_t qd = qwrite(fs, len, (uint8_t *)data);

  while (!qd_status(qd))
    switch (gab_yield(gab)) {
    case sGAB_TERM:
      return nullptr;
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    default:
      break;
    }

  int64_t result = qd_result(qd);

  if (result <= 0)
    gab_vmpush(gab_thisvm(gab), gab_err, gab_string(gab, strerror(-result)));
  else
    gab_vmpush(gab_thisvm(gab), gab_ok);

  return nullptr;
}

GAB_DYNLIB_MAIN_FN {
  bool initialized = false;

  thrd_t io_t;
  if (thrd_create(&io_t, io_loop_cb, &initialized) != thrd_success)
    return gab_fpanic(gab, "Failed to initialize QIO loop");

  while (!initialized)
    ;

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
              wrap_qfd(gab, gab_osfileno(stdin), false),
          },
          {
              gab_message(gab, "stdout"),
              gab_strtomsg(t),
              wrap_qfd(gab, gab_osfileno(stdout), false),
          },
          {
              gab_message(gab, "stderr"),
              gab_strtomsg(t),
              wrap_qfd(gab, gab_osfileno(stderr), false),
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

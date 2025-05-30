#define QIO_LOOP_INTERVAL_NS 50000
#define QIO_INTERNAL_QUEUE_INITIAL_LEN 2056
#include "qio/qio.h"

#include "gab.h"

int io_loop_cb(void *initialized) {
  _Atomic int *init = initialized;

  int res = qio_init(256);
  if (res < 0)
    return atomic_store(init, res), 1;

  atomic_store(init, 1);

  if (qio_loop() < 0)
    return qio_destroy(), 1;

  return qio_destroy(), 0;
}

void file_cb(struct gab_triple, uint64_t len, char data[static len]) {
  qfd_t qfd = *(qfd_t *)data;
  // Block for the file to close
  qd_destroy(qclose(qfd));
}

void sock_cb(struct gab_triple, uint64_t len, char data[static len]) {
  qfd_t qfd = *(qfd_t *)data;
  // Shutdown here
  qd_destroy(qshutdown(qfd));
  qd_destroy(qclose(qfd));
}

enum io_t {
  IO_,
  IO_FILE,
  IO_SOCK,
};

const char *io_tname[] = {
    [IO_] = tGAB_IO,
    [IO_FILE] = tGAB_IOFILE,
    [IO_SOCK] = tGAB_IOSOCK,
};

const gab_boxdestroy_f io_tdestroy[] = {
    [IO_FILE] = file_cb,
    [IO_SOCK] = sock_cb,
};

gab_value wrap_qfd(struct gab_triple gab, qfd_t qd, enum io_t t, bool owning) {
  return gab_box(gab, (struct gab_box_argt){
                          .type = gab_string(gab, io_tname[t]),
                          .data = &qd,
                          .size = sizeof(qfd_t),
                          .destructor = owning ? io_tdestroy[t] : nullptr,
                          .visitor = nullptr,
                      });
}

union gab_value_pair gab_iolib_open(struct gab_triple gab, uint64_t argc,
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
      return gab_union_cvalid(gab_nil);
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    default:
      break;
    }

  qfd_t qfd = qd_result(qd);
  qd_destroy(qd);

  if (qfd < 0)
    gab_vmpush(gab_thisvm(gab), gab_err, gab_string(gab, strerror(-qfd)));
  else
    gab_vmpush(gab_thisvm(gab), gab_ok, wrap_qfd(gab, qfd, IO_FILE, true));

  return gab_union_cvalid(gab_nil);
}

int64_t osfgetc(struct gab_triple gab, qfd_t qfd, int *c) {
  qd_t qd = qread(qfd, 1, (uint8_t *)c);

  while (!qd_status(qd))
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

  int64_t res = qd_result(qd);
  qd_destroy(qd);

  return res;
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
  qd_t qd = qread(qfd, n, buf);

  while (!qd_status(qd))
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

  int bytes_read = qd_result(qd);
  qd_destroy(qd);

  if (bytes_read < 0)
    return bytes_read;

  if (bytes_read == 0)
    return strlen((const char *)buf);

  return bytes_read;
}

union gab_value_pair gab_iolib_until(struct gab_triple gab, uint64_t argc,
                                     gab_value argv[argc]) {
  gab_value iostream = gab_arg(0);
  gab_value delim = gab_arg(1);

  if (gab_valkind(iostream) != kGAB_BOX)
    return gab_ptypemismatch(gab, iostream, gab_string(gab, tGAB_IOFILE));

  if (delim == gab_nil)
    delim = gab_binary(gab, "\n");

  if (gab_valkind(delim) != kGAB_BINARY)
    return gab_pktypemismatch(gab, delim, kGAB_BINARY);

  if (gab_strlen(delim) > 1)
    return gab_panicf(gab, "Expected delimiter '$' to be one byte long", delim);

  char delim_byte = gab_binat(delim, 0);

  v_char buffer;
  v_char_create(&buffer, 1024);

  int c = 0;
  qfd_t stream = *(qfd_t *)gab_boxdata(argv[0]);

  for (;;) {
    if (osfgetc(gab, stream, &c) < 0)
      return gab_union_cvalid(gab_nil);

    if (c == EOF)
      break;

    v_char_push(&buffer, c);

    if (c == delim_byte)
      break;
  }

  gab_vmpush(gab_thisvm(gab), gab_ok,
             gab_nstring(gab, buffer.len, buffer.data));

  return gab_union_cvalid(gab_nil);
}

union gab_value_pair gab_iolib_scan(struct gab_triple gab, uint64_t argc,
                                    gab_value argv[argc]) {
  gab_value iostream = gab_arg(0);
  gab_value bytesToRead = gab_arg(1);

  if (gab_valkind(iostream) != kGAB_BOX)
    return gab_ptypemismatch(gab, iostream, gab_string(gab, tGAB_IOFILE));

  if (gab_valkind(bytesToRead) != kGAB_NUMBER)
    return gab_pktypemismatch(gab, bytesToRead, kGAB_NUMBER);

  uint64_t bytes = gab_valtou(bytesToRead);

  if (bytes == 0) {
    gab_vmpush(gab_thisvm(gab), gab_string(gab, ""));
    return gab_union_cvalid(gab_nil);
  }

  char buffer[bytes];

  qfd_t stream = *(qfd_t *)gab_boxdata(iostream);

  // Try to read bytes number of bytes into buffer
  int result = osnfread(gab, stream, bytes, (uint8_t *)buffer);

  if (result < bytes)
    gab_vmpush(gab_thisvm(gab), gab_err, gab_string(gab, strerror(-result)));
  else
    gab_vmpush(gab_thisvm(gab), gab_ok, gab_nstring(gab, bytes, buffer));

  return gab_union_cvalid(gab_nil);
}

union gab_value_pair gab_iolib_read(struct gab_triple gab, uint64_t argc,
                                    gab_value argv[argc]) {
  if (argc != 1 || gab_valkind(argv[0]) != kGAB_BOX)
    return gab_panicf(gab, "&:read expects a file handle");

  qfd_t stream = *(qfd_t *)gab_boxdata(argv[0]);

  v_char sb = {0};
  int bytes_read = osfread(gab, stream, &sb);

  if (bytes_read < sb.len) {
    gab_vmpush(gab_thisvm(gab), gab_string(gab, "File was not fully read"));
    return gab_union_cvalid(gab_nil);
  }

  gab_vmpush(gab_thisvm(gab), gab_ok, gab_nstring(gab, sb.len, sb.data));

  return gab_union_cvalid(gab_nil);
}

union gab_value_pair gab_iolib_sock(struct gab_triple gab, uint64_t argc,
                                    gab_value argv[argc]) {
  qd_t qd = qsocket();

  while (!qd_status(qd))
    switch (gab_yield(gab)) {
    case sGAB_TERM:
      return gab_union_cvalid(gab_nil);
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    default:
      break;
    }

  qfd_t qfd = qd_result(qd);
  qd_destroy(qd);

  if (qfd < 0) {
    gab_vmpush(gab_thisvm(gab), gab_err, gab_string(gab, strerror(-qfd)));
    return gab_union_cvalid(gab_nil);
  }

  gab_vmpush(gab_thisvm(gab), gab_ok, wrap_qfd(gab, qfd, IO_SOCK, true));

  return gab_union_cvalid(gab_nil);
}

union gab_value_pair gab_iolib_send(struct gab_triple gab, uint64_t argc,
                                    gab_value argv[argc]) {
  gab_value sock = gab_arg(0);

  if (gab_valkind(sock) != kGAB_BOX)
    return gab_ptypemismatch(gab, sock, gab_string(gab, tGAB_IOSOCK));

  qfd_t fs = *(qfd_t *)gab_boxdata(sock);

  gab_value str = gab_arg(1);

  if (gab_valkind(str) != kGAB_BINARY)
    return gab_pktypemismatch(gab, str, kGAB_BINARY);

  const char *data = gab_strdata(&str);
  size_t len = gab_strlen(str);

  /*
   * See write for a description of retry-loop logic.
   */
  for (;;) {
    qd_t qd = qsend(fs, len, (uint8_t *)data);

    while (!qd_status(qd))
      switch (gab_yield(gab)) {
      case sGAB_TERM:
        return gab_union_cvalid(gab_nil);
      case sGAB_COLL:
        gab_gcepochnext(gab);
        gab_sigpropagate(gab);
        break;
      default:
        break;
      }

    int64_t result = qd_destroy(qd);

    if (result < 0) {
      gab_vmpush(gab_thisvm(gab), gab_err, gab_string(gab, strerror(-result)));
      return gab_union_cvalid(gab_nil);
    }

    if (result >= len) {
      gab_vmpush(gab_thisvm(gab), gab_ok);
      return gab_union_cvalid(gab_nil);
    }

    len -= result;
    data += result;
  }

  return gab_panicf(gab, "Reached unreachable codepath");
}

union gab_value_pair gab_iolib_recv(struct gab_triple gab, uint64_t argc,
                                    gab_value argv[argc]) {
  gab_value sock = gab_arg(0);
  gab_value msglen = gab_arg(1);

  if (gab_valkind(sock) != kGAB_BOX)
    return gab_ptypemismatch(gab, sock, gab_string(gab, tGAB_IOSOCK));

  if (msglen == gab_nil)
    msglen = gab_number(1024);

  if (gab_valkind(msglen) != kGAB_NUMBER)
    return gab_pktypemismatch(gab, sock, msglen);

  if (gab_valtoi(msglen) <= 0)
    return gab_panicf(gab, "Message length must be greater than 0. (not $)",
                      msglen);

  qfd_t fs = *(qfd_t *)gab_boxdata(sock);

  uint8_t buff[gab_valtou(msglen)];
  qd_t qd = qrecv(fs, sizeof(buff), buff);

  while (!qd_status(qd))
    switch (gab_yield(gab)) {
    case sGAB_TERM:
      return gab_union_cvalid(gab_nil);
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    default:
      break;
    }

  int64_t result = qd_result(qd);
  qd_destroy(qd);

  if (result <= 0)
    gab_vmpush(gab_thisvm(gab), gab_err, gab_string(gab, strerror(-result)));
  else
    gab_vmpush(gab_thisvm(gab), gab_ok, gab_nbinary(gab, result, (char *)buff));

  return gab_union_cvalid(gab_nil);
}

union gab_value_pair gab_iolib_connect(struct gab_triple gab, uint64_t argc,
                                       gab_value argv[argc]) {
  gab_value sock = gab_arg(0);
  gab_value port = gab_arg(1);
  gab_value ip = gab_arg(2);

  if (gab_valkind(sock) != kGAB_BOX)
    return gab_ptypemismatch(gab, sock, gab_string(gab, tGAB_IOSOCK));

  if (gab_valkind(port) != kGAB_NUMBER)
    return gab_pktypemismatch(gab, port, kGAB_NUMBER);

  // Use ipv6 loopback address by default
  if (ip == gab_nil)
    ip = gab_string(gab, "::1");

  if (gab_valkind(ip) != kGAB_STRING)
    return gab_pktypemismatch(gab, ip, kGAB_STRING);

  qfd_t fs = *(qfd_t *)gab_boxdata(sock);

  struct qio_addr addr = {};
  if (qio_addrfrom(gab_strdata(&ip), gab_valtou(port), &addr) < 0)
    return gab_vmpush(gab_thisvm(gab), gab_err,
                      gab_string(gab, "Invalid address")),
           gab_union_cvalid(gab_nil);

  qd_t qd = qconnect(fs, &addr);

  while (!qd_status(qd))
    switch (gab_yield(gab)) {
    case sGAB_TERM:
      return gab_union_cvalid(gab_nil);
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    default:
      break;
    }

  int64_t result = qd_result(qd);
  qd_destroy(qd);

  if (result <= 0)
    gab_vmpush(gab_thisvm(gab), gab_err, gab_string(gab, strerror(-result)));
  else
    gab_vmpush(gab_thisvm(gab), gab_ok, );

  return gab_union_cvalid(gab_nil);
}

union gab_value_pair gab_iolib_accept(struct gab_triple gab, uint64_t argc,
                                      gab_value argv[argc]) {
  gab_value sock = gab_arg(0);

  if (gab_valkind(sock) != kGAB_BOX)
    return gab_ptypemismatch(gab, sock, gab_string(gab, tGAB_IOSOCK));

  qfd_t fs = *(qfd_t *)gab_boxdata(sock);

  struct qio_addr addr = {};
  qd_t qd = qaccept(fs, &addr);

  while (!qd_status(qd))
    switch (gab_yield(gab)) {
    case sGAB_TERM:
      return gab_union_cvalid(gab_nil);
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    default:
      break;
    }

  int64_t result = qd_result(qd);
  qd_destroy(qd);

  // TODO: Do something with addr_out here - need a platform-agnostic and
  // thread-safe function in qio for converting addr to string.

  if (result <= 0)
    gab_vmpush(gab_thisvm(gab), gab_err, gab_string(gab, strerror(-result)));
  else
    gab_vmpush(gab_thisvm(gab), gab_ok, wrap_qfd(gab, result, IO_SOCK, true));

  return gab_union_cvalid(gab_nil);
}

union gab_value_pair gab_iolib_listen(struct gab_triple gab, uint64_t argc,
                                      gab_value argv[argc]) {
  gab_value sock = gab_arg(0);
  gab_value backlog = gab_arg(1);

  if (gab_valkind(sock) != kGAB_BOX)
    return gab_ptypemismatch(gab, sock, gab_string(gab, tGAB_IOSOCK));

  if (gab_valkind(backlog) != kGAB_NUMBER)
    return gab_pktypemismatch(gab, backlog, kGAB_NUMBER);

  qfd_t fs = *(qfd_t *)gab_boxdata(sock);

  qd_t qd = qlisten(fs, gab_valtou(backlog));

  while (!qd_status(qd))
    switch (gab_yield(gab)) {
    case sGAB_TERM:
      return gab_union_cvalid(gab_nil);
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    default:
      break;
    }

  int64_t result = qd_destroy(qd);

  if (result <= 0)
    gab_vmpush(gab_thisvm(gab), gab_err, gab_string(gab, strerror(-result)));
  else
    gab_vmpush(gab_thisvm(gab), gab_ok);

  return gab_union_cvalid(gab_nil);
}

union gab_value_pair gab_iolib_bind(struct gab_triple gab, uint64_t argc,
                                    gab_value argv[argc]) {
  gab_value sock = gab_arg(0);
  gab_value port = gab_arg(1);
  gab_value ip = gab_arg(2);

  if (gab_valkind(sock) != kGAB_BOX)
    return gab_ptypemismatch(gab, sock, gab_string(gab, tGAB_IOSOCK));

  if (gab_valkind(port) != kGAB_NUMBER)
    return gab_pktypemismatch(gab, port, kGAB_NUMBER);

  // Use ipv6 loopback address by default
  if (ip == gab_nil)
    ip = gab_string(gab, "::1");

  if (gab_valkind(ip) != kGAB_STRING)
    return gab_pktypemismatch(gab, ip, kGAB_STRING);

  qfd_t fs = *(qfd_t *)gab_boxdata(sock);

  struct qio_addr addr = {};
  if (qio_addrfrom(gab_strdata(&ip), gab_valtou(port), &addr) < 0)
    return gab_vmpush(gab_thisvm(gab), gab_err,
                      gab_string(gab, "Invalid address")),
           gab_union_cvalid(gab_nil);

  qd_t qd = qbind(fs, &addr);

  while (!qd_status(qd))
    switch (gab_yield(gab)) {
    case sGAB_TERM:
      return gab_union_cvalid(gab_nil);
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    default:
      break;
    }

  int64_t result = qd_destroy(qd);

  if (result <= 0)
    gab_vmpush(gab_thisvm(gab), gab_err, gab_string(gab, strerror(-result)));
  else
    gab_vmpush(gab_thisvm(gab), gab_ok);

  return gab_union_cvalid(gab_nil);
}

union gab_value_pair gab_iolib_write(struct gab_triple gab, uint64_t argc,
                                     gab_value argv[argc]) {
  gab_value stream = gab_arg(0);

  if (gab_valkind(stream) != kGAB_BOX)
    return gab_ptypemismatch(gab, stream, gab_string(gab, tGAB_IOFILE));

  qfd_t fs = *(qfd_t *)gab_boxdata(stream);

  gab_value str = gab_arg(1);

  if (gab_valkind(str) != kGAB_STRING)
    return gab_pktypemismatch(gab, str, kGAB_STRING);

  const char *data = gab_strdata(&str);
  size_t len = gab_strlen(str);

  /*
   * For nonblocking IO, we need to wrap our writes in a retry-loop.
   *
   * Try to write *len* of *data* to *fs*.
   *
   * If we get result < 0, the write failed. Bubble up the error.
   * If we got result < len, the write was incomplete. Bump our data ptr and
   * drop the written bytes from len. If we got result >= len (really should
   * just be =, but use >= for safety's sake), write was completed. return ok.
   */
  for (;;) {
    qd_t qd = qwrite(fs, len, (uint8_t *)data);

    while (!qd_status(qd))
      switch (gab_yield(gab)) {
      case sGAB_TERM:
        return gab_union_cvalid(gab_nil);
      case sGAB_COLL:
        gab_gcepochnext(gab);
        gab_sigpropagate(gab);
        break;
      default:
        break;
      }

    int64_t result = qd_destroy(qd);

    if (result < 0) {
      gab_vmpush(gab_thisvm(gab), gab_err, gab_string(gab, strerror(-result)));
      return gab_union_cvalid(gab_nil);
    }

    if (result >= len) {
      gab_vmpush(gab_thisvm(gab), gab_ok);
      return gab_union_cvalid(gab_nil);
    }

    len -= result;
    data += result;
  }

  return gab_panicf(gab, "Reached unreachable codepath");
}

GAB_DYNLIB_MAIN_FN {
  /*
   * This variable needs to be atomic or volatile.
   *
   * Otherwise, the compiler will attempt to optimize it out in this
   * call to thrd_create, and our initialze while loop will wait forever.
   */
  _Atomic int initialized;
  atomic_init(&initialized, 1);

  thrd_t io_t;
  if (thrd_create(&io_t, io_loop_cb, &initialized) != thrd_success)
    return gab_panicf(gab, "Failed to initialize QIO loop");

  while (!atomic_load(&initialized))
    ;

  if (atomic_load(&initialized) < 0)
    return gab_panicf(gab, "Failed to initialize QIO loop");

  assert(initialized == 1);

  gab_value file_t = gab_string(gab, tGAB_IOFILE);
  gab_value sock_t = gab_string(gab, tGAB_IOSOCK);
  gab_value mod = gab_message(gab, tGAB_IO);

  gab_def(gab,
          {
              gab_message(gab, "file.t"),
              mod,
              file_t,
          },
          {
              gab_message(gab, "sock.t"),
              mod,
              sock_t,
          },
          {
              gab_message(gab, "stdin"),
              mod,
              wrap_qfd(gab, gab_osfileno(stdin), IO_FILE, false),
          },
          {
              gab_message(gab, "stdout"),
              mod,
              wrap_qfd(gab, gab_osfileno(stdout), IO_FILE, false),
          },
          {
              gab_message(gab, "stderr"),
              mod,
              wrap_qfd(gab, gab_osfileno(stderr), IO_FILE, false),
          },
          {
              gab_message(gab, "sock"),
              mod,
              gab_snative(gab, "sock", gab_iolib_sock),
          },
          {
              gab_message(gab, "file"),
              mod,
              gab_snative(gab, "file", gab_iolib_open),
          },
          {
              gab_message(gab, "until"),
              file_t,
              gab_snative(gab, "until", gab_iolib_until),
          },
          {
              gab_message(gab, "read"),
              file_t,
              gab_snative(gab, "read", gab_iolib_read),
          },
          {
              gab_message(gab, "scan"),
              file_t,
              gab_snative(gab, "scan", gab_iolib_scan),
          },
          {
              gab_message(gab, "write"),
              file_t,
              gab_snative(gab, "write", gab_iolib_write),
          },
          {
              gab_message(gab, "send"),
              sock_t,
              gab_snative(gab, "send", gab_iolib_send),
          },
          {
              gab_message(gab, "recv"),
              sock_t,
              gab_snative(gab, "recv", gab_iolib_recv),
          },
          {
              gab_message(gab, "accept"),
              sock_t,
              gab_snative(gab, "accept", gab_iolib_accept),
          },
          {
              gab_message(gab, "listen"),
              sock_t,
              gab_snative(gab, "listen", gab_iolib_listen),
          },
          {
              gab_message(gab, "bind"),
              sock_t,
              gab_snative(gab, "bind", gab_iolib_bind),
          },
          {
              gab_message(gab, "connect"),
              sock_t,
              gab_snative(gab, "connect", gab_iolib_connect),
          });

  gab_value res[] = {gab_ok, mod};

  return (union gab_value_pair){
      .status = gab_cvalid,
      .aresult = a_gab_value_create(res, sizeof(res) / sizeof(gab_value)),
  };
}

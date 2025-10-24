#include "BearSSL/inc/bearssl.h"
#include "core.h"

#define QIO_LOOP_INTERVAL_NS 50000
#define QIO_INTERNAL_QUEUE_INITIAL_LEN 2056
#include "qio/qio.h"

#include "ta.h"

#include "gab.h"
#include "platform.h"

/*
 * MASSIVE TODO:
 * Formal connection implementation.
 * Instead of a little hard-coding of socket/connect/bind, lets do something a
 * little neater.
 *
 * IO.sock(tcp:)
 * IO.sock(tcp\ssl:)
 * IO.sock(udp:)
 * IO.sock(udp\ssl:)
 *
 * ^^ The issue with the above interface is that the state for TLS connections
 * (BearSSL stuff) is allocated when we see the user wants a tls socket. For
 * Servers (which are sockets that "bind") There isn't actually any TLS state
 * needed in that socket. All the memory is *per connection*, and so can be
 * stored on the 'client' (sockets returned by 'accept').
 *
 * More useful to provide the IO.bind() and IO.connect() Interface, which will
 * know how much memory is actually needed.
 *
 * IO.bind(tcp: "::1" 8080) -> Server socket with TCP protocol
 * IO.bind(udp: "::1" 8080) -> Server socket with TCP protocol
 * IO.bind(http: 8080) -> Server socket with HTTP, localhost by default
 * IO.bind(https: "mydomain" 8080) -> Server socket with HTTPS, localhost by
 * default
 *
 * IO.connect(tcp: 8080) -> Client socket with TCP on localhost
 * IO.connect(https: "google.com") -> Client socket with HTTPS to google.com
 *  -> Follow up with:
 *    socket.write("GET / HTTP/1.0\r\nConnection: close\r\nHost:
 * google.com\r\n\r\n") socket.read() -> Should get us back a web page
 *
 * Each of these sockets should have a different type - not just be io\sock
 *
 * io\sock\tcp\client
 * io\sock\tcp\server
 *
 * io\sock\udp\client
 * io\sock\udp\server
 *
 */

/*
 * MASSIVE TODO:
 * Is it unsafe to directly send pointers into gab strings to the io syscalls?
 *
 * I ask because all the IO is happening asynchronously,
 * it may be that the engine is freed and exiting while IO operations are still queued.
 *
 * What if the engine closes std/in/out/err before destroying the engine?
 *
 * Is there a better way to wait for IO to 'settle'?
 */

#define BR_SSL_WRITE_INCOMPLETE ((int64_t)1 << 31)

enum {
  BR_SSL_SENDREC_CHANNEL,
  BR_SSL_RECVREC_CHANNEL,
};

enum gab_io_k {
  IO_,
  IO_FILE,
  IO_SOCK_UNSPECIFIED,
  IO_SOCK_SSLUNSPECIFIED,
  IO_SOCK_CLIENT,
  IO_SOCK_SERVER,
  IO_SOCK_SSLCLIENT,
  IO_SOCK_SSLSERVER,
};

/*
 * Generic struct that all IO objects have at the top
 * of their definition. This way, a (struct gab_io*) is valid
 * to point to any io object, and the k can be used to
 * determine the rest of the structure.
 */
struct gab_io {
  qfd_t fd;
  _Atomic enum gab_io_k k;
};

#define BUFFER_SIZE (1 << 15)

/*
 * Wrap a file on disk, or std in/out/err.
 * Reads are buffered.
 *
 * Maybe the mutex can be dropped in favor of
 * atomic bfront, bback on the queue.
 */
struct gab_file {
  struct gab_io io;

  mtx_t mtx;

  uint16_t bfront, bback;
  unsigned char buffer[BUFFER_SIZE];
};

/*
 * Wrap an OS socket.
 *
 * Reads are buffered. See above.
 *
 * The *addr* struct is used for connect, accept, listen, etc.
 * It needs to be on the struct here so that the memory stays alive
 * as the functions yield back to the runtime.
 */
struct gab_sock {
  struct gab_io io;

  struct qio_addr addr;

  mtx_t mtx;

  uint16_t bfront, bback;
  unsigned char buffer[BUFFER_SIZE];
};

/*
 * Wrap an OS socket, with added transport-layer-security.
 */
struct gab_ssl_sock {
  struct gab_io io;

  struct qio_addr addr;

  mtx_t mtx;

  qd_t io_operations[2];

  br_sslio_context ioc;

  union {
    struct {
      br_ssl_client_context cc;
      br_x509_minimal_context xc;
      unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];
    } client;

    struct {
      br_ssl_server_context sc;
      unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];
    } server;
  };
};

// This function runs in the gc thread, and only during a gc. we can block.
void file_cb(struct gab_triple, uint64_t len, char data[static len]) {
  qfd_t qfd = *(qfd_t *)data;
  // Block for the file to close
  qd_destroy(qclose(qfd));
}

// This function runs in the gc thread, and only during a gc. we can block.
void sock_cb(struct gab_triple, uint64_t len, char data[static len]) {
  qfd_t qfd = *(qfd_t *)data;
  // Shutdown here
  qd_destroy(qshutdown(qfd));
  qd_destroy(qclose(qfd));
}

// Some convenient tables which map the IO kind t osome other data.

const char *io_tname[] = {
    [IO_] = tGAB_IO,
    [IO_FILE] = tGAB_IOFILE,
    [IO_SOCK_UNSPECIFIED] = tGAB_IOSOCK,
    [IO_SOCK_CLIENT] = tGAB_IOSOCK,
    [IO_SOCK_SERVER] = tGAB_IOSOCK,
    [IO_SOCK_SSLUNSPECIFIED] = tGAB_IOSOCK,
    [IO_SOCK_SSLCLIENT] = tGAB_IOSOCK,
    [IO_SOCK_SSLSERVER] = tGAB_IOSOCK,
};

const gab_boxdestroy_f io_tdestroy[] = {
    [IO_FILE] = file_cb,           [IO_SOCK_UNSPECIFIED] = sock_cb,
    [IO_SOCK_CLIENT] = sock_cb,    [IO_SOCK_SERVER] = sock_cb,
    [IO_SOCK_SSLCLIENT] = sock_cb, [IO_SOCK_SSLSERVER] = sock_cb,
};

gab_value wrap_qfdfd(struct gab_triple gab, qfd_t qd, enum gab_io_k t,
                     bool owning) {
  gab_value vbox =
      gab_box(gab, (struct gab_box_argt){
                       .type = gab_string(gab, io_tname[t]),
                       .size = sizeof(struct gab_file),
                       .destructor = owning ? io_tdestroy[t] : nullptr,
                   });

  struct gab_file *f = gab_boxdata(vbox);
  f->io.k = t;
  f->io.fd = qd;
  f->bfront = 0;
  f->bback = 0;

  return vbox;
}
gab_value wrap_qfdsock(struct gab_triple gab, qfd_t qd, enum gab_io_k t,
                       bool owning) {
  gab_value vbox =
      gab_box(gab, (struct gab_box_argt){
                       .type = gab_string(gab, io_tname[t]),
                       .size = sizeof(struct gab_sock),
                       .destructor = owning ? io_tdestroy[t] : nullptr,
                   });

  struct gab_sock *sk = gab_boxdata(vbox);
  sk->io.k = t;
  sk->io.fd = qd;
  sk->bfront = 0;
  sk->bback = 0;

  return vbox;
}

gab_value wrap_qfdsockssl(struct gab_triple gab, qfd_t qd, enum gab_io_k t,
                          bool owning) {
  gab_value vbox =
      gab_box(gab, (struct gab_box_argt){
                       .type = gab_string(gab, io_tname[t]),
                       .size = sizeof(struct gab_ssl_sock),
                       .destructor = owning ? io_tdestroy[t] : nullptr,
                   });

  struct gab_ssl_sock *sk = gab_boxdata(vbox);
  sk->io.k = t;
  sk->io.fd = qd;
  sk->io_operations[0] = -1;
  sk->io_operations[1] = -1;

  return vbox;
}

typedef gab_value (*wrap_fn)(struct gab_triple, qfd_t, enum gab_io_k, bool);

/*
 * Should do one (or some other fixed number) of *steps* of this loop per check.
 * When resuming a send
 */
static int run_until(struct gab_ssl_sock *sock, unsigned target) {
  br_sslio_context *ctx = &sock->ioc;

  for (;;) {
    unsigned state;

    state = br_ssl_engine_current_state(ctx->engine);
    if (state & BR_SSL_CLOSED)
      return -1;

    if (sock->io_operations[BR_SSL_SENDREC_CHANNEL] >= 0) {
      /*
       * We are already sending a record for this
       */
      qd_t qd = sock->io_operations[BR_SSL_SENDREC_CHANNEL];

      if (!qd_status(qd))
        return target;

      int wlen = qd_destroy(qd);

      if (wlen < 0)
        return br_ssl_engine_close(ctx->engine), -1;

      if (wlen > 0)
        br_ssl_engine_sendrec_ack(ctx->engine, wlen);

      sock->io_operations[BR_SSL_SENDREC_CHANNEL] = -1;
      continue;
    }

    if (sock->io_operations[BR_SSL_RECVREC_CHANNEL] >= 0) {
      qd_t qd = sock->io_operations[BR_SSL_RECVREC_CHANNEL];

      if (!qd_status(qd))
        return target;

      int wlen = qd_destroy(qd);

      if (wlen <= 0)
        return br_ssl_engine_close(ctx->engine), -1;

      if (wlen > 0)
        br_ssl_engine_recvrec_ack(ctx->engine, wlen);

      sock->io_operations[BR_SSL_RECVREC_CHANNEL] = -1;
      continue;
    }

    /*
     * If there is some record data to send, do it. This takes
     * precedence over everything else.
     */
    if (state & BR_SSL_SENDREC) {
      size_t len;
      unsigned char *buf = br_ssl_engine_sendrec_buf(ctx->engine, &len);

      qd_t qd = qsend(sock->io.fd, len, buf);
      sock->io_operations[BR_SSL_SENDREC_CHANNEL] = qd;

      return target;
    }

    /*
     * If we reached our target, then we are finished.
     */
    if (state & target)
      return 0;

    /*
     * If some application data must be read, and we did not
     * exit, then this means that we are trying to write data,
     * and that's not possible until the application data is
     * read. This may happen if using a shared in/out buffer,
     * and the underlying protocol is not strictly half-duplex.
     * This is unrecoverable here, so we report an error.
     */
    if (state & BR_SSL_RECVAPP)
      return -1;

    /*
     * If we reached that point, then either we are trying
     * to read data and there is some, or the engine is stuck
     * until a new record is obtained.
     */
    if (state & BR_SSL_RECVREC) {
      size_t len;
      unsigned char *buf = br_ssl_engine_recvrec_buf(ctx->engine, &len);

      qd_t qd = qrecv(sock->io.fd, len, buf);
      sock->io_operations[BR_SSL_RECVREC_CHANNEL] = qd;

      return target;
    }

    /*
     * We can reach that point if the target RECVAPP, and
     * the state contains SENDAPP only. This may happen with
     * a shared in/out buffer. In that case, we must flush
     * the buffered data to "make room" for a new incoming
     * record.
     */
    br_ssl_engine_flush(ctx->engine, 0);
  }
}

typedef struct {
  int amount;
  int status;
} io_op_res;

io_op_res sslio_read_available(struct gab_triple gab, struct gab_ssl_sock *sock,
                               s_char *out, uint64_t len) {
  br_sslio_context *ctx = &sock->ioc;

  int res = run_until(sock, BR_SSL_RECVAPP);

  /*
   * On a non-zero return value from run_until, we either:
   *  1. saw an error (-1)
   *  2. need to yield, io is in progress (1)
   */
  if (res)
    return (io_op_res){.status = res};

  size_t alen;
  unsigned char *buf = br_ssl_engine_recvapp_buf(ctx->engine, &alen);

  if (alen < len)
    len = alen;

  out->len = len;
  out->data = (char *)buf;

  br_ssl_engine_recvapp_ack(ctx->engine, len);

  return (io_op_res){.amount = len};
}

io_op_res sslio_write(struct gab_ssl_sock *sock, const void *src, size_t len) {
  br_sslio_context *ctx = &sock->ioc;

  if (len == 0)
    return (io_op_res){0};

  int res = run_until(sock, BR_SSL_SENDAPP);

  /*
   * On a non-zero return value from run_until, we either:
   *  1. saw an error (-1)
   *  2. need to yield, io is in progress (1)
   */
  if (res)
    return (io_op_res){.status = res};

  size_t alen;
  unsigned char *buf = br_ssl_engine_sendapp_buf(ctx->engine, &alen);

  if (alen > len)
    alen = len;

  memcpy(buf, src, alen);
  br_ssl_engine_sendapp_ack(ctx->engine, alen);

  return (io_op_res){.amount = alen};
}

io_op_res sslio_write_all(struct gab_ssl_sock *sock, const void *src,
                          size_t len) {
  const unsigned char *buf = src;

  size_t wantlen = len;

  while (len > 0) {
    io_op_res result = sslio_write(sock, buf, len);

    if (result.status)
      return (io_op_res){.status = result.status, .amount = wantlen - len};

    buf += result.amount;
    len -= (size_t)result.amount;
  }

  return (io_op_res){0};
}

int sslio_flush(struct gab_ssl_sock *sock) {
  br_ssl_engine_flush(sock->ioc.engine, 0);
  return run_until(sock, BR_SSL_SENDAPP | BR_SSL_RECVAPP);
}

gab_value complete_sockcreate(struct gab_triple gab, qd_t socket_qd, wrap_fn fn,
                              enum gab_io_k k) {
  while (!qd_status(socket_qd))
    switch (gab_yield(gab)) {
    case sGAB_TERM:
      return gab_cundefined;
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    default:
      break;
    }

  qfd_t qfd = qd_destroy(socket_qd);

  if (qfd < 0)
    return gab_string(gab, strerror(-qfd));

  return fn(gab, qfd, k, true);
}
union gab_value_pair resume_sslsockaccept(struct gab_triple gab,
                                          struct gab_ssl_sock *sock,
                                          uintptr_t reentrant) {
  if (!qd_status(reentrant - 1))
    return gab_union_ctimeout(reentrant);

  int64_t result = qd_destroy(reentrant - 1);

  if (result < 0) {
    gab_vmpush(gab_thisvm(gab), gab_err, gab_string(gab, strerror(-result)));
    return gab_union_cvalid(gab_nil);
  }

  gab_value vclient = wrap_qfdsockssl(gab, result, IO_SOCK_SSLCLIENT, true);
  struct gab_ssl_sock *client = gab_boxdata(vclient);

  // TODO: Somehow initialize RSA and chains here?
  // br_ssl_server_init_full_rsa(&client->client.sc, CHAIN, CHAIN_LEN, &RSA);

  br_ssl_engine_set_buffer(&client->server.sc.eng, client->server.iobuf,
                           sizeof client->server.iobuf, 1);

  /*
   * Reset the server context, for a new handshake.
   */
  br_ssl_server_reset(&client->server.sc);

  /*
   * Initialize this with nullptrs for all the callbacks, as we write a custom
   * engine.
   */
  br_sslio_init(&sock->ioc, &sock->client.cc.eng, nullptr, nullptr, nullptr,
                nullptr);

  gab_vmpush(gab_thisvm(gab), gab_ok, vclient);
  return gab_union_cvalid(gab_nil);
}

union gab_value_pair complete_sslsockaccept(struct gab_triple gab,
                                            struct gab_ssl_sock *sock) {
  qd_t qd = qaccept(sock->io.fd, &sock->addr);
  return gab_union_ctimeout(qd + 1);
};

union gab_value_pair resume_sockaccept(struct gab_triple gab,
                                       struct gab_sock *sock,
                                       uintptr_t reentrant) {
  if (!qd_status(reentrant - 1))
    return gab_union_ctimeout(reentrant);

  int64_t result = qd_destroy(reentrant - 1);

  if (result < 0)
    gab_vmpush(gab_thisvm(gab), gab_err, gab_string(gab, strerror(-result)));
  else
    gab_vmpush(gab_thisvm(gab), gab_ok,
               wrap_qfdsock(gab, result, IO_SOCK_CLIENT, true));

  return gab_union_cvalid(gab_nil);
}

union gab_value_pair complete_sockaccept(struct gab_triple gab,
                                         struct gab_sock *sock) {
  qd_t qd = qaccept(sock->io.fd, &sock->addr);
  return gab_union_ctimeout(qd + 1);
};

union gab_value_pair resume_sockbind(struct gab_triple gab,
                                     struct gab_sock *sock,
                                     uintptr_t reentrant) {

  if (!qd_status(reentrant - 1))
    return gab_union_ctimeout(reentrant);

  int64_t result = qd_destroy(reentrant - 1);

  if (result < 0) {
    atomic_store(&sock->io.k, IO_SOCK_UNSPECIFIED);
    gab_vmpush(gab_thisvm(gab), gab_err, gab_string(gab, strerror(-result)));
  } else {
    gab_vmpush(gab_thisvm(gab), gab_ok);
  }

  return gab_union_cvalid(gab_nil);
};

union gab_value_pair complete_sockbind(struct gab_triple gab,
                                       struct gab_sock *sock,
                                       const char *hostname, gab_int port) {
  if (qio_addrfrom(hostname, port, &sock->addr) < 0) {
    atomic_store(&sock->io.k, IO_SOCK_UNSPECIFIED);
    return gab_vmpush(gab_thisvm(gab), gab_err,
                      gab_string(gab, "Invalid address")),
           gab_union_cvalid(gab_nil);
  }

  qd_t qd = qbind(sock->io.fd, &sock->addr);
  return gab_union_ctimeout(qd + 1);
};

union gab_value_pair resume_sockconnect(struct gab_triple gab,
                                        struct gab_sock *sock,
                                        uintptr_t reentrant) {
  if (!qd_status(reentrant - 1))
    return gab_union_ctimeout(reentrant);

  int64_t result = qd_destroy(reentrant - 1);

  if (result < 0) {
    atomic_store(&sock->io.k, IO_SOCK_UNSPECIFIED);
    return gab_vmpush(gab_thisvm(gab), gab_err,
                      gab_string(gab, strerror(-result))),
           gab_union_cvalid(gab_nil);
  }

  return gab_vmpush(gab_thisvm(gab), gab_ok), gab_union_cvalid(gab_nil);
}

union gab_value_pair complete_sockconnect(struct gab_triple gab,
                                          struct gab_sock *sock,
                                          const char *hostname, gab_int port) {
  if (qio_addrfrom(hostname, port, &sock->addr) < 0) {
    atomic_store(&sock->io.k, IO_SOCK_UNSPECIFIED);
    return gab_vmpush(gab_thisvm(gab), gab_err,
                      gab_string(gab, "Invalid address")),
           gab_union_cvalid(gab_nil);
  }

  qd_t qd = qconnect(sock->io.fd, &sock->addr);
  return gab_union_ctimeout(qd + 1);
}

#define BUFFER_MASK (BUFFER_SIZE - 1)
#define buffer_back(b) (b->bback & BUFFER_MASK)
#define buffer_front(b) (b->bfront & BUFFER_MASK)

#define buffer_data(b) (b->buffer + buffer_front(b))
#define buffer_datasplit(b) (buffer_front(b) > buffer_back(b))
#define buffer_len(b)                                                          \
  (buffer_datasplit(b) ? BUFFER_SIZE - buffer_front(b) : b->bback - b->bfront)

#define buffer_space(b) (b->buffer + buffer_back(b))
#define buffer_spacesplit(b) (buffer_back(b) > buffer_front(b))
#define buffer_avail(b) (BUFFER_SIZE - buffer_back(b))

union gab_value_pair complete_filerecv(struct gab_triple gab,
                                       struct gab_file *file, gab_uint len,
                                       s_char *out) {
  gab_uint available = buffer_len(file);

  if (!len || len > available)
    len = available;

  if (!len) {
    // Yield while we queue up a read
    qd_t qd = qread(file->io.fd, buffer_avail(file), buffer_space(file));
    return gab_union_ctimeout(qd + 1);
  }

  out->len = len;
  out->data = (char *)buffer_data(file);

  /* mark this data as consumed */
  file->bfront += len;
  return gab_union_cvalid(gab_nil);
}

union gab_value_pair resume_filerecv(struct gab_triple gab,
                                     struct gab_file *file, gab_uint len,
                                     s_char *out, uintptr_t reentrant) {
  if (!qd_status(reentrant - 1))
    return gab_union_ctimeout(reentrant);

  int64_t res = qd_destroy(reentrant - 1);

  if (res < 0)
    return gab_vmpush(gab_thisvm(gab), gab_err,
                      gab_string(gab, strerror(-res))),
           gab_union_cvalid(gab_nil);

  file->bback += res;
  return complete_filerecv(gab, file, len, out);
}

union gab_value_pair complete_sockrecv(struct gab_triple gab,
                                       struct gab_sock *sock, gab_uint len,
                                       s_char *out) {
  gab_uint available = buffer_len(sock);

  if (!len || len > available)
    len = available;

  if (!len) {
    // Yield while we queue up a read
    qd_t qd = qrecv(sock->io.fd, buffer_avail(sock), buffer_space(sock));
    return gab_union_ctimeout(qd + 1);
  }

  out->len = len;
  out->data = (char *)buffer_data(sock);

  /* mark this data as consumed */
  sock->bfront += len;
  return gab_union_cvalid(gab_nil);
}

union gab_value_pair resume_sockrecv(struct gab_triple gab,
                                     struct gab_sock *sock, gab_uint len,
                                     s_char *out, uintptr_t reentrant) {
  if (!qd_status(reentrant - 1))
    return gab_union_ctimeout(reentrant);

  int64_t res = qd_destroy(reentrant - 1);

  if (res < 0)
    return gab_vmpush(gab_thisvm(gab), gab_err,
                      gab_string(gab, strerror(-res))),
           gab_union_cvalid(gab_nil);

  if (res == 0)
    return gab_union_cvalid(gab_nil);

  sock->bback += res;

  return complete_sockrecv(gab, sock, len, out);
}

union gab_value_pair resume_sslsockrecv(struct gab_triple gab,
                                        struct gab_ssl_sock *sock, gab_uint len,
                                        s_char *out) {
  io_op_res result = sslio_read_available(gab, sock, out, len);

  if (result.status < 0) {
    // TODO: Handle an SSL error gracefully
    // int err = br_ssl_engine_last_error(&sock->client.cc.eng);
    //
    // if (err)
    //   gab_vmpush(gab_thisvm(gab), gab_string(gab, "Internal SSL Error"));

    return gab_union_cvalid(gab_nil);
  }

  if (result.status > 0)
    return gab_union_ctimeout(result.status);

  return gab_union_cvalid(gab_nil);
}

union gab_value_pair complete_sslsockrecv(struct gab_triple gab,
                                          struct gab_ssl_sock *sock,
                                          gab_uint len, s_char *out) {
  return resume_sslsockrecv(gab, sock, len, out);
}

union gab_value_pair resume_sslsocksend(struct gab_triple gab,
                                        struct gab_ssl_sock *sock,
                                        const char *data, gab_uint len,
                                        uintptr_t reentrant) {
  if (reentrant & BR_SSL_WRITE_INCOMPLETE) {
    // we didn't finish writing this amount.
    int64_t written = reentrant >> 32;

    assert(written < len);

    io_op_res result = sslio_write_all(sock, data + written, len - written);

    if (result.status > 0) {
      int64_t amount = (written + result.amount) << 32;
      int64_t tag = (int64_t)result.status | BR_SSL_WRITE_INCOMPLETE;
      return gab_union_ctimeout(amount | tag);
    }

    if (result.status < 0) {
      int err = br_ssl_engine_last_error(&sock->client.cc.eng);
      gab_vmpush(gab_thisvm(gab), gab_err,
                 gab_string(gab, "Error during write"), gab_number(err));
      return gab_union_cvalid(gab_nil);
    }

    // This may yield as the engine runs until it is send/recv able.
    int res = sslio_flush(sock);

    if (res < 0) {
      gab_vmpush(gab_thisvm(gab), gab_err,
                 gab_string(gab, "Error during flush"));
      return gab_union_cvalid(gab_nil);
    }

    if (res > 0) {
      return gab_union_ctimeout(res);
    }

    gab_vmpush(gab_thisvm(gab), gab_ok);
    return gab_union_cvalid(gab_nil);
  }

  int res = run_until(sock, reentrant);

  if (res < 0) {
    gab_vmpush(gab_thisvm(gab), gab_err, gab_string(gab, "Error during flush"));
    return gab_union_cvalid(gab_nil);
  }

  if (res > 0) {
    assert(res == reentrant);
    return gab_union_ctimeout(res);
  }

  gab_vmpush(gab_thisvm(gab), gab_ok);
  return gab_union_cvalid(gab_nil);
}

union gab_value_pair complete_sslsocksend(struct gab_triple gab,
                                          struct gab_ssl_sock *sock,
                                          const char *data, gab_uint len) {
  // This may yield as the ssl_engine may need to flush out (send) records
  // in order to make room in the buffer for this write.
  io_op_res result = sslio_write_all(sock, data, len);

  if (result.status > 0) {
    int64_t amount = (int64_t)result.amount << 32;
    int64_t tag = (int64_t)result.status | BR_SSL_WRITE_INCOMPLETE;
    return gab_union_ctimeout(amount | tag);
  }

  if (result.status < 0) {
    int err = br_ssl_engine_last_error(&sock->client.cc.eng);
    gab_vmpush(gab_thisvm(gab), gab_err, gab_string(gab, "Error during write"),
               gab_number(err));
    return gab_union_cvalid(gab_nil);
  }

  // This may yield as the engine runs until it is send/recv able.
  int res = sslio_flush(sock);

  if (res < 0) {
    gab_vmpush(gab_thisvm(gab), gab_err, gab_string(gab, "Error during flush"));
    return gab_union_cvalid(gab_nil);
  }

  if (res > 0) {
    return gab_union_ctimeout(res);
  }

  gab_vmpush(gab_thisvm(gab), gab_ok);
  return gab_union_cvalid(gab_nil);
}

union gab_value_pair resume_sslsockconnect(struct gab_triple gab,
                                           struct gab_ssl_sock *sock,
                                           const char *hostname,
                                           uintptr_t reentrant) {
  if (!qd_status(reentrant - 1))
    return gab_union_ctimeout(reentrant);

  int64_t result = qd_destroy(reentrant - 1);

  if (result < 0) {
    // When the connect, fails, reset the socket
    atomic_store(&sock->io.k, IO_SOCK_SSLUNSPECIFIED);
    return gab_vmpush(gab_thisvm(gab), gab_err,
                      gab_string(gab, strerror(-result))),
           gab_union_cvalid(gab_nil);
  }

  /*
   * Initialise the client context:
   * -- Use the "full" profile (all supported algorithms).
   * -- The provided X.509 validation engine is initialised, with
   *    the hardcoded trust anchor.
   */
  br_ssl_client_init_full(&sock->client.cc, &sock->client.xc, TAs, TAs_NUM);

  /*
   * Set the I/O buffer to the provided array. We allocated a
   * buffer large enough for full-duplex behaviour with all
   * allowed sizes of SSL records, hence we set the last argument
   * to 1 (which means "split the buffer into separate input and
   * output areas").
   */
  br_ssl_engine_set_buffer(&sock->client.cc.eng, sock->client.iobuf,
                           sizeof(sock->client.iobuf), true);

  /*
   * Reset the client context, for a new handshake. We provide the
   * target host name: it will be used for the SNI extension. The
   * last parameter is 0: we are not trying to resume a session.
   */
  br_ssl_client_reset(&sock->client.cc, hostname, 0);

  /*
   * Initialize this with nullptrs for all the callbacks, as we write a custom
   * engine.
   */
  br_sslio_init(&sock->ioc, &sock->client.cc.eng, nullptr, nullptr, nullptr,
                nullptr);

  return gab_vmpush(gab_thisvm(gab), gab_ok), gab_union_cvalid(gab_nil);
}

union gab_value_pair complete_sslsockconnect(struct gab_triple gab,
                                             struct gab_ssl_sock *sock,
                                             const char *hostname,
                                             gab_int port) {
  // We immediately yield, so this becomes corrupted immediately.
  if (qio_addrfrom(hostname, port, &sock->addr) < 0) {
    // When the connect, fails, reset the socket
    atomic_store(&sock->io.k, IO_SOCK_SSLUNSPECIFIED);
    return gab_vmpush(gab_thisvm(gab), gab_err,
                      gab_string(gab, "Invalid address")),
           gab_union_cvalid(gab_nil);
  }

  qd_t qd = qconnect(sock->io.fd, &sock->addr);
  return gab_union_ctimeout(qd + 1);
};

gab_value create_tcp(struct gab_triple gab) {
  return complete_sockcreate(gab, qsocket(QSOCK_TCP), wrap_qfdsock,
                             IO_SOCK_UNSPECIFIED);
}

gab_value create_udp(struct gab_triple gab) {
  return complete_sockcreate(gab, qsocket(QSOCK_UDP), wrap_qfdsock,
                             IO_SOCK_UNSPECIFIED);
}

gab_value create_tcpssl(struct gab_triple gab) {
  return complete_sockcreate(gab, qsocket(QSOCK_TCP), wrap_qfdsockssl,
                             IO_SOCK_SSLUNSPECIFIED);
}

gab_value create_udpssl(struct gab_triple gab) {
  return complete_sockcreate(gab, qsocket(QSOCK_UDP), wrap_qfdsockssl,
                             IO_SOCK_SSLUNSPECIFIED);
}

GAB_DYNLIB_NATIVE_FN(io, open) {
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
    gab_vmpush(gab_thisvm(gab), gab_ok, wrap_qfdsock(gab, qfd, IO_FILE, true));

  return gab_union_cvalid(gab_nil);
}
GAB_DYNLIB_NATIVE_FN(io, sock) {
  gab_value type = gab_arg(1);
  if (gab_valkind(type) != kGAB_MESSAGE)
    return gab_pktypemismatch(gab, type, kGAB_MESSAGE);

  gab_value sock = gab_cinvalid;
  if (type == gab_message(gab, "tcp")) {
    sock = create_tcp(gab);
  } else if (type == gab_message(gab, "tcp\\tls")) {
    sock = create_tcpssl(gab);
  } else if (type == gab_message(gab, "udp")) {
    sock = create_udp(gab);
  } else if (type == gab_message(gab, "udp\\tls")) {
    sock = create_udpssl(gab);
  } else {
    return gab_panicf(gab, "Unknown socket type $", type);
  }

  if (sock == gab_cundefined)
    return gab_union_cvalid(gab_nil);

  // If our sock is a string, its an error.
  if (gab_valkind(sock) == kGAB_STRING)
    return gab_vmpush(gab_thisvm(gab), gab_err, sock),
           gab_union_cvalid(gab_nil);

  // Otherwise its our successfully created socket.
  return gab_vmpush(gab_thisvm(gab), gab_ok, sock), gab_union_cvalid(gab_nil);
}

union gab_value_pair resume_filesend(struct gab_triple gab,
                                     uintptr_t reentrant) {
  if (!qd_status(reentrant - 1))
    return gab_union_ctimeout(reentrant);

  int64_t result = qd_destroy(reentrant - 1);

  if (result < 0) {
    gab_vmpush(gab_thisvm(gab), gab_err, gab_string(gab, strerror(-result)));
    return gab_union_cvalid(gab_nil);
  }

  assert(result > 0);

  gab_vmpush(gab_thisvm(gab), gab_ok);
  return gab_union_cvalid(gab_nil);
}

union gab_value_pair complete_filesend(struct gab_triple gab, struct gab_io *io,
                                       const char *data, size_t len) {
  qd_t qd = qwrite(io->fd, len, (uint8_t *)data);
  return gab_union_ctimeout(qd + 1);
}

union gab_value_pair resume_socksend(struct gab_triple gab,
                                     uintptr_t reentrant) {
  if (!qd_status(reentrant - 1))
    return gab_union_ctimeout(reentrant);

  int64_t result = qd_destroy(reentrant - 1);

  if (result < 0) {
    gab_vmpush(gab_thisvm(gab), gab_err, gab_string(gab, strerror(-result)));
    return gab_union_cvalid(gab_nil);
  }

  assert(result > 0);

  gab_vmpush(gab_thisvm(gab), gab_ok);
  return gab_union_cvalid(gab_nil);
}

union gab_value_pair complete_socksend(struct gab_triple gab, struct gab_io *io,
                                       const char *data, size_t len) {
  qd_t qd = qsend(io->fd, len, (uint8_t *)data);
  return gab_union_ctimeout(qd + 1);
}

GAB_DYNLIB_NATIVE_FN(io, send) {
  gab_value vsock = gab_arg(0);

  if (gab_valkind(vsock) != kGAB_BOX)
    return gab_ptypemismatch(gab, vsock, gab_string(gab, tGAB_IOSOCK));

  gab_value str = gab_arg(1);

  if (gab_valkind(str) != kGAB_BINARY)
    return gab_pktypemismatch(gab, str, kGAB_BINARY);

  struct gab_io *io = gab_boxdata(vsock);

  const char *data = gab_strdata(&str);
  size_t len = gab_strlen(str);

  switch (io->k) {
  case IO_FILE:
    if (reentrant)
      return resume_filesend(gab, reentrant);
    else
      return complete_filesend(gab, io, data, len);
  case IO_SOCK_CLIENT:
    if (reentrant)
      return resume_socksend(gab, reentrant);
    else
      return complete_socksend(gab, io, data, len);
  case IO_SOCK_SSLCLIENT:
    if (reentrant)
      return resume_sslsocksend(gab, (struct gab_ssl_sock *)io, data, len,
                                reentrant);
    else
      return complete_sslsocksend(gab, (struct gab_ssl_sock *)io, data, len);
  default:
    return gab_panicf(gab, "$ may not send: $", vsock, gab_number(io->k));
  }

  return gab_panicf(gab, "Reached unreachable codepath");
}

union gab_value_pair gab_io_read(struct gab_triple gab, struct gab_io *io,
                                 uintptr_t reentrant, gab_uint len,
                                 s_char *out) {
  switch (io->k) {
  case IO_FILE:
    if (reentrant)
      return resume_filerecv(gab, (struct gab_file *)io, len, out, reentrant);
    else
      return complete_filerecv(gab, (struct gab_file *)io, len, out);
  case IO_SOCK_CLIENT:
    if (reentrant)
      return resume_sockrecv(gab, (struct gab_sock *)io, len, out, reentrant);
    else
      return complete_sockrecv(gab, (struct gab_sock *)io, len, out);
  case IO_SOCK_SSLCLIENT:
    if (reentrant)
      return resume_sslsockrecv(gab, (struct gab_ssl_sock *)io, len, out);
    else
      return complete_sslsockrecv(gab, (struct gab_ssl_sock *)io, len, out);
  default:
    return gab_panicf(gab, "IO object may not recv");
  }
}

GAB_DYNLIB_NATIVE_FN(io, recv) {
  gab_value vsock = gab_arg(0);
  gab_value vlen = gab_arg(1);

  if (gab_valkind(vsock) != kGAB_BOX)
    return gab_ptypemismatch(gab, vsock, gab_string(gab, tGAB_IOSOCK));

  gab_uint len = 0;
  if (gab_valisnum(vlen))
    len = gab_valtou(vlen);

  if (gab_valkind(vlen) != kGAB_NUMBER && vlen != gab_nil)
    return gab_pktypemismatch(gab, vlen, kGAB_NUMBER);

  struct gab_io *io = gab_boxdata(vsock);

  s_char out;
  union gab_value_pair res = gab_io_read(gab, io, reentrant, len, &out);

  // If we error or yield, do that.
  if (res.status != gab_cvalid)
    return res;

  // Else we succeeded, so create a binary and push it up.
  assert(out.data && out.len);
  assert(out.len == len);
  return gab_vmpush(gab_thisvm(gab), gab_ok,
                    gab_nbinary(gab, out.len, (const uint8_t *)out.data)),
         gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_NATIVE_FN(io, until) {
  gab_value vio = gab_arg(0);
  gab_value vdelim = gab_arg(1);

  if (gab_valkind(vio) != kGAB_BOX)
    return gab_ptypemismatch(gab, vio, gab_string(gab, tGAB_IOFILE));

  if (vdelim == gab_nil)
    vdelim = gab_binary(gab, "\n");

  if (gab_valkind(vdelim) != kGAB_BINARY)
    return gab_pktypemismatch(gab, vdelim, kGAB_BINARY);

  s_char delim = {
      .data = gab_strdata(&vdelim),
      .len = gab_strlen(vdelim),
  };

  struct gab_io *io = gab_boxdata(vio);

  for (;;) {
    s_char out = {0};
    union gab_value_pair res = gab_io_read(gab, io, reentrant, 1, &out);

    // If we saw an error or yielded, return it.
    if (res.status == gab_ctimeout)
      return res;

    if (res.status == gab_cinvalid)
      return res;

    assert(res.status == gab_cundefined);

    /**
     * This clears up the reentrant if we made it past the io_read above.
     */
    reentrant = 0;

    if (!out.len)
      break;

    if (delim.len && out.data[0] == delim.data[delim.len - 1]) {
      s_char acc = {
          .data = gab_fibat(gab_thisfiber(gab), 0),
          .len = gab_fibsize(gab_thisfiber(gab)),
      };

      if (acc.len > delim.len) {
        uint64_t back = acc.len - (delim.len - 1);

        if (!memcmp(acc.data + back, delim.data, delim.len - 1))
          break;
      }
    }

    gab_fibpush(gab_thisfiber(gab), out.data[0]);
  }

  gab_vmpush(gab_thisvm(gab), gab_ok,
             gab_nbinary(gab, gab_fibsize(gab_thisfiber(gab)),
                         gab_fibat(gab_thisfiber(gab), 0)));
  return gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_NATIVE_FN(io, connect) {
  gab_value vsock = gab_arg(0);
  gab_value ip = gab_arg(1);
  gab_value port = gab_arg(2);

  if (gab_valkind(vsock) != kGAB_BOX)
    return gab_ptypemismatch(gab, vsock, gab_string(gab, tGAB_IOSOCK));

  if (gab_valkind(port) != kGAB_NUMBER)
    return gab_pktypemismatch(gab, port, kGAB_NUMBER);

  if (ip == gab_nil)
    ip = gab_string(gab, "::1");

  if (gab_valkind(ip) != kGAB_STRING)
    return gab_pktypemismatch(gab, ip, kGAB_STRING);

  struct gab_sock *sock = gab_boxdata(vsock);

  switch (sock->io.k) {
  case IO_SOCK_UNSPECIFIED: {
    enum gab_io_k unspec = IO_SOCK_UNSPECIFIED;
    if (!atomic_compare_exchange_strong(&sock->io.k, &unspec, IO_SOCK_CLIENT))
      return gab_vmpush(
                 gab_thisvm(gab), gab_err,
                 gab_string(gab, "Socket is already connected or bound")),
             gab_union_cvalid(gab_nil);

    return complete_sockconnect(gab, sock, gab_strdata(&ip), gab_valtoi(port));
  }
  case IO_SOCK_SSLUNSPECIFIED: {
    enum gab_io_k unspec = IO_SOCK_SSLUNSPECIFIED;
    if (!atomic_compare_exchange_strong(&sock->io.k, &unspec,
                                        IO_SOCK_SSLCLIENT))
      return gab_vmpush(
                 gab_thisvm(gab), gab_err,
                 gab_string(gab, "Socket is already connected or bound")),
             gab_union_cvalid(gab_nil);

    return complete_sslsockconnect(gab, (struct gab_ssl_sock *)sock,
                                   gab_strdata(&ip), gab_valtoi(port));
  }
  case IO_SOCK_CLIENT:
  case IO_SOCK_SERVER:
    if (reentrant)
      return resume_sockconnect(gab, (struct gab_sock *)sock, reentrant);
  case IO_SOCK_SSLCLIENT:
  case IO_SOCK_SSLSERVER:
    if (reentrant)
      return resume_sslsockconnect(gab, (struct gab_ssl_sock *)sock,
                                   gab_strdata(&ip), reentrant);
  default:
    return gab_vmpush(gab_thisvm(gab), gab_err,
                      gab_string(gab, "Socket is in invalid state")),
           gab_union_cvalid(gab_nil);
  };
}

GAB_DYNLIB_NATIVE_FN(io, accept) {
  gab_value vsock = gab_arg(0);

  if (gab_valkind(vsock) != kGAB_BOX)
    return gab_ptypemismatch(gab, vsock, gab_string(gab, tGAB_IOSOCK));

  struct gab_sock *sock = gab_boxdata(vsock);
  switch (sock->io.k) {
  case IO_SOCK_SERVER:
    return complete_sockaccept(gab, sock);
  case IO_SOCK_SSLUNSPECIFIED:
    return complete_sslsockaccept(gab, (struct gab_ssl_sock *)sock);
  default:
    return gab_vmpush(gab_thisvm(gab), gab_err,
                      gab_string(gab, "Socket is in invalid state")),
           gab_union_cvalid(gab_nil);
  };
}

GAB_DYNLIB_NATIVE_FN(io, listen) {
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

  if (result < 0)
    gab_vmpush(gab_thisvm(gab), gab_err, gab_string(gab, strerror(-result)));
  else
    gab_vmpush(gab_thisvm(gab), gab_ok);

  return gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_NATIVE_FN(io, bind) {
  gab_value vsock = gab_arg(0);
  gab_value ip = gab_arg(1);
  gab_value port = gab_arg(2);

  if (gab_valkind(vsock) != kGAB_BOX)
    return gab_ptypemismatch(gab, vsock, gab_string(gab, tGAB_IOSOCK));

  if (gab_valkind(port) != kGAB_NUMBER)
    return gab_pktypemismatch(gab, port, kGAB_NUMBER);

  if (ip == gab_nil)
    ip = gab_string(gab, "::1");

  if (gab_valkind(ip) != kGAB_STRING)
    return gab_pktypemismatch(gab, ip, kGAB_STRING);

  struct gab_io *io = gab_boxdata(vsock);

  switch (io->k) {
  case IO_SOCK_UNSPECIFIED: {
    enum gab_io_k unspec = IO_SOCK_UNSPECIFIED;
    if (!atomic_compare_exchange_strong(&io->k, &unspec, IO_SOCK_SERVER))
      return gab_vmpush(
                 gab_thisvm(gab), gab_err,
                 gab_string(gab, "Socket is already connected or bound")),
             gab_union_cvalid(gab_nil);

    return complete_sockbind(gab, (struct gab_sock *)io, gab_strdata(&ip),
                             gab_valtoi(port));
  }
  case IO_SOCK_SSLUNSPECIFIED: {
    enum gab_io_k unspec = IO_SOCK_SSLUNSPECIFIED;
    if (!atomic_compare_exchange_strong(&io->k, &unspec, IO_SOCK_SSLSERVER))
      return gab_vmpush(
                 gab_thisvm(gab), gab_err,
                 gab_string(gab, "Socket is already connected or bound")),
             gab_union_cvalid(gab_nil);

    return complete_sockbind(gab, (struct gab_sock *)io, gab_strdata(&ip),
                             gab_valtoi(port));
  }
  case IO_SOCK_SSLSERVER:
  case IO_SOCK_SERVER:
    return resume_sockbind(gab, (struct gab_sock *)io, reentrant);
  default:
    return gab_vmpush(gab_thisvm(gab), gab_err,
                      gab_string(gab, "Socket is in invalid state")),
           gab_union_cvalid(gab_nil);
  }
}

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

GAB_DYNLIB_MAIN_FN {

  /*
   * This variable needs to be atomic or volatile.
   *
   * Otherwise, the compiler will attempt to optimize it out in this
   * call to thrd_create, and our initialze while loop will wait forever.
   */
  _Atomic int initialized;
  atomic_init(&initialized, 0);

  thrd_t io_t;
  if (thrd_create(&io_t, io_loop_cb, &initialized) != thrd_success)
    return gab_panicf(gab, "Failed to initialize QIO");

  while (!atomic_load(&initialized))
    ;

  if (atomic_load(&initialized) < 0)
    return gab_panicf(gab, "Failed to initialize QIO");

  assert(initialized == 1);

  gab_value file_t = gab_string(gab, tGAB_IOFILE);
  gab_value sock_t = gab_string(gab, tGAB_IOSOCK);
  gab_value mod = gab_message(gab, tGAB_IO);

  gab_def(gab,
          {
              gab_message(gab, "Files"),
              mod,
              gab_strtomsg(file_t),
          },
          {
              gab_message(gab, "Sockets"),
              mod,
              gab_strtomsg(sock_t),
          },
          {
              gab_message(gab, "t"),
              gab_strtomsg(file_t),
              file_t,
          },
          {
              gab_message(gab, "t"),
              gab_strtomsg(sock_t),
              sock_t,
          },
          {
              gab_message(gab, "stdin"),
              mod,
              wrap_qfdsock(gab, gab_osfileno(stdin), IO_FILE, false),
          },
          {
              gab_message(gab, "stdout"),
              mod,
              wrap_qfdsock(gab, gab_osfileno(stdout), IO_FILE, false),
          },
          {
              gab_message(gab, "stderr"),
              mod,
              wrap_qfdsock(gab, gab_osfileno(stderr), IO_FILE, false),
          },
          {
              gab_message(gab, "make"),
              gab_strtomsg(sock_t),
              gab_snative(gab, "make", gab_mod_io_sock),
          },
          {
              gab_message(gab, "make"),
              gab_strtomsg(file_t),
              gab_snative(gab, "make", gab_mod_io_open),
          },
          {
              gab_message(gab, "until"),
              file_t,
              gab_snative(gab, "until", gab_mod_io_until),
          },
          {
              gab_message(gab, "until"),
              sock_t,
              gab_snative(gab, "until", gab_mod_io_until),
          },
          {
              gab_message(gab, "read"),
              file_t,
              gab_snative(gab, "read", gab_mod_io_recv),
          },
          {
              gab_message(gab, "write"),
              file_t,
              gab_snative(gab, "write", gab_mod_io_send),
          },
          {
              gab_message(gab, "write"),
              sock_t,
              gab_snative(gab, "write", gab_mod_io_send),
          },
          {
              gab_message(gab, "read"),
              sock_t,
              gab_snative(gab, "read", gab_mod_io_recv),
          },
          {
              gab_message(gab, "accept"),
              sock_t,
              gab_snative(gab, "accept", gab_mod_io_accept),
          },
          {
              gab_message(gab, "listen"),
              sock_t,
              gab_snative(gab, "listen", gab_mod_io_listen),
          },
          {
              gab_message(gab, "bind"),
              sock_t,
              gab_snative(gab, "bind", gab_mod_io_bind),
          },
          {
              gab_message(gab, "connect"),
              sock_t,
              gab_snative(gab, "connect", gab_mod_io_connect),
          });

  gab_value res[] = {gab_ok, mod};

  return (union gab_value_pair){
      .status = gab_cvalid,
      .aresult = a_gab_value_create(res, sizeof(res) / sizeof(gab_value)),
  };
}

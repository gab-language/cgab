#include "BearSSL/inc/bearssl.h"
#include "BearSSL/inc/bearssl_x509.h"
#include "core.h"

#include "gab.h"
#include "platform.h"

#define QIO_LOOP_INTERVAL_NS 50000
#define QIO_INTERNAL_QUEUE_INITIAL_LEN 2056
#include "qio/qio.h"

#include "ta.h"

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
 * In order for the socket-creating interfaces to remain immutable, we should
 * forego the 'first create a socket, then bind *or* connect it' setup.
 *
 * We should simply say:
 *  IO.Sockets.bind(tcp: "::1" 8080)
 *    -> Returns a socket with type io\socket\tcp\server
 *
 *  IO.Sockets.connect(tcp\tls: "google.com" 997)
 *    -> Returns a socket with type io\socket\tcp\client
 *
 *  IO.Sockets.make(tcp: client: "::1" 8080)
 *  IO.Sockets.make(tcp: server: "::1" 8080)
 *    -> This is the simple, root call which we will write wrappers for.
 *
 * These *wrap* the operations of creating a socket, then either connecting or
 * binding.
 *
 * This way there is no mutable state visible to the user!
 *
 */

/*
 * MASSIVE TODO:
 * Is it unsafe to directly send pointers into gab strings to the io syscalls?
 *
 * I ask because all the IO is happening asynchronously,
 * it may be that the engine is freed and exiting while IO operations are still
 * queued. Supposedly, the gab engine itself will have been killed by this
 * point. So maybe it is irrelevant? I do see how it could result in a crash
 * sending random bytes to a socket somewhere, which could be bad.
 *
 * It also may be that at some point, gab_strings don't live forever. We would
 * need to hold a reference to them for the duration of this call.
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
  IO_SOCK_SSLSERVERCLIENT,
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

  mtx_t mtx;
};

#define BUFFER_SIZE (1 << 15)
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

struct iobuf {
  uint64_t bfront, bback;
  unsigned char buffer[BUFFER_SIZE];
};

#define eGAB_EOF -1000

static inline int64_t iobuf_recv(struct iobuf *io, gab_value fib, qfd_t fd,
                                 uint64_t len, s_char *out,
                                 uintptr_t *reentrant) {
  uint64_t sofar = gab_fibsize(fib);

  if (*reentrant) {
    if (!qd_status(*reentrant - 1))
      return 0;

    int64_t result = qd_destroy(*reentrant - 1);
    *reentrant = 0;

    if (result < 0)
      return result;

    if (result == 0)
      return eGAB_EOF;

    io->bback += result;
    // Recurse with the data we now have in the buffer.
    return iobuf_recv(io, fib, fd, len, out, reentrant);
  }

  gab_uint available = buffer_len(io);

  // If no specific amount of data was requested, return what is available.
  if (!len)
    len = available;

  /*
   * The amount of data we consume in this operation.
   *
   * If we are requesting more data than available,
   * we consume all that is available.
   *
   * Otherwise, just consume the 'len' requested.
   */
  gab_uint consumed = len < available ? len : available;

  gab_uint total = sofar ? sofar + consumed : consumed;

  // If we need more data, and we have room for it, request it.
  if ((!available || total < len) && (io->bback - io->bfront < BUFFER_SIZE)) {
    // Yield while we queue up a read
    qd_t qd = qread(fd, buffer_avail(io), buffer_space(io));
    *reentrant = qd + 1;
    return 0;
  }

  /*
   * Get the pointer to our data,
   * and then mark it as consumed.
   */
  const uint8_t *data = buffer_data(io);
  io->bfront += consumed;

  /*
   * We may already be accumulating data into the arena.
   * Continue if that is the case.
   *
   * Otherwise, if the data we consumed isn't enough,
   * begin accumulating into the arena. This usually
   * happens when the amount of data requested is bigger than BUFFER_SIZE.
   */
  if (sofar || consumed < len)
    for (uint64_t i = 0; i < consumed; i++)
      gab_fibpush(fib, data[i]);

  /*
   * If we didn't read len bytes yet, recurse.
   */
  if (total < len)
    return iobuf_recv(io, fib, fd, len - consumed, out, reentrant);

  /*
   * At this point, we've consumed len bytes.
   *
   * If we were accumulating into arena, use that.
   *
   * Otherwise, use the iobuf.
   */
  if (sofar) {
    out->len = gab_fibsize(fib);
    out->data = gab_fibat(fib, 0);
    return len;
  }

  out->len = len;
  out->data = (const char *)data;
  return len;
}

/*
 * TODO: Reimplement with atomic buffer impl.
 * Wrap a file on disk, or std in/out/err.
 * Reads are buffered.
 *
 * Maybe the mutex can be dropped in favor of
 * atomic bfront, bback on the queue.
 */
struct gab_file {
  struct gab_io io;

  struct iobuf recv_buf;
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

  struct iobuf recv_buf;
};

/*
 * Wrap an OS socket, with added transport-layer-security.
 */
#define MAX_CERTS 8
#define MAX_CERTSIZE 4096
struct gab_ssl_sock {
  struct gab_io io;

  struct qio_addr addr;

  qd_t io_operations[2];

  br_sslio_context ioc;

  union {
    struct {
      br_x509_minimal_context xc;

      int64_t nanchors;
      br_x509_trust_anchor anchors[10];

      int64_t nobj_bytes;
      unsigned char obj_bytes[MAX_CERTSIZE];

      br_ssl_client_context cc;
      unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];
    } client;

    struct {
      int64_t ncerts;
      br_x509_certificate certs[MAX_CERTS];

      br_skey_decoder_context dc;
      br_rsa_private_key rsa;

      int64_t nobj_bytes;
      unsigned char obj_bytes[MAX_CERTS * MAX_CERTSIZE];
    } server;

    struct {
      br_ssl_server_context sc;
      unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];
    } serverclient;
  };
};

/*
 * In these destruction callbacks, we only queue the operation.
 * It is possible we shouldn't try to do this at all.
 */

void file_cb(struct gab_triple, uint64_t len, char data[static len]) {
  struct gab_io *iod = (struct gab_io *)data;
  qclose(iod->fd);
}

void sock_cb(struct gab_triple, uint64_t len, char data[static len]) {
  struct gab_io *iod = (struct gab_io *)data;
  qshutdown(iod->fd);
  qclose(iod->fd);
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
    [IO_SOCK_SSLSERVERCLIENT] = tGAB_IOSOCK,
};

const gab_boxdestroy_f io_tdestroy[] = {
    [IO_FILE] = file_cb,
    [IO_SOCK_UNSPECIFIED] = sock_cb,
    [IO_SOCK_CLIENT] = sock_cb,
    [IO_SOCK_SERVER] = sock_cb,
    [IO_SOCK_SSLCLIENT] = sock_cb,
    [IO_SOCK_SSLSERVER] = sock_cb,
    [IO_SOCK_SSLSERVERCLIENT] = sock_cb,
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
  f->recv_buf.bfront = 0;
  f->recv_buf.bback = 0;
  mtx_init(&f->io.mtx, mtx_plain);

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
  sk->recv_buf.bfront = 0;
  sk->recv_buf.bback = 0;
  mtx_init(&sk->io.mtx, mtx_plain);

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
  mtx_init(&sk->io.mtx, mtx_plain);

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
    unsigned state = br_ssl_engine_current_state(ctx->engine);

    if (state & BR_SSL_CLOSED)
      return -1;

    if (sock->io_operations[BR_SSL_SENDREC_CHANNEL] >= 0) {
      /*
       * We are already sending a record for this
       */
      qd_t qd = sock->io_operations[BR_SSL_SENDREC_CHANNEL];

      if (!qd_status(qd))
        return target;

      int64_t wlen = qd_destroy(qd);

      if (wlen < 0)
        return br_ssl_engine_close(ctx->engine), -1;

      if (wlen >= 0)
        br_ssl_engine_sendrec_ack(ctx->engine, wlen);

      sock->io_operations[BR_SSL_SENDREC_CHANNEL] = -1;
      continue;
    }

    if (sock->io_operations[BR_SSL_RECVREC_CHANNEL] >= 0) {
      qd_t qd = sock->io_operations[BR_SSL_RECVREC_CHANNEL];

      if (!qd_status(qd))
        return target;

      int64_t wlen = qd_destroy(qd);

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
    gab_push(gab, gab_err, gab_string(gab, strerror(-result)));
    return gab_union_cvalid(gab_nil);
  }

  gab_value vclient =
      wrap_qfdsockssl(gab, result, IO_SOCK_SSLSERVERCLIENT, true);

  struct gab_ssl_sock *client = gab_boxdata(vclient);

  // TODO: Include decoding TAs from a PEM

  assert(sock->server.nobj_bytes > 0);
  assert(sock->server.ncerts > 0);
  assert(sock->server.rsa.dplen > 0);
  assert(sock->server.rsa.dqlen > 0);
  assert(sock->server.rsa.iqlen > 0);
  assert(sock->server.rsa.plen > 0);
  assert(sock->server.rsa.qlen > 0);

  br_ssl_server_init_full_rsa(&client->serverclient.sc, sock->server.certs,
                              sock->server.ncerts, &sock->server.rsa);

  br_ssl_engine_set_buffer(&client->serverclient.sc.eng,
                           client->serverclient.iobuf,
                           sizeof client->serverclient.iobuf, true);

  /*
   * Reset the server context, for a new handshake.
   */
  int res = br_ssl_server_reset(&client->serverclient.sc);
  assert(res);

  /*
   * Initialize this with nullptrs for all the callbacks, as we write a custom
   * engine.
   */
  br_sslio_init(&client->ioc, &client->serverclient.sc.eng, nullptr, nullptr,
                nullptr, nullptr);

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

static void server_slurp(void *cc, const void *data, size_t len) {
  struct gab_ssl_sock *sock = cc;

  int64_t sofar = sock->server.nobj_bytes;

  // Check if this slurp would overlflow.
  if (sofar + len > (MAX_CERTS * MAX_CERTSIZE)) {
    sock->server.nobj_bytes = -1;
    return;
  }

  // Check if we already overflowed.
  if (sofar < 0)
    return;

  unsigned char *dst = sock->server.obj_bytes + sofar;

  // Write our bytes.
  memcpy(dst, data, len);
  sock->server.nobj_bytes += len;
}

static void client_slurp(void *cc, const void *data, size_t len) {
  struct gab_ssl_sock *sock = cc;

  int64_t sofar = sock->client.nobj_bytes;

  // Check if this slurp would overlflow.
  if (sofar + len > (MAX_CERTS * MAX_CERTSIZE)) {
    sock->client.nobj_bytes = -1;
    return;
  }

  // Check if we already overflowed.
  if (sofar < 0)
    return;

  unsigned char *dst = sock->client.obj_bytes + sofar;

  // Write our bytes.
  memcpy(dst, data, len);
  sock->client.nobj_bytes += len;
}

int serverdecode_pem(struct gab_triple gab, struct gab_ssl_sock *sock,
                     gab_value pem) {
  br_pem_decoder_context pc;
  br_pem_decoder_init(&pc);

  const char *data = gab_strdata(&pem);
  uint64_t len = gab_strlen(pem);

  int64_t obj_start = sock->server.nobj_bytes;

  bool inobject = false;
  char name[128] = {0};

  while (len > 0) {
    int consumed = br_pem_decoder_push(&pc, data, len);
    data += consumed;
    len -= consumed;

    switch (br_pem_decoder_event(&pc)) {

    case BR_PEM_ERROR:
      atomic_store(&sock->io.k, IO_SOCK_UNSPECIFIED);
      return gab_vmpush(gab_thisvm(gab), gab_err,
                        gab_string(gab, "Invalid PEM")),
             -1;

    case BR_PEM_BEGIN_OBJ:
      strcpy(name, br_pem_decoder_name(&pc));
      br_pem_decoder_setdest(&pc, server_slurp, sock);
      inobject = true;
      obj_start = sock->server.nobj_bytes;
      break;

    case BR_PEM_END_OBJ: {
      assert(inobject);

      if (!strcmp(name, "X509 CERTIFICATE") || !strcmp(name, "CERTIFICATE")) {
        uint32_t c = sock->server.ncerts;

        if (c >= MAX_CERTS) {
          atomic_store(&sock->io.k, IO_SOCK_UNSPECIFIED);
          return gab_vmpush(gab_thisvm(gab), gab_err,
                            gab_string(gab, "Certificate chain too large")),
                 -1;
        }

        if (sock->server.nobj_bytes < 0) {
          atomic_store(&sock->io.k, IO_SOCK_UNSPECIFIED);
          return gab_vmpush(gab_thisvm(gab), gab_err,
                            gab_string(gab, "Certificate chain too large")),
                 -1;
        }

        int64_t certlen = sock->server.nobj_bytes - obj_start;

        sock->server.certs[c] = (br_x509_certificate){
            .data = sock->server.obj_bytes + obj_start,
            .data_len = certlen,
        };
        sock->server.ncerts++;

        inobject = false;
        break;
      }

      if (!strcmp(name, "RSA PRIVATE KEY") || !strcmp(name, "EC PRIVATE KEY") ||
          !strcmp(name, "PRIVATE KEY")) {
        if (sock->server.nobj_bytes < 0) {
          atomic_store(&sock->io.k, IO_SOCK_UNSPECIFIED);
          return gab_vmpush(gab_thisvm(gab), gab_err,
                            gab_string(gab, "RSA Key too large")),
                 -1;
        }
        unsigned char *key = sock->server.obj_bytes + obj_start;
        int64_t keylen = sock->server.nobj_bytes - obj_start;

        br_skey_decoder_init(&sock->server.dc);

        br_skey_decoder_push(&sock->server.dc, key, keylen);

        int32_t err = br_skey_decoder_last_error(&sock->server.dc);

        if (err) {
          atomic_store(&sock->io.k, IO_SOCK_UNSPECIFIED);
          return gab_vmpush(gab_thisvm(gab), gab_err,
                            gab_string(gab, "Could not decode private key"),
                            gab_number(err)),
                 -1;
        }

        const br_rsa_private_key *rk =
            br_skey_decoder_get_rsa(&sock->server.dc);

        if (!rk) {
          atomic_store(&sock->io.k, IO_SOCK_UNSPECIFIED);
          return gab_vmpush(gab_thisvm(gab), gab_err,
                            gab_string(gab, "Expected an RSA private key")),
                 -1;
        }

        sock->server.rsa = *rk;
        break;
      }

      atomic_store(&sock->io.k, IO_SOCK_UNSPECIFIED);
      return gab_vmpush(gab_thisvm(gab), gab_err,
                        gab_string(gab, "Unrecognized object in PEM")),
             -1;
    }
    };
  }

  return 0;
}

void *xblobdup(void *bytes, size_t n) {
  uint8_t *buf = malloc(n);
  memcpy(buf, bytes, n);
  return buf;
}

int clientdecode_pem(struct gab_triple gab, struct gab_ssl_sock *sock,
                     gab_value pem) {
  br_pem_decoder_context pc;
  br_pem_decoder_init(&pc);

  const char *data = gab_strdata(&pem);
  uint64_t len = gab_strlen(pem);

  int64_t obj_start = sock->client.nobj_bytes;

  bool inobject = false;
  char name[128] = {0};

  while (len > 0) {
    int consumed = br_pem_decoder_push(&pc, data, len);
    data += consumed;
    len -= consumed;

    switch (br_pem_decoder_event(&pc)) {

    case BR_PEM_ERROR:
      atomic_store(&sock->io.k, IO_SOCK_UNSPECIFIED);
      return gab_vmpush(gab_thisvm(gab), gab_err,
                        gab_string(gab, "Invalid address")),
             -1;

    case BR_PEM_BEGIN_OBJ:
      strcpy(name, br_pem_decoder_name(&pc));
      br_pem_decoder_setdest(&pc, client_slurp, sock);
      inobject = true;
      obj_start = sock->client.nobj_bytes;

      break;

    case BR_PEM_END_OBJ: {
      assert(inobject);

      if (!strcmp(name, "X509 CERTIFICATE") || !strcmp(name, "CERTIFICATE")) {
        if (sock->client.nobj_bytes < 0) {
          atomic_store(&sock->io.k, IO_SOCK_UNSPECIFIED);
          return gab_vmpush(gab_thisvm(gab), gab_err,
                            gab_string(gab, "Certificate chain too large")),
                 -1;
        }

        int64_t obj_len = sock->client.nobj_bytes - obj_start;
        unsigned char *obj = sock->client.obj_bytes + obj_start;

        br_x509_decoder_context dc;

        br_x509_trust_anchor *ta =
            &sock->client.anchors[sock->client.nanchors++];

        int64_t dn_start = sock->client.nobj_bytes;

        br_x509_decoder_init(&dc, client_slurp, sock);
        br_x509_decoder_push(&dc, obj, obj_len);

        int64_t dn_len = sock->client.nobj_bytes - dn_start;
        unsigned char *dn = sock->client.obj_bytes + dn_start;

        ta->dn.data = dn;
        ta->dn.len = dn_len;

        if (br_x509_decoder_isCA(&dc))
          ta->flags |= BR_X509_TA_CA;

        br_x509_pkey *pk = br_x509_decoder_get_pkey(&dc);
        if (!pk) {
          atomic_store(&sock->io.k, IO_SOCK_UNSPECIFIED);
          return gab_vmpush(gab_thisvm(gab), gab_err,
                            gab_string(gab, "Failed to decode key"),
                            gab_number(br_x509_decoder_last_error(&dc))),
                 -1;
        }

        switch (pk->key_type) {
        case BR_KEYTYPE_RSA:
          ta->pkey.key_type = BR_KEYTYPE_RSA;
          ta->pkey.key.rsa.n = xblobdup(pk->key.rsa.n, pk->key.rsa.nlen);
          ta->pkey.key.rsa.nlen = pk->key.rsa.nlen;
          ta->pkey.key.rsa.e = xblobdup(pk->key.rsa.e, pk->key.rsa.elen);
          ta->pkey.key.rsa.elen = pk->key.rsa.elen;
          break;
        case BR_KEYTYPE_EC:
          ta->pkey.key_type = BR_KEYTYPE_EC;
          ta->pkey.key.ec.curve = pk->key.ec.curve;
          ta->pkey.key.ec.q = xblobdup(pk->key.ec.q, pk->key.ec.qlen);
          ta->pkey.key.ec.qlen = pk->key.ec.qlen;
          break;
        default:
          atomic_store(&sock->io.k, IO_SOCK_UNSPECIFIED);
          return gab_vmpush(gab_thisvm(gab), gab_err,
                            gab_string(gab, "Unrecognized Key Type in x509")),
                 -1;
        }
        inobject = false;
        break;
      }

      atomic_store(&sock->io.k, IO_SOCK_UNSPECIFIED);
      return gab_vmpush(gab_thisvm(gab), gab_err,
                        gab_string(gab, "Unrecognized object in PEM")),
             -1;
    }
    };
  }

  return 0;
}

union gab_value_pair complete_sslsockbind(struct gab_triple gab,
                                          struct gab_ssl_sock *sock,
                                          const char *hostname, gab_int port,
                                          gab_value cert, gab_value pkey) {

  if (qio_addrfrom(hostname, port, &sock->addr) < 0) {
    atomic_store(&sock->io.k, IO_SOCK_UNSPECIFIED);
    return gab_vmpush(gab_thisvm(gab), gab_err,
                      gab_string(gab, "Invalid address")),
           gab_union_cvalid(gab_nil);
  }

  assert(gab_strlen(pkey) > 5);

  if (serverdecode_pem(gab, sock, cert) < 0)
    return gab_union_cvalid(gab_nil);

  if (serverdecode_pem(gab, sock, pkey) < 0)
    return gab_union_cvalid(gab_nil);

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

union gab_value_pair resume_sslsockrecv(struct gab_triple gab,
                                        struct gab_ssl_sock *sock, gab_uint len,
                                        s_char *out) {
  io_op_res result = sslio_read_available(gab, sock, out, len);

  if (result.status < 0) {
    // TODO: Handle an SSL error gracefully
    int err = br_ssl_engine_last_error(&sock->client.cc.eng);

    // If we had an ssl engine error, then this was a real error.
    // Else, the connection just closed.

    if (err)
      return gab_vmpush(gab_thisvm(gab), gab_err,
                        gab_string(gab, "Error during SSL Read"),
                        gab_number(err)),
             gab_union_cvalid(gab_nil);
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

union gab_value_pair resume_sslserversockrecv(struct gab_triple gab,
                                              struct gab_ssl_sock *sock,
                                              gab_uint len, s_char *out) {
  io_op_res result = sslio_read_available(gab, sock, out, len);

  if (result.status < 0) {
    // TODO: Handle an SSL error gracefully
    int err = br_ssl_engine_last_error(&sock->serverclient.sc.eng);

    // If we had an ssl engine error, then this was a real error.
    // Else, the connection just closed.

    if (err)
      return gab_vmpush(gab_thisvm(gab), gab_err,
                        gab_string(gab, "SSL Server Sock Recv"),
                        gab_number(err)),
             gab_union_cvalid(gab_nil);
  }

  if (result.status > 0)
    return gab_union_ctimeout(result.status);

  return gab_union_cvalid(gab_nil);
}

union gab_value_pair complete_sslserversockrecv(struct gab_triple gab,
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
                 gab_string(gab, "Error during ssl write"), gab_number(err));
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
    gab_vmpush(gab_thisvm(gab), gab_err,
               gab_string(gab, "Error during sslwrite"), gab_number(err));
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

union gab_value_pair resume_sslserversocksend(struct gab_triple gab,
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
      int err = br_ssl_engine_last_error(&sock->serverclient.sc.eng);
      gab_vmpush(gab_thisvm(gab), gab_err,
                 gab_string(gab, "Error during sslserver write"),
                 gab_number(err));
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

union gab_value_pair complete_sslserversocksend(struct gab_triple gab,
                                                struct gab_ssl_sock *sock,
                                                const char *data,
                                                gab_uint len) {
  // This may yield as the ssl_engine may need to flush out (send) records
  // in order to make room in the buffer for this write.
  io_op_res result = sslio_write_all(sock, data, len);

  if (result.status > 0) {
    int64_t amount = (int64_t)result.amount << 32;
    int64_t tag = (int64_t)result.status | BR_SSL_WRITE_INCOMPLETE;
    return gab_union_ctimeout(amount | tag);
  }

  if (result.status < 0) {
    int err = br_ssl_engine_last_error(&sock->serverclient.sc.eng);
    gab_vmpush(gab_thisvm(gab), gab_err,
               gab_string(gab, "Error during sslserver write"),
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
                                           const char *hostname, gab_value pem,
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

  if (pem != gab_nil && clientdecode_pem(gab, sock, pem) < 0) {
    atomic_store(&sock->io.k, IO_SOCK_SSLUNSPECIFIED);
    return gab_union_cvalid(gab_nil);
  }

  if (sock->client.nanchors < 0) {
    atomic_store(&sock->io.k, IO_SOCK_SSLUNSPECIFIED);
    return gab_vmpush(gab_thisvm(gab), gab_err,
                      gab_string(gab, "Failed to decode PEM")),
           gab_union_cvalid(gab_nil);
  }

  const br_x509_trust_anchor *anchors = TAs;
  size_t nanchors = TAs_NUM;

  if (sock->client.nanchors > 0) {
    anchors = sock->client.anchors;
    nanchors = sock->client.nanchors;
  }

  /*
   * Initialise the client context:
   * -- Use the "full" profile (all supported algorithms).
   * -- The provided X.509 validation engine is initialised, with
   *    the hardcoded trust anchor.
   */
  br_ssl_client_init_full(&sock->client.cc, &sock->client.xc, anchors,
                          nanchors);

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
   *
   * If the hostname is anything other than loopback ::1, we pass it in.
   */
  br_ssl_client_reset(&sock->client.cc,
                      strcmp(hostname, "::1") ? hostname : nullptr, 0);

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

union gab_value_pair complete_filesend(struct gab_triple gab, struct gab_io *io,
                                       const char *data, size_t len);

union gab_value_pair resume_filesend(struct gab_triple gab, struct gab_io *io,
                                     uintptr_t reentrant) {
  if (!qd_status(reentrant - 1))
    return gab_union_ctimeout(reentrant);

  int64_t result = qd_destroy(reentrant - 1);

  const char *data = *(void **)gab_fibat(gab_thisfiber(gab), 0);
  size_t len = *(size_t *)gab_fibat(gab_thisfiber(gab), 8);

  gab_fibclear(gab_thisfiber(gab));

  // We encountered an error.
  if (result < 0)
    return gab_vmpush(gab_thisvm(gab), gab_err,
                      gab_string(gab, strerror(-result))),
           gab_union_cvalid(gab_nil);

  // We wrote fewer than len bytes.
  if (result < len)
    return complete_filesend(gab, io, ((const char *)data) + result,
                             len - result);

  // We wrote len bytes!)
  assert(result == len);
  gab_vmpush(gab_thisvm(gab), gab_ok);
  return gab_union_cvalid(gab_nil);
}

union gab_value_pair complete_filesend(struct gab_triple gab, struct gab_io *io,
                                       const char *data, size_t len) {
  qd_t qd = qwrite(io->fd, len, (uint8_t *)data);

  gab_wfibpush(gab_thisfiber(gab), (uintptr_t)data);
  gab_wfibpush(gab_thisfiber(gab), len);

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

  // Get the data from the str on the fibers stack.
  // This is because gab_strdata may return a pointer *into*
  // the string value itself (if the string is short enough).
  // For this case, we need that pointer to be valid for the
  // lifetime of this send (ie, the fibers stack)
  const char *data = gab_strdata(argv + 1);
  size_t len = gab_strlen(str);

  union gab_value_pair res;
  mtx_lock(&io->mtx);

  switch (io->k) {
  case IO_FILE:
    if (reentrant)
      res = resume_filesend(gab, io, reentrant);
    else
      res = complete_filesend(gab, io, data, len);
    break;
  case IO_SOCK_CLIENT:
    if (reentrant)
      res = resume_socksend(gab, reentrant);
    else
      res = complete_socksend(gab, io, data, len);
    break;
  case IO_SOCK_SSLCLIENT:
    if (reentrant)
      res = resume_sslsocksend(gab, (struct gab_ssl_sock *)io, data, len,
                               reentrant);
    else
      res = complete_sslsocksend(gab, (struct gab_ssl_sock *)io, data, len);
    break;
  case IO_SOCK_SSLSERVERCLIENT:
    if (reentrant)
      res = resume_sslserversocksend(gab, (struct gab_ssl_sock *)io, data, len,
                                     reentrant);
    else
      res =
          complete_sslserversocksend(gab, (struct gab_ssl_sock *)io, data, len);
    break;
  default:
    return gab_panicf(gab, "$ may not send: $", vsock, gab_number(io->k));
  }

  mtx_unlock(&io->mtx);
  return res;
}

union gab_value_pair gab_io_read(struct gab_triple gab, struct gab_io *io,
                                 uintptr_t *reentrant, gab_uint len,
                                 s_char *out) {
  switch (io->k) {
  case IO_FILE: {
    struct gab_file *f = (struct gab_file *)io;

    int64_t result = iobuf_recv(&f->recv_buf, gab_thisfiber(gab), f->io.fd, len,
                                out, reentrant);

    if (result == 0)
      return gab_union_ctimeout(*reentrant);

    if (result < 0) {
      if (result == eGAB_EOF)
        return gab_vmpush(gab_thisvm(gab), gab_err,
                          gab_string(gab, "Reached End of File")),
               gab_union_cvalid(gab_nil);
      else
        return gab_vmpush(gab_thisvm(gab), gab_err,
                          gab_string(gab, strerror(-result))),
               gab_union_cvalid(gab_nil);
    }

    return gab_union_cvalid(gab_nil);
  }
  case IO_SOCK_CLIENT: {
    struct gab_sock *f = (struct gab_sock *)io;

    int result = iobuf_recv(&f->recv_buf, gab_thisfiber(gab), f->io.fd, len,
                            out, reentrant);

    if (result == 0)
      return gab_union_ctimeout(*reentrant);

    if (result < 0) {
      if (result == eGAB_EOF)
        return gab_vmpush(gab_thisvm(gab), gab_err,
                          gab_string(gab, "Connection Closed")),
               gab_union_cvalid(gab_nil);
      else
        return gab_vmpush(gab_thisvm(gab), gab_err,
                          gab_string(gab, strerror(-result))),
               gab_union_cvalid(gab_nil);
    }

    return gab_union_cvalid(gab_nil);
  }
  case IO_SOCK_SSLCLIENT:
    if (*reentrant)
      return resume_sslsockrecv(gab, (struct gab_ssl_sock *)io, len, out);
    else
      return complete_sslsockrecv(gab, (struct gab_ssl_sock *)io, len, out);
  case IO_SOCK_SSLSERVERCLIENT:
    if (*reentrant)
      return resume_sslserversockrecv(gab, (struct gab_ssl_sock *)io, len, out);
    else
      return complete_sslserversockrecv(gab, (struct gab_ssl_sock *)io, len,
                                        out);
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
  s_char data = {0};

  mtx_lock(&io->mtx);

  union gab_value_pair res = gab_io_read(gab, io, &reentrant, len, &data);

  mtx_unlock(&io->mtx);

  /*
   * We may have panicked or yielded, in which case we should return.
   *
   * We may also have encountered an error which we should return -
   * indicated by the data.len < 0;
   */
  if (res.status != gab_cundefined || !data.data)
    return res;

  assert(data.data);

  return gab_vmpush(gab_thisvm(gab), gab_ok,
                    gab_nbinary(gab, data.len, (uint8_t *)data.data)),

         res;
}

GAB_DYNLIB_NATIVE_FN(io, len) {
  gab_value vio = gab_arg(0);

  if (gab_valkind(vio) != kGAB_BOX)
    return gab_ptypemismatch(gab, vio, gab_string(gab, tGAB_IOFILE));

  struct gab_io *io = gab_boxdata(vio);

  if (reentrant) {
    if (!qd_status(reentrant - 1)) {
      return gab_union_ctimeout(reentrant);
    }

    int64_t result = qd_destroy(reentrant - 1);

    if (result < 0)
      return gab_panicf(gab, "stat failed: @", gab_number(result));

    struct qio_stat *stat = gab_fibat(gab_thisfiber(gab), 0);

    gab_vmpush(gab_thisvm(gab), gab_number(qio_statsize(stat)));
    return gab_union_cvalid(gab_cundefined);
  }

  struct qio_stat *stat =
      gab_fibmalloc(gab_thisfiber(gab), sizeof(struct qio_stat));

  qd_t qd = qstat(io->fd, stat);
  return gab_union_ctimeout(qd + 1);
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
    union gab_value_pair res = gab_io_read(gab, io, &reentrant, 1, &out);

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

    if (out.len == (uint64_t)-1)
      return res;

    if (!out.len)
      break;

    if (delim.len && out.data[0] == delim.data[delim.len - 1]) {
      s_char acc = {
          .data = gab_fibat(gab_thisfiber(gab), 0),
          .len = gab_fibsize(gab_thisfiber(gab)),
      };

      if (acc.len >= delim.len) {
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
  gab_value pem = gab_arg(3);

  if (gab_valkind(vsock) != kGAB_BOX)
    return gab_ptypemismatch(gab, vsock, gab_string(gab, tGAB_IOSOCK));

  if (gab_valkind(port) != kGAB_NUMBER)
    return gab_pktypemismatch(gab, port, kGAB_NUMBER);

  if (ip == gab_nil)
    ip = gab_string(gab, "::1");

  if (gab_valkind(ip) != kGAB_STRING)
    return gab_pktypemismatch(gab, ip, kGAB_STRING);

  if (pem != gab_nil && gab_valkind(pem) != kGAB_BINARY)
    return gab_pktypemismatch(gab, pem, kGAB_BINARY);

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
    assert(reentrant);
    return resume_sockconnect(gab, (struct gab_sock *)sock, reentrant);
  case IO_SOCK_SSLCLIENT:
    assert(reentrant);
    return resume_sslsockconnect(gab, (struct gab_ssl_sock *)sock,
                                 gab_strdata(&ip), gab_arg(3), reentrant);
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
    if (reentrant)
      return resume_sockaccept(gab, sock, reentrant);
    else
      return complete_sockaccept(gab, sock);
  case IO_SOCK_SSLSERVER:
    if (reentrant)
      return resume_sslsockaccept(gab, (struct gab_ssl_sock *)sock, reentrant);
    else
      return complete_sslsockaccept(gab, (struct gab_ssl_sock *)sock);
  default:
    return gab_vmpush(gab_thisvm(gab), gab_err,
                      gab_string(gab, "Can only accept from server sockets")),
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

    gab_value cert = gab_arg(3);
    if (gab_valkind(cert) != kGAB_BINARY)
      return gab_pktypemismatch(gab, cert, kGAB_BINARY);

    gab_value pkey = gab_arg(4);

    if (gab_valkind(pkey) != kGAB_BINARY)
      return gab_pktypemismatch(gab, pkey, kGAB_BINARY);

    if (!atomic_compare_exchange_strong(&io->k, &unspec, IO_SOCK_SSLSERVER))
      return gab_vmpush(
                 gab_thisvm(gab), gab_err,
                 gab_string(gab, "Socket is already connected or bound")),
             gab_union_cvalid(gab_nil);

    return complete_sslsockbind(gab, (struct gab_ssl_sock *)io,
                                gab_strdata(&ip), gab_valtoi(port), cert, pkey);
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
              gab_message(gab, "len"),
              file_t,
              gab_snative(gab, "len", gab_mod_io_len),
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

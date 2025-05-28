#include "BearSSL/inc/bearssl_ssl.h"
#include <stdatomic.h>
#define QIO_LOOP_INTERVAL_NS 50000
#define QIO_INTERNAL_QUEUE_INITIAL_LEN 2056
#include "qio/qio.h"

#include "BearSSL/inc/bearssl.h"
#include "ta.h"

#include "gab.h"

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
 * ^^ The issue with the above interface is that the state for TLS connections (BearSSL stuff)
 * is allocated when we see the user wants a tls socket. For Servers (which are sockets that "bind")
 * There isn't actually any TLS state needed in that socket. All the memory is *per connection*, and so
 * can be stored on the 'client' (sockets returned by 'accept'). 
 *
 * More useful to provide the IO.bind() and IO.connect() Interface, which will know how much memory is 
 * actually needed.
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
 * These protocols will require quite a bit of c-implementation, and a TLS layer
 * for https. BearSSL Looks like it might be a great option! We can trivially
 * compile it with zig-cc for all our platforms
 *  - I love it when a plan comes together.
 *
 */

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

struct gab_sock {
  qfd_t fd;
  _Atomic enum gab_io_k k;
};

struct gab_ssl_sock {
  struct gab_sock sock;
  struct gab_triple gab;

  mtx_t ssl_mutex;

  union {
    struct {
      br_ssl_client_context sc;
      br_x509_minimal_context xc;
      br_sslio_context ioc;
      unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];
    } client;

    // These are really *connections*, when an ssl_server socket *accepts* a client.
    struct {
      br_ssl_server_context sc;
      br_sslio_context ioc;
      unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];
    } server;
  };
};

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

gab_value wrap_qfd(struct gab_triple gab, qfd_t qd, enum gab_io_k t,
                   bool owning) {
  gab_value vbox =
      gab_box(gab, (struct gab_box_argt){
                       .type = gab_string(gab, io_tname[t]),
                       .data = &qd,
                       .size = sizeof(qfd_t),
                       .destructor = owning ? io_tdestroy[t] : nullptr,
                   });

  struct gab_ssl_sock *sk = gab_boxdata(vbox);
  sk->sock.k = t;
  sk->sock.fd = qd;

  return vbox;
}

gab_value wrap_qfdssl(struct gab_triple gab, qfd_t qd, enum gab_io_k t,
                      bool owning) {
  gab_value vbox =
      gab_box(gab, (struct gab_box_argt){
                       .type = gab_string(gab, io_tname[t]),
                       .size = sizeof(struct gab_ssl_sock),
                       .destructor = owning ? io_tdestroy[t] : nullptr,
                   });

  struct gab_ssl_sock *sk = gab_boxdata(vbox);
  sk->sock.k = t;
  sk->sock.fd = qd;

  return vbox;
}

typedef gab_value (*wrap_fn)(struct gab_triple, qfd_t, enum gab_io_k, bool);

/*
 * BearSSL Read/Write callbacks, using QIO.
 */
static int sock_read(void *context, unsigned char *buf, size_t len) {
  struct gab_ssl_sock *ctx = context;

  for (;;) {
    qd_t qd = qrecv(ctx->sock.fd, len, buf);

    while (!qd_status(qd))
      switch (gab_yield(ctx->gab)) {
      case sGAB_TERM:
        return -1;
      case sGAB_COLL:
        gab_gcepochnext(ctx->gab);
        gab_sigpropagate(ctx->gab);
        break;
      default:
        break;
      }

    int64_t result = qd_destroy(qd);

    if (result <= 0) {
      if (result == -EINTR)
        continue;

      return -1;
    }

    return result;
  }
}

/*
 * Low-level data write callback for the simplified SSL I/O API.
 */
static int sock_write(void *context, const unsigned char *buf, size_t len) {
  struct gab_ssl_sock *ctx = context;

  for (;;) {
    qd_t qd = qsend(ctx->sock.fd, len, (unsigned char *)buf);

    while (!qd_status(qd))
      switch (gab_yield(ctx->gab)) {
      case sGAB_TERM:
        return -1;
      case sGAB_COLL:
        gab_gcepochnext(ctx->gab);
        gab_sigpropagate(ctx->gab);
        break;
      default:
        break;
      }

    int64_t result = qd_destroy(qd);

    if (result <= 0) {
      if (result < 0 && (-result) == EINTR) {
        continue;
      }
      return -1;
    }

    return result;
  }
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
union gab_value_pair complete_sslsockaccept(struct gab_triple gab,
                                            struct gab_ssl_sock *sock) {
  struct qio_addr addr = {};
  qd_t qd = qaccept(sock->sock.fd, &addr);

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

  if (result <= 0) {
    gab_vmpush(gab_thisvm(gab), gab_err, gab_string(gab, strerror(-result)));
    return gab_union_cvalid(gab_nil);
  }

  gab_value vclient = wrap_qfdssl(gab, result, IO_SOCK_SSLCLIENT, true);
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
   * Initialise the simplified I/O wrapper context.
   */
  br_sslio_init(&client->server.ioc, &client->server.sc.eng, sock_read, &client->sock.fd, sock_write, client);

  gab_vmpush(gab_thisvm(gab), gab_ok, vclient);
  return gab_union_cvalid(gab_nil);
};

union gab_value_pair complete_sockaccept(struct gab_triple gab,
                                         struct gab_sock *sock) {
  struct qio_addr addr = {};
  qd_t qd = qaccept(sock->fd, &addr);

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
    gab_vmpush(gab_thisvm(gab), gab_ok,
               wrap_qfd(gab, result, IO_SOCK_CLIENT, true));

  return gab_union_cvalid(gab_nil);
};

union gab_value_pair complete_sockbind(struct gab_triple gab,
                                       struct gab_sock *sock,
                                       const char *hostname, gab_int port) {
  struct qio_addr addr = {};
  if (qio_addrfrom(hostname, gab_valtou(port), &addr) < 0)
    return gab_vmpush(gab_thisvm(gab), gab_err,
                      gab_string(gab, "Invalid address")),
           gab_union_cvalid(gab_nil);

  qd_t qd = qbind(sock->fd, &addr);

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
};

union gab_value_pair complete_sockconnect(struct gab_triple gab,
                                          struct gab_sock *sock,
                                          const char *hostname, gab_int port) {
  struct qio_addr addr = {};
  if (qio_addrfrom(hostname, port, &addr) < 0)
    return gab_vmpush(gab_thisvm(gab), gab_err,
                      gab_string(gab, "Invalid address")),
           gab_union_cvalid(gab_nil);

  qd_t qd = qconnect(sock->fd, &addr);

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
    return gab_vmpush(gab_thisvm(gab), gab_err,
                      gab_string(gab, strerror(-result))),
           gab_union_cvalid(gab_nil);

  return gab_vmpush(gab_thisvm(gab), gab_ok), gab_union_cvalid(gab_nil);
}

union gab_value_pair complete_sockrecv(struct gab_triple gab,
                                       struct gab_sock *sock, size_t len) {

  uint8_t buff[len];
  qd_t qd = qrecv(sock->fd, sizeof(buff), buff);

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

union gab_value_pair complete_sslsockrecv(struct gab_triple gab,
                                          struct gab_ssl_sock *sock,
                                          size_t len) {
  unsigned char buf[len];

  sock->gab = gab;
  int64_t result = br_sslio_read_all(&sock->client.ioc, buf, sizeof buf);

  if (result < 0) {
    // TODO: Check error properly here, above err always returns -1
    gab_vmpush(gab_thisvm(gab), gab_err, gab_string(gab, strerror(-result)));
    return gab_union_cvalid(gab_nil);
  }

  // If we got zero bytes, or received as many total as expected...
  gab_vmpush(gab_thisvm(gab), gab_ok, gab_nbinary(gab, len, (char *)buf));
  return gab_union_cvalid(gab_nil);
}

union gab_value_pair complete_socksend(struct gab_triple gab,
                                       struct gab_sock *sock, const char *data,
                                       size_t len) {
  for (;;) {
    qd_t qd = qsend(sock->fd, len, (uint8_t *)data);

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
}

union gab_value_pair complete_sslsocksend(struct gab_triple gab,
                                          struct gab_ssl_sock *sock,
                                          const char *data, size_t len) {

  sock->gab = gab;
  int64_t result = br_sslio_write_all(&sock->client.ioc, data, len);

  if (result < 0) {
    int err = br_ssl_engine_last_error(&sock->client.sc.eng);
    gab_vmpush(gab_thisvm(gab), gab_err, gab_string(gab, "Error during write"),
               gab_number(err));
    return gab_union_cvalid(gab_nil);
  }

  result = br_sslio_flush(&sock->client.ioc);

  if (result < 0) {
    gab_vmpush(gab_thisvm(gab), gab_err, gab_string(gab, "Error during flush"));
    return gab_union_cvalid(gab_nil);
  }

  gab_vmpush(gab_thisvm(gab), gab_ok);
  return gab_union_cvalid(gab_nil);
}

union gab_value_pair complete_sslsockconnect(struct gab_triple gab,
                                             struct gab_ssl_sock *sock,
                                             const char *hostname,
                                             gab_int port) {
  struct qio_addr addr = {};
  if (qio_addrfrom(hostname, port, &addr) < 0)
    return gab_vmpush(gab_thisvm(gab), gab_err,
                      gab_string(gab, "Invalid address")),
           gab_union_cvalid(gab_nil);

  qd_t qd = qconnect(sock->sock.fd, &addr);

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
    return gab_vmpush(gab_thisvm(gab), gab_err,
                      gab_string(gab, strerror(-result))),
           gab_union_cvalid(gab_nil);

  /*
   * Initialise the client context:
   * -- Use the "full" profile (all supported algorithms).
   * -- The provided X.509 validation engine is initialised, with
   *    the hardcoded trust anchor.
   */
  br_ssl_client_init_full(&sock->client.sc, &sock->client.xc, TAs, TAs_NUM);

  /*
   * Set the I/O buffer to the provided array. We allocated a
   * buffer large enough for full-duplex behaviour with all
   * allowed sizes of SSL records, hence we set the last argument
   * to 1 (which means "split the buffer into separate input and
   * output areas").
   */
  br_ssl_engine_set_buffer(&sock->client.sc.eng, sock->client.iobuf,
                           sizeof(sock->client.iobuf), true);

  /*
   * Reset the client context, for a new handshake. We provide the
   * target host name: it will be used for the SNI extension. The
   * last parameter is 0: we are not trying to resume a session.
   */
  br_ssl_client_reset(&sock->client.sc, hostname, 0);

  /*
   * Initialise the simplified I/O wrapper context, to use our
   * SSL client context, and the two callbacks for socket I/O.
   */
  br_sslio_init(&sock->client.ioc, &sock->client.sc.eng, sock_read,
                &sock->sock.fd, sock_write, sock);

  return gab_vmpush(gab_thisvm(gab), gab_ok), gab_union_cvalid(gab_nil);
};

gab_value create_tcp(struct gab_triple gab) {
  return complete_sockcreate(gab, qsocket(QSOCK_TCP), wrap_qfd,
                             IO_SOCK_UNSPECIFIED);
}

gab_value create_udp(struct gab_triple gab) {
  return complete_sockcreate(gab, qsocket(QSOCK_UDP), wrap_qfd,
                             IO_SOCK_UNSPECIFIED);
}

gab_value create_tcpssl(struct gab_triple gab) {
  return complete_sockcreate(gab, qsocket(QSOCK_TCP), wrap_qfdssl,
                             IO_SOCK_SSLUNSPECIFIED);
}

gab_value create_udpssl(struct gab_triple gab) {
  return complete_sockcreate(gab, qsocket(QSOCK_UDP), wrap_qfdssl,
                             IO_SOCK_SSLUNSPECIFIED);
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

union gab_value_pair gab_iolib_send(struct gab_triple gab, uint64_t argc,
                                    gab_value argv[argc]) {
  gab_value vsock = gab_arg(0);

  if (gab_valkind(vsock) != kGAB_BOX)
    return gab_ptypemismatch(gab, vsock, gab_string(gab, tGAB_IOSOCK));

  gab_value str = gab_arg(1);

  if (gab_valkind(str) != kGAB_BINARY)
    return gab_pktypemismatch(gab, str, kGAB_BINARY);

  struct gab_sock *sock = gab_boxdata(vsock);

  const char *data = gab_strdata(&str);
  size_t len = gab_strlen(str);

  switch (sock->k) {
  case IO_SOCK_CLIENT:
    return complete_socksend(gab, sock, data, len);
  case IO_SOCK_SSLCLIENT:
    return complete_sslsocksend(gab, (struct gab_ssl_sock *)sock, data, len);
  default:
    return gab_panicf(gab, "$ may not send", vsock);
  }

  return gab_panicf(gab, "Reached unreachable codepath");
}

union gab_value_pair gab_iolib_recv(struct gab_triple gab, uint64_t argc,
                                    gab_value argv[argc]) {
  gab_value vsock = gab_arg(0);
  gab_value msglen = gab_arg(1);

  if (gab_valkind(vsock) != kGAB_BOX)
    return gab_ptypemismatch(gab, vsock, gab_string(gab, tGAB_IOSOCK));

  if (msglen == gab_nil)
    msglen = gab_number(1024);

  if (gab_valkind(msglen) != kGAB_NUMBER)
    return gab_pktypemismatch(gab, vsock, msglen);

  if (gab_valtoi(msglen) <= 0)
    return gab_panicf(gab, "Message length must be greater than 0. (not $)",
                      msglen);

  struct gab_sock *sock = gab_boxdata(vsock);
  gab_uint len = gab_valtou(msglen);

  switch (sock->k) {
  case IO_SOCK_CLIENT:
    return complete_sockrecv(gab, sock, len);
  case IO_SOCK_SSLCLIENT:
    return complete_sslsockrecv(gab, (struct gab_ssl_sock *)sock, len);
  default:
    return gab_panicf(gab, "$ may not recv", vsock);
  }
}

union gab_value_pair gab_iolib_connect(struct gab_triple gab, uint64_t argc,
                                       gab_value argv[argc]) {
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

  switch (sock->k) {
  case IO_SOCK_UNSPECIFIED: {
    enum gab_io_k unspec = IO_SOCK_UNSPECIFIED;
    if (!atomic_compare_exchange_strong(&sock->k, &unspec, IO_SOCK_CLIENT))
      return gab_vmpush(
                 gab_thisvm(gab), gab_err,
                 gab_string(gab, "Socket is already connected or bound")),
             gab_union_cvalid(gab_nil);

    return complete_sockconnect(gab, sock, gab_strdata(&ip), gab_valtoi(port));
  }
  case IO_SOCK_SSLUNSPECIFIED: {
    enum gab_io_k unspec = IO_SOCK_SSLUNSPECIFIED;
    if (!atomic_compare_exchange_strong(&sock->k, &unspec, IO_SOCK_SSLCLIENT))
      return gab_vmpush(
                 gab_thisvm(gab), gab_err,
                 gab_string(gab, "Socket is already connected or bound")),
             gab_union_cvalid(gab_nil);

    return complete_sslsockconnect(gab, (struct gab_ssl_sock *)sock,
                                   gab_strdata(&ip), gab_valtoi(port));
  }
  default:
    return gab_vmpush(gab_thisvm(gab), gab_err,
                      gab_string(gab, "Socket is in invalid state")),
           gab_union_cvalid(gab_nil);
  };
}

union gab_value_pair gab_iolib_accept(struct gab_triple gab, uint64_t argc,
                                      gab_value argv[argc]) {
  gab_value vsock = gab_arg(0);

  if (gab_valkind(vsock) != kGAB_BOX)
    return gab_ptypemismatch(gab, vsock, gab_string(gab, tGAB_IOSOCK));

  struct gab_sock *sock = gab_boxdata(vsock);
  switch (sock->k) {
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

  switch (sock->k) {
  case IO_SOCK_UNSPECIFIED: {
    enum gab_io_k unspec = IO_SOCK_UNSPECIFIED;
    if (!atomic_compare_exchange_strong(&sock->k, &unspec, IO_SOCK_SERVER))
      return gab_vmpush(
                 gab_thisvm(gab), gab_err,
                 gab_string(gab, "Socket is already connected or bound")),
             gab_union_cvalid(gab_nil);

    return complete_sockbind(gab, sock, gab_strdata(&ip), gab_valtoi(port));
  }
  case IO_SOCK_SSLUNSPECIFIED: {
    enum gab_io_k unspec = IO_SOCK_SSLUNSPECIFIED;
    if (!atomic_compare_exchange_strong(&sock->k, &unspec, IO_SOCK_SSLSERVER))
      return gab_vmpush(
                 gab_thisvm(gab), gab_err,
                 gab_string(gab, "Socket is already connected or bound")),
             gab_union_cvalid(gab_nil);

    return complete_sockbind(gab, sock, gab_strdata(&ip), gab_valtoi(port));
  }
  default:
    return gab_vmpush(gab_thisvm(gab), gab_err,
                      gab_string(gab, "Socket is in invalid state")),
           gab_union_cvalid(gab_nil);
  }
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
              gab_message(gab, "file\\t"),
              mod,
              file_t,
          },
          {
              gab_message(gab, "sock\\t"),
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
              gab_message(gab, "make"),
              gab_strtomsg(sock_t),
              gab_snative(gab, "make", gab_iolib_sock),
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

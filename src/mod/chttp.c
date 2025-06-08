#include "gab.h"
#include "llhttp.h"

#include "furi/code/furi/furi.h"

#define M_HTTP_VERSION "http\\version"
#define M_HTTP_METHOD "http\\method"
#define M_HTTP_HEADERS "http\\headers"
#define M_HTTP_URL "http\\uri"
#define M_HTTP_BODY "http\\body"
#define M_HTTP_STATUS "http\\status"

#define M_URL_SCHEME "uri\\scheme"
#define M_URL_AUTHORITY "uri\\authority"
#define M_URL_PATH "uri\\path"
#define M_URL_QUERY "uri\\query"
#define M_URL_FRAGMENT "uri\\fragment"

#define MAX_SLICES 128

struct slice {
  const char *ptr;
  size_t len;
};

struct ParserData {
  struct gab_triple gab;
  struct slice url;
  struct slice body;

  int nheaders;
  struct slice headers[MAX_SLICES];
};

int on_url(llhttp_t *parser, const char *ptr, size_t len) {
  struct ParserData *pd = parser->data;
  pd->url = (struct slice){.ptr = ptr, .len = len};
  return HPE_OK;
}

int on_header_field(llhttp_t *parser, const char *ptr, size_t len) {
  struct ParserData *pd = parser->data;

  if (!len)
    return HPE_OK;

  if (pd->nheaders >= MAX_SLICES)
    return -1;

  pd->headers[pd->nheaders++] = (struct slice){.ptr = ptr, .len = len};
  return HPE_OK;
}

int on_header_value(llhttp_t *parser, const char *ptr, size_t len) {
  struct ParserData *pd = parser->data;

  if (!len)
    return HPE_OK;

  if (pd->nheaders >= MAX_SLICES)
    return -1;

  pd->headers[pd->nheaders++] = (struct slice){.ptr = ptr, .len = len};
  return HPE_OK;
}

int on_body(llhttp_t *parser, const char *ptr, size_t len) {
  struct ParserData *pd = parser->data;

  if (!len)
    return HPE_OK;

  pd->body = (struct slice){.ptr = ptr, .len = len};
  return HPE_OK;
}

gab_value build_method(llhttp_t *parser) {
  struct ParserData *pd = parser->data;
  switch (llhttp_get_method(parser)) {
  case HTTP_DELETE:
    return gab_message(pd->gab, "DELETE");
  case HTTP_GET:
    return gab_message(pd->gab, "GET");
  case HTTP_HEAD:
    return gab_message(pd->gab, "HEAD");
  case HTTP_POST:
    return gab_message(pd->gab, "POST");
  case HTTP_PUT:
    return gab_message(pd->gab, "PUT");
  case HTTP_CONNECT:
    return gab_message(pd->gab, "CONNECT");
  case HTTP_OPTIONS:
    return gab_message(pd->gab, "OPTIONS");
  case HTTP_TRACE:
    return gab_message(pd->gab, "TRACE");
  case HTTP_COPY:
    return gab_message(pd->gab, "COPY");
  case HTTP_LOCK:
    return gab_message(pd->gab, "LOCK");
  case HTTP_MKCOL:
    return gab_message(pd->gab, "MKCOL");
  case HTTP_MOVE:
    return gab_message(pd->gab, "MOVE");
  case HTTP_PROPFIND:
    return gab_message(pd->gab, "PROPFIND");
  case HTTP_PROPPATCH:
    return gab_message(pd->gab, "PROPPATCH");
  case HTTP_SEARCH:
    return gab_message(pd->gab, "SEARCH");
  case HTTP_UNLOCK:
    return gab_message(pd->gab, "UNLOCK");
  case HTTP_BIND:
    return gab_message(pd->gab, "BIND");
  case HTTP_REBIND:
    return gab_message(pd->gab, "REBIND");
  case HTTP_UNBIND:
    return gab_message(pd->gab, "UNBIND");
  case HTTP_ACL:
    return gab_message(pd->gab, "ACL");
  case HTTP_REPORT:
    return gab_message(pd->gab, "REPORT");
  case HTTP_MKACTIVITY:
    return gab_message(pd->gab, "MKACTIVITY");
  case HTTP_CHECKOUT:
    return gab_message(pd->gab, "CHECKOUT");
  case HTTP_MERGE:
    return gab_message(pd->gab, "MERGE");
  case HTTP_MSEARCH:
    return gab_message(pd->gab, "MSEARCH");
  case HTTP_NOTIFY:
    return gab_message(pd->gab, "NOTIFY");
  case HTTP_SUBSCRIBE:
    return gab_message(pd->gab, "SUBSCRIBE");
  case HTTP_UNSUBSCRIBE:
    return gab_message(pd->gab, "UNSUBSCRIBE");
  case HTTP_PATCH:
    return gab_message(pd->gab, "PATCH");
  case HTTP_PURGE:
    return gab_message(pd->gab, "PURGE");
  case HTTP_MKCALENDAR:
    return gab_message(pd->gab, "MKCALENDAR");
  case HTTP_LINK:
    return gab_message(pd->gab, "LINK");
  case HTTP_UNLINK:
    return gab_message(pd->gab, "UNLINK");
  case HTTP_SOURCE:
    return gab_message(pd->gab, "SOURCE");
  case HTTP_PRI:
    return gab_message(pd->gab, "PRI");
  case HTTP_DESCRIBE:
    return gab_message(pd->gab, "DESCRIBE");
  case HTTP_ANNOUNCE:
    return gab_message(pd->gab, "ANNOUNCE");
  case HTTP_SETUP:
    return gab_message(pd->gab, "SETUP");
  case HTTP_PLAY:
    return gab_message(pd->gab, "PLAY");
  case HTTP_PAUSE:
    return gab_message(pd->gab, "PAUSE");
  case HTTP_TEARDOWN:
    return gab_message(pd->gab, "TEARDOWN");
  case HTTP_GET_PARAMETER:
    return gab_message(pd->gab, "GET_PARAMETER");
  case HTTP_SET_PARAMETER:
    return gab_message(pd->gab, "SET_PARAMETER");
  case HTTP_REDIRECT:
    return gab_message(pd->gab, "REDIRECT");
  case HTTP_RECORD:
    return gab_message(pd->gab, "RECORD");
  case HTTP_FLUSH:
    return gab_message(pd->gab, "FLUSH");
  case HTTP_QUERY:
    return gab_message(pd->gab, "QUERY");
  default:
    return gab_message(pd->gab, "UNKNOWN");
  }
}

gab_value furi_sv_to_value(struct gab_triple gab, furi_sv sv) {
  return gab_nstring(gab, furi_sv_length(sv), sv.begin);
}

gab_value build_path(struct gab_triple gab, furi_sv path) {
  v_gab_value elems = {0};
  for (furi_path_iter iter = furi_make_path_iter_begin(path);
       !furi_path_iter_is_done(iter); furi_path_iter_next(&iter)) {
    v_gab_value_push(&elems,
                     furi_sv_to_value(gab, furi_path_iter_get_value(iter)));
  }
  if (elems.len) {
  gab_value vpath = gab_list(gab, elems.len, elems.data);
  v_gab_value_destroy(&elems);
  return vpath;
  } else {
    return gab_listof(gab);
  }
};

gab_value build_query(struct gab_triple gab, furi_sv query) {
  v_gab_value elems = {0};

  for (furi_query_iter iter = furi_make_query_iter_begin(query);
       !furi_query_iter_is_done(iter); furi_query_iter_next(&iter)) {

    furi_query_iter_value elem = furi_query_iter_get_value(iter);
    v_gab_value_push(&elems, furi_sv_to_value(gab, elem.key));
    v_gab_value_push(&elems, furi_sv_to_value(gab, elem.value));
  }

  if (elems.len) {
    gab_value vquery =
        gab_record(gab, 2, elems.len / 2, elems.data, elems.data + 1);
    v_gab_value_destroy(&elems);
    return vquery;
  } else {
    return gab_listof(gab);
  }
};

gab_value build_url(struct gab_triple gab, const char *ptr, size_t len) {
  furi_uri_split split = furi_split_uri(furi_make_sv(ptr, ptr + len));

  v_gab_value kvps = {0};

  if (furi_sv_is_null(split.path))
    return gab_cundefined;

  v_gab_value_push(&kvps, gab_message(gab, M_URL_SCHEME));
  v_gab_value_push(&kvps, furi_sv_to_value(gab, split.scheme));

  // TODO: Pull this out appropriately
  v_gab_value_push(&kvps, gab_message(gab, M_URL_AUTHORITY));
  v_gab_value_push(&kvps, furi_sv_to_value(gab, split.authority));

  v_gab_value_push(&kvps, gab_message(gab, M_URL_PATH));
  v_gab_value_push(&kvps, build_path(gab, split.path));

  v_gab_value_push(&kvps, gab_message(gab, M_URL_QUERY));
  v_gab_value_push(&kvps, build_query(gab, split.query));

  v_gab_value_push(&kvps, gab_message(gab, M_URL_FRAGMENT));
  v_gab_value_push(&kvps, furi_sv_to_value(gab, split.fragment));

  gab_value url = gab_record(gab, 2, kvps.len / 2, kvps.data, kvps.data + 1);
  v_gab_value_destroy(&kvps);

  return url;
}

gab_value build_headers(struct ParserData *pd) {
  if (pd->nheaders) {
    gab_value headers[pd->nheaders];
    int nheaders = pd->nheaders / 2;

    for (size_t i = 0; i < nheaders; i++) {
      struct slice hf = pd->headers[(i * 2)];
      struct slice hv = pd->headers[1 + (i * 2)];
      assert(hf.len);
      assert(hv.len);
      headers[i * 2] = gab_nstring(pd->gab, hf.len, hf.ptr);
      headers[1 + (i * 2)] = gab_nstring(pd->gab, hv.len, hv.ptr);
    }

    return gab_record(pd->gab, 2, nheaders, headers, headers + 1);
  } else {
    return gab_listof(pd->gab);
  }
}

gab_value build_http(llhttp_t *parser) {
  struct ParserData *pd = parser->data;
  gab_gclock(pd->gab);

  switch (llhttp_get_type(parser)) {
  case HTTP_REQUEST: {
    assert(pd->url.len);

    gab_value kvps[] = {
        gab_message(pd->gab, M_HTTP_VERSION),
        gab_number(llhttp_get_http_major(parser) +
                   llhttp_get_http_minor(parser) / 10.0f),

        gab_message(pd->gab, M_HTTP_METHOD),
        build_method(parser),

        gab_message(pd->gab, M_HTTP_HEADERS),
        build_headers(pd),

        gab_message(pd->gab, M_HTTP_URL),
        gab_nstring(pd->gab, pd->url.len, pd->url.ptr),

        gab_message(pd->gab, M_HTTP_BODY),
        gab_nstring(pd->gab, pd->body.len, pd->body.ptr),
    };

    gab_value rec = gab_record(
        pd->gab, 2, (sizeof(kvps) / sizeof(gab_value) / 2), kvps, kvps + 1);

    return gab_gcunlock(pd->gab), rec;
  }
  case HTTP_RESPONSE: {
    gab_value kvps[] = {
        gab_message(pd->gab, M_HTTP_VERSION),
        gab_number(llhttp_get_http_major(parser) +
                   llhttp_get_http_minor(parser) / 10.0),

        gab_message(pd->gab, M_HTTP_STATUS),
        gab_number(llhttp_get_status_code(parser)),

        gab_message(pd->gab, M_HTTP_HEADERS),
        build_headers(pd),

        gab_message(pd->gab, M_HTTP_BODY),
        gab_nstring(pd->gab, pd->body.len, pd->body.ptr),
    };

    gab_value rec = gab_record(
        pd->gab, 2, (sizeof(kvps) / sizeof(gab_value) / 2), kvps, kvps + 1);

    return gab_gcunlock(pd->gab), rec;
  }
  }

  assert(false && "UNREACHABLE");
}

union gab_value_pair gab_urilib_decode(struct gab_triple gab, uint64_t argc,
                                       gab_value argv[static argc]) {
  gab_value vuri = gab_arg(0);

  gab_value res = build_url(gab, gab_strdata(&vuri), gab_strlen(vuri));
  if (res == gab_cundefined)
    return gab_vmpush(gab_thisvm(gab), gab_err,
                      gab_string(gab, "Failed to parse uri")),
           gab_union_cvalid(gab_nil);
  else
    return gab_vmpush(gab_thisvm(gab), gab_ok, res), gab_union_cvalid(gab_nil);
}

union gab_value_pair gab_httplib_decode(struct gab_triple gab, uint64_t argc,
                                        gab_value argv[static argc]) {
  gab_value vreq = gab_arg(0);

  llhttp_t parser;
  llhttp_settings_t settings;

  /*Initialize user callbacks and settings */
  llhttp_settings_init(&settings);
  settings.on_url = on_url;
  settings.on_header_field = on_header_field;
  settings.on_header_value = on_header_value;
  settings.on_body = on_body;

  struct ParserData pd = {.gab = gab};

  llhttp_init(&parser, HTTP_BOTH, &settings);
  parser.data = &pd;

  enum llhttp_errno err =
      llhttp_execute(&parser, gab_strdata(&vreq), gab_strlen(vreq));

  if (err == HPE_OK)
    gab_vmpush(gab_thisvm(gab), gab_ok, build_http(&parser));
  else
    gab_vmpush(gab_thisvm(gab), gab_err,
               gab_string(gab, llhttp_errno_name(err)));

  return gab_union_cvalid(gab_nil);
};

GAB_DYNLIB_MAIN_FN {
  gab_def(gab,
          {
              gab_message(gab, "as\\http"),
              gab_type(gab, kGAB_STRING),
              gab_snative(gab, "as\\http", gab_httplib_decode),
          },
          {
              gab_message(gab, "as\\uri"),
              gab_type(gab, kGAB_STRING),
              gab_snative(gab, "as\\uri", gab_urilib_decode),
          });

  gab_value res[] = {gab_ok};

  return (union gab_value_pair){
      .status = gab_cvalid,
      .aresult = a_gab_value_create(res, sizeof(res) / sizeof(gab_value)),
  };
}

#if GAB_PLATFORM_UNIX
/*
 * Termbox2 doesn't currently support windows - however there is an out-of-date PR that
 * covers it.
 */

#define TB_IMPL
#include "../vendor/termbox/termbox2.h"

#include "gab.h"
/**
 * TEMRMBOX API
 * int tb_init();
 * int tb_shutdown();
 *
 * int tb_width();
 * int tb_height();
 *
 * int tb_clear();
 * int tb_present();
 *
 * int tb_set_cursor(int cx, int cy);
 * int tb_hide_cursor();
 *
 * int tb_set_cell(int x, int y, uint32_t ch, uintattr_t fg, uintattr_t bg);
 *
 * int tb_peek_event(struct tb_event *event, int timeout_ms);
 * int tb_poll_event(struct tb_event *event);
 *
 * int tb_print(int x, int y, uintattr_t fg, uintattr_t bg, const char *str);
 * int tb_printf(int x, int y, uintattr_t fg, uintattr_t bg, const char *fmt,
 * ...);
 */

FILE *ogin = nullptr, *ogout = nullptr, *ogerr = nullptr;

gab_value singleton = gab_invalid;

void gab_termshutdown(struct gab_triple gab, size_t, char *) {
  tb_clear();
  tb_shutdown();

  gab.eg->sin = ogin;
  gab.eg->sout = ogout;
  gab.eg->serr = ogerr;
}

a_gab_value *gab_termlib_make(struct gab_triple gab, uint64_t argc,
                              gab_value argv[static argc]) {
  tb_init();

  if (singleton != gab_invalid) {
    gab_vmpush(gab_thisvm(gab), singleton);
    return nullptr;
  }

  singleton = gab_box(gab, (struct gab_box_argt){
                               .type = gab_string(gab, tGAB_TERMINAL),
                               .destructor = gab_termshutdown,
                           });

  ogin = gab.eg->sin;
  ogout = gab.eg->sout;
  ogerr = gab.eg->serr;

  gab.eg->sin = tmpfile();
  gab.eg->sout = tmpfile();
  gab.eg->serr = tmpfile();

  gab_vmpush(gab_thisvm(gab), singleton);
  return nullptr;
}

a_gab_value *gab_termlib_shutdown(struct gab_triple gab, uint64_t argc,
                                  gab_value argv[static argc]) {
  tb_clear();
  tb_shutdown();

  gab.eg->sin = ogin;
  gab.eg->sout = ogout;
  gab.eg->serr = ogerr;
  return nullptr;
}

a_gab_value *gab_termlib_width(struct gab_triple gab, uint64_t argc,
                               gab_value argv[static argc]) {
  size_t w = tb_width();

  gab_vmpush(gab_thisvm(gab), gab_number(w));

  return nullptr;
}

a_gab_value *gab_termlib_height(struct gab_triple gab, uint64_t argc,
                                gab_value argv[static argc]) {
  size_t w = tb_height();

  gab_vmpush(gab_thisvm(gab), gab_number(w));

  return nullptr;
}

a_gab_value *gab_termlib_clear(struct gab_triple gab, uint64_t argc,
                               gab_value argv[static argc]) {
  tb_clear();

  return nullptr;
}

a_gab_value *gab_termlib_present(struct gab_triple gab, uint64_t argc,
                                 gab_value argv[static argc]) {
  tb_present();

  return nullptr;
}

a_gab_value *gab_termlib_setcursor(struct gab_triple gab, uint64_t argc,
                                   gab_value argv[static argc]) {
  gab_value x = gab_arg(1);
  gab_value y = gab_arg(2);

  if (gab_valkind(x) != kGAB_NUMBER)
    return gab_pktypemismatch(gab, x, kGAB_NUMBER);

  if (gab_valkind(y) != kGAB_NUMBER)
    return gab_pktypemismatch(gab, y, kGAB_NUMBER);

  tb_set_cursor(gab_valtou(x), gab_valtou(y));

  return nullptr;
}

a_gab_value *gab_termlib_hidecursor(struct gab_triple gab, uint64_t argc,
                                    gab_value argv[static argc]) {
  tb_hide_cursor();

  return nullptr;
}

a_gab_value *gab_termlib_setcell(struct gab_triple gab, uint64_t argc,
                                 gab_value argv[static argc]) {
  gab_value x = gab_arg(1);
  gab_value y = gab_arg(2);

  if (gab_valkind(x) != kGAB_NUMBER)
    return gab_pktypemismatch(gab, x, kGAB_NUMBER);

  if (gab_valkind(y) != kGAB_NUMBER)
    return gab_pktypemismatch(gab, y, kGAB_NUMBER);

  gab_value cp = gab_arg(3);

  if (gab_valkind(cp) != kGAB_STRING)
    return gab_pktypemismatch(gab, cp, kGAB_STRING);

  uint32_t uni;

  tb_utf8_char_to_unicode(&uni, gab_strdata(&cp));

  tb_set_cell(gab_valtou(x), gab_valtou(y), uni, 0, 0);

  return nullptr;
}

gab_value shape = gab_invalid;

gab_value type_to_value(struct gab_triple gab, uint8_t t) {
  switch (t) {
  case TB_EVENT_KEY:
    return gab_message(gab, "key");
  case TB_EVENT_MOUSE:
    return gab_message(gab, "mouse");
  case TB_EVENT_RESIZE:
    return gab_message(gab, "resize");
  default:
    return gab_nil;
  }
}

gab_value ch_to_value(struct gab_triple gab, uint32_t ch) {
  char buf[8];
  tb_utf8_unicode_to_char(buf, ch);
  return gab_string(gab, buf);
}

gab_value key_to_value(struct gab_triple gab, uint16_t key) {
  switch (key) {
  case TB_KEY_BACKSPACE:
  case TB_KEY_BACKSPACE2:
    return gab_message(gab, "backspace");
  case TB_KEY_ENTER:
    return gab_message(gab, "enter");
  case TB_KEY_ARROW_UP:
    return gab_message(gab, "arrow\\up");
  case TB_KEY_ARROW_DOWN:
    return gab_message(gab, "arrow\\down");
  case TB_KEY_ARROW_LEFT:
    return gab_message(gab, "arrow\\left");
  case TB_KEY_ARROW_RIGHT:
    return gab_message(gab, "arrow\\right");
  case TB_KEY_CTRL_C:
    gab_sigterm(gab);
  default:
    return gab_number(key);
  }
}

gab_value mod_to_value(struct gab_triple gab, uint8_t mod) {
  switch (mod) {
  case TB_MOD_ALT:
    return gab_message(gab, "alt");
  case TB_MOD_CTRL:
    return gab_message(gab, "ctrl");
  case TB_MOD_SHIFT:
    return gab_message(gab, "shift");
  case TB_MOD_MOTION:
    return gab_message(gab, "motion");
  default:
    return gab_message(gab, "key");
  }
}

a_gab_value *gab_termlib_peekevent(struct gab_triple gab, uint64_t argc,
                                   gab_value argv[static argc]) {
  assert(shape != gab_invalid);

  struct tb_event ev = {};
  tb_peek_event(&ev, 1 << 6);

  gab_value vals[] = {
      gab_number(ev.x),
      gab_number(ev.y),
      gab_number(ev.w),
      gab_number(ev.h),
      type_to_value(gab, ev.type),
      ch_to_value(gab, ev.ch),
      key_to_value(gab, ev.key),
      mod_to_value(gab, ev.mod),
  };

  gab_value rec = gab_recordfrom(gab, shape, 1, vals);
  gab_vmpush(gab_thisvm(gab), rec);

  return nullptr;
}

a_gab_value *gab_termlib_pollevent(struct gab_triple gab, uint64_t argc,
                                   gab_value argv[static argc]) {
  assert(shape != gab_invalid);

  struct tb_event ev = {};
  /*tb_poll_event(&ev);*/

  while (tb_peek_event(&ev, cGAB_CHANNEL_STEP_MS) == TB_ERR_NO_EVENT)
    switch (gab_yield(gab)) {
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    case sGAB_TERM:
      return nullptr;
    default:
      break;
    };

  gab_value vals[] = {
      gab_number(ev.x),
      gab_number(ev.y),
      gab_number(ev.w),
      gab_number(ev.h),
      type_to_value(gab, ev.type),
      ch_to_value(gab, ev.ch),
      key_to_value(gab, ev.key),
      mod_to_value(gab, ev.mod),
  };

  gab_value rec = gab_recordfrom(gab, shape, 1, vals);
  gab_vmpush(gab_thisvm(gab), rec);

  return nullptr;
}

a_gab_value *gab_termlib_print(struct gab_triple gab, uint64_t argc,
                               gab_value argv[static argc]) {
  gab_value x = gab_arg(1);
  gab_value y = gab_arg(2);
  gab_value str = gab_arg(3);

  if (gab_valkind(x) != kGAB_NUMBER)
    return gab_pktypemismatch(gab, x, kGAB_NUMBER);

  if (gab_valkind(y) != kGAB_NUMBER)
    return gab_pktypemismatch(gab, y, kGAB_NUMBER);

  if (gab_valkind(str) != kGAB_STRING)
    return gab_pktypemismatch(gab, str, kGAB_STRING);

  const char *bytes = gab_strdata(&str);

  int res = tb_print(gab_valtou(x), gab_valtou(y), 0, 0, bytes);

  gab_vmpush(gab_thisvm(gab), res == TB_ERR_OUT_OF_BOUNDS ? gab_err : gab_ok);

  return nullptr;
};

GAB_DYNLIB_MAIN_FN {
  gab_value t = gab_string(gab, tGAB_TERMINAL);
  gab_value t_m = gab_strtomsg(t);

  shape = gab_shapeof(gab, gab_message(gab, "x"), gab_message(gab, "y"),
                      gab_message(gab, "w"), gab_message(gab, "h"),
                      gab_message(gab, "type"), gab_message(gab, "ch"),
                      gab_message(gab, "key"), gab_message(gab, "mod"));

  gab_def(gab,
          {
              gab_message(gab, "t"),
              gab_strtomsg(t),
              t,
          },
          {
              gab_message(gab, "make"),
              t_m,
              gab_snative(gab, "make", gab_termlib_make),
          },
          {
              gab_message(gab, "width"),
              t,
              gab_snative(gab, "width", gab_termlib_width),
          },
          {
              gab_message(gab, "height"),
              t,
              gab_snative(gab, "height", gab_termlib_height),
          },
          {
              gab_message(gab, "clear!"),
              t,
              gab_snative(gab, "clear!", gab_termlib_clear),
          },
          {
              gab_message(gab, "render!"),
              t,
              gab_snative(gab, "render!", gab_termlib_present),
          },
          {
              gab_message(gab, "shutdown!"),
              t,
              gab_snative(gab, "shutdown!", gab_termlib_shutdown),
          },
          {
              gab_message(gab, "print!"),
              t,
              gab_snative(gab, "print!", gab_termlib_print),
          },
          {
              gab_message(gab, "event\\peek"),
              t,
              gab_snative(gab, "event\\peek", gab_termlib_peekevent),
          },
          {
              gab_message(gab, "event\\poll"),
              t,
              gab_snative(gab, "event\\poll", gab_termlib_pollevent),
          },
          {
              gab_message(gab, "cursor\\set"),
              t,
              gab_snative(gab, "cursor\\set", gab_termlib_setcursor),
          },
          {
              gab_message(gab, "cursor\\hide"),
              t,
              gab_snative(gab, "cursor\\hide", gab_termlib_hidecursor),
          },
          {
              gab_message(gab, "cell\\set"),
              t,
              gab_snative(gab, "cell\\set", gab_termlib_setcell),
          });

  gab_value results[] = {gab_ok, t_m};

  return a_gab_value_create(results, sizeof(results) / sizeof(gab_value));
}
#endif

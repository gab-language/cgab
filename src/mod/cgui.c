#include <stdint.h>
#include <unistd.h>

// GL/gl.h won't work because some OS's have files which override.
// Use glad/gl.h instead.
#define GLAD_GL
#define GLAD_GL_IMPLEMENTATION
#include "glad/gl.h"

// GL/gl.h points to our glad directory. Therefore it is necessary
// to undefined these as further includes of "GL/gl.h"
// should not define implementations.
#undef GLAD_GL
#undef GLAD_GL_IMPLEMENTATION

#define RGFW_IMPLEMENTATION
#include "RGFW/RGFW.h"

#define CLAY_IMPLEMENTATION
#include "Clay/clay.h"

// #include <wchar.h>
// #define TB_OPT_LIBC_WCHAR
#define TB_OPT_EGC
#define TB_IMPL
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "Clay/renderers/termbox2/clay_renderer_termbox2.c"

#include <stdio.h>
#define FONTSTASH_IMPLEMENTATION
#include "fontstash/src/fontstash.h"

unsigned char fontData[] = {
#embed "resources/SauceCodeProNerdFont-Regular.ttf"
};

#define SOKOL_IMPL
#define SOKOL_GLCORE
#define SOKOL_EXTERNAL_GL_LOADER
#include "sokol/sokol_gfx.h"
#include "sokol/sokol_log.h"
#include "sokol/util/sokol_gl.h"

#include "sokol/util/sokol_fontstash.h"

#define SOKOL_CLAY_NO_SOKOL_APP
#define SOKOL_CLAY_IMPL
#include "Clay/renderers/sokol/sokol_clay.h"

#include "gab.h"

void HandleClayErrors(Clay_ErrorData errorData) {
  printf("%s\n", errorData.errorText.chars);
}

Clay_Vector2 mousePosition;
bool clay_RGFW_update(RGFW_window *win, double deltaTime) {
  RGFW_event ev = win->event;
  switch (ev.type) {
  case RGFW_mouseButtonPressed: {
    switch (ev.button) {
    case RGFW_mouseScrollUp:
    case RGFW_mouseScrollDown:
      Clay_UpdateScrollContainers(false, (Clay_Vector2){0, ev.scroll},
                                  deltaTime);
      return true;
    default:
      Clay_SetPointerState(mousePosition,
                           RGFW_isMousePressed(win, RGFW_mouseLeft));
      return true;
    }

    return false;
  }
  case RGFW_mouseButtonReleased:
    Clay_SetPointerState(mousePosition,
                         RGFW_isMousePressed(win, RGFW_mouseLeft));
    return true;
  case RGFW_mousePosChanged:
    mousePosition = (Clay_Vector2){(float)ev.point.x, (float)ev.point.y};
    Clay_SetPointerState(mousePosition,
                         RGFW_isMousePressed(win, RGFW_mouseLeft));
    return false;
  case RGFW_windowMoved:
  case RGFW_windowResized:
    Clay_Dimensions dim = {(float)win->r.w, (float)win->r.h};
    sclay_set_layout_dimensions(dim, 1);
    return false;
  case RGFW_keyPressed:
  case RGFW_keyReleased:
    return true;
  default:
    return false;
  }
}

struct gui {
  struct RGFW_window win;
  struct gab_triple gab;
  gab_value appch, evch;
  uint64_t n;
};

struct gui gui;
gab_value clayGetTopmostId() {
  Clay_ElementIdArray arr = Clay_GetPointerOverIds();

  // Skip the root - we don't care about clicks there.
  for (size_t i = arr.length; i > 1; i--) {
    Clay_String str = arr.internalArray[i - 1].stringId;
    if (!str.length)
      continue;

    return gab_nmessage(gui.gab, str.length, str.chars);
  }

  return gab_cundefined;
}

void putevent(const char *type, const char *event, gab_value data1,
              gab_value data2, gab_value data3) {
  if (gui.evch != gab_cundefined) {
    gab_value ev[] = {
        gab_message(gui.gab, type),
        gab_message(gui.gab, event),
        data1,
        data2,
        data3,
    };

    size_t len = sizeof(ev) / sizeof(gab_value) - (data3 == gab_cundefined);

    gab_niref(gui.gab, 1, LEN_CARRAY(ev), ev);

    gab_nchnput(gui.gab, gui.evch, len, ev);

    gab_ndref(gui.gab, 1, LEN_CARRAY(ev), ev);
  }
}

#define RGFW_KEY_CASE(key, str)                                                \
  case RGFW_##key:                                                             \
    putevent("key", #str, gab_number(key_mod), gab_bool(down),                 \
             gab_cundefined);                                                  \
    break;

void onkey(RGFW_window *win, RGFW_key key, unsigned char key_char,
           RGFW_keymod key_mod, RGFW_bool down) {
  switch (key) {
    RGFW_KEY_CASE(enter, enter);
    RGFW_KEY_CASE(backSpace, backspace);
    RGFW_KEY_CASE(capsLock, capslock);
    RGFW_KEY_CASE(insert, insert);
    RGFW_KEY_CASE(end, end);
    RGFW_KEY_CASE(home, home);
    RGFW_KEY_CASE(pageUp, pageup);
    RGFW_KEY_CASE(pageDown, pagedown);
    RGFW_KEY_CASE(F1, f1);
    RGFW_KEY_CASE(F2, f2);
    RGFW_KEY_CASE(F3, f3);
    RGFW_KEY_CASE(F4, f4);
    RGFW_KEY_CASE(F5, f5);
    RGFW_KEY_CASE(F6, f6);
    RGFW_KEY_CASE(F7, f7);
    RGFW_KEY_CASE(F8, f8);
    RGFW_KEY_CASE(F9, f9);
    RGFW_KEY_CASE(F10, f10);
    RGFW_KEY_CASE(F11, f11);
    RGFW_KEY_CASE(F12, f12);
  default:
    const char ev[] = {key_char, '\0'};
    putevent("key", ev, gab_number(key_mod), gab_bool(down), gab_cundefined);
    break;
  }
}

void onmousebutton(RGFW_window *win, unsigned char b, double dbl,
                   unsigned char down) {
  gab_value target = clayGetTopmostId();
  switch (b) {
  case RGFW_mouseLeft:
    putevent("mouse", "left", gab_number(dbl), gab_bool(down), target);
    break;
  case RGFW_mouseRight:
    putevent("mouse", "right", gab_number(dbl), gab_bool(down), target);
    break;
  case RGFW_mouseScrollDown:
    putevent("mouse", "scroll\\down", gab_number(dbl), gab_bool(down),
             gab_cundefined);
    break;
  case RGFW_mouseScrollUp:
    putevent("mouse", "scroll\\up", gab_number(dbl), gab_bool(down),
             gab_cundefined);
    break;
  };
}

void onmousepos(RGFW_window *win, RGFW_point dst, RGFW_point) {
  // Mouse position events flood the event channel too much
  // if (evch != gab_cundefined) {
  //   gab_value ev[] = {
  //       gab_message(gui.gab, "mouse"),
  //       gab_message(gui.gab, "pos"),
  //       gab_number(dst.x),
  //       gab_number(dst.y),
  //   };
  //
  //   gab_nchnput(gui.gab, evch, sizeof(ev) / sizeof(gab_value), ev);
  // }
}

#define TERMBOX_KEY_CASE(key, str)                                             \
  case TB_KEY_##key:                                                           \
    putevent("key", #str, gab_number(e->mod), gab_bool(true), gab_cundefined); \
    break;

bool clay_termbox_update(struct tb_event *e, double deltaTime) {
  switch (e->type) {
  case TB_EVENT_RESIZE:
    return false;
  case TB_EVENT_KEY:
    switch (e->key) {
      TERMBOX_KEY_CASE(BACKSPACE, backspace);
      TERMBOX_KEY_CASE(ENTER, enter);
      // CAPSLOCK MISSING
      TERMBOX_KEY_CASE(INSERT, insert);
      TERMBOX_KEY_CASE(END, end);
      TERMBOX_KEY_CASE(HOME, home);
      TERMBOX_KEY_CASE(PGUP, pageup);
      TERMBOX_KEY_CASE(PGDN, pagedown);
      TERMBOX_KEY_CASE(F1, f1);
      TERMBOX_KEY_CASE(F2, f2);
      TERMBOX_KEY_CASE(F3, f3);
      TERMBOX_KEY_CASE(F4, f4);
      TERMBOX_KEY_CASE(F5, f5);
      TERMBOX_KEY_CASE(F6, f6);
      TERMBOX_KEY_CASE(F7, f7);
      TERMBOX_KEY_CASE(F8, f8);
      TERMBOX_KEY_CASE(F9, f9);
      TERMBOX_KEY_CASE(F10, f10);
      TERMBOX_KEY_CASE(F11, f11);
      TERMBOX_KEY_CASE(F12, f12);
    default:
      const char ev[] = {e->ch, '\0'};
      putevent("key", ev, gab_number(e->mod), gab_bool(true), gab_cundefined);
      break;
    }
    return true;
  case TB_EVENT_MOUSE:
    switch (e->key) {
    case TB_KEY_MOUSE_RELEASE:
      Clay_SetPointerState((Clay_Vector2){e->x, e->y}, false);
      putevent("mouse", "left", gab_number(0), gab_false, clayGetTopmostId());
      return true;
    case TB_KEY_MOUSE_RIGHT:
      Clay_SetPointerState((Clay_Vector2){e->x, e->y}, false);
      putevent("mouse", "right", gab_number(0), gab_true, clayGetTopmostId());
      return true;
    case TB_KEY_MOUSE_LEFT:
      Clay_SetPointerState((Clay_Vector2){e->x, e->y}, true);
      putevent("mouse", "left", gab_number(0), gab_true, clayGetTopmostId());
      return true;
    case TB_KEY_MOUSE_WHEEL_UP:
      Clay_UpdateScrollContainers(false, (Clay_Vector2){0, e->y}, deltaTime);
      putevent("mouse", "scroll\\up", gab_number(0), gab_false,
               clayGetTopmostId());
      return true;
    case TB_KEY_MOUSE_WHEEL_DOWN:
      Clay_UpdateScrollContainers(false, (Clay_Vector2){0, e->y}, deltaTime);
      putevent("mouse", "scroll\\down", gab_number(0), gab_false,
               clayGetTopmostId());
      return true;
    default:
      goto err;
    }
  }

err:
  assert(false && "UNREACHABLE");
  return false;
}

Clay_Color packedToClayColor(gab_value vcolor) {
  gab_uint color = gab_valtou(vcolor);

  return (Clay_Color){
      color >> 24 & 0xff,
      color >> 16 & 0xff,
      color >> 8 & 0xff,
      color & 0xff,
  };
}

Clay_Padding parsePadding(struct gab_triple gab, gab_value props) {
  gab_value vpadding = gab_mrecat(gab, props, "padding");
  gab_value vpaddingt = gab_mrecat(gab, props, "padding\\top");
  gab_value vpaddingb = gab_mrecat(gab, props, "padding\\bottom");
  gab_value vpaddingl = gab_mrecat(gab, props, "padding\\left");
  gab_value vpaddingr = gab_mrecat(gab, props, "padding\\right");

  if (gab_valkind(vpadding) == kGAB_NUMBER)
    return CLAY_PADDING_ALL(gab_valtou(vpadding));

  Clay_Padding p = CLAY_PADDING_ALL(0);
  if (gab_valkind(vpaddingt) == kGAB_NUMBER)
    p.top = gab_valtou(vpaddingt);
  if (gab_valkind(vpaddingb) == kGAB_NUMBER)
    p.bottom = gab_valtou(vpaddingb);
  if (gab_valkind(vpaddingl) == kGAB_NUMBER)
    p.left = gab_valtou(vpaddingl);
  if (gab_valkind(vpaddingr) == kGAB_NUMBER)
    p.right = gab_valtou(vpaddingr);

  return p;
}

Clay_Sizing parseSizing(struct gab_triple gab, gab_value props) {
  gab_value vfixedwidth = gab_mrecat(gab, props, "width");
  gab_value vrelwidth = gab_mrecat(gab, props, "width\\relative");
  gab_value vgrowwidth = gab_mrecat(gab, props, "width\\grow");

  bool hasfixedw = vfixedwidth != gab_cundefined;
  bool hasrelw = vrelwidth != gab_cundefined;
  bool hasgroww = vgrowwidth != gab_cundefined;

  Clay_SizingAxis w;
  if (hasfixedw) {
    if (gab_valkind(vfixedwidth) != kGAB_NUMBER)
      return (Clay_Sizing){};

    w = CLAY_SIZING_FIXED(gab_valtof(vfixedwidth));
  } else if (hasrelw) {
    if (gab_valkind(vrelwidth) != kGAB_NUMBER)
      return (Clay_Sizing){};
    double percent = gab_valtof(vrelwidth) / 100.f;
    w = CLAY_SIZING_PERCENT(percent);
  } else if (hasgroww) {
    if (gab_valkind(vgrowwidth) != kGAB_NUMBER)
      return (Clay_Sizing){};
    w = CLAY_SIZING_GROW(gab_valtou(vgrowwidth));
  } else {
    // w = (Clay_SizingAxis){0};
    w = CLAY_SIZING_FIT();
  }

  gab_value vfixedheight = gab_mrecat(gab, props, "height");
  gab_value vrelheight = gab_mrecat(gab, props, "height\\relative");
  gab_value vgrowheight = gab_mrecat(gab, props, "height\\grow");

  bool hasfixedh = vfixedheight != gab_cundefined;
  bool hasrelh = vrelheight != gab_cundefined;
  bool hasgrowh = vgrowheight != gab_cundefined;

  Clay_SizingAxis h;
  if (hasfixedh) {
    if (gab_valkind(vfixedheight) != kGAB_NUMBER)
      return (Clay_Sizing){};

    h = CLAY_SIZING_FIXED(gab_valtof(vfixedheight));
  } else if (hasrelh) {
    if (gab_valkind(vrelheight) != kGAB_NUMBER)
      return (Clay_Sizing){};
    double percent = gab_valtof(vrelheight) / 100.f;
    h = CLAY_SIZING_PERCENT(percent);
  } else if (hasgrowh) {
    if (gab_valkind(vgrowheight) != kGAB_NUMBER)
      return (Clay_Sizing){};
    h = CLAY_SIZING_GROW(gab_valtou(vgrowheight));
  } else {
    // h = (Clay_SizingAxis){0};
    h = CLAY_SIZING_FIT();
  }

  return (Clay_Sizing){.width = w, .height = h};
}

Clay_LayoutConfig parseLayout(struct gab_triple gab, gab_value props) {
  return (Clay_LayoutConfig){
      .layoutDirection = CLAY_TOP_TO_BOTTOM,
      .padding = parsePadding(gab, props),
      .sizing = parseSizing(gab, props),
  };
}

union gab_value_pair render_componentlist(struct gab_triple gab, gab_value app);

union gab_value_pair render_box(struct gab_triple gab, gab_value props,
                                gab_value children) {
  if (gab_valkind(props) != kGAB_RECORD)
    return gab_pktypemismatch(gab, props, kGAB_RECORD);

  if (gab_valkind(children) != kGAB_RECORD)
    return gab_pktypemismatch(gab, children, kGAB_RECORD);

  gab_value vborderColor = gab_cundefined;
  gab_value vborderWidth = gab_cundefined;
  gab_value vborder = gab_mrecat(gab, props, "border");
  if (vborder != gab_cundefined)
    vborderColor = gab_mrecat(gab, vborder, "color"),
    vborderWidth = gab_mrecat(gab, vborder, "width");

  if (vborderColor == gab_cundefined)
    vborderColor = gab_number(0);

  if (vborderWidth == gab_cundefined)
    vborderWidth = gab_number(0);

  if (gab_valkind(vborderWidth) != kGAB_NUMBER)
    return gab_pktypemismatch(gab, vborderWidth, kGAB_NUMBER);

  if (gab_valkind(vborderColor) != kGAB_NUMBER)
    return gab_pktypemismatch(gab, vborderColor, kGAB_NUMBER);

  gab_value vradius = gab_mrecat(gab, props, "radius");
  if (vradius == gab_cundefined)
    vradius = gab_number(0);

  if (gab_valkind(vradius) != kGAB_NUMBER)
    return gab_pktypemismatch(gab, vradius, kGAB_NUMBER);

  gab_float cornerRadius = gab_valtof(vradius);

  gab_value vid = gab_mrecat(gab, props, "id");
  if (vid != gab_cundefined && gab_valkind(vid) != kGAB_MESSAGE)
    return gab_pktypemismatch(gab, vid, kGAB_MESSAGE);

  CLAY({
      .id = vid == gab_cundefined ? CLAY_IDI("", gui.n++)
                                  : CLAY_SID(((Clay_String){
                                        .length = gab_strlen(vid),
                                        .chars = gab_strdata(&vid),
                                    })),
      .layout = parseLayout(gab, props),
      .cornerRadius =
          {
              cornerRadius,
              cornerRadius,
              cornerRadius,
              cornerRadius,
          },
      .border =
          {
              .color = packedToClayColor(vborderColor),
              .width =
                  {
                      gab_valtou(vborderWidth),
                      gab_valtou(vborderWidth),
                      gab_valtou(vborderWidth),
                      gab_valtou(vborderWidth),
                  },
          },
  }) {
    render_componentlist(gab, children);
  };

  return gab_union_cvalid(gab_nil);
}

union gab_value_pair render_rect(struct gab_triple gab, gab_value props) {
  if (gab_valkind(props) != kGAB_RECORD)
    return gab_pktypemismatch(gab, props, kGAB_RECORD);

  gab_value vx = gab_mrecat(gab, props, "x");
  if (vx == gab_cundefined)
    return gab_panicf(gab, "Props missing required key $",
                      gab_message(gab, "x"));

  gab_float x = gab_valtof(vx);

  gab_value vy = gab_mrecat(gab, props, "y");
  if (vy == gab_cundefined)
    return gab_panicf(gab, "Props missing required key $",
                      gab_message(gab, "y"));

  gab_float y = gab_valtof(vy);

  gab_value vw = gab_mrecat(gab, props, "w");
  if (vw == gab_cundefined)
    return gab_panicf(gab, "Props missing required key $",
                      gab_message(gab, "w"));

  gab_float w = gab_valtof(vw);

  gab_value vh = gab_mrecat(gab, props, "h");
  if (vh == gab_cundefined)
    return gab_panicf(gab, "Props missing required key $",
                      gab_message(gab, "h"));

  gab_float h = gab_valtof(vh);

  gab_value vcolor = gab_mrecat(gab, props, "color");
  if (vcolor == gab_cundefined)
    vcolor = gab_number(0xffffffff);

  if (gab_valkind(vcolor) != kGAB_NUMBER)
    return gab_pktypemismatch(gab, vcolor, kGAB_NUMBER);

  gab_value vid = gab_mrecat(gab, props, "id");
  if (vid != gab_cundefined && gab_valkind(vid) != kGAB_MESSAGE)
    return gab_pktypemismatch(gab, vid, kGAB_MESSAGE);

  CLAY({
      .id = vid == gab_cundefined ? (Clay_ElementId){0}
                                  : CLAY_SID(((Clay_String){
                                        .length = gab_strlen(vid),
                                        .chars = gab_strdata(&vid),
                                    })),
      .backgroundColor = packedToClayColor(vcolor),
      .layout = parseLayout(gab, props),
      .floating =
          {
              .attachTo = CLAY_ATTACH_TO_ROOT,
              .offset = {.x = x, .y = y},
              .expand = {.height = h, .width = w},
          },
  });

  return gab_union_cvalid(gab_nil);
}

union gab_value_pair render_image(struct gab_triple gab, gab_value props) {
  if (gab_valkind(props) != kGAB_RECORD)
    return gab_pktypemismatch(gab, props, kGAB_RECORD);

  gab_value vw = gab_mrecat(gab, props, "w");
  if (vw == gab_cundefined)
    return gab_panicf(gab, "Props missing required key $",
                      gab_message(gab, "w"));

  gab_float w = gab_valtof(vw);

  gab_value vh = gab_mrecat(gab, props, "h");
  if (vh == gab_cundefined)
    return gab_panicf(gab, "Props missing required key $",
                      gab_message(gab, "h"));

  gab_float h = gab_valtof(vh);

  gab_value vimage = gab_mrecat(gab, props, "content");
  if (vimage == gab_cundefined)
    return gab_panicf(gab, "Props missing required key $",
                      gab_message(gab, "content"));

  if (gab_valkind(vimage) != kGAB_BINARY)
    return gab_pktypemismatch(gab, vimage, kGAB_BINARY);

  gab_value vid = gab_mrecat(gab, props, "id");
  if (vid != gab_cundefined && gab_valkind(vid) != kGAB_MESSAGE)
    return gab_pktypemismatch(gab, vid, kGAB_MESSAGE);

  CLAY({
      .id = vid == gab_cundefined ? (Clay_ElementId){0}
                                  : CLAY_SID(((Clay_String){
                                        .length = gab_strlen(vid),
                                        .chars = gab_strdata(&vid),
                                    })),
      .layout =
          {
              .sizing =
                  {
                      .height = CLAY_SIZING_FIXED(h),
                      .width = CLAY_SIZING_FIXED(w),
                  },
          },
      .image =
          {
              .imageData = (void *)gab_strdata(&vimage),
          },
  });

  return gab_union_cvalid(gab_nil);
}

union gab_value_pair render_text(struct gab_triple gab, gab_value props) {
  if (gab_valkind(props) != kGAB_RECORD)
    return gab_pktypemismatch(gab, props, kGAB_RECORD);

  gab_value vtext = gab_mrecat(gab, props, "content");
  if (vtext == gab_cundefined)
    return gab_panicf(gab, "Props missing required key $",
                      gab_message(gab, "content"));

  if (gab_valkind(vtext) != kGAB_STRING)
    return gab_pktypemismatch(gab, vtext, kGAB_STRING);

  // This leaks every time
  uint64_t len = gab_strlen(vtext);
  char *str = malloc(len + 1);
  assert(str != nullptr);
  strcpy(str, gab_strdata(&vtext));

  // not guaranteed to work here.
  Clay_String text = {
      .length = len,
      .chars = str,
  };

  gab_value vsize = gab_mrecat(gab, props, "size");
  if (vsize == gab_cundefined)
    vsize = gab_number(16);

  if (gab_valkind(vsize) != kGAB_NUMBER)
    return gab_pktypemismatch(gab, vsize, kGAB_NUMBER);

  gab_uint size = gab_valtou(vsize);

  gab_value vcolor = gab_mrecat(gab, props, "color");
  if (vcolor == gab_cundefined)
    vcolor = gab_number(0xffffffff);

  if (gab_valkind(vcolor) != kGAB_NUMBER)
    return gab_pktypemismatch(gab, vcolor, kGAB_NUMBER);

  gab_value vspacing = gab_mrecat(gab, props, "spacing");
  if (vspacing == gab_cundefined)
    vspacing = gab_number(0);

  if (gab_valkind(vspacing) != kGAB_NUMBER)
    return gab_pktypemismatch(gab, vspacing, kGAB_NUMBER);

  gab_uint spacing = gab_valtou(vspacing);

  gab_value vheight = gab_mrecat(gab, props, "height");
  if (vheight == gab_cundefined)
    vheight = gab_number(0);

  if (gab_valkind(vheight) != kGAB_NUMBER)
    return gab_pktypemismatch(gab, vheight, kGAB_NUMBER);

  gab_uint height = gab_valtou(vheight);

  CLAY({
      .layout = parseLayout(gab, props),
  }) {
    CLAY_TEXT(text, CLAY_TEXT_CONFIG({
                        .fontSize = size,
                        .letterSpacing = spacing,
                        .lineHeight = height,
                        .textColor = packedToClayColor(vcolor),
                    }));
  };

  return gab_union_cvalid(gab_nil);
}

union gab_value_pair render_component(struct gab_triple gab,
                                      gab_value component) {
  if (gab_valkind(component) != kGAB_RECORD)
    return gab_pktypemismatch(gab, component, kGAB_RECORD);

  if (!gab_recisl(component))
    return gab_panicf(gab, "Expected a list, found $", component);

  if (gab_reclen(component) < 2)
    return gab_panicf(gab, "Expected a list of at least 2 elements, found $",
                      component);

  gab_value kind = gab_lstat(component, 0);
  gab_value props = gab_lstat(component, 1);

  if (kind == gab_message(gab, "text"))
    return render_text(gab, props);

  if (kind == gab_message(gab, "rect"))
    return render_rect(gab, props);

  // Currently unsupported by sokol_clay.h
  // if (kind == gab_message(gab, "img"))
  //   return render_image(gab, props);

  if (gab_reclen(component) < 3)
    return gab_panicf(gab, "Expected a list of at least 3 elements, found $",
                      component);

  gab_value children = gab_lstat(component, 2);

  if (kind == gab_message(gab, "box"))
    return render_box(gab, props, children);

  return gab_panicf(gab, "Unknown component type $", kind);
}

union gab_value_pair render_componentlist(struct gab_triple gab,
                                          gab_value app) {
  if (gab_valkind(app) != kGAB_RECORD)
    return gab_pktypemismatch(gab, app, kGAB_RECORD);

  if (!gab_recisl(app))
    return gab_panicf(gab, "Expected a list, found $", app);

  gab_uint len = gab_reclen(app);
  CLAY({
      .layout =
          {
              .layoutDirection = CLAY_TOP_TO_BOTTOM,
              .sizing =
                  {
                      .width = CLAY_SIZING_GROW(1),
                      .height = CLAY_SIZING_GROW(1),
                  },
          },
  }) {
    for (uint64_t i = 0; i < len; i++) {
      gab_value component = gab_lstat(app, i);
      union gab_value_pair res = render_component(gui.gab, component);

      if (res.status != gab_cundefined)
        return res; // TODO: Put an error event into the channel
    }
  }

  return gab_union_cvalid(gab_nil);
}

sclay_font_t fonts[1];

bool render(Clay_RenderCommandArray *array_out) {
  // Reset our id counter;
  gui.n = 0;

  gab_value app = gab_chntake(gui.gab, gui.appch);

  if (app == gab_cundefined)
    return false;

  if (app == gab_cinvalid)
    return false;

  Clay_BeginLayout();

  union gab_value_pair res = render_componentlist(gui.gab, app);

  if (res.status != gab_cundefined)
    return false; // Put an error event into the channel

  *array_out = Clay_EndLayout();
  return true;
}

bool dotuirender() {
  Clay_RenderCommandArray renderCommands;
  if (!render(&renderCommands))
    return false;

  tb_clear();
  Clay_Termbox_Render(renderCommands);
  tb_present();
  return true;
}

bool doguirender() {
  Clay_RenderCommandArray renderCommands;
  if (!render(&renderCommands))
    return false;

  sg_begin_pass(&(sg_pass){
      .swapchain =
          {
              .width = gui.win.r.w,
              .height = gui.win.r.h,
              .sample_count = 1, // or 4 for MSAA
              .color_format = SG_PIXELFORMAT_RGBA8,
              .depth_format = SG_PIXELFORMAT_DEPTH_STENCIL,
          },
  });

  sgl_matrix_mode_modelview();
  sgl_load_identity();
  sclay_render(renderCommands, fonts);
  sgl_draw();
  sg_end_pass();
  sg_commit();

  RGFW_window_swapBuffers(&gui.win);
  return true;
}

void tuirenderloop() {
  for (;;) {
    if (gab_chnisclosed(gui.appch))
      break;

    if (gab_chnisclosed(gui.evch))
      break;

    struct tb_event e;
    for (;;) {
      Clay_SetLayoutDimensions((Clay_Dimensions){
          .height = Clay_Termbox_Width(),
          .width = Clay_Termbox_Height(),
      });

      switch (tb_peek_event(&e, 0)) {
      case TB_ERR_NO_EVENT:

        gab_chnput(gui.gab, gui.evch, gab_message(gui.gab, "tick"));

        if (!dotuirender())
          return;

        continue;
      case TB_OK:
        if (e.type == TB_EVENT_KEY && e.key == TB_KEY_ESC)
          goto fin;

        if (clay_termbox_update(&e, 10))
          if (!dotuirender())
            return;

        continue;
      case TB_ERR_POLL:
        if (tb_last_errno() == EINTR)
          continue;
      default:
        return;
        // fallthrough
      }

      break;
    }

    gab_chnput(gui.gab, gui.evch, gab_message(gui.gab, "tick"));

    if (!dotuirender())
      return;

    switch (gab_yield(gui.gab)) {
    case sGAB_TERM:
      return;
    case sGAB_COLL:
      gab_gcepochnext(gui.gab);
      gab_sigpropagate(gui.gab);
      break;
    default:
      break;
    }
  }

fin:
  if (gui.evch != gab_cundefined)
    gab_chnclose(gui.evch);

  if (gui.appch != gab_cundefined)
    gab_chnclose(gui.appch);

  Clay_Termbox_Close();
}

void guirenderloop() {
  for (;;) {
    if (RGFW_window_shouldClose(&gui.win) == RGFW_TRUE)
      break;

    if (gab_chnisclosed(gui.appch))
      break;

    if (gab_chnisclosed(gui.evch))
      break;

    while (RGFW_window_checkEvent(&gui.win) != nullptr) {
      Clay_Dimensions dim = {(float)gui.win.r.w, (float)gui.win.r.h};
      sclay_set_layout_dimensions(dim, 1);

      if (clay_RGFW_update(&gui.win, 10))
        if (!doguirender())
          return;
    }

    gab_chnput(gui.gab, gui.evch, gab_message(gui.gab, "tick"));

    if (!doguirender())
      return;

    switch (gab_yield(gui.gab)) {
    case sGAB_TERM:
      return;
    case sGAB_COLL:
      gab_gcepochnext(gui.gab);
      gab_sigpropagate(gui.gab);
      break;
    default:
      break;
    }
  }

  if (gui.evch != gab_cundefined)
    gab_chnclose(gui.evch);

  if (gui.appch != gab_cundefined)
    gab_chnclose(gui.appch);

  RGFW_window_close(&gui.win);
}

GAB_DYNLIB_NATIVE_FN(ui, run_gui) {
  if (!RGFW_createWindowPtr("window", RGFW_RECT(0, 0, 800, 800),
                            RGFW_windowCenter | RGFW_windowNoResize, &gui.win))
    return gab_vmpush(gab_thisvm(gab), gab_err,
                      gab_string(gab, "Failed to create window")),
           gab_union_cvalid(gab_nil);

  gab_value evch = gab_arg(1);
  gab_value appch = gab_arg(2);

  if (!gab_valisch(evch))
    return gab_pktypemismatch(gab, appch, kGAB_CHANNEL);

  if (!gab_valisch(appch))
    return gab_pktypemismatch(gab, evch, kGAB_CHANNEL);

  gui.gab = gab;
  gui.appch = appch;
  gui.evch = evch;
  gui.win.userPtr = &gui;
  RGFW_setKeyCallback(onkey);
  RGFW_setMouseButtonCallback(onmousebutton);
  RGFW_setMousePosCallback(onmousepos);

  if (!gladLoadGL(RGFW_getProcAddress))
    return gab_vmpush(gab_thisvm(gab), gab_err,
                      gab_string(gab, "Failed to load OpenGL")),
           gab_union_cvalid(gab_nil);

  sg_setup(&(sg_desc){
      .logger.func = slog_func,
  });

  sgl_setup(&(sgl_desc_t){
      .logger.func = slog_func,
  });

  sclay_setup();

  fonts[0] = sclay_add_font_mem(fontData, sizeof(fontData));

  uint64_t totalMemorySize = Clay_MinMemorySize();
  Clay_Arena clayMemory = Clay_CreateArenaWithCapacityAndMemory(
      totalMemorySize, malloc(totalMemorySize));

  Clay_Initialize(clayMemory,
                  (Clay_Dimensions){(float)gui.win.r.w, (float)gui.win.r.h},
                  (Clay_ErrorHandler){HandleClayErrors});

  Clay_SetMeasureTextFunction(sclay_measure_text, &fonts);

  // Clay_SetDebugModeEnabled(true);

  guirenderloop();

  return gab_vmpush(gab_thisvm(gab), gab_ok), gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_NATIVE_FN(ui, run_tui) {
  gab_value evch = gab_arg(1);
  gab_value appch = gab_arg(2);

  if (!gab_valisch(evch))
    return gab_pktypemismatch(gab, appch, kGAB_CHANNEL);

  if (!gab_valisch(appch))
    return gab_pktypemismatch(gab, evch, kGAB_CHANNEL);

  gui.gab = gab;
  gui.appch = appch;
  gui.evch = evch;

  Clay_Termbox_Initialize(TB_OUTPUT_256, CLAY_TB_BORDER_MODE_DEFAULT,
                          CLAY_TB_BORDER_CHARS_DEFAULT,
                          CLAY_TB_IMAGE_MODE_DEFAULT, false);

  uint64_t totalMemorySize = Clay_MinMemorySize();
  Clay_Arena clayMemory = Clay_CreateArenaWithCapacityAndMemory(
      totalMemorySize, malloc(totalMemorySize));

  Clay_Initialize(clayMemory,
                  (Clay_Dimensions){.height = Clay_Termbox_Width(),
                                    .width = Clay_Termbox_Height()},
                  (Clay_ErrorHandler){HandleClayErrors});

  Clay_SetMeasureTextFunction(Clay_Termbox_MeasureText, nullptr);

  Clay_SetLayoutDimensions(
      (Clay_Dimensions){Clay_Termbox_Width(), Clay_Termbox_Height()});

  tuirenderloop();

  return gab_vmpush(gab_thisvm(gab), gab_ok), gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_MAIN_FN {
  gab_value mod = gab_message(gab, "ui");
  gab_def(gab,
          {
              gab_message(gab, "run\\gui"),
              mod,
              gab_snative(gab, "run\\gui", gab_mod_ui_run_gui),
          },
          {
              gab_message(gab, "run\\tui"),
              mod,
              gab_snative(gab, "run\\tui", gab_mod_ui_run_tui),
          }, );

  gab_value res[] = {gab_ok, mod};

  return (union gab_value_pair){
      .status = gab_cvalid,
      .aresult = a_gab_value_create(res, sizeof(res) / sizeof(gab_value)),
  };
}

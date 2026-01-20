#include "core.h"
#include <stdint.h>

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
#define RGFW_OPENGL
#define RGFW_PRINT_ERRORS
#include "RGFW/RGFW.h"

#define CLAY_IMPLEMENTATION
#include "Clay/clay.h"

#ifdef GAB_PLATFORM_UNIX

// #include <wchar.h>
// #define TB_OPT_LIBC_WCHAR
#define TB_OPT_EGC
#define TB_IMPL
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "Clay/renderers/termbox2/clay_renderer_termbox2.c"
#endif

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

struct gui {
  struct RGFW_window win;
  gab_value appch, evch;
  uint64_t n;
  bool ready;
};

gab_value gab_gui(struct gab_triple gab) {
  return gab_box(gab, (struct gab_box_argt){
                          .size = sizeof(struct gui),
                          .type = gab_string(gab, "gab\\gui"),
                      });
}

void HandleClayErrors(Clay_ErrorData errorData) {
  fprintf(stderr, "%s\n", errorData.errorText.chars);
}

/*
 * TODO: The channel put/take on the event and app channels don't yield here.
 * This blocks two threads on these channels, and if the user queues up a lot
 * of other fibers, the UI can appear to lock-up, as the user-fiber operating on
 * these channels *does* yield.
 */
bool putevent(struct gab_triple gab, struct gui *gui, const char *type,
              const char *event, gab_value data1, gab_value data2,
              gab_value data3) {
  if (gui->evch != gab_cundefined) {
    gab_value ev[] = {
        gab_message(gab, type), gab_message(gab, event), data1, data2, data3,
    };

    size_t len = sizeof(ev) / sizeof(gab_value) - (data3 == gab_cundefined);

    gab_niref(gab, 1, LEN_CARRAY(ev), ev);

    if (gab_nchnput(gab, gui->evch, len, ev) == gab_cinvalid)
      return true;

    gab_ndref(gab, 1, LEN_CARRAY(ev), ev);
  }

  return false;
}

#define RGFW_KEY_CASE(keyname, str)                                            \
  case RGFW_##keyname:                                                         \
    return putevent(gab, gui, "key", #str, gab_number(ev->key.mod),            \
                    gab_bool(ev->type == RGFW_keyPressed), gab_cundefined);

gab_value clayGetTopmostId(struct gab_triple gab) {
  Clay_ElementIdArray arr = Clay_GetPointerOverIds();

  // Skip the root - we don't care about clicks there.
  for (size_t i = arr.length; i > 1; i--) {
    Clay_String str = arr.internalArray[i - 1].stringId;
    if (!str.length)
      continue;

    return gab_nmessage(gab, str.length, str.chars);
  }

  return gab_cundefined;
}

Clay_Vector2 mousePosition;
bool clay_RGFW_update(struct gab_triple gab, struct gui *gui, double deltaTime,
                      RGFW_event *ev) {
  switch (ev->type) {
  case RGFW_mouseButtonPressed: {
    gab_value target = clayGetTopmostId(gab);
    switch (ev->button.type) {
    case RGFW_mouseScroll:
      // Clay_UpdateScrollContainers(false, (Clay_Vector2){0, ev->scroll.y},
      //                             deltaTime);
      return false;
    case RGFW_mouseLeft:
      return putevent(gab, gui, "mouse", "left", target, gab_nil, gab_nil);
      // Clay_SetPointerState(mousePosition, true);
    case RGFW_mouseRight:
      return putevent(gab, gui, "mouse", "right", target, gab_nil, gab_nil);
      // Clay_SetPointerState(mousePosition, true);
    }

    return false;
  }
  case RGFW_mouseButtonReleased: {
    gab_value target = clayGetTopmostId(gab);
    switch (ev->button.value) {
    case RGFW_mouseLeft:
      return putevent(gab, gui, "mouse", "left", target, gab_nil, gab_nil);
      // Clay_SetPointerState(mousePosition, true);
    case RGFW_mouseRight:
      return putevent(gab, gui, "mouse", "right", target, gab_nil, gab_nil);
      // Clay_SetPointerState(mousePosition, true);
    }

    return false;
  }
  case RGFW_mousePosChanged:
    mousePosition = (Clay_Vector2){(float)ev->mouse.x, (float)ev->mouse.y};
    // Clay_SetPointerState(mousePosition, RGFW_isMousePressed(RGFW_mouseLeft));
    return false;
  case RGFW_windowMoved:
  case RGFW_windowResized:
    return false;
  case RGFW_keyPressed:
  case RGFW_keyReleased:
    switch (ev->key.value) {
      RGFW_KEY_CASE(return, enter);
      RGFW_KEY_CASE(escape, escape);
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
      const char event[] = {ev->key.sym, '\0'};
      return putevent(gab, gui, "key", event, gab_number(ev->key.mod),
                      gab_bool(ev->type == RGFW_keyPressed), gab_cundefined);
    }

  default:
    return false;
  }
}

#ifdef GAB_PLATFORM_UNIX

#define TERMBOX_KEY_CASE(key, str)                                             \
  case TB_KEY_##key:                                                           \
    return putevent(gab, gui, "key", #str, gab_number(e->mod), gab_bool(true), \
                    gab_cundefined);

bool clay_termbox_update(struct gab_triple gab, struct gui *gui,
                         struct tb_event *e, double deltaTime) {
  switch (e->type) {
  case TB_EVENT_RESIZE:
    return false;
  case TB_EVENT_KEY:
    switch (e->key) {
    case TB_KEY_BACKSPACE2:
      TERMBOX_KEY_CASE(BACKSPACE, backspace);
      TERMBOX_KEY_CASE(ENTER, enter);
      TERMBOX_KEY_CASE(ESC, escape);
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
      return putevent(gab, gui, "key", ev, gab_number(e->mod), gab_bool(true),
                      gab_cundefined);
    }
  case TB_EVENT_MOUSE:
    switch (e->key) {
    case TB_KEY_MOUSE_RELEASE:
      // Clay_SetPointerState((Clay_Vector2){e->x, e->y}, false);
      return putevent(gab, gui, "mouse", "left", gab_number(0), gab_false,
                      clayGetTopmostId(gab));
    case TB_KEY_MOUSE_RIGHT:
      // Clay_SetPointerState((Clay_Vector2){e->x, e->y}, false);
      return putevent(gab, gui, "mouse", "right", gab_number(0), gab_true,
                      clayGetTopmostId(gab));
    case TB_KEY_MOUSE_LEFT:
      // Clay_SetPointerState((Clay_Vector2){e->x, e->y}, true);
      return putevent(gab, gui, "mouse", "left", gab_number(0), gab_true,
                      clayGetTopmostId(gab));
    case TB_KEY_MOUSE_WHEEL_UP:
      // Clay_UpdateScrollContainers(false, (Clay_Vector2){0, e->y}, deltaTime);
      return putevent(gab, gui, "mouse", "scroll\\up", gab_number(0), gab_false,
                      clayGetTopmostId(gab));
    case TB_KEY_MOUSE_WHEEL_DOWN:
      // Clay_UpdateScrollContainers(false, (Clay_Vector2){0, e->y}, deltaTime);
      return putevent(gab, gui, "mouse", "scroll\\down", gab_number(0),
                      gab_false, clayGetTopmostId(gab));
    default:
      goto err;
    }
  }

err:
  assert(false && "UNREACHABLE");
  return false;
}

#endif

Clay_Color packedToClayColor(gab_value vcolor) {
  gab_uint color = gab_valtou(vcolor);

  // return (Clay_Color){
  //   255,
  //   255,
  //   255,
  //   255,
  // };

  return (Clay_Color){
      color >> 24 & 0xff,
      color >> 16 & 0xff,
      color >> 8 & 0xff,
      color & 0xff,
  };
}

Clay_LayoutDirection parseDirection(struct gab_triple gab, gab_value props) {
  gab_value vlayout = gab_mrecat(gab, props, "layout");

  if (gab_valkind(vlayout) != kGAB_STRING)
    return CLAY_TOP_TO_BOTTOM;

  if (vlayout == gab_string(gab, "horizontal"))
    return CLAY_LEFT_TO_RIGHT;
  else
    return CLAY_TOP_TO_BOTTOM;
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
      .layoutDirection = parseDirection(gab, props),
      .padding = parsePadding(gab, props),
      .sizing = parseSizing(gab, props),
  };
}

union gab_value_pair render_componentlist(struct gab_triple gab,
                                          struct gui *gui, gab_value app,
                                          Clay_LayoutDirection dir);

union gab_value_pair render_box(struct gab_triple gab, struct gui *gui,
                                gab_value props, gab_value children) {
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

  Clay_LayoutConfig layout = parseLayout(gab, props);

  CLAY(vid == gab_cundefined ? CLAY_IDI("", gui->n++)
                             : CLAY_SID(((Clay_String){
                                   .length = gab_strlen(vid),
                                   .chars = gab_strdata(&vid),
                               })),
       {
           .layout = layout,
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
    render_componentlist(gab, gui, children, layout.layoutDirection);
  };

  return gab_union_cvalid(gab_nil);
}

union gab_value_pair render_rect(struct gab_triple gab, struct gui *gui,
                                 gab_value props) {
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

  CLAY(vid == gab_cundefined ? CLAY_IDI("", gui->n++)
                             : CLAY_SID(((Clay_String){
                                   .length = gab_strlen(vid),
                                   .chars = gab_strdata(&vid),
                               })),
       {
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

union gab_value_pair render_image(struct gab_triple gab, struct gui *gui,
                                  gab_value props) {
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

  CLAY(vid == gab_cundefined ? CLAY_IDI("", gui->n++)
                             : CLAY_SID(((Clay_String){
                                   .length = gab_strlen(vid),
                                   .chars = gab_strdata(&vid),
                               })),
       {
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

union gab_value_pair render_text(struct gab_triple gab, struct gui *gui,
                                 gab_value props) {
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

  CLAY(CLAY_IDI("", gui->n++), {
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

union gab_value_pair render_component(struct gab_triple gab, struct gui *gui,
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
    return render_text(gab, gui, props);

  if (kind == gab_message(gab, "rect"))
    return render_rect(gab, gui, props);

  // Currently unsupported by sokol_clay.h
  if (kind == gab_message(gab, "img"))
    return render_image(gab, gui, props);

  if (gab_reclen(component) < 3)
    return gab_panicf(gab, "Expected a list of at least 3 elements, found $",
                      component);

  gab_value children = gab_lstat(component, 2);

  if (kind == gab_message(gab, "box"))
    return render_box(gab, gui, props, children);

  return gab_panicf(gab, "Unknown component type $", kind);
}

union gab_value_pair render_componentlist(struct gab_triple gab,
                                          struct gui *gui, gab_value app,
                                          Clay_LayoutDirection dir) {
  if (gab_valkind(app) != kGAB_RECORD)
    return gab_pktypemismatch(gab, app, kGAB_RECORD);

  if (!gab_recisl(app))
    return gab_panicf(gab, "Expected a list, found $", app);

  gab_uint len = gab_reclen(app);
  CLAY(CLAY_IDI("", gui->n++),
       {
           .layout =
               {
                   .layoutDirection = dir,
                   .sizing =
                       {
                           .width = CLAY_SIZING_GROW(1),
                           .height = CLAY_SIZING_GROW(1),
                       },
               },
       }) {
    for (uint64_t i = 0; i < len; i++) {
      gab_value component = gab_lstat(app, i);
      union gab_value_pair res = render_component(gab, gui, component);

      if (res.status != gab_cundefined)
        return res; // TODO: Put an error event into the channel
    }
  }

  return gab_union_cvalid(gab_nil);
}

sclay_font_t fonts[1];

bool render(struct gab_triple gab, struct gui *gui,
            Clay_RenderCommandArray *array_out) {
  // Reset our id counter;
  gui->n = 0;

  assert(!gab_chnisclosed(gui->appch));
  gab_value app = gab_chntake(gab, gui->appch);

  if (app == gab_cundefined)
    return false;

  if (app == gab_cinvalid)
    return false;

  Clay_BeginLayout();

  union gab_value_pair res =
      render_componentlist(gab, gui, app, CLAY_TOP_TO_BOTTOM);

  if (res.status != gab_cundefined)
    return false; // Put an error event into the channel

  *array_out = Clay_EndLayout();
  return true;
}

bool dotuirender(struct gab_triple gab, struct gui *gui) {
  Clay_RenderCommandArray renderCommands;
  if (!render(gab, gui, &renderCommands))
    return false;

  tb_clear();
  Clay_Termbox_Render(renderCommands);
  tb_present();
  return true;
}

bool doguirender(struct gab_triple gab, struct gui *gui) {
  Clay_Dimensions dim = {
      .width = gui->win.w,
      .height = gui->win.h,
  };

  sclay_set_layout_dimensions(dim, 1);

  Clay_RenderCommandArray renderCommands;
  if (!render(gab, gui, &renderCommands))
    return false;

  sg_begin_pass(&(sg_pass){
      .action =
          {
              .colors[0] =
                  {
                      .load_action = SG_LOADACTION_CLEAR,
                      .clear_value = {0.f, 0.f, 0.f, 1.f},
                  },
          },
      .swapchain =
          {
              .width = dim.width,
              .height = dim.height,
          },
  });

  sgl_matrix_mode_modelview();
  sgl_load_identity();

  sclay_render(renderCommands, fonts);

  sgl_draw();
  sg_end_pass();
  sg_commit();

  RGFW_window_swapBuffers_OpenGL(&gui->win);
  return true;
}

#ifdef GAB_PLATFORM_UNIX

GAB_DYNLIB_NATIVE_FN(ui, tui_event) {
  gab_value vgui = gab_arg(0);

  if (gab_valtype(gab, vgui) != gab_string(gab, "gab\\gui"))
    return gab_ptypemismatch(gab, vgui, gab_string(gab, "gab\\gui"));

  struct gui *gui = gab_boxdata(vgui);

  for (;;) {
    if (gab_chnisclosed(gui->appch))
      goto fin;

    if (gab_chnisclosed(gui->evch))
      goto fin;

    struct tb_event e;
    int res = tb_peek_event(&e, 0);

    switch (res) {
    case TB_ERR_NO_EVENT:
      break;
    case TB_OK:
      if (clay_termbox_update(gab, gui, &e, 10))
        goto fin;

      continue;
    case TB_ERR_POLL:
      if (tb_last_errno() == EINTR)
        continue;

      goto fin;
    default:
      break;
    }

    break;
  }

  switch (gab_yield(gab)) {
  case sGAB_TERM:
    goto fin;
  case sGAB_COLL:
    gab_gcepochnext(gab);
    gab_sigpropagate(gab);
    break;
  default:
    break;
  }

  gab_chnput(gab, gui->evch, gab_message(gab, "tick"));
  return gab_union_ctimeout(gab_cundefined);

fin:
  gab_chnclose(gui->appch);
  gab_chnclose(gui->evch);
  return gab_union_cvalid(gab_ok);
}

GAB_DYNLIB_NATIVE_FN(ui, tui_render) {
  gab_value vgui = gab_arg(0);

  if (gab_valtype(gab, vgui) != gab_string(gab, "gab\\gui"))
    return gab_ptypemismatch(gab, vgui, gab_string(gab, "gab\\gui"));

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

  Clay_SetLayoutDimensions((Clay_Dimensions){
      .width = Clay_Termbox_Width(),
      .height = Clay_Termbox_Height(),
  });

  struct gui *gui = gab_boxdata(vgui);
  for (;;) {
    if (gab_chnisclosed(gui->appch))
      goto fin;

    if (gab_chnisclosed(gui->evch))
      goto fin;

    Clay_SetLayoutDimensions((Clay_Dimensions){
        .width = Clay_Termbox_Width(),
        .height = Clay_Termbox_Height(),
    });

    if (!dotuirender(gab, gui))
      goto fin;

    switch (gab_yield(gab)) {
    case sGAB_TERM:
      goto fin;
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    default:
      break;
    }
  }

  Clay_Termbox_Close();
  return gab_union_ctimeout(gab_cundefined);

fin:
  gab_chnclose(gui->appch);
  gab_chnclose(gui->evch);
  Clay_Termbox_Close();
  return gab_union_cinvalid;
}
#endif
GAB_DYNLIB_NATIVE_FN(ui, gui_render) {
  gab_value vgui = gab_arg(0);

  if (gab_valtype(gab, vgui) != gab_string(gab, "gab\\gui"))
    return gab_ptypemismatch(gab, vgui, gab_string(gab, "gab\\gui"));

  struct gui *gui = gab_boxdata(vgui);

  if (!gui->ready) {
    RGFW_glHints *hints = RGFW_getGlobalHints_OpenGL();
    hints->samples = 1;
    hints->major = 3;
    hints->minor = 1;
    RGFW_setGlobalHints_OpenGL(hints);

    gui->win.userPtr = &gui;
    if (!RGFW_createWindowPtr("", 500, 500, 500, 500, RGFW_windowOpenGL,
                              &gui->win))
      return gab_vmpush(gab_thisvm(gab), gab_err,
                        gab_string(gab, "Failed to create window")),
             gab_union_cvalid(gab_nil);

    if (!gladLoadGL(RGFW_getProcAddress_OpenGL))
      return gab_vmpush(gab_thisvm(gab), gab_err,
                        gab_string(gab, "Failed to load OpenGL")),
             gab_union_cvalid(gab_nil);

    RGFW_window_makeCurrentContext_OpenGL(&gui->win);

    sg_setup(&(sg_desc){
        .logger.func = slog_func,
        .environment =
            {
                .defaults =
                    {
                        .sample_count = 1,
                        .color_format = SG_PIXELFORMAT_RGBA8,
                        .depth_format = SG_PIXELFORMAT_DEPTH_STENCIL,
                    },
            },
    });

    sgl_setup(&(sgl_desc_t){
        .logger.func = slog_func,
    });

    sclay_setup();

    fonts[0] = sclay_add_font_mem(fontData, sizeof(fontData));

    if (fonts[0] == FONS_INVALID) {
      return gab_vmpush(gab_thisvm(gab), gab_err,
                        gab_string(gab, "Failed to load Font")),
             gab_union_cvalid(gab_nil);
    }

    uint64_t totalMemorySize = Clay_MinMemorySize();
    Clay_Arena clayMemory = Clay_CreateArenaWithCapacityAndMemory(
        totalMemorySize, malloc(totalMemorySize));

    Clay_Initialize(clayMemory,
                    (Clay_Dimensions){
                        .width = gui->win.w,
                        .height = gui->win.h,
                    },
                    (Clay_ErrorHandler){
                        HandleClayErrors,
                    });

    Clay_SetMeasureTextFunction(sclay_measure_text, &fonts);

    gui->ready = true;
  }

  for (;;) {
    RGFW_pollEvents();

    if (gab_chnisclosed(gui->appch))
      goto fin;

    if (gab_chnisclosed(gui->evch))
      goto fin;

    if (!doguirender(gab, gui))
      goto fin;

    switch (gab_yield(gab)) {
    case sGAB_TERM:
      goto fin;
    case sGAB_COLL:
      gab_gcepochnext(gab);
      gab_sigpropagate(gab);
      break;
    default:
      continue;
    }
  }

  return gab_union_ctimeout(gab_cundefined);

fin:
  gab_chnclose(gui->appch);
  gab_chnclose(gui->evch);
  sclay_shutdown();
  RGFW_window_closePtr(&gui->win);
  return gab_union_cinvalid;
}

GAB_DYNLIB_NATIVE_FN(ui, gui_event) {
  gab_value vgui = gab_arg(0);

  if (gab_valtype(gab, vgui) != gab_string(gab, "gab\\gui"))
    return gab_ptypemismatch(gab, vgui, gab_string(gab, "gab\\gui"));

  struct gui *gui = gab_boxdata(vgui);
  if (!gui->ready)
    return gab_union_ctimeout(gab_cundefined);

  if (RGFW_window_shouldClose(&gui->win) == RGFW_TRUE)
    goto fin;

  if (gab_chnisclosed(gui->appch))
    goto fin;

  if (gab_chnisclosed(gui->evch))
    goto fin;

  RGFW_event ev;
  while (RGFW_window_checkQueuedEvent(&gui->win, &ev)) {
    if (gab_chnisclosed(gui->appch))
      goto fin;

    if (gab_chnisclosed(gui->evch))
      goto fin;

    if (ev.type == RGFW_quit)
      goto fin;

    if (clay_RGFW_update(gab, gui, 10, &ev))
      goto fin;
  }

  switch (gab_yield(gab)) {
  case sGAB_TERM:
    goto fin;
  case sGAB_COLL:
    gab_gcepochnext(gab);
    gab_sigpropagate(gab);
    break;
  default:
    break;
  }

  gab_chnput(gab, gui->evch, gab_message(gab, "tick"));
  return gab_union_ctimeout(gab_cundefined);

fin:
  gab_chnclose(gui->appch);
  gab_chnclose(gui->evch);
  return gab_union_cinvalid;
}

GAB_DYNLIB_NATIVE_FN(ui, run_gui) {
  gab_value evch = gab_arg(1);
  gab_value appch = gab_arg(2);

  if (!gab_valischn(evch))
    return gab_pktypemismatch(gab, appch, kGAB_CHANNEL);

  if (!gab_valischn(appch))
    return gab_pktypemismatch(gab, evch, kGAB_CHANNEL);

  gab_value vgui = gab_gui(gab);
  struct gui *gui = gab_boxdata(vgui);

  gab_irefall(gab, evch, appch, vgui);

  gui->appch = appch;
  gui->evch = evch;

  union gab_value_pair res =
      gab_asend(gab, (struct gab_send_argt){
                         .message = gab_message(gab, mGAB_CALL),
                         .receiver = gab_snative(gab, "ui\\gui\\loop\\render",
                                                 gab_mod_ui_gui_render),
                         .len = 1,
                         .argv = (gab_value[]){vgui},
                     });

  if (res.status != gab_cvalid) {
    return gab_panicf(gab, "Couldn't start render thread");
  }

  res = gab_asend(gab, (struct gab_send_argt){
                           .message = gab_message(gab, mGAB_CALL),
                           .receiver = gab_snative(gab, "ui\\gui\\loop\\event",
                                                   gab_mod_ui_gui_event),
                           .len = 1,
                           .argv = (gab_value[]){vgui},
                       });

  if (res.status != gab_cvalid) {
    return gab_panicf(gab, "Couldn't start event thread");
  }

  return gab_vmpush(gab_thisvm(gab), gab_ok), gab_union_cvalid(gab_nil);
}

#ifdef GAB_PLATFORM_UNIX
GAB_DYNLIB_NATIVE_FN(ui, run_tui) {
  gab_value evch = gab_arg(1);
  gab_value appch = gab_arg(2);

  gab_value vgui = gab_gui(gab);
  struct gui *gui = gab_boxdata(vgui);

  if (!gab_valischn(evch))
    return gab_pktypemismatch(gab, appch, kGAB_CHANNEL);

  if (!gab_valischn(appch))
    return gab_pktypemismatch(gab, evch, kGAB_CHANNEL);

  gab_irefall(gab, evch, appch, vgui);

  gui->appch = appch;
  gui->evch = evch;

  union gab_value_pair res =
      gab_asend(gab, (struct gab_send_argt){
                         .message = gab_message(gab, mGAB_CALL),
                         .receiver = gab_snative(gab, "ui\\tui\\loop\\render",
                                                 gab_mod_ui_tui_render),
                         .len = 1,
                         .argv = (gab_value[]){vgui},
                     });

  if (res.status != gab_cvalid) {
    return gab_panicf(gab, "Couldn't start render thread");
  }

  res = gab_asend(gab, (struct gab_send_argt){
                           .message = gab_message(gab, mGAB_CALL),
                           .receiver = gab_snative(gab, "ui\\tui\\loop\\event",
                                                   gab_mod_ui_tui_event),
                           .len = 1,
                           .argv = (gab_value[]){vgui},
                       });

  if (res.status != gab_cvalid) {
    return gab_panicf(gab, "Couldn't start event thread");
  }

  return gab_vmpush(gab_thisvm(gab), gab_ok), gab_union_cvalid(gab_nil);
}
#endif

GAB_DYNLIB_MAIN_FN {
  gab_value mod = gab_message(gab, "ui");
  gab_def(gab,
          {
              gab_message(gab, "run\\gui"),
              mod,
              gab_snative(gab, "run\\gui", gab_mod_ui_run_gui),
          },
#ifdef GAB_PLATFORM_UNIX
          {
              gab_message(gab, "run\\tui"),
              mod,
              gab_snative(gab, "run\\tui", gab_mod_ui_run_tui),
          },
#endif
  );

  gab_value res[] = {gab_ok, mod};

  return (union gab_value_pair){
      .status = gab_cvalid,
      .aresult = a_gab_value_create(res, sizeof(res) / sizeof(gab_value)),
  };
}

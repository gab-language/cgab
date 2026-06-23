/**
 *  MIT License
 *
 *  Copyright (c) 2023 Teddy Randby
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#if GAB_PLATFORM_UNIX
#define HAS_TERMBOX 1
#else
#define HAS_TERMBOX 0
#endif

#include <stdint.h>
#include <time.h>

/*
 * The hygeine of these imports is important.
 *
 * The *_IMPLEMENTATION are undefined after each include
 * because these headers may include each other,
 * but we only want to define the implementation ONCE for
 * each library.
 */

// GL/gl.h won't work because some OS's have files which override.
// Use glad/gl.h instead.
#define GLAD_GL_IMPLEMENTATION
#define GLAD_GLX_IMPLEMENTATION
#include "glad/gl.h"
#undef GLAD_GL
#undef GLAD_GL_IMPLEMENTATION
#undef GLAD_GLX_IMPLEMENTATION

#define RGFW_IMPLEMENTATION
#define RGFW_OPENGL
#define RGFW_PRINT_ERRORS
#include "RGFW/RGFW.h"
#undef RGFW_IMPLEMENTATION

#define CLAY_IMPLEMENTATION
#include "Clay/clay.h"
#undef CLAY_IMPLEMENTATION

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_resize2.h"
#undef STB_IMAGE_IMPLEMENTATION
#undef STB_IMAGE_RESIZE_IMPLEMENTATION

#if HAS_TERMBOX

// #include <wchar.h>
// #define TB_OPT_LIBC_WCHAR
#define TB_OPT_EGC
#define TB_IMPL

#ifndef NDEBUG
#define STBIR__SEPARATE_ALLOCATIONS
#endif

#include "Clay/renderers/termbox2/clay_renderer_termbox2.c"
#endif

#include <stdio.h>
#define FONTSTASH_IMPLEMENTATION
#include "fontstash/src/fontstash.h"

// TODO @cui @qol: This font isn't great, replace it. Also whats its license?
unsigned char fontData[] = {
#embed "resources/JetBrainsMonoNLNerdFontMono-Regular.ttf"
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

#define UI_BOX_TYPE "gab\\ui"

typedef struct clay_sg_image {
  sclay_image sclay;
  stbi_uc *pixels;
  int width, height, channels;
  sg_image img;
  sg_view view;
} clay_sg_image;

struct ui_image {

  union {
    clay_sg_image gui;
#if HAS_TERMBOX
    clay_tb_image tui;
#endif
  } as;
};

#define K gab_value
#define V struct ui_image
#define NAME ui_image
#define HASH(v) (gab_strhash(v))
#define EQUAL(a, b) (a == b)
#define DEF_V ((struct ui_image){0})
#include "dict.h"

struct ui {
  d_ui_image image_cache;
  gab_value appch, evch;
  uint64_t n;
  enum gui_kind { kGAB_GUI, kGAB_TUI, kGAB_HUI } k;
  bool ready;
  union {
    struct RGFW_window win;
  };
};

#define MAX_MS_PER_FRAME 15

struct timestep {
  clock_t dt;
  double dt_d;
};

struct timestep limit_fps(clock_t last) {
  clock_t dt = clock() - last;

  double dt_d = (double)dt / CLOCKS_PER_SEC;

  if (dt_d < MAX_MS_PER_FRAME) {
    // milliseconds -> nanoseconds
    int64_t ns = (MAX_MS_PER_FRAME - dt_d) * 1000;
    struct timespec remaining = {};

    nanosleep(&(struct timespec){.tv_nsec = ns}, &remaining);

    // TODO @ui @bug: Add some dt for sleeping to the clock time also?

    // nanoseconds -> seconds
    dt_d += (double)(ns - remaining.tv_nsec) / 1000000.f;
  }

  return (struct timestep){dt, dt_d};
}

gab_value gab_gui(struct gab_triple gab) {
  return gab_box(gab, (struct gab_box_argt){
                          .size = sizeof(struct ui),
                          .type = gab_string(gab, UI_BOX_TYPE),
                      });
}

void HandleClayErrors(Clay_ErrorData errorData) {
  fprintf(stderr, "%s\n", errorData.errorText.chars);
}

bool putevent(struct gab_triple gab, struct ui *gui, const char *type,
              const char *event, gab_value data1, gab_value data2,
              gab_value data3) {
  gab_value ev[] = {
      gab_message(gab, type), gab_message(gab, event), data1, data2, data3,
  };

  size_t len = 2;
  if (data1 != gab_cundefined)
    len++;
  if (data2 != gab_cundefined)
    len++;
  if (data3 != gab_cundefined)
    len++;

  gab_niref(gab, 1, len, ev);

  // for (size_t i = 0; i < len; i++)
  //   gab_fprintf(stderr, "PUTEVENT $: $\n", gab_number(i), ev[i]);

  for (size_t i = 0; i < len; i++)
    gab_wfibpush(gab_thisfiber(gab), ev[i]);

  // Don't request shutdown
  return false;
}

#define RGFW_KEY_CASE(keyname, str, up)                                        \
  case RGFW_key##keyname:                                                      \
    return putevent(gab, gui, "key", up, gab_string(gab, #str),                \
                    gab_cundefined, gab_cundefined);

gab_value clayGetTopmostId(struct gab_triple gab) {
  Clay_ElementIdArray arr = Clay_GetPointerOverIds();

  // Skip the root - we don't care about clicks there.
  for (int i = arr.length; i > 1; i--) {
    Clay_String str = arr.internalArray[i - 1].stringId;

    if (!str.length)
      continue;

    return gab_nmessage(gab, str.length, str.chars);
  }

  return gab_cundefined;
}

// TODO @ui @qol: Switch to distinct key\up key\down events.

bool clay_RGFW_update(struct gab_triple gab, struct ui *gui, double deltaTime,
                      RGFW_event *ev) {
  switch (ev->type) {
  case RGFW_mouseButtonPressed: {
    gab_value target = clayGetTopmostId(gab);

    switch (ev->button.value) {
    case RGFW_mouseLeft:
      return putevent(gab, gui, "mouse", "down", gab_message(gab, "left"),
                      target, gab_cundefined);
    case RGFW_mouseRight:
      return putevent(gab, gui, "mouse", "down", gab_message(gab, "right"),
                      target, gab_cundefined);
    default:
      return true;
    }

    return false;
  }
  case RGFW_mouseButtonReleased: {
    gab_value target = clayGetTopmostId(gab);

    switch (ev->button.value) {
    case RGFW_mouseLeft:
      return putevent(gab, gui, "mouse", "up", gab_message(gab, "left"), target,
                      gab_cundefined);
    case RGFW_mouseRight:
      return putevent(gab, gui, "mouse", "up", gab_message(gab, "right"),
                      target, gab_cundefined);
    default:
      return true;
    }

    return false;
  }
  case RGFW_mouseScroll: {
    Clay_UpdateScrollContainers(false, (Clay_Vector2){0, ev->delta.y},
                                deltaTime);
    return false;
  }
  case RGFW_mousePosChanged:
    Clay_SetPointerState((Clay_Vector2){(float)ev->mouse.x, (float)ev->mouse.y},
                         RGFW_isMousePressed(RGFW_mouseLeft));
    return false;
  case RGFW_windowMoved:
  case RGFW_windowResized:
    return false;
  // TODO: Fix how keychar event is handled.
  // Non-alphabet keys should respond to pressed-released?
  case RGFW_keyPressed:
  case RGFW_keyReleased:
    switch (ev->keyChar.value) {
      RGFW_KEY_CASE(Enter, enter, ev->type == RGFW_keyPressed ? "down" : "up");
      RGFW_KEY_CASE(Escape, escape,
                    ev->type == RGFW_keyPressed ? "down" : "up");
      RGFW_KEY_CASE(BackSpace, backspace,
                    ev->type == RGFW_keyPressed ? "down" : "up");
      RGFW_KEY_CASE(CapsLock, capslock,
                    ev->type == RGFW_keyPressed ? "down" : "up");
      RGFW_KEY_CASE(Insert, insert,
                    ev->type == RGFW_keyPressed ? "down" : "up");
      RGFW_KEY_CASE(End, end, ev->type == RGFW_keyPressed ? "down" : "up");
      RGFW_KEY_CASE(Home, home, ev->type == RGFW_keyPressed ? "down" : "up");
      RGFW_KEY_CASE(PageUp, pageup,
                    ev->type == RGFW_keyPressed ? "down" : "up");
      RGFW_KEY_CASE(PageDown, pagedown,
                    ev->type == RGFW_keyPressed ? "down" : "up");
      RGFW_KEY_CASE(Space, space, ev->type == RGFW_keyPressed ? "down" : "up");
      RGFW_KEY_CASE(F1, f1, ev->type == RGFW_keyPressed ? "down" : "up");
      RGFW_KEY_CASE(F2, f2, ev->type == RGFW_keyPressed ? "down" : "up");
      RGFW_KEY_CASE(F3, f3, ev->type == RGFW_keyPressed ? "down" : "up");
      RGFW_KEY_CASE(F4, f4, ev->type == RGFW_keyPressed ? "down" : "up");
      RGFW_KEY_CASE(F5, f5, ev->type == RGFW_keyPressed ? "down" : "up");
      RGFW_KEY_CASE(F6, f6, ev->type == RGFW_keyPressed ? "down" : "up");
      RGFW_KEY_CASE(F7, f7, ev->type == RGFW_keyPressed ? "down" : "up");
      RGFW_KEY_CASE(F8, f8, ev->type == RGFW_keyPressed ? "down" : "up");
      RGFW_KEY_CASE(F9, f9, ev->type == RGFW_keyPressed ? "down" : "up");
      RGFW_KEY_CASE(F10, f10, ev->type == RGFW_keyPressed ? "down" : "up");
      RGFW_KEY_CASE(F11, f11, ev->type == RGFW_keyPressed ? "down" : "up");
      RGFW_KEY_CASE(F12, f12, ev->type == RGFW_keyPressed ? "down" : "up");
    default:
      return false;
    }
  case RGFW_keyChar:
    switch (ev->keyChar.value) {
    case RGFW_keyEnter:
    case RGFW_keyEscape:
    case RGFW_keySpace:
    case RGFW_keyBackSpace:
      return false;
    default: {
      const char event[] = {ev->keyChar.value, '\0'};
      return putevent(gab, gui, "key", "up", gab_string(gab, event),
                      gab_cundefined, gab_cundefined);
    }
    }

  default:
    return false;
  }
}

#if HAS_TERMBOX

#define TERMBOX_KEY_CASE(key, str)                                             \
  case TB_KEY_##key:                                                           \
    return putevent(gab, gui, "key", "up", gab_string(gab, #str),              \
                    gab_number(e->mod), gab_cundefined);

bool clay_termbox_update(struct gab_triple gab, struct ui *gui,
                         struct tb_event *e, double deltaTime) {
  switch (e->type) {
  case TB_EVENT_RESIZE:
    return false;
  case TB_EVENT_KEY:
    switch (e->key) {
    case TB_KEY_CTRL_C:
      return true;
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
      TERMBOX_KEY_CASE(SPACE, space);
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
      return putevent(gab, gui, "key", "up", gab_string(gab, ev),
                      gab_number(e->mod), gab_cundefined);
    }
  case TB_EVENT_MOUSE:
    switch (e->key) {
    case TB_KEY_MOUSE_RELEASE:
      Clay_SetPointerState((Clay_Vector2){e->x * clay_tb_cell_size.width,
                                          e->y * clay_tb_cell_size.height},
                           false);

      // when the motion mod is present, this is not a true release event.
      if (e->mod & TB_MOD_MOTION)
        return false;

      // TODO @ui @bug: fix 'unknown' mouse up event in tb
      return putevent(gab, gui, "mouse", "up", gab_message(gab, "unknown"),
                      clayGetTopmostId(gab), gab_cundefined);
    case TB_KEY_MOUSE_RIGHT:
      Clay_SetPointerState((Clay_Vector2){e->x * clay_tb_cell_size.width,
                                          e->y * clay_tb_cell_size.height},
                           false);
      return putevent(gab, gui, "mouse", "down", gab_message(gab, "right"),
                      clayGetTopmostId(gab), gab_cundefined);
    case TB_KEY_MOUSE_LEFT:
      Clay_SetPointerState((Clay_Vector2){e->x * clay_tb_cell_size.width,
                                          e->y * clay_tb_cell_size.height},
                           true);
      return putevent(gab, gui, "mouse", "down", gab_message(gab, "left"),
                      clayGetTopmostId(gab), gab_cundefined);
    case TB_KEY_MOUSE_WHEEL_UP:
      Clay_UpdateScrollContainers(false, (Clay_Vector2){0, e->y}, deltaTime);
      return putevent(gab, gui, "mouse", "scroll\\up", gab_number(e->y),
                      gab_cundefined, gab_cundefined);
    case TB_KEY_MOUSE_WHEEL_DOWN:
      Clay_UpdateScrollContainers(false, (Clay_Vector2){0, e->y}, deltaTime);
      return putevent(gab, gui, "mouse", "scroll\\down", gab_number(e->y),
                      gab_cundefined, gab_cundefined);
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

  return (Clay_Color){
      .b = color & 0xff,
      .g = color >> 8 & 0xff,
      .r = color >> 16 & 0xff,
      .a = color >> 24 & 0xff,
  };
}

Clay_LayoutDirection parseDirection(struct gab_triple gab, gab_value props) {
  gab_value vlayout = gab_mrecat(gab, props, "layout");

  if (gab_valkind(vlayout) != kGAB_MESSAGE)
    return CLAY_TOP_TO_BOTTOM;

  if (vlayout == gab_message(gab, "horizontal"))
    return CLAY_LEFT_TO_RIGHT;
  else
    return CLAY_TOP_TO_BOTTOM;
}

Clay_ChildAlignment parseChildAlignment(struct gab_triple gab,
                                        gab_value props) {
  gab_value valignx = gab_mrecat(gab, props, "align\\x");
  gab_value valigny = gab_mrecat(gab, props, "align\\y");

  Clay_LayoutAlignmentX align_x = CLAY_ALIGN_X_LEFT;
  Clay_LayoutAlignmentY align_y = CLAY_ALIGN_Y_TOP;

  if (valignx == gab_message(gab, "left"))
    align_x = CLAY_ALIGN_X_LEFT;
  else if (valignx == gab_message(gab, "center"))
    align_x = CLAY_ALIGN_X_CENTER;
  else if (valignx == gab_message(gab, "right"))
    align_x = CLAY_ALIGN_X_RIGHT;

  if (valigny == gab_message(gab, "top"))
    align_y = CLAY_ALIGN_Y_TOP;
  else if (valigny == gab_message(gab, "center"))
    align_y = CLAY_ALIGN_Y_CENTER;
  else if (valigny == gab_message(gab, "bottom"))
    align_y = CLAY_ALIGN_Y_BOTTOM;

  return (Clay_ChildAlignment){align_x, align_y};
}

Clay_Padding parsePadding(struct gab_triple gab, gab_value props) {
  gab_value vpadding = gab_mrecat(gab, props, "p");
  gab_value vpaddingt = gab_mrecat(gab, props, "p\\t");
  gab_value vpaddingb = gab_mrecat(gab, props, "p\\b");
  gab_value vpaddingl = gab_mrecat(gab, props, "p\\l");
  gab_value vpaddingr = gab_mrecat(gab, props, "p\\r");
  gab_value vpaddingx = gab_mrecat(gab, props, "p\\x");
  gab_value vpaddingy = gab_mrecat(gab, props, "p\\y");

  Clay_Padding p = CLAY_PADDING_ALL(0);

  if (gab_valkind(vpadding) == kGAB_NUMBER)
    p = CLAY_PADDING_ALL(gab_valtou(vpadding));

  if (gab_valkind(vpaddingy) == kGAB_NUMBER)
    p.bottom = gab_valtou(vpaddingy), p.top = gab_valtou(vpaddingy);

  if (gab_valkind(vpaddingx) == kGAB_NUMBER)
    p.left = gab_valtou(vpaddingx), p.right = gab_valtou(vpaddingx);

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

Clay_Sizing parseSizing(struct gab_triple gab, gab_value props,
                        int default_width, int default_height) {
  gab_value vfixedwidth = gab_mrecat(gab, props, "w");
  gab_value vrelwidth = gab_mrecat(gab, props, "w\\r");
  gab_value vgrowwidth = gab_mrecat(gab, props, "w\\g");

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
    if (default_width)
      w = CLAY_SIZING_FIT(default_width);
    else
      w = CLAY_SIZING_FIT();
  }

  gab_value vfixedheight = gab_mrecat(gab, props, "h");
  gab_value vrelheight = gab_mrecat(gab, props, "h\\r");
  gab_value vgrowheight = gab_mrecat(gab, props, "h\\g");

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
    if (default_height)
      h = CLAY_SIZING_FIT(default_height);
    else
      h = CLAY_SIZING_FIT();
  }

  return (Clay_Sizing){.width = w, .height = h};
}

Clay_LayoutConfig parseLayout(struct gab_triple gab, gab_value props) {
  gab_value vgap = gab_mrecat(gab, props, "gap");

  return (Clay_LayoutConfig){
      .layoutDirection = parseDirection(gab, props),
      .padding = parsePadding(gab, props),
      .sizing = parseSizing(gab, props, 0, 0),
      .childAlignment = parseChildAlignment(gab, props),
      .childGap = gab_valkind(vgap) == kGAB_NUMBER ? gab_valtou(vgap) : 0,
  };
}

Clay_TransitionElementConfig parseTransition(struct gab_triple gab,
                                             gab_value props) {
  gab_value vduration = gab_mrecat(gab, props, "transition\\duration");

  // gab_value vfunction = gab_mrecat(gab, props, "transition\\function");

  gab_value vproperties = gab_mrecat(gab, props, "transition\\properties");

  uint16_t properties = 0;

  // Transition everything if the key doesn't exist.
  if (vproperties == gab_cundefined)
    properties = 0xffff;

  // Transition *one* property, or known property set.
  if (gab_valkind(vproperties) == kGAB_MESSAGE) {
    if (vproperties == gab_message(gab, "border"))
      properties |= CLAY_TRANSITION_PROPERTY_BORDER;
    else if (vproperties == gab_message(gab, "pos"))
      properties |= CLAY_TRANSITION_PROPERTY_POSITION;
    else if (vproperties == gab_message(gab, "box"))
      properties |= CLAY_TRANSITION_PROPERTY_BOUNDING_BOX;
    else if (vproperties == gab_message(gab, "dim"))
      properties |= CLAY_TRANSITION_PROPERTY_DIMENSIONS;
    else if (vproperties == gab_message(gab, "x"))
      properties |= CLAY_TRANSITION_PROPERTY_X;
    else if (vproperties == gab_message(gab, "y"))
      properties |= CLAY_TRANSITION_PROPERTY_Y;
    else if (vproperties == gab_message(gab, "w"))
      properties |= CLAY_TRANSITION_PROPERTY_WIDTH;
    else if (vproperties == gab_message(gab, "h"))
      properties |= CLAY_TRANSITION_PROPERTY_HEIGHT;
    else if (vproperties == gab_message(gab, "bg"))
      properties |= CLAY_TRANSITION_PROPERTY_BACKGROUND_COLOR;
    else if (vproperties == gab_message(gab, "r"))
      properties |= CLAY_TRANSITION_PROPERTY_CORNER_RADIUS;
  }

  // TODO @ui @qol: Add more transition functions
  return (Clay_TransitionElementConfig){
      .handler = Clay_EaseOut,
      .duration =
          gab_valkind(vduration) == kGAB_NUMBER ? gab_valtou(vduration) : 0.2f,
      .properties = properties,
  };
}

[[nodiscard]]
union gab_value_pair
render_componentlist(struct gab_triple gab, struct ui *gui, gab_value app,
                     Clay_LayoutDirection dir, Clay_ChildAlignment align);

Clay_String str_for_gab_string(gab_value str) {
  size_t len = gab_strlen(str);
  if (len > 5)
    return (Clay_String){
        .length = len,
        .chars = gab_strdata(&str),
    };

  // TODO @ui @bug: This leaks every time.
  return (Clay_String){
      .length = len,
      .chars = strdup(gab_strdata(&str)),
  };
}

#define UI_ID(vid)                                                             \
  (vid == gab_cundefined ? CLAY_IDI("", gui->n++)                              \
                         : CLAY_SID(str_for_gab_string(vid)))

[[nodiscard]]
union gab_value_pair render_box(struct gab_triple gab, struct ui *gui,
                                gab_value props, gab_value children) {
  gab_value vfg = gab_cundefined;
  gab_value vbg = gab_mrecat(gab, props, "bg");

  gab_value vborderWidth = gab_cundefined;
  gab_value vborder = gab_mrecat(gab, props, "border");

  if (vborder != gab_cundefined)
    vfg = gab_mrecat(gab, vborder, "fg"),
    vborderWidth = gab_mrecat(gab, vborder, "w");

  if (vfg == gab_cundefined)
    vfg = gab_number(0);

  if (vborderWidth == gab_cundefined)
    vborderWidth = gab_number(0);

  if (gab_valkind(vborderWidth) != kGAB_NUMBER)
    return gab_pktypemismatch(gab, vborderWidth, kGAB_NUMBER);

  if (gab_valkind(vfg) != kGAB_NUMBER)
    return gab_pktypemismatch(gab, vfg, kGAB_NUMBER);

  if (vbg == gab_cundefined)
    vbg = gab_number(0xffffffff);

  if (gab_valkind(vbg) != kGAB_NUMBER)
    return gab_pktypemismatch(gab, vbg, kGAB_NUMBER);

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

  gab_uint border_w = gab_valtou(vborderWidth);

  CLAY(UI_ID(vid),
       {
           .layout = layout,
           .cornerRadius =
               {
                   cornerRadius,
                   cornerRadius,
                   cornerRadius,
                   cornerRadius,
               },
           .transition = parseTransition(gab, props),
           .backgroundColor = packedToClayColor(vbg),
           .border =
               {
                   .color = border_w ? packedToClayColor(vfg) : (Clay_Color){0},
                   .width =
                       {
                           gab_valtou(vborderWidth),
                           gab_valtou(vborderWidth),
                           gab_valtou(vborderWidth),
                           gab_valtou(vborderWidth),
                       },
               },
       }) {
    union gab_value_pair res = render_componentlist(
        gab, gui, children, layout.layoutDirection, layout.childAlignment);

    if (res.status != gab_cundefined)
      return res;
  };

  return gab_union_cvalid(gab_nil);
}

[[nodiscard]]
union gab_value_pair render_rect(struct gab_triple gab, struct ui *gui,
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

  gab_value vcolor = gab_mrecat(gab, props, "fg");
  if (vcolor == gab_cundefined)
    vcolor = gab_number(0xffffffff);

  if (gab_valkind(vcolor) != kGAB_NUMBER)
    return gab_pktypemismatch(gab, vcolor, kGAB_NUMBER);

  gab_value vid = gab_mrecat(gab, props, "id");
  if (vid != gab_cundefined && gab_valkind(vid) != kGAB_MESSAGE)
    return gab_pktypemismatch(gab, vid, kGAB_MESSAGE);

  CLAY(UI_ID(vid), {
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

const struct ui_image *image_cache_read(struct gab_triple gab, struct ui *gui,
                                        gab_value data) {
  assert(gab.wkid == 1);

  size_t idx = d_ui_image_index_of(&gui->image_cache, data);
  if (d_ui_image_iexists(&gui->image_cache, idx))
    return &gui->image_cache.buckets[idx].val;

  struct ui_image insert = {};

  switch (gui->k) {
  case kGAB_GUI: {
    const int desired_color_channels = 4;

    insert.as.gui.pixels = stbi_load_from_memory(
        (const void *)gab_strdata(&data), gab_strlen(data),
        &insert.as.gui.width, &insert.as.gui.height, &insert.as.gui.channels,
        desired_color_channels);

    insert.as.gui.channels = 4;

    insert.as.gui.img = sg_make_image(&(struct sg_image_desc){
        .type = SG_IMAGETYPE_2D,
        .usage.immutable = true,
        .width = insert.as.gui.width,
        .height = insert.as.gui.height,
        .pixel_format = SG_PIXELFORMAT_RGBA8,
        .data.mip_levels[0] =
            {
                .ptr = insert.as.gui.pixels,
                .size = (insert.as.gui.width * insert.as.gui.height *
                         insert.as.gui.channels),
            },
    });

    sg_resource_state state = sg_query_image_state(insert.as.gui.img);
    assert(state == SG_RESOURCESTATE_VALID);

    insert.as.gui.view = sg_make_view(&(struct sg_view_desc){
        .texture.image = insert.as.gui.img,
    });

    state = sg_query_view_state(insert.as.gui.view);
    assert(state == SG_RESOURCESTATE_VALID);

    insert.as.gui.sclay = (sclay_image){
        .view = insert.as.gui.view,
    };
    break;
  }
#if HAS_TERMBOX
  case kGAB_TUI: {
    insert.as.tui =
        Clay_Termbox_Image_Load_Memory(gab_strdata(&data), gab_strlen(data));
    break;
  }
#endif
  case kGAB_HUI:
    break;
  }

  bool inserted = d_ui_image_insert(&gui->image_cache, data, insert);
  assert(inserted);

  return &gui->image_cache.buckets[idx].val;
};

[[nodiscard]]
union gab_value_pair render_image(struct gab_triple gab, struct ui *gui,
                                  gab_value props, gab_value content) {
  gab_value vid = gab_mrecat(gab, props, "id");
  if (vid != gab_cundefined && gab_valkind(vid) != kGAB_MESSAGE)
    return gab_pktypemismatch(gab, vid, kGAB_MESSAGE);

  if (gab_valkind(content) != kGAB_BINARY)
    return gab_panicf(gab,
                      "An image component's third element should be a binary, "
                      "containing the image content. Found:\n\n$",
                      content);

  const struct ui_image *img = image_cache_read(gab, gui, content);

  // Pull default w/h out of the image width/height itself.

  gab_uint w = gui->k == kGAB_GUI ? img->as.gui.width
#if HAS_TERMBOX
               : gui->k == kGAB_TUI ? img->as.tui.pixel_width
#endif
                                    : 0;

  gab_uint h = gui->k == kGAB_GUI ? img->as.gui.height
#if HAS_TERMBOX
               : gui->k == kGAB_TUI ? img->as.tui.pixel_height
#endif
                                    : 0;

  CLAY(UI_ID(vid),
       {
           .layout =
               {
                   .padding = parsePadding(gab, props),
                   .sizing = parseSizing(gab, props, w, h),
               },
           .image = {.imageData = (void *)img},
       }, );

  return gab_union_cvalid(gab_nil);
}

[[nodiscard]]
union gab_value_pair render_text(struct gab_triple gab, struct ui *gui,
                                 gab_value props, gab_value content) {
  if (gab_valkind(content) != kGAB_STRING)
    return gab_panicf(gab,
                      "A text component's third element should be a string, "
                      "containing the text content. Found:\n\n$",
                      content);

  // This leaks every time
  uint64_t len = gab_strlen(content);
  char *str = malloc(len + 1);
  assert(str != nullptr);
  strcpy(str, gab_strdata(&content));

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

  gab_value vfg = gab_mrecat(gab, props, "fg");
  if (vfg == gab_cundefined)
    vfg = gab_number(0xffffffff);

  if (gab_valkind(vfg) != kGAB_NUMBER)
    return gab_pktypemismatch(gab, vfg, kGAB_NUMBER);

  gab_value vbg = gab_mrecat(gab, props, "bg");
  if (vbg == gab_cundefined)
    vbg = gab_number(0xffffffff);

  if (gab_valkind(vbg) != kGAB_NUMBER)
    return gab_pktypemismatch(gab, vbg, kGAB_NUMBER);

  gab_value vspacing = gab_mrecat(gab, props, "spacing");
  if (vspacing == gab_cundefined)
    vspacing = gab_number(0);

  if (gab_valkind(vspacing) != kGAB_NUMBER)
    return gab_pktypemismatch(gab, vspacing, kGAB_NUMBER);

  gab_uint spacing = gab_valtou(vspacing);

  gab_value vheight = gab_mrecat(gab, props, "h");
  if (vheight == gab_cundefined)
    vheight = gab_number(0);

  if (gab_valkind(vheight) != kGAB_NUMBER)
    return gab_pktypemismatch(gab, vheight, kGAB_NUMBER);

  gab_uint height = gab_valtou(vheight);

  gab_value valign = gab_mrecat(gab, props, "align");
  Clay_TextAlignment align = CLAY_TEXT_ALIGN_LEFT;

  if (valign == gab_message(gab, "left"))
    align = CLAY_TEXT_ALIGN_LEFT;
  else if (valign == gab_message(gab, "right"))
    align = CLAY_TEXT_ALIGN_RIGHT;
  else if (valign == gab_message(gab, "center"))
    align = CLAY_TEXT_ALIGN_CENTER;

  CLAY(CLAY_IDI("", gui->n++), {
                                   .layout = parseLayout(gab, props),
                                   .transition = parseTransition(gab, props),
                                   .backgroundColor = packedToClayColor(vbg),
                               }) {
    CLAY_TEXT(text, CLAY_TEXT_CONFIG({
                        .fontSize = size,
                        .letterSpacing = spacing,
                        .lineHeight = height,
                        .textColor = packedToClayColor(vfg),
                        .textAlignment = align,
                    }));
  };

  return gab_union_cvalid(gab_nil);
}

#define EXPECTED_COMPONENT_FMT "Expected a component, found:\n\n$\n\n"

#define EXPECTED_COMPONENT_ERROR(component, help, ...)                         \
  gab_panicf(gab, EXPECTED_COMPONENT_FMT help,                                 \
             component __VA_OPT__(, ) __VA_ARGS__)

[[nodiscard]]
union gab_value_pair render_component(struct gab_triple gab, struct ui *gui,
                                      gab_value component) {
  if (gab_valkind(component) != kGAB_RECORD)
    return EXPECTED_COMPONENT_ERROR(component,
                                    "A component is a list-type record.");

  if (!gab_recisl(component))
    return EXPECTED_COMPONENT_ERROR(component,
                                    "A component is a list-type record.");

  if (gab_reclen(component) < 3)
    return EXPECTED_COMPONENT_ERROR(
        component, "A component must have at least 3 elements.");

  gab_value kind = gab_lstat(component, 0);

  if (gab_valkind(kind) != kGAB_MESSAGE)
    return EXPECTED_COMPONENT_ERROR(
        component,
        "A component's first element should be a message, indicating the kind "
        "of component. Instead, found:\n\n$",
        kind);

  gab_value props = gab_lstat(component, 1);

  if (gab_valkind(props) != kGAB_RECORD)
    return EXPECTED_COMPONENT_ERROR(
        component,
        "A component's second element should be a record, carrying properties "
        "of the component. Instead, found:\n\n$",
        props);

  gab_value content = gab_lstat(component, 2);

  if (kind == gab_message(gab, "text"))
    return render_text(gab, gui, props, content);

  if (kind == gab_message(gab, "image"))
    return render_image(gab, gui, props, content);

  if (kind == gab_message(gab, "box"))
    return render_box(gab, gui, props, content);

  return EXPECTED_COMPONENT_ERROR(
      component,
      "Unrecognized component kind: \n\n$\n\nExpected one of: \n\n$\n$\n$",
      kind, gab_message(gab, "text"), gab_message(gab, "image"),
      gab_message(gab, "box"));
}

#define EXPECTED_COMPONENTLIST_FMT "Expected a component-list, found:\n\n$\n\n"

#define EXPECTED_COMPONENTLIST_ERROR(component, help, ...)                     \
  gab_panicf(gab, EXPECTED_COMPONENTLIST_FMT help,                             \
             component __VA_OPT__(, ) __VA_ARGS__)

union gab_value_pair render_componentlist(struct gab_triple gab, struct ui *gui,
                                          gab_value components,
                                          Clay_LayoutDirection dir,
                                          Clay_ChildAlignment align) {
  if (gab_valkind(components) != kGAB_RECORD)
    return EXPECTED_COMPONENTLIST_ERROR(
        components, "A component-list is a list-type record.");

  if (!gab_recisl(components))
    return EXPECTED_COMPONENTLIST_ERROR(
        components, "A component-list is a list-type record.");

  gab_uint len = gab_reclen(components);
  CLAY(CLAY_IDI("", gui->n++),
       {
           .layout =
               {
                   .layoutDirection = dir,
                   .childAlignment = align,
                   .sizing =
                       {
                           .width = CLAY_SIZING_GROW(1),
                           .height = CLAY_SIZING_GROW(1),
                       },
               },
       }) {
    for (uint64_t i = 0; i < len; i++) {
      gab_value component = gab_lstat(components, i);
      union gab_value_pair res = render_component(gab, gui, component);

      if (res.status != gab_cundefined)
        return res;
    }
  }

  return gab_union_cvalid(gab_nil);
}

sclay_font_t fonts[1];

/**
 * These are headless render/event loops, useful for testing.
 */
GAB_DYNLIB_NATIVE_FN(ui, hui_event) {
  gab_value vgui = gab_arg(0);

  if (gab_valtype(gab, vgui) != gab_string(gab, UI_BOX_TYPE))
    return gab_ptypemismatch(gab, vgui, gab_string(gab, UI_BOX_TYPE));

  struct ui *gui = gab_boxdata(vgui);
  union gab_value_pair res;
  static gab_value ev[2] = {};

  if (!reentrant) {
    ev[0] = gab_message(gab, "tick");
    ev[1] = gab_message(gab, "tick");
  }

  for (;;) {
    if (gab_chnisclosed(gui->appch))
      goto fin;

    if (gab_chnisclosed(gui->evch))
      goto fin;

    // Try to put on the channel.
    gab_value app = gab_untchnput(gab, gui->evch, LEN_CARRAY(ev), ev, 1);

    if (app == gab_cundefined)
      goto fin;

    if (app == gab_cinvalid) {
      res = gab_panicf(gab, "Crashed UI thrd due to some error");
      goto err;
    }

    goto yield;
  }

yield:
  if (gab_chnisclosed(gui->appch))
    goto fin;

  if (gab_chnisclosed(gui->evch))
    goto fin;

  switch (gab_yield(gab)) {
  case sGAB_TERM:
    goto fin;
  default:
    return gab_union_ctimeout(gab_cinvalid);
  }

err:
  gab_chnclose(gui->appch);
  gab_chnclose(gui->evch);
  return res;

fin:
  gab_chnclose(gui->appch);
  gab_chnclose(gui->evch);
  return gab_union_cinvalid;
}

static inline Clay_Dimensions
Clay_HUI_measure_text(Clay_StringSlice text, Clay_TextElementConfig *config,
                      void *userData) {
  return (Clay_Dimensions){};
}

// Just pull apps out and don't do anything with the values.
GAB_DYNLIB_NATIVE_FN(ui, hui_render) {
  gab_value vgui = gab_arg(0);

  if (gab_valtype(gab, vgui) != gab_string(gab, UI_BOX_TYPE))
    return gab_ptypemismatch(gab, vgui, gab_string(gab, UI_BOX_TYPE));

  struct ui *gui = gab_boxdata(vgui);
  union gab_value_pair res;

  static clock_t time;

  if (!reentrant) {
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

    Clay_SetMeasureTextFunction(Clay_HUI_measure_text, nullptr);

    gui->ready = true;
    time = 0;
  }

  for (;;) {
    if (gab_chnisclosed(gui->appch))
      goto fin;

    if (gab_chnisclosed(gui->evch))
      goto fin;

    gab_value app = gab_tchntake(gab, gui->appch, 1);

    if (app == gab_cundefined)
      goto fin;

    if (app == gab_ctimeout)
      goto yield;

    if (app == gab_cinvalid) {
      res = gab_panicf(gab, "Crashed UI thrd due to some error");
      goto err;
    }

    gab_iref(gab, app);

    Clay_BeginLayout();

    struct timestep step = limit_fps(time);

    /* Try to render, so that we can see errors */
    res = render_componentlist(gab, gui, app, CLAY_TOP_TO_BOTTOM,
                               (Clay_ChildAlignment){});
    Clay_RenderCommandArray cmd = Clay_EndLayout(step.dt_d);

    if (res.status != gab_cundefined)
      goto err;

    gab_dref(gab, app);
  }

yield:
  if (gab_chnisclosed(gui->appch))
    goto fin;

  if (gab_chnisclosed(gui->evch))
    goto fin;

  switch (gab_yield(gab)) {
  case sGAB_TERM:
    goto fin;
  default:
    return gab_union_ctimeout(gab_cinvalid);
  }

err:
  gab_chnclose(gui->appch);
  gab_chnclose(gui->evch);
  return res;

fin:
  gab_chnclose(gui->appch);
  gab_chnclose(gui->evch);
  return gab_union_cinvalid;
}

#if HAS_TERMBOX

GAB_DYNLIB_NATIVE_FN(ui, tui_event) {
  gab_value vgui = gab_arg(0);

  if (gab_valtype(gab, vgui) != gab_string(gab, UI_BOX_TYPE))
    return gab_ptypemismatch(gab, vgui, gab_string(gab, UI_BOX_TYPE));

  gab_value res = 0;

  struct ui *gui = gab_boxdata(vgui);
  if (!gui->ready) {
    res = gab_ctimeout;
    goto yield;
  }

  if (reentrant && gab_fibsize(gab_thisfiber(gab))) {
    gab_value *ev = gab_fibat(gab_thisfiber(gab), 0);
    size_t len = gab_fibsize(gab_thisfiber(gab)) / sizeof(gab_value);

    if (gab_chnmatches(gui->evch, ev)) {
      res = gab_cvalid;
      goto yield;
    } else if (reentrant != gab_cvalid) {
      res = gab_ctimeout;
      goto put_event;
    } else {
      res = gab_cundefined;
      // Otherwise we succeeded, and can deref the values and go to next
      // event
      gab_ndref(gab, 1, len, ev);

      // Clear the event we put
      gab_fibclear(gab_thisfiber(gab));
    }
  }

  for (;;) {
    if (gab_chnisclosed(gui->appch))
      goto fin;

    if (gab_chnisclosed(gui->evch))
      goto fin;

    struct tb_event e;
    int event_result = tb_peek_event(&e, 0);

    switch (event_result) {
    case TB_ERR_NOT_INIT:
    case TB_ERR_NO_EVENT:
      if (putevent(gab, gui, "tick", "tick", clayGetTopmostId(gab),
                   gab_cundefined, gab_cundefined))
        goto fin;

      goto put_event;

    case TB_OK:
      if (clay_termbox_update(gab, gui, &e, 10))
        goto fin;

    put_event:
      // Our event did not yield a value.
      if (!gab_fibsize(gab_thisfiber(gab)))
        continue;

      // Assert we have a number of bytes which is reasonable for a list of
      // gab_values
      assert(gab_fibsize(gab_thisfiber(gab)) % sizeof(gab_value) == 0);
      // Determine the number of values
      size_t len = gab_fibsize(gab_thisfiber(gab)) / sizeof(gab_value);
      // Get the ptr
      gab_value *ev = gab_fibat(gab_thisfiber(gab), 0);

      // If we are are resuming a put, but it has been taken since last check
      // we accidentally re-put the same event
      // We can yield with some value if we did put something on the channel
      // successfully
      // If we're already putting, and we match this event, then yield.
      if (reentrant && gab_chnmatches(gui->evch, ev)) {
        res = gab_ctimeout;
        goto yield;
      }

      // We need to retry this put until we get cvalid - that is how
      // we know the put is on the channel.
      // we can do this by yielding a specific value, instead of just
      // undefined.
      res = gab_untchnput(gab, gui->evch, len, ev, 1);

      // Check for error and timeout
      if (res == gab_cundefined)
        goto fin;

      if (res == gab_cinvalid)
        goto fin;

      // If we timedout on the put or successfully put, we yield
      goto yield;
    case TB_ERR:
    case TB_ERR_READ:
    case TB_ERR_POLL:
      if (tb_last_errno() == EINTR)
        break;

      goto fin;
    default:
      goto fin;
      break;
    }
  }

yield:
  if (gab_chnisclosed(gui->appch))
    goto fin;

  if (gab_chnisclosed(gui->evch))
    goto fin;

  assert(res != 0);
  return gab_union_ctimeout(res);

fin:
  gab_chnclose(gui->appch);
  gab_chnclose(gui->evch);
  return gab_union_cvalid(gab_ok);
}

GAB_DYNLIB_NATIVE_FN(ui, tui_render) {

  gab_value vgui = gab_arg(0);
  struct ui *gui = gab_boxdata(vgui);

  static clock_t time;

  if (!reentrant) {
    if (gab_valtype(gab, vgui) != gab_string(gab, UI_BOX_TYPE))
      return gab_ptypemismatch(gab, vgui, gab_string(gab, UI_BOX_TYPE));

    Clay_Termbox_Initialize(TB_OUTPUT_TRUECOLOR, CLAY_TB_BORDER_MODE_MINIMUM,
                            CLAY_TB_BORDER_CHARS_BLANK,
                            CLAY_TB_IMAGE_MODE_UNICODE, true);

    uint64_t totalMemorySize = Clay_MinMemorySize();
    Clay_Arena clayMemory = Clay_CreateArenaWithCapacityAndMemory(
        totalMemorySize, malloc(totalMemorySize));

    Clay_Initialize(clayMemory,
                    (Clay_Dimensions){
                        .width = Clay_Termbox_Width(),
                        .height = Clay_Termbox_Height(),
                    },
                    (Clay_ErrorHandler){
                        HandleClayErrors,
                        nullptr,
                    });

    Clay_SetMeasureTextFunction(Clay_Termbox_MeasureText, nullptr);

    gui->ready = true;
    time = 0;
  }

  union gab_value_pair res;

  for (;;) {
    gab_value app = gab_tchntake(gab, gui->appch, 1);

    if (gab_chnisclosed(gui->appch))
      goto fin;

    if (gab_chnisclosed(gui->evch))
      goto fin;

    if (app == gab_cundefined)
      goto fin;

    if (app == gab_cinvalid) {
      res = gab_panicf(gab, "Crashed UI thrd due to some error");
      goto err;
    }

    if (app == gab_ctimeout)
      return gab_union_ctimeout(gab_cundefined);

    // Reset our id counter;
    gui->n = 0;

    // Increment the app data structure, so it isn't collected while
    // we are in this function.
    gab_iref(gab, app);

    // Somehow here, call Clay_Termbox_set_cell_size() to update the cell size
    // to match terminal font/pixel size.

    // Clay_Termbox_Set_Cell_Pixel_Size(10, 21);

    Clay_SetLayoutDimensions((Clay_Dimensions){
        .width = Clay_Termbox_Width(),
        .height = Clay_Termbox_Height(),
    });

    Clay_BeginLayout();

    res = render_componentlist(gab, gui, app, CLAY_TOP_TO_BOTTOM,
                               (Clay_ChildAlignment){});

    if (res.status != gab_cundefined)
      goto err;

    // Compute our dt
    struct timestep step = limit_fps(time);

    Clay_RenderCommandArray cmd = Clay_EndLayout(step.dt_d);
    time += step.dt;

    tb_clear();
    Clay_Termbox_Render(cmd);
    tb_present();

    gab_dref(gab, app);
  }

err:
  gab_chnclose(gui->appch);
  gab_chnclose(gui->evch);
  return res;

fin:
  gab_chnclose(gui->appch);
  gab_chnclose(gui->evch);
  Clay_Termbox_Close();
  return gab_union_cinvalid;
}

#endif

GAB_DYNLIB_NATIVE_FN(ui, gui_render) {
  gab_value vgui = gab_arg(0);
  static clock_t time;

  if (gab_valtype(gab, vgui) != gab_string(gab, UI_BOX_TYPE))
    return gab_ptypemismatch(gab, vgui, gab_string(gab, UI_BOX_TYPE));

  struct ui *gui = gab_boxdata(vgui);

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
    time = 0;
  }

  union gab_value_pair res;

  for (;;) {
    if (gab_chnisclosed(gui->appch))
      goto fin;

    if (gab_chnisclosed(gui->evch))
      goto fin;

    gab_value app = gab_tchntake(gab, gui->appch, 1);

    if (app == gab_cundefined)
      goto fin;

    if (app == gab_cinvalid) {
      res = gab_panicf(gab, "Crashed UI thrd due to some error");
      goto err;
    }

    if (app == gab_ctimeout)
      return gab_union_ctimeout(gab_cundefined);

    // Reset our id counter;
    gui->n = 0;

    // Increment the app data structure, so it isn't collected while
    // we are in this function.
    gab_iref(gab, app);

    Clay_Dimensions dim = {
        .width = gui->win.w,
        .height = gui->win.h,
    };

    sclay_set_layout_dimensions(dim, 1);

    Clay_BeginLayout();

    res = render_componentlist(gab, gui, app, CLAY_TOP_TO_BOTTOM,
                               (Clay_ChildAlignment){});

    if (res.status != gab_cundefined)
      goto err;

    // Compute our dt
    struct timestep step = limit_fps(time);

    Clay_RenderCommandArray cmd = Clay_EndLayout(step.dt_d);
    time += step.dt;

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

    sclay_render(cmd, fonts);

    sgl_draw();
    sg_end_pass();
    sg_commit();

    RGFW_window_swapBuffers_OpenGL(&gui->win);

    gab_dref(gab, app);
  }

  return gab_union_ctimeout(gab_cundefined);

err:
  gab_chnclose(gui->appch);
  gab_chnclose(gui->evch);
  sclay_shutdown();
  RGFW_window_closePtr(&gui->win);
  return res;

fin:
  gab_chnclose(gui->appch);
  gab_chnclose(gui->evch);
  sclay_shutdown();
  RGFW_window_closePtr(&gui->win);
  return gab_union_cinvalid;
}

GAB_DYNLIB_NATIVE_FN(ui, gui_event) {
  gab_value vgui = gab_arg(0);

  if (gab_valtype(gab, vgui) != gab_string(gab, UI_BOX_TYPE))
    return gab_ptypemismatch(gab, vgui, gab_string(gab, UI_BOX_TYPE));

  gab_value res = 0;

  struct ui *gui = gab_boxdata(vgui);
  if (!gui->ready) {
    res = gab_ctimeout;
    goto yield;
  }

  if (reentrant && gab_fibsize(gab_thisfiber(gab))) {
    gab_value *ev = gab_fibat(gab_thisfiber(gab), 0);
    size_t len = gab_fibsize(gab_thisfiber(gab)) / sizeof(gab_value);

    if (gab_chnmatches(gui->evch, ev)) {
      res = gab_cvalid;
      goto yield;
    } else if (reentrant != gab_cvalid) {
      res = gab_ctimeout;
      goto put_event;
    } else {
      res = gab_cundefined;
      // Otherwise we succeeded, and can deref the values and go to next
      // event
      gab_ndref(gab, 1, len, ev);

      // Clear the event we put
      gab_fibclear(gab_thisfiber(gab));
    }
  }

  for (;;) {
    if (gab_chnisclosed(gui->appch))
      goto fin;

    if (gab_chnisclosed(gui->evch))
      goto fin;

    if (RGFW_window_shouldClose(&gui->win) == RGFW_TRUE)
      goto fin;

    RGFW_pollEvents();

    RGFW_event ev;
    if (RGFW_window_checkQueuedEvent(&gui->win, &ev)) {
      if (ev.type == RGFW_windowClose)
        goto fin;

      if (clay_RGFW_update(gab, gui, 10, &ev))
        goto fin;

    put_event:
      // Our event did not yield a value.
      if (!gab_fibsize(gab_thisfiber(gab)))
        continue;

      // Assert we have a number of bytes which is reasonable for a list of
      // gab_values
      assert(gab_fibsize(gab_thisfiber(gab)) % sizeof(gab_value) == 0);
      assert(gab_fibsize(gab_thisfiber(gab)) > 0);
      // Determine the number of values
      size_t len = gab_fibsize(gab_thisfiber(gab)) / sizeof(gab_value);
      // Get the ptr
      gab_value *ev = gab_fibat(gab_thisfiber(gab), 0);

      if (reentrant && gab_chnmatches(gui->evch, ev)) {
        res = gab_ctimeout;
        goto yield;
      }

      res = gab_untchnput(gab, gui->evch, len, ev, 1);

      // Check for error and timeout
      if (res == gab_cundefined)
        goto fin;

      if (res == gab_cinvalid)
        goto fin;

      goto yield;
    } else {
      if (putevent(gab, gui, "tick", "tick", clayGetTopmostId(gab),
                   gab_cundefined, gab_cundefined))
        goto fin;

      goto put_event;
    }
  }

yield:
  if (gab_chnisclosed(gui->appch))
    goto fin;

  if (gab_chnisclosed(gui->evch))
    goto fin;

  assert(res != 0);
  return gab_union_ctimeout(res);

fin:
  gab_chnclose(gui->appch);
  gab_chnclose(gui->evch);
  return gab_union_cvalid(gab_ok);
}

GAB_DYNLIB_NATIVE_FN(ui, run) {
  gab_value kind = gab_arg(1);
  gab_value evch = gab_arg(2);
  gab_value appch = gab_arg(3);

  if (!gab_valischn(evch))
    return gab_pktypemismatch(gab, appch, kGAB_CHANNEL);

  if (!gab_valischn(appch))
    return gab_pktypemismatch(gab, evch, kGAB_CHANNEL);

  const char *render_rec_name = nullptr;
  gab_native_f render_rec_target = nullptr;

  const char *event_rec_name = nullptr;
  gab_native_f event_rec_target = nullptr;

  gab_value vgui = gab_gui(gab);
  struct ui *gui = gab_boxdata(vgui);
  d_ui_image_create(&gui->image_cache, 8);

  if (kind == gab_message(gab, "gui")) {
    render_rec_name = "ui\\gui\\loop\\render";
    render_rec_target = gab_mod_ui_gui_render;
    event_rec_name = "ui\\gui\\loop\\event";
    event_rec_target = gab_mod_ui_gui_event;
    gui->k = kGAB_GUI;
#if HAS_TERMBOX
  } else if (kind == gab_message(gab, "tui")) {
    render_rec_name = "ui\\tui\\loop\\render";
    render_rec_target = gab_mod_ui_tui_render;
    event_rec_name = "ui\\tui\\loop\\event";
    event_rec_target = gab_mod_ui_tui_event;
    gui->k = kGAB_TUI;
#endif
  } else if (kind == gab_message(gab, "hui")) {
    render_rec_name = "ui\\hui\\loop\\render";
    render_rec_target = gab_mod_ui_hui_render;
    event_rec_name = "ui\\hui\\loop\\event";
    event_rec_target = gab_mod_ui_hui_event;
    gui->k = kGAB_HUI;
  } else {
    return gab_panicf(gab, "Expected @, @ or @. Found:\n\n$",
                      gab_message(gab, "gui"), gab_message(gab, "tui"),
                      gab_message(gab, "hui"), kind);
  }

  gab_irefall(gab, evch, appch, vgui);

  gui->appch = appch;
  gui->evch = evch;

  // TODO @ui: It might be better to *combine* these two.
  union gab_value_pair res = gab_asend(
      gab, (struct gab_send_argt){
               .message = gab_message(gab, mGAB_CALL),
               .receiver = gab_snative(gab, render_rec_name, render_rec_target),
               .len = 1,
               .argv = (gab_value[]){vgui},
               .pin = 1,
           });

  if (res.status != gab_cvalid) {
    return gab_panicf(gab, "Couldn't start render thread");
  }

  res = gab_asend(
      gab, (struct gab_send_argt){
               .message = gab_message(gab, mGAB_CALL),
               .receiver = gab_snative(gab, event_rec_name, event_rec_target),
               .len = 1,
               .argv = (gab_value[]){vgui},
               .pin = 1,
           });

  if (res.status != gab_cvalid) {
    return gab_panicf(gab, "Couldn't start event thread");
  }

  return gab_vmpush(gab_thisvm(gab), gab_ok, vgui), gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_MAIN_FN {
  gab_value mod = gab_message(gab, "ui");
  gab_def(gab,
          {
              gab_message(gab, "run"),
              mod,
              gab_snative(gab, "run", gab_mod_ui_run),
          }, );

  gab_value res[] = {gab_ok, mod};

  return (union gab_value_pair){
      .status = gab_cvalid,
      .aresult = a_gab_value_create(res, sizeof(res) / sizeof(gab_value)),
  };
}

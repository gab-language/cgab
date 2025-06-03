#include <stdint.h>
#define GLAD_GL
#define GLAD_GL_IMPLEMENTATION
#include "GL/gl.h"

// Further includes of "GL/gl.h" should not define implementations.
#undef GLAD_GL
#undef GLAD_GL_IMPLEMENTATION

#define RGFW_IMPLEMENTATION
#include "RGFW/RGFW.h"

#define CLAY_IMPLEMENTATION
#include "Clay/clay.h"

#define FONTSTASH_IMPLEMENTATION
#include "fontstash/src/fontstash.h"

// Embed a font in the binary. So sick!
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
      Clay_UpdateScrollContainers(false, (Clay_Vector2){0, ev.scroll}, 0);
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

void onkey(RGFW_window *win, RGFW_key key, unsigned char key_char,
           RGFW_keymod key_mod, RGFW_bool down) {
  if (gui.evch != gab_cundefined) {
    gab_value ev[] = {
        gab_message(gui.gab, "key"),
        gab_nstring(gui.gab, 1, (const char *)&key_char),
        gab_number(key_mod),
        gab_bool(down),
    };

    gab_niref(gui.gab, 1, LEN_CARRAY(ev), ev);

    gab_nchnput(gui.gab, gui.evch, LEN_CARRAY(ev), ev);

    gab_ndref(gui.gab, 1, LEN_CARRAY(ev), ev);
  }
}

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

void onmousebutton(RGFW_window *win, unsigned char b, double dbl,
                   unsigned char down) {
  switch (b) {
  case RGFW_mouseLeft:
    if (gui.evch != gab_cundefined) {
      gab_value target = clayGetTopmostId();
      gab_value ev[] = {
          gab_message(gui.gab, "mouse"),
          gab_message(gui.gab, "left"),
          gab_number(dbl),
          gab_bool(down),
          target,
      };

      size_t len = sizeof(ev) / sizeof(gab_value) - (target == gab_cundefined);

      gab_niref(gui.gab, 1, LEN_CARRAY(ev), ev);

      gab_nchnput(gui.gab, gui.evch, len, ev);

      gab_ndref(gui.gab, 1, LEN_CARRAY(ev), ev);
    }
    break;
  case RGFW_mouseRight:
    if (gui.evch != gab_cundefined) {
      gab_value target = clayGetTopmostId();
      gab_value ev[] = {
          gab_message(gui.gab, "mouse"),
          gab_message(gui.gab, "right"),
          gab_number(dbl),
          gab_bool(down),
          clayGetTopmostId(),
      };
      size_t len = sizeof(ev) / sizeof(gab_value) - (target == gab_cundefined);

      gab_niref(gui.gab, 1, LEN_CARRAY(ev), ev);

      gab_nchnput(gui.gab, gui.evch, len, ev);

      gab_ndref(gui.gab, 1, LEN_CARRAY(ev), ev);
    }
    break;
  case RGFW_mouseScrollDown:
    if (gui.evch != gab_cundefined) {
      gab_value ev[] = {
          gab_message(gui.gab, "mouse"),
          gab_message(gui.gab, "scroll\\down"),
          gab_number(dbl),
          gab_bool(down),
      };

      gab_niref(gui.gab, 1, LEN_CARRAY(ev), ev);
      gab_nchnput(gui.gab, gui.evch, sizeof(ev) / sizeof(gab_value), ev);
      gab_ndref(gui.gab, 1, LEN_CARRAY(ev), ev);
    }
    break;
  case RGFW_mouseScrollUp:
    if (gui.evch != gab_cundefined) {
      gab_value ev[] = {
          gab_message(gui.gab, "mouse"),
          gab_message(gui.gab, "scroll\\up"),
          gab_number(dbl),
          gab_bool(down),
      };

      gab_niref(gui.gab, 1, LEN_CARRAY(ev), ev);
      gab_nchnput(gui.gab, gui.evch, sizeof(ev) / sizeof(gab_value), ev);
      gab_ndref(gui.gab, 1, LEN_CARRAY(ev), ev);
    }
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

Clay_Color packedToClayColor(gab_value vcolor) {
  gab_uint color = gab_valtou(vcolor);

  return (Clay_Color){
      color >> 24 & 0xff,
      color >> 16 & 0xff,
      color >> 8 & 0xff,
      color & 0xff,
  };
}

Clay_LayoutConfig parseLayout(struct gab_triple gab, gab_value props) {
  // Do some work in parsing universal layout config from props into our
  // Clay_LayoutConfig
  return (Clay_LayoutConfig){};
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
      .layout = {.layoutDirection = CLAY_TOP_TO_BOTTOM, .padding = {}},
      .cornerRadius = {cornerRadius, cornerRadius, cornerRadius, cornerRadius},
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
              .sourceDimensions = {.height = h, .width = w},
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

  Clay_String text = {.length = gab_strlen(vtext),
                      .chars = gab_strdata(&vtext)};

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

  CLAY() {
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
  CLAY({.layout = {.layoutDirection = CLAY_TOP_TO_BOTTOM}}) {
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

bool dorender() {
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

  Clay_RenderCommandArray renderCommands = Clay_EndLayout();

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

void renderloop() {
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
        if (!dorender())
          return;
    }

    gab_chnput(gui.gab, gui.evch, gab_message(gui.gab, "tick"));

    if (!dorender())
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

union gab_value_pair gab_uilib_run(struct gab_triple gab, uint64_t argc,
                                   gab_value argv[argc]) {
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

  renderloop();

  return gab_vmpush(gab_thisvm(gab), gab_ok), gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_MAIN_FN {
  gab_value mod = gab_message(gab, "ui");
  gab_def(gab, {
                   gab_message(gab, "run"),
                   mod,
                   gab_snative(gab, "run", gab_uilib_run),
               });

  gab_value res[] = {gab_ok, mod};

  return (union gab_value_pair){
      .status = gab_cvalid,
      .aresult = a_gab_value_create(res, sizeof(res) / sizeof(gab_value)),
  };
}

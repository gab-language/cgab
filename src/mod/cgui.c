
#define GLAD_GL
#define GLAD_GL_IMPLEMENTATION
#include "GL/gl.h"

// Further includes of "GL/gl.h" should not define implementations.
#undef GLAD_GL
#undef GLAD_GL_IMPLEMENTATION

#define RGFW_IMPLEMENTATION
#include "RGFW/RGFW.h"

#include "gab.h"

union gab_value_pair gab_uilib_open(struct gab_triple gab, uint64_t argc,
                                    gab_value argv[argc]) {
  gab_value vwin = gab_box(gab, (struct gab_box_argt){
                                    .size = sizeof(struct RGFW_window),
                                    .type = gab_string(gab, "gab\\window"),
                                });
  struct RGFW_window *win = gab_boxdata(vwin);

  if (!RGFW_createWindowPtr("window", RGFW_RECT(0, 0, 800, 800),
                            RGFW_windowCenter | RGFW_windowNoResize, win)) {
    gab_vmpush(gab_thisvm(gab), gab_err,
               gab_string(gab, "Failed to create window"));
    return gab_union_cvalid(gab_nil);
  }

  if (!gladLoadGL(RGFW_getProcAddress)) {
    gab_vmpush(gab_thisvm(gab), gab_err,
               gab_string(gab, "Failed to load OpenGL"));
    return gab_union_cvalid(gab_nil);
  }

  while (RGFW_window_shouldClose(win) == RGFW_FALSE) {
    while (RGFW_window_checkEvent(
        win)) { // or RGFW_window_checkEvents(); if you only want callbacks
      // you can either check the current event yourself
      if (win->event.type == RGFW_quit)
        break;

      if (win->event.type == RGFW_mouseButtonPressed &&
          win->event.button == RGFW_mouseLeft) {
        printf("You clicked at x: %d, y: %d\n", win->event.point.x,
               win->event.point.y);
      }

      // or use the existing functions
      if (RGFW_isMousePressed(win, RGFW_mouseRight)) {
        printf("The right mouse button was clicked at x: %d, y: %d\n",
               win->event.point.x, win->event.point.y);
      }
    }

    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // You can use modern OpenGL techniques, but this method is more
    // straightforward for drawing just one triangle.
    glBegin(GL_TRIANGLES);
    glColor3f(1, 0, 0);
    glVertex2f(-0.6, -0.75);
    glColor3f(0, 1, 0);
    glVertex2f(0.6, -0.75);
    glColor3f(0, 0, 1);
    glVertex2f(0, 0.75);
    glEnd();

    RGFW_window_swapBuffers(win);
  }

  RGFW_window_close(win);

  gab_vmpush(gab_thisvm(gab), gab_ok);
  return gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_MAIN_FN {
  gab_value mod = gab_message(gab, "ui");
  gab_def(gab, {
                   gab_message(gab, "open"),
                   mod,
                   gab_snative(gab, "open", gab_uilib_open),
               });

  gab_value res[] = {gab_ok, mod};

  return (union gab_value_pair){
      .status = gab_cvalid,
      .aresult = a_gab_value_create(res, sizeof(res) / sizeof(gab_value)),
  };
}

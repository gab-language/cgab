
#define GLAD_GL
#define GLAD_GL_IMPLEMENTATION
#include "GL/gl.h"

// Further includes of "GL/gl.h" should not define implementations.
#undef GLAD_GL
#undef GLAD_GL_IMPLEMENTATION

#define RGFW_IMPLEMENTATION
#include "RGFW/RGFW.h"

#include "gab.h"

/* nuklear - v1.05 - public domain */
#include <assert.h>
#include <dirent.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_IMPLEMENTATION
#include "nuklear/nuklear.h"

/* macros */
#define WINDOW_WIDTH 1200
#define WINDOW_HEIGHT 800

#define MAX_VERTEX_MEMORY 512 * 1024
#define MAX_ELEMENT_MEMORY 128 * 1024

#define UNUSED(a) (void)a
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) < (b) ? (b) : (a))
#define LEN(a) (sizeof(a) / sizeof(a)[0])

#ifdef __APPLE__
#define NK_SHADER_VERSION "#version 150\n"
#else
#define NK_SHADER_VERSION "#version 300 es\n"
#endif

struct nk_glfw_vertex {
  float position[2];
  float uv[2];
  nk_byte col[4];
};

struct device {
  struct nk_buffer cmds;
  struct nk_draw_null_texture tex_null;
  GLuint vbo, vao, ebo;
  GLuint prog;
  GLuint vert_shdr;
  GLuint frag_shdr;
  GLint attrib_pos;
  GLint attrib_uv;
  GLint attrib_col;
  GLint uniform_tex;
  GLint uniform_proj;
  GLuint font_tex;
};

static void device_init(struct device *dev) {
  GLint status;
  static const GLchar *vertex_shader =
      NK_SHADER_VERSION "uniform mat4 ProjMtx;\n"
                        "in vec2 Position;\n"
                        "in vec2 TexCoord;\n"
                        "in vec4 Color;\n"
                        "out vec2 Frag_UV;\n"
                        "out vec4 Frag_Color;\n"
                        "void main() {\n"
                        "   Frag_UV = TexCoord;\n"
                        "   Frag_Color = Color;\n"
                        "   gl_Position = ProjMtx * vec4(Position.xy, 0, 1);\n"
                        "}\n";
  static const GLchar *fragment_shader = NK_SHADER_VERSION
      "precision mediump float;\n"
      "uniform sampler2D Texture;\n"
      "in vec2 Frag_UV;\n"
      "in vec4 Frag_Color;\n"
      "out vec4 Out_Color;\n"
      "void main(){\n"
      "   Out_Color = Frag_Color * texture(Texture, Frag_UV.st);\n"
      "}\n";

  nk_buffer_init_default(&dev->cmds);
  dev->prog = glCreateProgram();
  dev->vert_shdr = glCreateShader(GL_VERTEX_SHADER);
  dev->frag_shdr = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(dev->vert_shdr, 1, &vertex_shader, 0);
  glShaderSource(dev->frag_shdr, 1, &fragment_shader, 0);
  glCompileShader(dev->vert_shdr);
  glCompileShader(dev->frag_shdr);
  glGetShaderiv(dev->vert_shdr, GL_COMPILE_STATUS, &status);
  assert(status == GL_TRUE);
  glGetShaderiv(dev->frag_shdr, GL_COMPILE_STATUS, &status);
  assert(status == GL_TRUE);
  glAttachShader(dev->prog, dev->vert_shdr);
  glAttachShader(dev->prog, dev->frag_shdr);
  glLinkProgram(dev->prog);
  glGetProgramiv(dev->prog, GL_LINK_STATUS, &status);
  assert(status == GL_TRUE);

  dev->uniform_tex = glGetUniformLocation(dev->prog, "Texture");
  dev->uniform_proj = glGetUniformLocation(dev->prog, "ProjMtx");
  dev->attrib_pos = glGetAttribLocation(dev->prog, "Position");
  dev->attrib_uv = glGetAttribLocation(dev->prog, "TexCoord");
  dev->attrib_col = glGetAttribLocation(dev->prog, "Color");

  {
    /* buffer setup */
    GLsizei vs = sizeof(struct nk_glfw_vertex);
    size_t vp = offsetof(struct nk_glfw_vertex, position);
    size_t vt = offsetof(struct nk_glfw_vertex, uv);
    size_t vc = offsetof(struct nk_glfw_vertex, col);

    glGenBuffers(1, &dev->vbo);
    glGenBuffers(1, &dev->ebo);
    glGenVertexArrays(1, &dev->vao);

    glBindVertexArray(dev->vao);
    glBindBuffer(GL_ARRAY_BUFFER, dev->vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, dev->ebo);

    glEnableVertexAttribArray((GLuint)dev->attrib_pos);
    glEnableVertexAttribArray((GLuint)dev->attrib_uv);
    glEnableVertexAttribArray((GLuint)dev->attrib_col);

    glVertexAttribPointer((GLuint)dev->attrib_pos, 2, GL_FLOAT, GL_FALSE, vs,
                          (void *)vp);
    glVertexAttribPointer((GLuint)dev->attrib_uv, 2, GL_FLOAT, GL_FALSE, vs,
                          (void *)vt);
    glVertexAttribPointer((GLuint)dev->attrib_col, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                          vs, (void *)vc);
  }

  glBindTexture(GL_TEXTURE_2D, 0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  glBindVertexArray(0);
}

static void device_upload_atlas(struct device *dev, const void *image,
                                int width, int height) {
  glGenTextures(1, &dev->font_tex);
  glBindTexture(GL_TEXTURE_2D, dev->font_tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)width, (GLsizei)height, 0,
               GL_RGBA, GL_UNSIGNED_BYTE, image);
}

static void device_shutdown(struct device *dev) {
  glDetachShader(dev->prog, dev->vert_shdr);
  glDetachShader(dev->prog, dev->frag_shdr);
  glDeleteShader(dev->vert_shdr);
  glDeleteShader(dev->frag_shdr);
  glDeleteProgram(dev->prog);
  glDeleteTextures(1, &dev->font_tex);
  glDeleteBuffers(1, &dev->vbo);
  glDeleteBuffers(1, &dev->ebo);
  nk_buffer_free(&dev->cmds);
}

static void device_draw(struct device *dev, struct nk_context *ctx, int width,
                        int height, struct nk_vec2 scale,
                        enum nk_anti_aliasing AA) {
  GLfloat ortho[4][4] = {
      {2.0f, 0.0f, 0.0f, 0.0f},
      {0.0f, -2.0f, 0.0f, 0.0f},
      {0.0f, 0.0f, -1.0f, 0.0f},
      {-1.0f, 1.0f, 0.0f, 1.0f},
  };
  ortho[0][0] /= (GLfloat)width;
  ortho[1][1] /= (GLfloat)height;

  /* setup global state */
  glEnable(GL_BLEND);
  glBlendEquation(GL_FUNC_ADD);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_CULL_FACE);
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_SCISSOR_TEST);
  glActiveTexture(GL_TEXTURE0);

  /* setup program */
  glUseProgram(dev->prog);
  glUniform1i(dev->uniform_tex, 0);
  glUniformMatrix4fv(dev->uniform_proj, 1, GL_FALSE, &ortho[0][0]);
  {
    /* convert from command queue into draw list and draw to screen */
    const struct nk_draw_command *cmd;
    void *vertices, *elements;
    const nk_draw_index *offset = NULL;

    /* allocate vertex and element buffer */
    glBindVertexArray(dev->vao);
    glBindBuffer(GL_ARRAY_BUFFER, dev->vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, dev->ebo);

    glBufferData(GL_ARRAY_BUFFER, MAX_VERTEX_MEMORY, NULL, GL_STREAM_DRAW);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, MAX_ELEMENT_MEMORY, NULL,
                 GL_STREAM_DRAW);

    /* load draw vertices & elements directly into vertex + element buffer */
    vertices = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
    elements = glMapBuffer(GL_ELEMENT_ARRAY_BUFFER, GL_WRITE_ONLY);
    {
      /* fill convert configuration */
      struct nk_convert_config config;
      static const struct nk_draw_vertex_layout_element vertex_layout[] = {
          {
              NK_VERTEX_POSITION,
              NK_FORMAT_FLOAT,
              NK_OFFSETOF(struct nk_glfw_vertex, position),
          },
          {
              NK_VERTEX_TEXCOORD,
              NK_FORMAT_FLOAT,
              NK_OFFSETOF(struct nk_glfw_vertex, uv),
          },
          {
              NK_VERTEX_COLOR,
              NK_FORMAT_R8G8B8A8,
              NK_OFFSETOF(struct nk_glfw_vertex, col),
          },
          {
              NK_VERTEX_LAYOUT_END,
          },
      };

      NK_MEMSET(&config, 0, sizeof(config));
      config.vertex_layout = vertex_layout;
      config.vertex_size = sizeof(struct nk_glfw_vertex);
      config.vertex_alignment = NK_ALIGNOF(struct nk_glfw_vertex);
      config.tex_null = dev->tex_null;
      config.circle_segment_count = 22;
      config.curve_segment_count = 22;
      config.arc_segment_count = 22;
      config.global_alpha = 1.0f;
      config.shape_AA = AA;
      config.line_AA = AA;

      /* setup buffers to load vertices and elements */
      {
        struct nk_buffer vbuf, ebuf;
        nk_buffer_init_fixed(&vbuf, vertices, MAX_VERTEX_MEMORY);
        nk_buffer_init_fixed(&ebuf, elements, MAX_ELEMENT_MEMORY);
        nk_convert(ctx, &dev->cmds, &vbuf, &ebuf, &config);
      }
    }
    glUnmapBuffer(GL_ARRAY_BUFFER);
    glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);

    /* iterate over and execute each draw command */
    nk_draw_foreach(cmd, ctx, &dev->cmds) {
      if (!cmd->elem_count)
        continue;
      glBindTexture(GL_TEXTURE_2D, (GLuint)cmd->texture.id);
      glScissor(
          (GLint)(cmd->clip_rect.x * scale.x),
          (GLint)((height - (GLint)(cmd->clip_rect.y + cmd->clip_rect.h)) *
                  scale.y),
          (GLint)(cmd->clip_rect.w * scale.x),
          (GLint)(cmd->clip_rect.h * scale.y));
      glDrawElements(GL_TRIANGLES, (GLsizei)cmd->elem_count, GL_UNSIGNED_SHORT,
                     offset);
      offset += cmd->elem_count;
    }
    nk_clear(ctx);
    nk_buffer_clear(&dev->cmds);
  }

  /* default OpenGL state */
  glUseProgram(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  glBindVertexArray(0);
  glDisable(GL_BLEND);
  glDisable(GL_SCISSOR_TEST);
}

struct gui {
  struct RGFW_window win;
  struct device device;
  struct nk_context ctx;
  struct nk_font *font;
  struct nk_font_atlas atlas;
  struct gab_triple gab;
};

gab_value evch = gab_cundefined;
struct gui gui;

void onkey(RGFW_window *win, RGFW_key key, unsigned char keyChar,
           RGFW_keymod keyMod, RGFW_bool down) {
  nk_input_char(&gui.ctx, down);

  gab_chnput(gui.gab, evch, gab_nstring(gui.gab, 1, (const char *)&keyChar));
}

void onmousebutton(RGFW_window *win, unsigned char b, double dbl,
                   unsigned char down) {
  struct gui *gui = win->userPtr;
  switch (b) {
  case RGFW_mouseLeft:
    nk_input_button(&gui->ctx, NK_BUTTON_LEFT, gui->win._lastMousePoint.x,
                    gui->win._lastMousePoint.y, down);
    break;
  case RGFW_mouseRight:
    nk_input_button(&gui->ctx, NK_BUTTON_RIGHT, gui->win._lastMousePoint.x,
                    gui->win._lastMousePoint.y, down);
    break;
  };
}

void onmousepos(RGFW_window *win, RGFW_point dst, RGFW_point) {
  struct gui *gui = win->userPtr;
  nk_input_motion(&gui->ctx, dst.x, dst.y);
}

union gab_value_pair gab_uilib_draw(struct gab_triple gab, uint64_t argc,
                                    gab_value argv[argc]) {
  // scale.x = (float)display_width / (float)gui->win.r.w;
  // scale.y = (float)display_height / (float)gui->win.r.h;
  if (nk_begin(&gui.ctx, "Show", nk_rect(0, 0, gui.win.r.w, gui.win.r.h), 0)) {
    /* fixed widget pixel width */
    nk_layout_row_static(&gui.ctx, 30, 80, 1);
    if (nk_button_label(&gui.ctx, "button")) {
      /* event handling */
      printf("PRESSED!\n");
    }
    // init gui state
    enum { EASY, HARD };
    static int op = EASY;
    static float value = 0.6f;

    /* fixed widget window ratio width */
    nk_layout_row_dynamic(&gui.ctx, 30, 2);
    if (nk_option_label(&gui.ctx, "easy", op == EASY))
      op = EASY;
    if (nk_option_label(&gui.ctx, "hard", op == HARD))
      op = HARD;

    /* custom widget pixel width */
    nk_layout_row_begin(&gui.ctx, NK_STATIC, 30, 2);
    {
      nk_layout_row_push(&gui.ctx, 50);
      nk_label(&gui.ctx, "Volume:", NK_TEXT_LEFT);
      nk_layout_row_push(&gui.ctx, 110);
      nk_slider_float(&gui.ctx, 0, &value, 1.0f, 0.1f);
    }
    nk_layout_row_end(&gui.ctx);
  }
  nk_end(&gui.ctx);
  return gab_union_cvalid(gab_nil);
}

union gab_value_pair gab_uilib_open(struct gab_triple gab, uint64_t argc,
                                    gab_value argv[argc]) {
  if (evch != gab_cundefined)
    return gab_vmpush(gab_thisvm(gab), gab_ok, evch), gab_union_cvalid(gab_nil);

  if (!RGFW_createWindowPtr("window", RGFW_RECT(0, 0, 800, 800),
                            RGFW_windowCenter | RGFW_windowNoResize, &gui.win))
    return gab_vmpush(gab_thisvm(gab), gab_err,
                      gab_string(gab, "Failed to create window")),
           gab_union_cvalid(gab_nil);

  gui.win.userPtr = &gui;
  RGFW_setKeyCallback(onkey);
  RGFW_setMouseButtonCallback(onmousebutton);
  RGFW_setMousePosCallback(onmousepos);

  if (!gladLoadGL(RGFW_getProcAddress))
    return gab_vmpush(gab_thisvm(gab), gab_err,
                      gab_string(gab, "Failed to load OpenGL")),
           gab_union_cvalid(gab_nil);

  device_init(&gui.device);
  nk_font_atlas_init_default(&gui.atlas);
  nk_font_atlas_begin(&gui.atlas);
  gui.font = nk_font_atlas_add_default(&gui.atlas, 13.0f, NULL);

  int w, h;
  const void *image =
      nk_font_atlas_bake(&gui.atlas, &w, &h, NK_FONT_ATLAS_RGBA32);

  device_upload_atlas(&gui.device, image, w, h);

  nk_font_atlas_end(&gui.atlas, nk_handle_id((int)gui.device.font_tex),
                    &gui.device.tex_null);

  nk_init_default(&gui.ctx, &gui.font->handle);

  while (RGFW_window_shouldClose(&gui.win) == RGFW_FALSE) {
    RGFW_window_checkEvents(&gui.win, 10);

    glViewport(0, 0, gui.win.r.w, gui.win.r.h);
    glClear(GL_COLOR_BUFFER_BIT);
    glClearColor(0.2f, 0.2f, 0.2f, 1.0f);

    struct nk_vec2 scale = {1, 1};
    device_draw(&gui.device, &gui.ctx, gui.win.r.w, gui.win.r.h, scale,
                NK_ANTI_ALIASING_ON);
    RGFW_window_swapBuffers(&gui.win);
  }

  evch = gab_channel(gab);
  gab_egkeep(gab.eg, gab_iref(gab, evch));

  return gab_vmpush(gab_thisvm(gab), gab_ok, evch), gab_union_cvalid(gab_nil);
}

GAB_DYNLIB_MAIN_FN {
  gab_value mod = gab_message(gab, "ui");
  gab_def(gab,
          {
              gab_message(gab, "open"),
              mod,
              gab_snative(gab, "open", gab_uilib_open),
          },
          {
              gab_message(gab, "draw"),
              mod,
              gab_snative(gab, "draw", gab_uilib_draw),
          }, );

  gab_value res[] = {gab_ok, mod};

  return (union gab_value_pair){
      .status = gab_cvalid,
      .aresult = a_gab_value_create(res, sizeof(res) / sizeof(gab_value)),
  };
}

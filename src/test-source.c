// OBS Test Card Plugin - Optimized Rewrite
// High-performance implementation with cross-platform text rendering

#include <graphics/effect.h>
#include <graphics/graphics.h>
#include <graphics/image-file.h>
#include <graphics/matrix4.h>
#include <obs-module.h>
#include <util/bmem.h>

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Define STB_TRUETYPE implementation once
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#define CELL_SIZE 50
#define DEFAULT_WIDTH 1920
#define DEFAULT_HEIGHT 1080
#define FONT_SIZE_BASE 48

// Dirty flags for intelligent regeneration
typedef enum {
  DIRTY_NONE = 0,
  DIRTY_STATIC = 1 << 0,    // Resolution changed, full regen
  DIRTY_TEXT = 1 << 1,      // Clock second changed
  DIRTY_GRID_ANIM = 1 << 2, // Grid cell animations
  DIRTY_SCANLINES = 1 << 3  // Scan lines (always dirty)
} dirty_flags_t;

// Text cache structure
typedef struct {
  uint8_t *bitmap; // Grayscale bitmap
  int width;
  int height;
  int baseline;
  char text[64];
  bool valid;
} text_cache_t;

struct test_source_data {
  obs_source_t *source;

  // Layer textures
  gs_texture_t *layer_static;  // Checkerboard + borders
  gs_texture_t *layer_dynamic; // Animations + scan lines
  gs_texture_t *layer_text;    // Text overlay
  gs_texture_t *logo_tex;

  // CPU buffers
  uint32_t *static_buffer;
  uint32_t *dynamic_buffer;
  uint32_t *text_buffer;

  // Text rendering
  text_cache_t cache_clock;
  text_cache_t cache_resolution;
  text_cache_t cache_title;
  unsigned char *font_data;
  stbtt_fontinfo font;

  // Dirty tracking
  dirty_flags_t dirty;
  time_t last_second;
  uint32_t width;
  uint32_t height;
  float rotation_time;

  // Frame caching for video mapping (multiple renders per frame)
  uint64_t last_render_time;
  bool frame_cached;

  // Grid animation
  int cell_size;
  uint32_t bg_dark_color;  // Background dark squares color
  uint32_t bg_light_color; // Background light squares color
  uint32_t *grid_colors;
  int grid_cols;
  int grid_rows;
  int grid_min_x;
  int grid_min_y;
  float grid_timer;
  float *grid_cell_timers;
  uint32_t *grid_random_targets;

  // Scan lines
  float h_scan_time;
  float v_scan_time;
};

extern obs_module_t *module;

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

static uint32_t hsv_to_rgb(float h, float s, float v) {
  float c = v * s;
  float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
  float m = v - c;
  float r, g, b;

  if (h < 60.0f) {
    r = c;
    g = x;
    b = 0;
  } else if (h < 120.0f) {
    r = x;
    g = c;
    b = 0;
  } else if (h < 180.0f) {
    r = 0;
    g = c;
    b = x;
  } else if (h < 240.0f) {
    r = 0;
    g = x;
    b = c;
  } else if (h < 300.0f) {
    r = x;
    g = 0;
    b = c;
  } else {
    r = c;
    g = 0;
    b = x;
  }

  uint8_t ri = (uint8_t)((r + m) * 255.0f);
  uint8_t gi = (uint8_t)((g + m) * 255.0f);
  uint8_t bi = (uint8_t)((b + m) * 255.0f);

  return 0xFF000000 | (bi << 16) | (gi << 8) | ri;
}

// ============================================================================
// FONT LOADING (Cross-platform)
// ============================================================================

static bool load_system_font(struct test_source_data *data) {
  const char *font_paths[] = {
#ifdef _WIN32
      "C:/Windows/Fonts/arial.ttf",
      "C:/Windows/Fonts/segoeui.ttf",
#elif __APPLE__
      "/System/Library/Fonts/Helvetica.ttc",
      "/System/Library/Fonts/SFNS.ttf",
#else
      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
      "/usr/share/fonts/TTF/DejaVuSans.ttf",
#endif
  };

  for (size_t i = 0; i < sizeof(font_paths) / sizeof(font_paths[0]); i++) {
    FILE *f = fopen(font_paths[i], "rb");
    if (!f)
      continue;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    data->font_data = bmalloc(size);
    fread(data->font_data, 1, size, f);
    fclose(f);

    if (stbtt_InitFont(&data->font, data->font_data, 0)) {
      blog(LOG_INFO, "[test_source] Font loaded: %s (%ld bytes)", font_paths[i],
           size);
      return true;
    }

    bfree(data->font_data);
    data->font_data = NULL;
  }

  blog(LOG_ERROR, "[test_source] Failed to load system font");
  return false;
}

// ============================================================================
// TEXT RENDERING (Optimized with stb_truetype)
// ============================================================================

static void render_text_cached(text_cache_t *cache, const char *text,
                               stbtt_fontinfo *font, int font_size) {
  // Skip if unchanged
  if (cache->valid && strcmp(cache->text, text) == 0)
    return;

  float scale = stbtt_ScaleForPixelHeight(font, (float)font_size);
  int ascent, descent, line_gap;
  stbtt_GetFontVMetrics(font, &ascent, &descent, &line_gap);
  cache->baseline = (int)(ascent * scale);

  // Calculate total width
  int total_width = 0;
  for (const char *p = text; *p; p++) {
    int advance, lsb;
    stbtt_GetCodepointHMetrics(font, *p, &advance, &lsb);
    total_width += (int)(advance * scale);
  }

  // Allocate bitmap
  cache->width = total_width;
  cache->height = font_size + 4;

  if (cache->bitmap)
    bfree(cache->bitmap);
  cache->bitmap = bzalloc(cache->width * cache->height * sizeof(uint8_t));

  // Rasterize glyphs
  int x = 0;
  for (const char *p = text; *p; p++) {
    int advance, lsb, x0, y0, x1, y1;
    stbtt_GetCodepointHMetrics(font, *p, &advance, &lsb);
    stbtt_GetCodepointBitmapBox(font, *p, scale, scale, &x0, &y0, &x1, &y1);

    int glyph_w = x1 - x0;
    int glyph_h = y1 - y0;
    int glyph_x = x + (int)(lsb * scale);
    int glyph_y = cache->baseline + y0;

    if (glyph_x >= 0 && glyph_y >= 0 && glyph_x + glyph_w <= cache->width &&
        glyph_y + glyph_h <= cache->height) {
      unsigned char *dst = cache->bitmap + glyph_y * cache->width + glyph_x;
      stbtt_MakeCodepointBitmap(font, dst, glyph_w, glyph_h, cache->width,
                                scale, scale, *p);
    }

    x += (int)(advance * scale);
  }

  strncpy(cache->text, text, sizeof(cache->text) - 1);
  cache->valid = true;
}

static void blend_text_to_buffer(uint32_t *buffer, int buf_width,
                                 int buf_height, text_cache_t *cache, int x,
                                 int y, uint32_t color) {
  if (!cache->valid || !cache->bitmap)
    return;

  uint8_t fg_r = color & 0xFF;
  uint8_t fg_g = (color >> 8) & 0xFF;
  uint8_t fg_b = (color >> 16) & 0xFF;

  for (int row = 0; row < cache->height; row++) {
    int dest_y = y + row;
    if (dest_y < 0 || dest_y >= buf_height)
      continue;

    for (int col = 0; col < cache->width; col++) {
      int dest_x = x + col;
      if (dest_x < 0 || dest_x >= buf_width)
        continue;

      uint8_t alpha = cache->bitmap[row * cache->width + col];
      if (alpha == 0)
        continue;

      uint32_t bg = buffer[dest_y * buf_width + dest_x];
      uint8_t bg_r = bg & 0xFF;
      uint8_t bg_g = (bg >> 8) & 0xFF;
      uint8_t bg_b = (bg >> 16) & 0xFF;

      float a = alpha / 255.0f;
      uint8_t r = (uint8_t)(fg_r * a + bg_r * (1.0f - a));
      uint8_t g = (uint8_t)(fg_g * a + bg_g * (1.0f - a));
      uint8_t b = (uint8_t)(fg_b * a + bg_b * (1.0f - a));

      buffer[dest_y * buf_width + dest_x] =
          0xFF000000 | (b << 16) | (g << 8) | r;
    }
  }
}

// ============================================================================
// LAYERED RENDERING
// ============================================================================

static void resize_grid(struct test_source_data *data) {
  if (data->width == 0 || data->height == 0 || data->cell_size <= 0)
    return;

  int cx = data->width / 2;
  int cy = data->height / 2;
  int min_cell_x = (int)floor((double)(-cx) / data->cell_size);
  int max_cell_x = (int)floor((double)(data->width - 1 - cx) / data->cell_size);
  int min_cell_y = (int)floor((double)(-cy) / data->cell_size);
  int max_cell_y =
      (int)floor((double)(data->height - 1 - cy) / data->cell_size);

  int cols = max_cell_x - min_cell_x + 1;
  int rows = max_cell_y - min_cell_y + 1;

  data->grid_min_x = min_cell_x;
  data->grid_min_y = min_cell_y;

  // Only reallocate if size changed
  if (data->grid_colors && data->grid_cols == cols && data->grid_rows == rows)
    return;

  if (data->grid_colors)
    bfree(data->grid_colors);
  if (data->grid_cell_timers)
    bfree(data->grid_cell_timers);
  if (data->grid_random_targets)
    bfree(data->grid_random_targets);

  data->grid_cols = cols;
  data->grid_rows = rows;
  data->grid_colors = bmalloc(cols * rows * sizeof(uint32_t));
  data->grid_cell_timers = bzalloc(cols * rows * sizeof(float));
  data->grid_random_targets = bzalloc(cols * rows * sizeof(uint32_t));

  // Initialize checkerboard pattern with configurable colors
  for (int y = 0; y < rows; y++) {
    for (int x = 0; x < cols; x++) {
      bool is_light = (x + y) % 2 == 0;
      data->grid_colors[y * cols + x] =
          is_light ? data->bg_light_color : data->bg_dark_color;
    }
  }
}

static void render_layer_static(struct test_source_data *data) {
  if (!(data->dirty & DIRTY_STATIC))
    return;

  uint32_t width = data->width;
  uint32_t height = data->height;
  uint32_t *buf = data->static_buffer;

  // Background color
  uint32_t bg_color = 0xFF500050;
  for (uint32_t i = 0; i < width * height; i++)
    buf[i] = bg_color;

  // === LAYER 1: GRID (background layer) ===
  resize_grid(data);
  int cx = width / 2;
  int cy = height / 2;

  for (uint32_t y = 0; y < height; y++) {
    for (uint32_t x = 0; x < width; x++) {
      int grid_x = (int)x - cx;
      int grid_y = (int)y - cy;
      int cell_x = (int)floor((double)grid_x / data->cell_size);
      int cell_y = (int)floor((double)grid_y / data->cell_size);
      int buf_x = cell_x - data->grid_min_x;
      int buf_y = cell_y - data->grid_min_y;

      if (buf_x >= 0 && buf_x < data->grid_cols && buf_y >= 0 &&
          buf_y < data->grid_rows) {
        buf[y * width + x] = data->grid_colors[buf_y * data->grid_cols + buf_x];
      }
    }
  }

  // Calculate dynamic HSV color (shared by cross and borders)
  float hue = fmodf(data->rotation_time * 30.0f, 360.0f);
  uint32_t dynamic_color = hsv_to_rgb(hue, 1.0f, 1.0f);

  // === LAYER 2: CRUZ CENTRAL (overlays grid, dynamic color) ===
  int cx_line = width / 2;
  int cy_line = height / 2;

  // Vertical cross line (4px thick)
  if (cx_line >= 2 && cx_line < (int)width - 2) {
    for (uint32_t y = 0; y < height; y++) {
      buf[y * width + cx_line - 2] = dynamic_color;
      buf[y * width + cx_line - 1] = dynamic_color;
      buf[y * width + cx_line] = dynamic_color;
      buf[y * width + cx_line + 1] = dynamic_color;
    }
  }

  // Horizontal cross line (4px thick)
  if (cy_line >= 2 && cy_line < (int)height - 2) {
    for (uint32_t x = 0; x < width; x++) {
      buf[(cy_line - 2) * width + x] = dynamic_color;
      buf[(cy_line - 1) * width + x] = dynamic_color;
      buf[cy_line * width + x] = dynamic_color;
      buf[(cy_line + 1) * width + x] = dynamic_color;
    }
  }

  // === LAYER 3: BORDES (same depth as cross, dynamic color) ===
  uint32_t border_color = dynamic_color;

  // Minimum size check
  if (width >= 6 && height >= 6) {
    // === OUTER BORDERS (3px) ===
    // Top 3 rows
    uint32_t max_row = (height < 3) ? height : 3;
    for (uint32_t y = 0; y < max_row; y++) {
      for (uint32_t x = 0; x < width; x++) {
        buf[y * width + x] = border_color;
      }
    }

    // Bottom 3 rows
    uint32_t start_row = (height > 3) ? (height - 3) : 0;
    for (uint32_t y = start_row; y < height; y++) {
      for (uint32_t x = 0; x < width; x++) {
        buf[y * width + x] = border_color;
      }
    }

    // Left 3 columns (skip corners)
    if (height > 6) {
      uint32_t max_col = (width < 3) ? width : 3;
      for (uint32_t y = 3; y < height - 3; y++) {
        for (uint32_t x = 0; x < max_col; x++) {
          buf[y * width + x] = border_color;
        }
      }
    }

    // Right 3 columns (skip corners)
    if (height > 6 && width > 3) {
      uint32_t start_col = width - 3;
      for (uint32_t y = 3; y < height - 3; y++) {
        for (uint32_t x = start_col; x < width; x++) {
          buf[y * width + x] = border_color;
        }
      }
    }

    // === INNER BORDERS (3px with 5% margin) ===
    uint32_t margin_x = (uint32_t)(width * 0.05f);
    uint32_t margin_y = (uint32_t)(height * 0.05f);

    // Need at least 12px space: margin (both sides) + 3px border + 3px safe
    // zone
    if (margin_x * 2 + 12 <= width && margin_y * 2 + 12 <= height) {
      uint32_t inner_left = margin_x;
      uint32_t inner_right = width - margin_x;
      uint32_t inner_top = margin_y;
      uint32_t inner_bottom = height - margin_y;

      // Additional safety check
      if (inner_left < inner_right && inner_top < inner_bottom &&
          inner_right <= width && inner_bottom <= height) {

        // Top inner border (3 rows)
        for (uint32_t offset = 0; offset < 3; offset++) {
          uint32_t y = inner_top + offset;
          if (y >= height)
            break;
          for (uint32_t x = inner_left; x < inner_right; x++) {
            if (x >= width)
              break;
            buf[y * width + x] = border_color;
          }
        }

        // Bottom inner border (3 rows)
        for (uint32_t offset = 0; offset < 3; offset++) {
          if (inner_bottom <= offset)
            break;
          uint32_t y = inner_bottom - 3 + offset;
          if (y >= height)
            continue;
          for (uint32_t x = inner_left; x < inner_right; x++) {
            if (x >= width)
              break;
            buf[y * width + x] = border_color;
          }
        }

        // Left inner border (3 columns, skip corners)
        if (inner_top + 3 < inner_bottom && inner_bottom > 3) {
          for (uint32_t y = inner_top + 3; y < inner_bottom - 3; y++) {
            if (y >= height)
              break;
            for (uint32_t offset = 0; offset < 3; offset++) {
              uint32_t x = inner_left + offset;
              if (x >= width)
                break;
              buf[y * width + x] = border_color;
            }
          }
        }

        // Right inner border (3 columns, skip corners)
        if (inner_top + 3 < inner_bottom && inner_bottom > 3 &&
            inner_right > 3) {
          for (uint32_t y = inner_top + 3; y < inner_bottom - 3; y++) {
            if (y >= height)
              break;
            for (uint32_t offset = 0; offset < 3; offset++) {
              uint32_t x = inner_right - 3 + offset;
              if (x >= width)
                break;
              buf[y * width + x] = border_color;
            }
          }
        }
      }
    }
  }

  // === LAYER 4: COLORED RECTANGLE (topmost, overlays everything) ===
  float min_dim = (float)(width < height ? width : height);
  int rect_width = (int)(min_dim * 0.5f);
  int rect_left = (width - rect_width) / 2;
  int rect_right = rect_left + rect_width;
  int rect_top = 0;
  int rect_bottom = height / 2;
  float rect_hue = fmodf(data->rotation_time / 60.0f, 1.0f) * 360.0f;
  uint32_t rect_color = hsv_to_rgb(rect_hue, 1.0f, 0.3f);

  for (uint32_t y = rect_top; y < (uint32_t)rect_bottom && y < height; y++) {
    for (uint32_t x = rect_left; x < (uint32_t)rect_right && x < width; x++) {
      buf[y * width + x] = rect_color;
    }
  }

skip_borders:

  // Upload to GPU
  if (!data->layer_static) {
    data->layer_static =
        gs_texture_create(width, height, GS_RGBA, 1, NULL, GS_DYNAMIC);
  }

  gs_texture_set_image(data->layer_static, (const uint8_t *)buf, width * 4,
                       false);

  data->dirty &= ~DIRTY_STATIC;
  blog(LOG_DEBUG, "[test_source] Static layer regenerated");
}

static void render_layer_text(struct test_source_data *data) {
  if (!(data->dirty & DIRTY_TEXT))
    return;

  uint32_t width = data->width;
  uint32_t height = data->height;
  uint32_t *buf = data->text_buffer;

  // Clear to transparent
  memset(buf, 0, width * height * sizeof(uint32_t));

  // Calculate positions
  float min_dim = (float)(width < height ? width : height);
  int font_size = (int)(min_dim * 0.04f);
  if (font_size < 16)
    font_size = 16;

  float logo_height = min_dim * 0.5f;
  float logo_center_y = height / 2.0f;
  float logo_top = logo_center_y - logo_height / 2.0f;

  int rect_width = (int)logo_height;
  int rect_left = (width - rect_width) / 2;

  int padding = (int)(min_dim * 0.02f);
  if (padding < 10)
    padding = 10;

  // Render text to caches
  time_t now = time(NULL);
  struct tm *timeinfo = localtime(&now);
  char clock_str[64];
  sprintf(clock_str, "%02d:%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min,
          timeinfo->tm_sec);

  char res_str[64];
  sprintf(res_str, "%dx%d", width, height);

  const char *title = "OBS TEST CARD";

  render_text_cached(&data->cache_clock, clock_str, &data->font, font_size);
  render_text_cached(&data->cache_resolution, res_str, &data->font, font_size);
  render_text_cached(&data->cache_title, title, &data->font, font_size);

  // Calculate Y positions (stacking upwards from logo)
  int title_y = (int)logo_top - padding - data->cache_title.height;
  int res_y = title_y - padding - data->cache_resolution.height;
  int clock_y = res_y - padding - data->cache_clock.height;

  // Calculate X positions (centered)
  int title_x = rect_left + (rect_width - data->cache_title.width) / 2;
  int res_x = rect_left + (rect_width - data->cache_resolution.width) / 2;
  int clock_x = rect_left + (rect_width - data->cache_clock.width) / 2;

  // Blend to buffer
  uint32_t text_color = 0xFFCCCCCC;
  blend_text_to_buffer(buf, width, height, &data->cache_title, title_x, title_y,
                       text_color);
  blend_text_to_buffer(buf, width, height, &data->cache_resolution, res_x,
                       res_y, text_color);
  blend_text_to_buffer(buf, width, height, &data->cache_clock, clock_x, clock_y,
                       text_color);

  // Upload to GPU
  if (!data->layer_text) {
    data->layer_text =
        gs_texture_create(width, height, GS_RGBA, 1, NULL, GS_DYNAMIC);
  }

  gs_texture_set_image(data->layer_text, (const uint8_t *)buf, width * 4,
                       false);

  data->dirty &= ~DIRTY_TEXT;
  blog(LOG_DEBUG, "[test_source] Text layer regenerated");
}

static void render_layer_dynamic(struct test_source_data *data) {
  // Always regenerate (scan lines change every frame)
  uint32_t width = data->width;
  uint32_t height = data->height;
  uint32_t *buf = data->dynamic_buffer;

  // Clear to transparent
  memset(buf, 0, width * height * sizeof(uint32_t));

  // Scan lines (simple white lines)
  uint32_t white = 0xFFFFFFFF;

  float h_val = data->h_scan_time;
  if (h_val > 20.0f)
    h_val = 40.0f - h_val;
  float h_factor = (20.0f - h_val) / 20.0f;
  int h_y = (int)(h_factor * height);

  float v_val = data->v_scan_time;
  float v_factor = (20.0f - v_val) / 20.0f;
  int v_x = (int)(v_factor * width);

  // Horizontal scan line
  if (h_y >= 0 && h_y < (int)height - 2) {
    for (uint32_t x = 0; x < width; x++) {
      buf[h_y * width + x] = white;
      buf[(h_y + 1) * width + x] = white;
    }
  }

  // Vertical scan line
  if (v_x >= 0 && v_x < (int)width - 2) {
    for (uint32_t y = 0; y < height; y++) {
      buf[y * width + v_x] = white;
      buf[y * width + v_x + 1] = white;
    }
  }

  // Upload to GPU
  if (!data->layer_dynamic) {
    data->layer_dynamic =
        gs_texture_create(width, height, GS_RGBA, 1, NULL, GS_DYNAMIC);
  }

  gs_texture_set_image(data->layer_dynamic, (const uint8_t *)buf, width * 4,
                       false);
}

// ============================================================================
// OBS CALLBACKS
// ============================================================================

static const char *test_source_get_name(void *type_data) {
  UNUSED_PARAMETER(type_data);
  return "TEST CARD OBS";
}

static void *test_source_create(obs_data_t *settings, obs_source_t *source) {
  struct test_source_data *data = bzalloc(sizeof(struct test_source_data));
  data->source = source;
  data->width = DEFAULT_WIDTH;
  data->height = DEFAULT_HEIGHT;
  data->cell_size = (int)obs_data_get_int(settings, "cell_size");
  if (data->cell_size <= 0)
    data->cell_size = CELL_SIZE;

  // Load background colors (format: 0xRRGGBB, need to add alpha)
  data->bg_dark_color =
      0xFF000000 | (uint32_t)obs_data_get_int(settings, "bg_dark_color");
  data->bg_light_color =
      0xFF000000 | (uint32_t)obs_data_get_int(settings, "bg_light_color");

  // Load font
  if (!load_system_font(data)) {
    blog(LOG_WARNING, "[test_source] Failed to load font, text disabled");
  }

  // Allocate buffers
  uint32_t buf_size = DEFAULT_WIDTH * DEFAULT_HEIGHT * sizeof(uint32_t);
  data->static_buffer = bmalloc(buf_size);
  data->dynamic_buffer = bmalloc(buf_size);
  data->text_buffer = bmalloc(buf_size);

  // Load logo
  obs_enter_graphics();
  char *path = obs_module_file("logo.png");
  if (path) {
    gs_image_file_t image;
    gs_image_file_init(&image, path);
    if (image.loaded) {
      gs_image_file_init_texture(&image);
      data->logo_tex = image.texture;
      image.texture = NULL;
    }
    gs_image_file_free(&image);
    bfree(path);
  }
  obs_leave_graphics();

  // Mark everything dirty
  data->dirty = DIRTY_STATIC | DIRTY_TEXT;
  data->last_second = -1;
  data->last_render_time = 0;
  data->frame_cached = false;

  srand((unsigned int)time(NULL));

  blog(LOG_INFO, "[test_source] Created (optimized version)");
  return data;
}

static void test_source_destroy(void *data) {
  struct test_source_data *src = data;

  if (src->static_buffer)
    bfree(src->static_buffer);
  if (src->dynamic_buffer)
    bfree(src->dynamic_buffer);
  if (src->text_buffer)
    bfree(src->text_buffer);

  if (src->cache_clock.bitmap)
    bfree(src->cache_clock.bitmap);
  if (src->cache_resolution.bitmap)
    bfree(src->cache_resolution.bitmap);
  if (src->cache_title.bitmap)
    bfree(src->cache_title.bitmap);

  if (src->font_data)
    bfree(src->font_data);

  if (src->grid_colors)
    bfree(src->grid_colors);
  if (src->grid_cell_timers)
    bfree(src->grid_cell_timers);
  if (src->grid_random_targets)
    bfree(src->grid_random_targets);

  obs_enter_graphics();
  if (src->layer_static)
    gs_texture_destroy(src->layer_static);
  if (src->layer_dynamic)
    gs_texture_destroy(src->layer_dynamic);
  if (src->layer_text)
    gs_texture_destroy(src->layer_text);
  if (src->logo_tex)
    gs_texture_destroy(src->logo_tex);
  obs_leave_graphics();

  bfree(data);
}

static uint32_t test_source_get_width(void *data) {
  return ((struct test_source_data *)data)->width;
}

static uint32_t test_source_get_height(void *data) {
  return ((struct test_source_data *)data)->height;
}

static void test_source_update(void *data, obs_data_t *settings) {
  struct test_source_data *src = data;
  src->cell_size = (int)obs_data_get_int(settings, "cell_size");
  if (src->cell_size <= 0)
    src->cell_size = CELL_SIZE;

  // Update background colors
  src->bg_dark_color =
      0xFF000000 | (uint32_t)obs_data_get_int(settings, "bg_dark_color");
  src->bg_light_color =
      0xFF000000 | (uint32_t)obs_data_get_int(settings, "bg_light_color");

  src->dirty |= DIRTY_STATIC; // Force regeneration with new colors
}

static void test_source_video_tick(void *data, float seconds) {
  struct test_source_data *src = data;

  src->rotation_time += seconds;
  src->h_scan_time += seconds;
  if (src->h_scan_time >= 40.0f)
    src->h_scan_time -= 40.0f;

  src->v_scan_time += seconds;
  if (src->v_scan_time >= 20.0f)
    src->v_scan_time -= 20.0f;

  // Update grid cell animations
  if (src->grid_colors && src->grid_cell_timers && src->grid_random_targets) {
    uint32_t light_gray = 0xFF682f25;
    uint32_t dark_gray = 0xFF502822;
    int total_cells = src->grid_cols * src->grid_rows;

    // Update existing animations
    for (int i = 0; i < total_cells; i++) {
      if (src->grid_cell_timers[i] > 0.0f) {
        src->grid_cell_timers[i] -= seconds;
        if (src->grid_cell_timers[i] < 0.0f)
          src->grid_cell_timers[i] = 0.0f;

        // Interpolate color
        int r = i / src->grid_cols;
        int c = i % src->grid_cols;
        uint32_t base_color = ((r + c) % 2 == 0) ? light_gray : dark_gray;

        float t = src->grid_cell_timers[i] / 2.0f;
        uint8_t b1 = (base_color >> 16) & 0xFF;
        uint8_t g1 = (base_color >> 8) & 0xFF;
        uint8_t r1 = base_color & 0xFF;

        uint32_t target_color = src->grid_random_targets[i];
        uint8_t b2 = (target_color >> 16) & 0xFF;
        uint8_t g2 = (target_color >> 8) & 0xFF;
        uint8_t r2 = target_color & 0xFF;

        uint8_t r_mix = (uint8_t)((r1 * (1.0f - t)) + (r2 * t));
        uint8_t g_mix = (uint8_t)((g1 * (1.0f - t)) + (g2 * t));
        uint8_t b_mix = (uint8_t)((b1 * (1.0f - t)) + (b2 * t));

        src->grid_colors[i] = 0xFF000000 | (b_mix << 16) | (g_mix << 8) | r_mix;
        src->dirty |= DIRTY_STATIC; // Mark for re-render
      }
    }

    // Trigger new random animations
    src->grid_timer += seconds;
    if (src->grid_timer >= 0.1f) {
      src->grid_timer = 0.0f;
      if (src->grid_cols > 0 && src->grid_rows > 0) {
        int start_row = 0;
        int num_rows = 1;
        int half_cols = src->grid_cols / 2;

        if (src->grid_rows >= 2) {
          start_row = (src->grid_rows / 2) - 1;
          num_rows = 2;
        } else {
          start_row = 0;
          num_rows = src->grid_rows;
        }

        // Left half: random HSV colors
        if (half_cols > 0) {
          int r_col = rand() % half_cols;
          int r_row = start_row + (rand() % num_rows);
          int idx = r_row * src->grid_cols + r_col;
          src->grid_cell_timers[idx] = 2.0f;
          float hue = (float)(rand() % 360);
          src->grid_random_targets[idx] = hsv_to_rgb(hue, 1.0f, 1.0f);
        }

        // Right half: grayscale
        if (src->grid_cols > half_cols) {
          int r_col = (rand() % (src->grid_cols - half_cols)) + half_cols;
          int r_row = start_row + (rand() % num_rows);
          int idx = r_row * src->grid_cols + r_col;
          src->grid_cell_timers[idx] = 2.0f;
          uint8_t val = (uint8_t)(rand() % 256);
          src->grid_random_targets[idx] =
              0xFF000000 | (val << 16) | (val << 8) | val;
        }
      }
    }
  }

  // Check if resolution changed
  struct obs_video_info ovi;
  if (obs_get_video_info(&ovi)) {
    if (ovi.base_width > 0 && ovi.base_height > 0) {
      if (ovi.base_width != src->width || ovi.base_height != src->height) {
        uint32_t new_width = ovi.base_width;
        uint32_t new_height = ovi.base_height;
        uint32_t buf_size = new_width * new_height * sizeof(uint32_t);

        // Try to reallocate all buffers FIRST
        uint32_t *new_static = brealloc(src->static_buffer, buf_size);
        uint32_t *new_dynamic = brealloc(src->dynamic_buffer, buf_size);
        uint32_t *new_text = brealloc(src->text_buffer, buf_size);

        // Only update if ALL reallocations succeeded
        if (new_static && new_dynamic && new_text) {
          src->static_buffer = new_static;
          src->dynamic_buffer = new_dynamic;
          src->text_buffer = new_text;

          // Clear buffers to prevent visual glitches from old data
          memset(src->static_buffer, 0, buf_size);
          memset(src->dynamic_buffer, 0, buf_size);
          memset(src->text_buffer, 0, buf_size);

          // Destroy old GPU textures - they have the wrong size
          obs_enter_graphics();
          if (src->layer_static) {
            gs_texture_destroy(src->layer_static);
            src->layer_static = NULL;
          }
          if (src->layer_dynamic) {
            gs_texture_destroy(src->layer_dynamic);
            src->layer_dynamic = NULL;
          }
          if (src->layer_text) {
            gs_texture_destroy(src->layer_text);
            src->layer_text = NULL;
          }
          obs_leave_graphics();

          // Now it's safe to update dimensions
          src->width = new_width;
          src->height = new_height;

          src->dirty = DIRTY_STATIC | DIRTY_TEXT;
          blog(LOG_INFO, "[test_source] Resolution changed to %dx%d",
               src->width, src->height);
        } else {
          blog(
              LOG_ERROR,
              "[test_source] Failed to reallocate buffers for resolution %dx%d",
              new_width, new_height);
          // Keep old resolution and buffers
        }
      }
    }
  }

  // Check if second changed (for clock)
  time_t now = time(NULL);
  if (now != src->last_second) {
    src->last_second = now;
    src->dirty |= DIRTY_TEXT;
  }
}

static void test_source_video_render(void *data, gs_effect_t *effect) {
  UNUSED_PARAMETER(effect);
  struct test_source_data *src = data;

  // Frame caching: check if we already rendered this frame
  uint64_t current_time = obs_get_video_frame_time();
  if (src->last_render_time == current_time && src->frame_cached) {
    // Same frame, skip regeneration (for video mapping with multiple slices)
  } else {
    // New frame or first render
    src->last_render_time = current_time;
    src->frame_cached = true;

    // Regenerate dirty layers
    render_layer_static(src);
    render_layer_text(src);
    render_layer_dynamic(src);
  }

  // Composite layers
  gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_DEFAULT);
  gs_technique_t *tech = gs_effect_get_technique(solid, "Draw");

  gs_technique_begin(tech);
  gs_technique_begin_pass(tech, 0);

  // Draw static layer
  if (src->layer_static) {
    gs_effect_set_texture(gs_effect_get_param_by_name(solid, "image"),
                          src->layer_static);
    gs_draw_sprite(src->layer_static, 0, src->width, src->height);
  }

  // Draw text layer (with alpha blending)
  if (src->layer_text) {
    gs_blend_state_push();
    gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);

    gs_effect_set_texture(gs_effect_get_param_by_name(solid, "image"),
                          src->layer_text);
    gs_draw_sprite(src->layer_text, 0, src->width, src->height);

    gs_blend_state_pop();
  }

  // Draw dynamic layer (scan lines)
  if (src->layer_dynamic) {
    gs_blend_state_push();
    gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);

    gs_effect_set_texture(gs_effect_get_param_by_name(solid, "image"),
                          src->layer_dynamic);
    gs_draw_sprite(src->layer_dynamic, 0, src->width, src->height);

    gs_blend_state_pop();
  }

  // Draw logo (rotated)
  if (src->logo_tex) {
    float min_dim =
        (float)(src->width < src->height ? src->width : src->height);
    float logo_h = min_dim * 0.5f;
    float logo_center_y = src->height / 2.0f;

    uint32_t tex_w = gs_texture_get_width(src->logo_tex);
    uint32_t tex_h = gs_texture_get_height(src->logo_tex);
    float scale = logo_h / tex_h;
    float final_w = tex_w * scale;
    float final_h = tex_h * scale;

    float lx = (src->width - final_w) / 2.0f;
    float ly = logo_center_y - final_h / 2.0f;

    gs_matrix_push();
    gs_matrix_translate3f(lx + final_w / 2.0f, ly + final_h / 2.0f, 0.0f);

    float angle_rad =
        (src->rotation_time / 60.0f) * 360.0f * (3.14159265f / 180.0f);
    gs_matrix_rotaa4f(0.0f, 0.0f, 1.0f, angle_rad);
    gs_matrix_translate3f(-final_w / 2.0f, -final_h / 2.0f, 0.0f);

    gs_effect_set_texture(gs_effect_get_param_by_name(solid, "image"),
                          src->logo_tex);
    gs_draw_sprite(src->logo_tex, 0, (uint32_t)final_w, (uint32_t)final_h);

    gs_matrix_pop();
  }

  gs_technique_end_pass(tech);
  gs_technique_end(tech);
}

static void test_source_get_defaults(obs_data_t *settings) {
  obs_data_set_default_int(settings, "cell_size", CELL_SIZE);
  obs_data_set_default_int(settings, "bg_dark_color",
                           0x502822); // Dark brown-red
  obs_data_set_default_int(settings, "bg_light_color",
                           0x682f25); // Light brown-red
}

static obs_properties_t *test_source_get_properties(void *data) {
  UNUSED_PARAMETER(data);
  obs_properties_t *props = obs_properties_create();

  obs_properties_add_int(props, "cell_size", "Grid Cell Size (pixels)", 20, 256,
                         1);
  obs_properties_add_color(props, "bg_dark_color",
                           "Background Dark Color (RGB)");
  obs_properties_add_color(props, "bg_light_color",
                           "Background Light Color (RGB)");

  return props;
}

struct obs_source_info test_source_info = {
    .id = "test_source",
    .type = OBS_SOURCE_TYPE_INPUT,
    .output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CAP_DISABLED,
    .get_name = test_source_get_name,
    .create = test_source_create,
    .destroy = test_source_destroy,
    .get_width = test_source_get_width,
    .get_height = test_source_get_height,
    .get_defaults = test_source_get_defaults,
    .get_properties = test_source_get_properties,
    .update = test_source_update,
    .video_render = test_source_video_render,
    .video_tick = test_source_video_tick,
};

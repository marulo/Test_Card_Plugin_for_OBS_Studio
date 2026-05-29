// OBS Test Card Plugin - Optimized Rewrite
// High-performance GPU implementation

#include <graphics/effect.h>
#include <graphics/graphics.h>
#include <graphics/image-file.h>
#include <graphics/matrix4.h>
#include <obs-module.h>
#include <util/bmem.h>
#include <util/platform.h>

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* clang-format off */
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcomma"
#endif

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#ifdef __clang__
#pragma clang diagnostic pop
#endif
/* clang-format on */

#include "logo_png.h"
#include "obs_logo_png.h"

#define CELL_SIZE 50
#define DEFAULT_WIDTH 1920
#define DEFAULT_HEIGHT 1080

#define ALPHA_MASK 0xFF000000

typedef enum {
	DIRTY_NONE = 0,
	DIRTY_GRID = 1 << 0,
	DIRTY_TEXT = 1 << 1,
} dirty_flags_t;

typedef struct {
	uint8_t *bitmap;
	int width;
	int height;
	int baseline;
	char text[64];
	int font_size;
	bool valid;
} text_cache_t;

struct test_source_data {
	obs_source_t *source;

	gs_texture_t *tex_white;
	gs_texture_t *tex_grid;
	gs_texture_t *logo_tex;
	gs_texture_t *obs_text_tex;
	gs_texture_t *layer_text;

	// Custom Effects
	gs_effect_t *point_effect;

	uint32_t *text_buffer;
	int text_tex_width;
	int text_tex_height;

	text_cache_t cache_clock;
	text_cache_t cache_resolution;
	text_cache_t cache_title;
	unsigned char *font_data;
	stbtt_fontinfo font;
	char custom_text[256];
	bool config_loaded;

	dirty_flags_t dirty;
	time_t last_second;
	uint32_t width;
	uint32_t height;
	float rotation_time;

	int cell_size;
	uint32_t bg_dark_color;
	uint32_t bg_light_color;

	uint32_t *grid_colors;
	int grid_cols;
	int grid_rows;
	int grid_min_x;
	int grid_min_y;
	float grid_timer;
	float *grid_cell_timers;
	uint32_t *grid_random_targets;

	float h_scan_time;
	float v_scan_time;
};

extern obs_module_t *module;

static uint32_t hsv_to_rgb(float h, float s, float v)
{
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

static bool load_system_font(struct test_source_data *data)
{
	const char *font_paths[] = {
#ifdef _WIN32
		"C:/Windows/Fonts/arialbd.ttf",
		"C:/Windows/Fonts/segoeuib.ttf",
		"C:/Windows/Fonts/arial.ttf",
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

		if (size <= 0) {
			fclose(f);
			continue;
		}

		data->font_data = bmalloc((size_t)size);
		size_t read_bytes = fread(data->font_data, 1, (size_t)size, f);
		fclose(f);

		if (read_bytes != (size_t)size) {
			blog(LOG_WARNING, "[test_source] Could not read entire font file");
			bfree(data->font_data);
			data->font_data = NULL;
			continue;
		}

		if (stbtt_InitFont(&data->font, data->font_data, 0)) {
			blog(LOG_INFO, "[test_source] Font loaded: %s (%ld bytes)", font_paths[i], size);
			return true;
		}

		bfree(data->font_data);
		data->font_data = NULL;
	}
	blog(LOG_ERROR, "[test_source] Failed to load system font");
	return false;
}

static void render_text_cached(text_cache_t *cache, const char *text, stbtt_fontinfo *font, int font_size)
{
	if (!text || text[0] == '\0')
		return; // Guard: nothing to render
	if (cache->valid && cache->font_size == font_size && strcmp(cache->text, text) == 0)
		return;

	float scale = stbtt_ScaleForPixelHeight(font, (float)font_size);
	int ascent, descent, line_gap;
	stbtt_GetFontVMetrics(font, &ascent, &descent, &line_gap);
	cache->baseline = (int)(ascent * scale);

	int total_width = 0;
	int current_x = 0;
	for (const char *p = text; *p; p++) {
		int advance, lsb, x0, y0, x1, y1;
		stbtt_GetCodepointHMetrics(font, *p, &advance, &lsb);
		stbtt_GetCodepointBitmapBox(font, *p, scale, scale, &x0, &y0, &x1, &y1);

		int glyph_w = x1 - x0;
		int glyph_x = current_x + (int)(lsb * scale);
		if (glyph_x + glyph_w > total_width)
			total_width = glyph_x + glyph_w;

		current_x += (int)(advance * scale);
		if (current_x > total_width)
			total_width = current_x;
	}

	cache->width = total_width;
	cache->height = font_size + 4;

	if (cache->bitmap)
		bfree(cache->bitmap);
	cache->bitmap = bzalloc((size_t)cache->width * (size_t)cache->height * sizeof(uint8_t));

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
			stbtt_MakeCodepointBitmap(font, dst, glyph_w, glyph_h, cache->width, scale, scale, *p);
		}
		x += (int)(advance * scale);
	}
	strncpy(cache->text, text, sizeof(cache->text) - 1);
	cache->text[sizeof(cache->text) - 1] = '\0';
	cache->font_size = font_size;
	cache->valid = true;
}

static void blend_text_to_buffer(uint32_t *buffer, int buf_width, int buf_height, text_cache_t *cache, int x, int y,
				 uint32_t color)
{
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

			buffer[dest_y * buf_width + dest_x] = 0xFF000000 | (b << 16) | (g << 8) | r;
		}
	}
}

static void resize_grid(struct test_source_data *data)
{
	if (data->width == 0 || data->height == 0 || data->cell_size <= 0)
		return;

	int cx = (int)data->width / 2;
	int cy = (int)data->height / 2;
	int min_cell_x = (int)floor((double)(-cx) / data->cell_size);
	int max_cell_x = (int)floor((double)((int)data->width - 1 - cx) / data->cell_size);
	int min_cell_y = (int)floor((double)(-cy) / data->cell_size);
	int max_cell_y = (int)floor((double)((int)data->height - 1 - cy) / data->cell_size);

	int cols = max_cell_x - min_cell_x + 1;
	int rows = max_cell_y - min_cell_y + 1;

	data->grid_min_x = min_cell_x;
	data->grid_min_y = min_cell_y;

	if (data->grid_colors && data->grid_cols == cols && data->grid_rows == rows)
		return;

	if (data->grid_colors)
		bfree(data->grid_colors);
	if (data->grid_cell_timers)
		bfree(data->grid_cell_timers);
	if (data->grid_random_targets)
		bfree(data->grid_random_targets);

	obs_enter_graphics();
	if (data->tex_grid) {
		gs_texture_destroy(data->tex_grid);
		data->tex_grid = NULL;
	}
	obs_leave_graphics();

	data->grid_cols = cols;
	data->grid_rows = rows;
	data->grid_colors = bmalloc((size_t)cols * (size_t)rows * sizeof(uint32_t));
	data->grid_cell_timers = bzalloc((size_t)cols * (size_t)rows * sizeof(float));
	data->grid_random_targets = bzalloc((size_t)cols * (size_t)rows * sizeof(uint32_t));

	for (int y = 0; y < rows; y++) {
		for (int x = 0; x < cols; x++) {
			bool is_light = (x + y) % 2 == 0;
			data->grid_colors[y * cols + x] = is_light ? data->bg_light_color : data->bg_dark_color;
		}
	}
	data->dirty |= DIRTY_GRID;
}

static void update_grid_texture(struct test_source_data *data)
{
	if (!(data->dirty & DIRTY_GRID))
		return;
	if (!data->grid_colors || data->grid_cols == 0 || data->grid_rows == 0)
		return;

	obs_enter_graphics();
	if (!data->tex_grid) {
		data->tex_grid = gs_texture_create((uint32_t)data->grid_cols, (uint32_t)data->grid_rows, GS_RGBA, 1,
						   NULL, GS_DYNAMIC);
	}

	gs_texture_set_image(data->tex_grid, (const uint8_t *)data->grid_colors, (uint32_t)data->grid_cols * 4, false);
	obs_leave_graphics();
	data->dirty &= ~DIRTY_GRID;
}

static void render_layer_text(struct test_source_data *data)
{
	if (!(data->dirty & DIRTY_TEXT))
		return;

	float min_dim = (float)(data->width < data->height ? data->width : data->height);
	int font_size = (int)(min_dim * 0.04f);
	if (font_size < 16)
		font_size = 16;
	int padding = (int)(min_dim * 0.02f);
	if (padding < 10)
		padding = 10;

	time_t now = time(NULL);
	struct tm *timeinfo = localtime(&now);
	char clock_str[64];
	if (timeinfo) {
		sprintf(clock_str, "%02d:%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
	} else {
		strcpy(clock_str, "--:--:--");
	}
	char res_str[64];
	sprintf(res_str, "%dx%d", data->width, data->height);
	const char *title = (data->custom_text[0] != '\0') ? data->custom_text : "OBS TEST CARD";

	render_text_cached(&data->cache_clock, clock_str, &data->font, font_size);
	render_text_cached(&data->cache_resolution, res_str, &data->font, font_size);
	render_text_cached(&data->cache_title, title, &data->font, font_size);

	int max_w = data->cache_title.width;
	if (data->cache_resolution.width > max_w)
		max_w = data->cache_resolution.width;
	if (data->cache_clock.width > max_w)
		max_w = data->cache_clock.width;

	int outline = 3;
	int total_h =
		data->cache_title.height + padding + data->cache_resolution.height + padding + data->cache_clock.height;
	int tex_w = max_w + outline * 2;
	int tex_h = total_h + outline * 2;
	if (tex_w == 0 || tex_h == 0)
		return;

	if (data->text_tex_width != tex_w || data->text_tex_height != tex_h) {
		if (data->text_buffer)
			bfree(data->text_buffer);
		data->text_buffer = bmalloc((size_t)tex_w * (size_t)tex_h * sizeof(uint32_t));
		data->text_tex_width = tex_w;
		data->text_tex_height = tex_h;

		obs_enter_graphics();
		if (data->layer_text) {
			gs_texture_destroy(data->layer_text);
			data->layer_text = NULL;
		}
		obs_leave_graphics();
	}

	memset(data->text_buffer, 0, (size_t)tex_w * (size_t)tex_h * sizeof(uint32_t));

	int title_y = outline;
	int res_y = title_y + data->cache_title.height + padding;
	int clock_y = res_y + data->cache_resolution.height + padding;

	int title_x = (tex_w - data->cache_title.width) / 2;
	int res_x = (tex_w - data->cache_resolution.width) / 2;
	int clock_x = (tex_w - data->cache_clock.width) / 2;

	uint32_t text_color = 0xFFCCCCCC;
	uint32_t outline_color = 0xFF000000;

	for (int dy = -outline; dy <= outline; dy++) {
		for (int dx = -outline; dx <= outline; dx++) {
			if (dx == 0 && dy == 0)
				continue;
			if (dx * dx + dy * dy > outline * outline + 1)
				continue; // Round outline
			blend_text_to_buffer(data->text_buffer, tex_w, tex_h, &data->cache_title, title_x + dx,
					     title_y + dy, outline_color);
			blend_text_to_buffer(data->text_buffer, tex_w, tex_h, &data->cache_resolution, res_x + dx,
					     res_y + dy, outline_color);
			blend_text_to_buffer(data->text_buffer, tex_w, tex_h, &data->cache_clock, clock_x + dx,
					     clock_y + dy, outline_color);
		}
	}

	blend_text_to_buffer(data->text_buffer, tex_w, tex_h, &data->cache_title, title_x, title_y, text_color);
	blend_text_to_buffer(data->text_buffer, tex_w, tex_h, &data->cache_resolution, res_x, res_y, text_color);
	blend_text_to_buffer(data->text_buffer, tex_w, tex_h, &data->cache_clock, clock_x, clock_y, text_color);

	obs_enter_graphics();
	if (!data->layer_text) {
		data->layer_text = gs_texture_create((uint32_t)tex_w, (uint32_t)tex_h, GS_RGBA, 1, NULL, GS_DYNAMIC);
	}
	gs_texture_set_image(data->layer_text, (const uint8_t *)data->text_buffer, (uint32_t)tex_w * 4, false);
	obs_leave_graphics();
	data->dirty &= ~DIRTY_TEXT;
}

static void draw_solid_rect(gs_effect_t *effect, gs_texture_t *white_tex, float x, float y, float w, float h,
			    uint32_t color)
{
	if (!white_tex)
		return;
	gs_eparam_t *color_param = gs_effect_get_param_by_name(effect, "color");

	struct vec4 v_color;
	float r = (color & 0xFF) / 255.0f;
	float g = ((color >> 8) & 0xFF) / 255.0f;
	float b = ((color >> 16) & 0xFF) / 255.0f;
	float a = ((color >> 24) & 0xFF) / 255.0f;
	vec4_set(&v_color, r, g, b, a);

	gs_effect_set_vec4(color_param, &v_color);

	gs_technique_t *tech = gs_effect_get_technique(effect, "Solid");
	gs_technique_begin_pass(tech, 0);

	gs_matrix_push();
	gs_matrix_translate3f(x, y, 0.0f);
	gs_matrix_scale3f(w, h, 1.0f);
	gs_draw_sprite(white_tex, 0, 1, 1);
	gs_matrix_pop();

	gs_technique_end_pass(tech);
}

static const char *point_effect_code =
	"uniform float4x4 ViewProj;\n"
	"uniform texture2d image;\n"
	"sampler_state pointSampler { Filter = Point; AddressU = Clamp; AddressV = Clamp; };\n"
	"struct VertData { float4 pos : POSITION; float2 uv : TEXCOORD0; };\n"
	"struct VertOut { float4 pos : POSITION; float2 uv : TEXCOORD0; };\n"
	"VertOut VShader(VertData v_in) {\n"
	"    VertOut vert_out;\n"
	"    vert_out.pos = mul(float4(v_in.pos.xyz, 1.0), ViewProj);\n"
	"    vert_out.uv = v_in.uv;\n"
	"    return vert_out;\n"
	"}\n"
	"float4 PShader(VertOut v_in) : TARGET {\n"
	"    return image.Sample(pointSampler, v_in.uv);\n"
	"}\n"
	"technique Draw {\n"
	"    pass {\n"
	"        vertex_shader = VShader(v_in);\n"
	"        pixel_shader  = PShader(v_in);\n"
	"    }\n"
	"}\n";

static const char *test_source_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("TestCard.Name");
}

static void *test_source_create(obs_data_t *settings, obs_source_t *source)
{
	struct test_source_data *data = bzalloc(sizeof(struct test_source_data));
	data->source = source;
	data->width = DEFAULT_WIDTH;
	data->height = DEFAULT_HEIGHT;
	data->cell_size = (int)obs_data_get_int(settings, "cell_size");
	if (data->cell_size < 16)
		data->cell_size = CELL_SIZE;

	data->bg_dark_color = 0xFF000000 | (uint32_t)obs_data_get_int(settings, "bg_dark_color");
	data->bg_light_color = 0xFF000000 | (uint32_t)obs_data_get_int(settings, "bg_light_color");

	if (!load_system_font(data)) {
		blog(LOG_WARNING, "[test_source] Failed to load font");
	}

	obs_enter_graphics();
	char *errors = NULL;
	data->point_effect = gs_effect_create(point_effect_code, "point_effect", &errors);
	if (errors) {
		blog(LOG_WARNING, "[obs-test-card] Error compiling point_effect: %s", errors);
		bfree(errors);
	}

	uint32_t white_pixel = 0xFFFFFFFF;
	const uint8_t *tex_data[1];
	tex_data[0] = (const uint8_t *)&white_pixel;
	data->tex_white = gs_texture_create(1, 1, GS_RGBA, 1, tex_data, 0);

	int img_w, img_h, img_c;
	uint8_t *pixels;

	pixels = stbi_load_from_memory(logo_png, (int)logo_png_len, &img_w, &img_h, &img_c, 4);
	if (pixels) {
		const uint8_t *ptr = pixels;
		data->logo_tex = gs_texture_create((uint32_t)img_w, (uint32_t)img_h, GS_RGBA, 1, &ptr, 0);
		stbi_image_free(pixels);
	}

	pixels = stbi_load_from_memory(obs_logo_png, (int)obs_logo_png_len, &img_w, &img_h, &img_c, 4);
	if (pixels) {
		const uint8_t *ptr = pixels;
		data->obs_text_tex = gs_texture_create((uint32_t)img_w, (uint32_t)img_h, GS_RGBA, 1, &ptr, 0);
		stbi_image_free(pixels);
	}
	obs_leave_graphics();

	data->dirty = DIRTY_GRID | DIRTY_TEXT;
	data->last_second = -1;
	srand((unsigned int)time(NULL));

	blog(LOG_INFO, "[test_source] Created (fully GPU optimized version)");
	return data;
}

static void test_source_destroy(void *data)
{
	struct test_source_data *src = data;

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
	if (src->tex_white)
		gs_texture_destroy(src->tex_white);
	if (src->tex_grid)
		gs_texture_destroy(src->tex_grid);
	if (src->layer_text)
		gs_texture_destroy(src->layer_text);
	if (src->point_effect)
		gs_effect_destroy(src->point_effect);
	if (src->logo_tex)
		gs_texture_destroy(src->logo_tex);
	if (src->obs_text_tex)
		gs_texture_destroy(src->obs_text_tex);
	obs_leave_graphics();

	bfree(data);
}

static uint32_t test_source_get_width(void *data)
{
	return ((struct test_source_data *)data)->width;
}

static uint32_t test_source_get_height(void *data)
{
	return ((struct test_source_data *)data)->height;
}

static void test_source_update(void *data, obs_data_t *settings)
{
	struct test_source_data *src = data;
	int old_cell_size = src->cell_size;
	uint32_t old_dark = src->bg_dark_color;
	uint32_t old_light = src->bg_light_color;

	src->cell_size = (int)obs_data_get_int(settings, "cell_size");
	if (src->cell_size < 16)
		src->cell_size = CELL_SIZE;

	src->bg_dark_color = ALPHA_MASK | (uint32_t)obs_data_get_int(settings, "bg_dark_color");
	src->bg_light_color = ALPHA_MASK | (uint32_t)obs_data_get_int(settings, "bg_light_color");

	const char *new_text = obs_data_get_string(settings, "custom_text");
	if (!new_text || new_text[0] == '\0')
		new_text = "OBS TEST CARD";
	if (strcmp(src->custom_text, new_text) != 0) {
		strncpy(src->custom_text, new_text, sizeof(src->custom_text) - 1);
		src->custom_text[sizeof(src->custom_text) - 1] = '\0';
		src->dirty |= DIRTY_TEXT;
	}

	if (old_dark != src->bg_dark_color || old_light != src->bg_light_color || old_cell_size != src->cell_size) {
		if (src->grid_colors) {
			bfree(src->grid_colors);
			src->grid_colors = NULL;
		}
		src->dirty |= DIRTY_GRID;
	}

	/* Save all settings whenever anything changed */
	if (src->dirty || old_dark != src->bg_dark_color || old_light != src->bg_light_color ||
	    old_cell_size != src->cell_size) {
		char *config_path = obs_module_get_config_path(obs_current_module(), "obs-test-card.json");
		if (config_path) {
			char *dir = bstrdup(config_path);
			char *sep = strrchr(dir, '/');
			if (!sep)
				sep = strrchr(dir, '\\');
			if (sep) {
				*sep = '\0';
				os_mkdirs(dir);
			}
			bfree(dir);

			obs_data_t *save_data = obs_data_create();
			obs_data_set_string(save_data, "custom_text", src->custom_text);
			obs_data_set_int(save_data, "cell_size", src->cell_size);
			obs_data_set_int(save_data, "bg_dark_color", (int64_t)(src->bg_dark_color & ~ALPHA_MASK));
			obs_data_set_int(save_data, "bg_light_color", (int64_t)(src->bg_light_color & ~ALPHA_MASK));
			obs_data_save_json_safe(save_data, config_path, "tmp", "bak");
			obs_data_release(save_data);
			bfree(config_path);
		}
	}
}

static void test_source_video_tick(void *data, float seconds)
{
	struct test_source_data *src = data;

	/* One-shot: apply saved custom text on the first tick after OBS has fully
	 * loaded the scene collection. This runs on the video thread so there is
	 * no race with the deferred-update mechanism. */
	if (!src->config_loaded) {
		src->config_loaded = true;
		char *cfg = obs_module_get_config_path(obs_current_module(), "obs-test-card.json");
		if (cfg) {
			obs_data_t *d = obs_data_create_from_json_file(cfg);
			bfree(cfg);
			if (d) {
				/* Restore custom text */
				const char *saved_text = obs_data_get_string(d, "custom_text");
				if (saved_text && *saved_text && strcmp(src->custom_text, saved_text) != 0) {
					strncpy(src->custom_text, saved_text, sizeof(src->custom_text) - 1);
					src->custom_text[sizeof(src->custom_text) - 1] = '\0';
					src->dirty |= DIRTY_TEXT;
				}

				/* Restore cell size */
				int64_t saved_cell = obs_data_get_int(d, "cell_size");
				if (saved_cell >= 16 && saved_cell != src->cell_size) {
					src->cell_size = (int)saved_cell;
					if (src->grid_colors) {
						bfree(src->grid_colors);
						src->grid_colors = NULL;
					}
					src->dirty |= DIRTY_GRID;
				}

				/* Restore colors */
				int64_t saved_dark = obs_data_get_int(d, "bg_dark_color");
				int64_t saved_light = obs_data_get_int(d, "bg_light_color");
				uint32_t new_dark = ALPHA_MASK | (uint32_t)saved_dark;
				uint32_t new_light = ALPHA_MASK | (uint32_t)saved_light;
				if (saved_dark != 0 && new_dark != src->bg_dark_color) {
					src->bg_dark_color = new_dark;
					if (src->grid_colors) {
						bfree(src->grid_colors);
						src->grid_colors = NULL;
					}
					src->dirty |= DIRTY_GRID;
				}
				if (saved_light != 0 && new_light != src->bg_light_color) {
					src->bg_light_color = new_light;
					if (src->grid_colors) {
						bfree(src->grid_colors);
						src->grid_colors = NULL;
					}
					src->dirty |= DIRTY_GRID;
				}

				obs_data_release(d);
			}
		}
	}

	src->rotation_time += seconds;
	src->h_scan_time += seconds;
	if (src->h_scan_time >= 40.0f)
		src->h_scan_time -= 40.0f;
	src->v_scan_time += seconds;
	if (src->v_scan_time >= 20.0f)
		src->v_scan_time -= 20.0f;

	if (src->grid_colors && src->grid_cell_timers && src->grid_random_targets) {
		int total_cells = src->grid_cols * src->grid_rows;

		for (int i = 0; i < total_cells; i++) {
			if (src->grid_cell_timers[i] > 0.0f) {
				src->grid_cell_timers[i] -= seconds;
				if (src->grid_cell_timers[i] < 0.0f)
					src->grid_cell_timers[i] = 0.0f;

				int r = i / src->grid_cols;
				int c = i % src->grid_cols;
				uint32_t base_color = ((r + c) % 2 == 0) ? src->bg_light_color : src->bg_dark_color;

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
				src->dirty |= DIRTY_GRID;
			}
		}

		src->grid_timer += seconds;
		if (src->grid_timer >= 0.1f) {
			src->grid_timer = 0.0f;
			if (src->grid_cols > 0 && src->grid_rows > 0) {
				int half_cols = src->grid_cols / 2;
				int start_row = src->grid_rows / 2;
				int num_rows = src->grid_rows - start_row;
				if (num_rows < 1)
					num_rows = 1;

				if (half_cols > 0) {
					int r_col = rand() % half_cols;
					int r_row = start_row + (rand() % num_rows);
					int idx = r_row * src->grid_cols + r_col;
					src->grid_cell_timers[idx] = 2.0f;
					float hue = (float)(rand() % 360);
					src->grid_random_targets[idx] = hsv_to_rgb(hue, 1.0f, 1.0f);
				}

				if (src->grid_cols > half_cols) {
					int r_col = (rand() % (src->grid_cols - half_cols)) + half_cols;
					int r_row = start_row + (rand() % num_rows);
					int idx = r_row * src->grid_cols + r_col;
					src->grid_cell_timers[idx] = 2.0f;
					uint8_t val = (uint8_t)(rand() % 256);
					src->grid_random_targets[idx] = 0xFF000000 | (val << 16) | (val << 8) | val;
				}
			}
		}
	}

	struct obs_video_info ovi;
	if (obs_get_video_info(&ovi)) {
		if (ovi.base_width > 0 && ovi.base_height > 0) {
			if (ovi.base_width != src->width || ovi.base_height != src->height) {
				src->width = ovi.base_width;
				src->height = ovi.base_height;
				src->dirty |= DIRTY_GRID | DIRTY_TEXT;
				blog(LOG_INFO, "[test_source] Resolution changed to %dx%d", src->width, src->height);
			}
		}
	}

	time_t now = time(NULL);
	if (now != src->last_second) {
		src->last_second = now;
		src->dirty |= DIRTY_TEXT;
	}
}

static void test_source_video_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct test_source_data *src = data;

	resize_grid(src);
	update_grid_texture(src);
	render_layer_text(src);

	// 1. DEFAULT effect for Textures (Logo, Text)
	gs_effect_t *default_effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_technique_t *tech_def = gs_effect_get_technique(default_effect, "Draw");

	// 2. SOLID effect for Vectors
	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);

	// --- DRAW GRID (Hardware Point Filtered Tiny Texture) ---
	if (src->tex_grid && src->point_effect) {
		gs_technique_t *tech_point = gs_effect_get_technique(src->point_effect, "Draw");
		gs_technique_begin(tech_point);
		gs_technique_begin_pass(tech_point, 0);

		int cx = (int)src->width / 2;
		int cy = (int)src->height / 2;
		float start_x = (float)(cx + src->grid_min_x * src->cell_size);
		float start_y = (float)(cy + src->grid_min_y * src->cell_size);

		gs_matrix_push();
		gs_matrix_translate3f(start_x, start_y, 0.0f);

		gs_effect_set_texture(gs_effect_get_param_by_name(src->point_effect, "image"), src->tex_grid);

		// Draw the tiny texture scaled up to the exact pixel boundaries
		uint32_t tex_w = (uint32_t)src->grid_cols * (uint32_t)src->cell_size;
		uint32_t tex_h = (uint32_t)src->grid_rows * (uint32_t)src->cell_size;
		gs_draw_sprite(src->tex_grid, 0, tex_w, tex_h);

		gs_matrix_pop();

		gs_technique_end_pass(tech_point);
		gs_technique_end(tech_point);
	}

	// --- DRAW VECTORS (Solid) ---
	gs_blend_state_push();

	// Borders, Cross, Scanlines
	float hue = fmodf(src->rotation_time * 30.0f, 360.0f);
	uint32_t border_color = hsv_to_rgb(hue, 1.0f, 1.0f);

	int cx_line = (int)src->width / 2;
	int cy_line = (int)src->height / 2;

	if (src->tex_white) {
		draw_solid_rect(solid, src->tex_white, (float)(cx_line - 2), 0.0f, 4.0f, (float)src->height,
				border_color);
		draw_solid_rect(solid, src->tex_white, 0.0f, (float)(cy_line - 2), (float)src->width, 4.0f,
				border_color);

		draw_solid_rect(solid, src->tex_white, 0.0f, 0.0f, (float)src->width, 3.0f, border_color);
		draw_solid_rect(solid, src->tex_white, 0.0f, (float)src->height - 3.0f, (float)src->width, 3.0f,
				border_color);
		draw_solid_rect(solid, src->tex_white, 0.0f, 0.0f, 3.0f, (float)src->height, border_color);
		draw_solid_rect(solid, src->tex_white, (float)src->width - 3.0f, 0.0f, 3.0f, (float)src->height,
				border_color);

		float margin_x = (float)src->width * 0.05f;
		float margin_y = (float)src->height * 0.05f;
		draw_solid_rect(solid, src->tex_white, margin_x, margin_y, (float)src->width - 2.0f * margin_x, 3.0f,
				border_color);
		draw_solid_rect(solid, src->tex_white, margin_x, (float)src->height - margin_y - 3.0f,
				(float)src->width - 2.0f * margin_x, 3.0f, border_color);
		draw_solid_rect(solid, src->tex_white, margin_x, margin_y, 3.0f, (float)src->height - 2.0f * margin_y,
				border_color);
		draw_solid_rect(solid, src->tex_white, (float)src->width - margin_x - 3.0f, margin_y, 3.0f,
				(float)src->height - 2.0f * margin_y, border_color);

		uint32_t white = 0xFFFFFFFF;
		float h_val = src->h_scan_time;
		if (h_val > 20.0f)
			h_val = 40.0f - h_val;
		float h_factor = (20.0f - h_val) / 20.0f;
		int h_y = (int)(h_factor * (float)src->height);
		draw_solid_rect(solid, src->tex_white, 0.0f, (float)h_y, (float)src->width, 2.0f, white);

		float v_val = src->v_scan_time;
		if (v_val > 10.0f)
			v_val = 20.0f - v_val;
		float v_factor = v_val / 10.0f;
		int v_x = (int)(v_factor * (float)src->width);
		draw_solid_rect(solid, src->tex_white, (float)v_x, 0.0f, 2.0f, (float)src->height, white);
	}

	// Translucent Center Rect
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);

	float min_dim = (float)(src->width < src->height ? src->width : src->height);

	float final_logo_w = min_dim * 0.5f; // Fallback
	if (src->logo_tex) {
		uint32_t tex_w = gs_texture_get_width(src->logo_tex);
		uint32_t tex_h = gs_texture_get_height(src->logo_tex);
		float scale = (min_dim * 0.5f) / (float)tex_h;
		final_logo_w = (float)tex_w * scale;
	}
	// Multiplicamos por 0.9 para quitar el margen transparente del PNG
	// y que los bordes rectos queden perfectamente ocultos detras del circulo blanco.
	int rect_width = (int)(final_logo_w * 0.9f);
	int rect_left = ((int)src->width - rect_width) / 2;
	int rect_top = 0;
	int rect_bottom = (int)src->height / 2;
	float rect_hue = fmodf(src->rotation_time / 60.0f, 1.0f) * 360.0f;
	uint32_t rect_color = hsv_to_rgb(rect_hue, 1.0f, 0.3f);
	rect_color = (rect_color & 0x00FFFFFF) | 0x80000000;
	draw_solid_rect(solid, src->tex_white, (float)rect_left, (float)rect_top, (float)rect_width, (float)rect_bottom,
			rect_color);

	gs_blend_state_pop();

	// --- DRAW LOGO & TEXT (Texture) ---
	gs_technique_begin(tech_def);

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);

	// Logo
	if (src->logo_tex) {
		float logo_h = min_dim * 0.5f;
		float logo_center_y = (float)src->height / 2.0f;
		uint32_t tex_w = gs_texture_get_width(src->logo_tex);
		uint32_t tex_h = gs_texture_get_height(src->logo_tex);
		float scale = logo_h / (float)tex_h;
		float final_w = (float)tex_w * scale;
		float final_h = (float)tex_h * scale;

		float lx = ((float)src->width - final_w) / 2.0f;
		float ly = logo_center_y - final_h / 2.0f;

		gs_matrix_push();
		gs_matrix_translate3f(lx + final_w / 2.0f, ly + final_h / 2.0f, 0.0f);

		float angle_rad = (src->rotation_time / 60.0f) * 360.0f * (3.14159265f / 180.0f);
		gs_matrix_rotaa4f(0.0f, 0.0f, 1.0f, angle_rad);
		gs_matrix_translate3f(-final_w / 2.0f, -final_h / 2.0f, 0.0f);

		gs_effect_set_texture(gs_effect_get_param_by_name(default_effect, "image"), src->logo_tex);
		gs_technique_begin_pass(tech_def, 0);
		gs_draw_sprite(src->logo_tex, 0, (uint32_t)final_w, (uint32_t)final_h);
		gs_technique_end_pass(tech_def);

		gs_matrix_pop();
	}

	// Second Logo (OBS_LOGO.png)
	if (src->obs_text_tex) {
		uint32_t t_w = gs_texture_get_width(src->obs_text_tex);
		uint32_t t_h = gs_texture_get_height(src->obs_text_tex);

		float target_w = min_dim * 0.5f; // Match spinning logo width
		float scale = target_w / (float)t_w;
		float target_h = (float)t_h * scale;

		float img_padding = min_dim * 0.01f;
		float margin_y = (float)src->height * 0.05f;

		float dx = ((float)src->width - target_w) / 2.0f;
		float dy = (float)src->height - margin_y - 3.0f - target_h -
			   img_padding; // Just above the orange border line

		gs_matrix_push();
		gs_matrix_translate3f(dx, dy, 0.0f);

		gs_effect_set_texture(gs_effect_get_param_by_name(default_effect, "image"), src->obs_text_tex);
		gs_technique_begin_pass(tech_def, 0);
		gs_draw_sprite(src->obs_text_tex, 0, (uint32_t)target_w, (uint32_t)target_h);
		gs_technique_end_pass(tech_def);

		gs_matrix_pop();
	}

	// Text
	if (src->layer_text) {
		float logo_h = min_dim * 0.5f;
		float logo_center_y = (float)src->height / 2.0f;
		float logo_top = logo_center_y - logo_h / 2.0f;

		int txt_padding = (int)(min_dim * 0.02f);
		if (txt_padding < 10)
			txt_padding = 10;

		float text_y = logo_top - (float)txt_padding - (float)src->text_tex_height;
		float text_x = ((float)src->width - (float)src->text_tex_width) / 2.0f;

		gs_matrix_push();
		gs_matrix_translate3f(text_x, text_y, 0.0f);

		gs_effect_set_texture(gs_effect_get_param_by_name(default_effect, "image"), src->layer_text);
		gs_technique_begin_pass(tech_def, 0);
		gs_draw_sprite(src->layer_text, 0, (uint32_t)src->text_tex_width, (uint32_t)src->text_tex_height);
		gs_technique_end_pass(tech_def);

		gs_matrix_pop();
	}

	gs_blend_state_pop();
	gs_technique_end(tech_def);
}

static void test_source_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "cell_size", CELL_SIZE);
	obs_data_set_default_int(settings, "bg_dark_color", 0x502822);
	obs_data_set_default_int(settings, "bg_light_color", 0x682f25);
	obs_data_set_default_string(settings, "custom_text", "OBS TEST CARD");
}

static obs_properties_t *test_source_get_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *props = obs_properties_create();

	obs_properties_add_int(props, "cell_size", obs_module_text("TestCard.GridSize"), 16, 256, 1);
	obs_properties_add_color(props, "bg_dark_color", obs_module_text("TestCard.BgDarkColor"));
	obs_properties_add_color(props, "bg_light_color", obs_module_text("TestCard.BgLightColor"));

	obs_properties_add_text(props, "custom_text", obs_module_text("TestCard.CustomText"), OBS_TEXT_DEFAULT);

	obs_properties_add_text(props, "version_info", "OBS Test Card V. 0.2.6 by Marulo", OBS_TEXT_INFO);

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

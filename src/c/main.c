#include <pebble.h>
#include "crackle.h"

// Spacing and flame size for each screen size. The flames are a window of the fire animation
// that spans the digits exactly: its top row meets the top of the first row of digits.
#if PBL_DISPLAY_WIDTH == 200
  #define COLUMN_GAP 16
  #define ROW_GAP 28
  #define FLAME_WIDTH 184
  #define FLAME_HEIGHT 198
#elif PBL_DISPLAY_WIDTH == 260
  #define COLUMN_GAP 15
  #define ROW_GAP 26
  #define FLAME_WIDTH 174
  #define FLAME_HEIGHT 188
#else
  #define COLUMN_GAP 8
  #define ROW_GAP 14
  #define FLAME_WIDTH PBL_IF_COLOR_ELSE(130, 128)
  #define FLAME_HEIGHT 106
#endif

#define FRAME_DELAY_MS 100

#define DIGIT_COUNT 4

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#if defined(PBL_BW)
  #define FLAME_BYTES_PER_ROW (FLAME_WIDTH / 8)
  #define FLAME_FRAME_BYTES (FLAME_BYTES_PER_ROW * FLAME_HEIGHT)
#endif

#if defined(PBL_RGB_BACKLIGHT)
  #define PERSIST_KEY_BACKLIGHT 1

  // How the backlight's flicker reads the flames: it checks every Nth pixel of a frame
  // for hot ones. The animation's frames have between about MIN and MAX of them.
  #define FLICKER_SAMPLE_STEP 8
  #define FLICKER_HOT_MIN 15
  #define FLICKER_HOT_MAX 110
  #define FLICKER_JITTER 70
#endif

#if defined(PBL_SPEAKER)
  #define PERSIST_KEY_SOUND 2
#endif

// Watches with neither feature have nothing to configure, so they skip AppMessage entirely
#if defined(PBL_RGB_BACKLIGHT) || defined(PBL_SPEAKER)
  #define HAS_SETTINGS
#endif

static const uint32_t DIGIT_RESOURCES[] = {
  RESOURCE_ID_DIGIT_0, RESOURCE_ID_DIGIT_1, RESOURCE_ID_DIGIT_2, RESOURCE_ID_DIGIT_3, RESOURCE_ID_DIGIT_4,
  RESOURCE_ID_DIGIT_5, RESOURCE_ID_DIGIT_6, RESOURCE_ID_DIGIT_7, RESOURCE_ID_DIGIT_8, RESOURCE_ID_DIGIT_9,
};

static Window *s_main_window;
static Layer *s_canvas_layer;
static AppTimer *s_frame_timer;

#if defined(PBL_RGB_BACKLIGHT)
  static bool s_backlight_enabled;
#endif

#if defined(PBL_SPEAKER)
  static bool s_sound_enabled;
#endif

// Hours are digits 0 and 1, minutes are digits 2 and 3
static GBitmap *s_digits[DIGIT_COUNT];
static char s_time[DIGIT_COUNT + 1];

#if defined(PBL_BW)
  // B&W frames are stored raw, so a frame is one flash read and needs no image decoding
  static ResHandle s_flames;
  static uint8_t *s_flame_frame;
  static int s_frame_index;
  static int s_frame_count;
#else
  static GBitmapSequence *s_sequence;
  static GBitmap *s_flame_bitmap;
#endif

static void flames_create(void) {
  #if defined(PBL_BW)
    s_flames = resource_get_handle(RESOURCE_ID_FLAMES);
    s_frame_count = resource_size(s_flames) / FLAME_FRAME_BYTES;
    s_flame_frame = malloc(FLAME_FRAME_BYTES);
    resource_load_byte_range(s_flames, 0, s_flame_frame, FLAME_FRAME_BYTES);
  #else
    s_sequence = gbitmap_sequence_create_with_resource(RESOURCE_ID_FLAMES);
    s_flame_bitmap = gbitmap_create_blank(gbitmap_sequence_get_bitmap_size(s_sequence), GBitmapFormat8Bit);
    uint32_t delay;
    gbitmap_sequence_update_bitmap_next_frame(s_sequence, s_flame_bitmap, &delay);
  #endif
}

static void flames_destroy(void) {
  #if defined(PBL_BW)
    free(s_flame_frame);
  #else
    gbitmap_destroy(s_flame_bitmap);
    gbitmap_sequence_destroy(s_sequence);
  #endif
}

// Advances to the next frame and returns how long to show it
static uint32_t flames_advance(void) {
  #if defined(PBL_BW)
    s_frame_index = (s_frame_index + 1) % s_frame_count;
    resource_load_byte_range(s_flames, s_frame_index * FLAME_FRAME_BYTES, s_flame_frame, FLAME_FRAME_BYTES);
    return FRAME_DELAY_MS;
  #else
    uint32_t delay = FRAME_DELAY_MS;
    if (!gbitmap_sequence_update_bitmap_next_frame(s_sequence, s_flame_bitmap, &delay)) {
      gbitmap_sequence_restart(s_sequence);
      gbitmap_sequence_update_bitmap_next_frame(s_sequence, s_flame_bitmap, &delay);
    }
    return delay;
  #endif
}

// Replaces the white digits in the framebuffer with flames. The rest of the screen stays black.
static void flames_fill_digits(GContext *ctx, int left, int top) {
  GBitmap *frame_buffer = graphics_capture_frame_buffer(ctx);
  if (!frame_buffer) {
    return;
  }

  int first_row = MAX(top, 0);
  int last_row = MIN(top + FLAME_HEIGHT, PBL_DISPLAY_HEIGHT) - 1;

  #if defined(PBL_BW)
    // White is 1 and black is 0, so ANDing the flames onto the digits masks a byte at a time
    int bytes_per_row = gbitmap_get_bytes_per_row(frame_buffer);
    uint8_t *screen_data = gbitmap_get_data(frame_buffer);
    for (int y = first_row; y <= last_row; y++) {
      uint8_t *screen_row = screen_data + (y * bytes_per_row) + (left / 8);
      const uint8_t *flame_row = s_flame_frame + ((y - top) * FLAME_BYTES_PER_ROW);
      for (int i = 0; i < FLAME_BYTES_PER_ROW; i++) {
        screen_row[i] &= flame_row[i];
      }
    }
  #else
    int flame_stride = gbitmap_get_bytes_per_row(s_flame_bitmap);
    const uint8_t *flame_data = gbitmap_get_data(s_flame_bitmap);
    for (int y = first_row; y <= last_row; y++) {
      // Row info gives valid x ranges on round screens too
      GBitmapDataRowInfo row = gbitmap_get_data_row_info(frame_buffer, y);
      const uint8_t *flame_row = flame_data + ((y - top) * flame_stride) - left;
      int last_x = MIN(left + FLAME_WIDTH - 1, row.max_x);
      for (int x = MAX(left, row.min_x); x <= last_x; x++) {
        if (row.data[x] == GColorWhiteARGB8) {
          row.data[x] = flame_row[x];
        }
      }
    }
  #endif

  graphics_release_frame_buffer(ctx, frame_buffer);
}

// Draws a pair of digits in white, centered by their actual width
static void draw_digit_row(GContext *ctx, GBitmap *first, GBitmap *second, int screen_width, int y) {
  GSize first_size = gbitmap_get_bounds(first).size;
  GSize second_size = gbitmap_get_bounds(second).size;
  int x = (screen_width - (first_size.w + COLUMN_GAP + second_size.w)) / 2;

  graphics_draw_bitmap_in_rect(ctx, first, GRect(x, y, first_size.w, first_size.h));
  graphics_draw_bitmap_in_rect(ctx, second, GRect(x + first_size.w + COLUMN_GAP, y, second_size.w, second_size.h));
}

static void canvas_update_proc(Layer *layer, GContext *ctx) {
  // Unobstructed so that the digits and flames recenter above a Timeline Quick View,
  // moving the rows closer together if that's what it takes to fit
  GRect bounds = layer_get_unobstructed_bounds(layer);
  int digit_height = gbitmap_get_bounds(s_digits[0]).size.h;
  int row_gap = MIN(ROW_GAP, bounds.size.h - (2 * digit_height));
  int top = (bounds.size.h - ((2 * digit_height) + row_gap)) / 2;

  draw_digit_row(ctx, s_digits[0], s_digits[1], bounds.size.w, top);
  draw_digit_row(ctx, s_digits[2], s_digits[3], bounds.size.w, top + digit_height + row_gap);

  // B&W flames are masked in whole bytes, so they need a byte-aligned left edge
  int left = (bounds.size.w - FLAME_WIDTH) / 2;
  flames_fill_digits(ctx, PBL_IF_BW_ELSE(left & ~7, left), top);
}

#if defined(PBL_RGB_BACKLIGHT)
// Tints the backlight to follow the current flame frame: the more of it is burning hot, the
// brighter and yellower the light, from deep ember to bright amber. The animation alone only
// swells and fades once per loop, so a random jitter on top makes it flicker like a real fire.
static void backlight_flicker(void) {
  if (!s_backlight_enabled) {
    return;
  }

  GSize size = gbitmap_get_bounds(s_flame_bitmap).size;
  int stride = gbitmap_get_bytes_per_row(s_flame_bitmap);
  const uint8_t *data = gbitmap_get_data(s_flame_bitmap);
  int hot = 0;
  for (int y = 0; y < size.h; y += FLICKER_SAMPLE_STEP) {
    for (int x = 0; x < size.w; x += FLICKER_SAMPLE_STEP) {
      GColor8 pixel = (GColor8) { .argb = data[(y * stride) + x] };
      hot += pixel.g >= 2;
    }
  }

  int heat = ((hot - FLICKER_HOT_MIN) * 255) / (FLICKER_HOT_MAX - FLICKER_HOT_MIN);
  heat += (rand() % (2 * FLICKER_JITTER + 1)) - FLICKER_JITTER;
  heat = MAX(0, MIN(heat, 255));

  uint32_t red = 150 + ((heat * 105) / 255);
  uint32_t green = 12 + ((heat * 140) / 255);
  uint32_t blue = (heat * 24) / 255;
  light_set_color_rgb888((red << 16) | (green << 8) | blue);
}

static void backlight_update(void) {
  if (s_backlight_enabled) {
    backlight_flicker();  // so the light doesn't first come on in the system color
    light_enable(true);
  } else {
    light_set_system_color();
  }
}
#endif

static void frame_timer_handler(void *context) {
  uint32_t delay = flames_advance();
  #if defined(PBL_RGB_BACKLIGHT)
    backlight_flicker();
  #endif
  layer_mark_dirty(s_canvas_layer);
  s_frame_timer = app_timer_register(delay, frame_timer_handler, NULL);
}

#if defined(PBL_SPEAKER)
static void sound_update(void) {
  if (s_sound_enabled) {
    crackle_start();
  } else {
    crackle_stop();
  }
}
#endif

static void animation_start(void) {
  if (!s_frame_timer) {
    s_frame_timer = app_timer_register(FRAME_DELAY_MS, frame_timer_handler, NULL);
  }
  #if defined(PBL_SPEAKER)
    sound_update();
  #endif
  #if defined(PBL_RGB_BACKLIGHT)
    backlight_update();
  #endif
}

static void animation_stop(void) {
  if (s_frame_timer) {
    app_timer_cancel(s_frame_timer);
    s_frame_timer = NULL;
  }
  crackle_stop();
}

// Pauses while a notification covers the face. Resuming also restores the digits, which
// are otherwise left unpainted after a notification is dismissed.
static void app_focus_changed(bool focused) {
  if (focused) {
    animation_start();
    layer_mark_dirty(s_canvas_layer);
  } else {
    animation_stop();
  }
}

#if defined(HAS_SETTINGS)
static void settings_load(void) {
  #if defined(PBL_RGB_BACKLIGHT)
    s_backlight_enabled = persist_exists(PERSIST_KEY_BACKLIGHT) ? persist_read_bool(PERSIST_KEY_BACKLIGHT) : true;
  #endif
  #if defined(PBL_SPEAKER)
    s_sound_enabled = persist_exists(PERSIST_KEY_SOUND) ? persist_read_bool(PERSIST_KEY_SOUND) : true;
  #endif
}

// New settings apply right away, unless a notification is covering the face:
// then they apply when the animation resumes
static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  #if defined(PBL_RGB_BACKLIGHT)
    Tuple *backlight_tuple = dict_find(iter, MESSAGE_KEY_BACKLIGHT);
    if (backlight_tuple) {
      bool enabled = backlight_tuple->value->int32 != 0;
      if (s_backlight_enabled && !enabled) {
        light_enable(false);  // hand the backlight back to the system
      }
      s_backlight_enabled = enabled;
      persist_write_bool(PERSIST_KEY_BACKLIGHT, enabled);
      if (s_frame_timer) {
        backlight_update();
      }
    }
  #endif
  #if defined(PBL_SPEAKER)
    Tuple *sound_tuple = dict_find(iter, MESSAGE_KEY_SOUND);
    if (sound_tuple) {
      s_sound_enabled = sound_tuple->value->int32 != 0;
      persist_write_bool(PERSIST_KEY_SOUND, s_sound_enabled);
      if (s_frame_timer) {
        sound_update();
      }
    }
  #endif
}
#endif

// Loads only the digits that changed
static void update_time(struct tm *tick_time) {
  char time_text[DIGIT_COUNT + 1];
  strftime(time_text, sizeof(time_text), clock_is_24h_style() ? "%H%M" : "%I%M", tick_time);

  for (int i = 0; i < DIGIT_COUNT; i++) {
    if (time_text[i] != s_time[i]) {
      if (s_digits[i]) {
        gbitmap_destroy(s_digits[i]);
      }
      s_digits[i] = gbitmap_create_with_resource(DIGIT_RESOURCES[time_text[i] - '0']);
      s_time[i] = time_text[i];
    }
  }
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time(tick_time);
  layer_mark_dirty(s_canvas_layer);
}

static void main_window_load(Window *window) {
  Layer *root_layer = window_get_root_layer(window);

  flames_create();

  time_t now = time(NULL);
  update_time(localtime(&now));
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);

  s_canvas_layer = layer_create(layer_get_bounds(root_layer));
  layer_set_update_proc(s_canvas_layer, canvas_update_proc);
  layer_add_child(root_layer, s_canvas_layer);

  animation_start();
}

static void main_window_unload(Window *window) {
  animation_stop();
  tick_timer_service_unsubscribe();
  layer_destroy(s_canvas_layer);
  flames_destroy();
  for (int i = 0; i < DIGIT_COUNT; i++) {
    gbitmap_destroy(s_digits[i]);
  }
}

static void init(void) {
  #if defined(HAS_SETTINGS)
    settings_load();
    app_message_register_inbox_received(inbox_received_handler);
    app_message_open(64, 0);
  #endif

  app_focus_service_subscribe_handlers((AppFocusHandlers) {
    .did_focus = app_focus_changed,
  });

  s_main_window = window_create();
  window_set_background_color(s_main_window, GColorBlack);
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload,
  });
  window_stack_push(s_main_window, false);
}

static void deinit(void) {
  app_focus_service_unsubscribe();
  window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}

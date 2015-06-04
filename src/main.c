#include <pebble.h>
#include "effect_layer.h"  

static Window *s_main_window;

static GBitmap *s_bitmap = NULL;
static GBitmapSequence *s_sequence = NULL;

EffectLayer *effect_layer;
static EffectMask mask;

static void timer_handler(void *context) {
  uint32_t next_delay;

  // Advance to the next APNG frame
  if(gbitmap_sequence_update_bitmap_next_frame(s_sequence, s_bitmap, &next_delay)) {
    
    mask.bitmap_background = s_bitmap;
    mask.text = "4488";
    layer_mark_dirty(effect_layer_get_layer(effect_layer));

  } else {
    // Start again
    gbitmap_sequence_restart(s_sequence);
  }
  
  app_timer_register(next_delay, timer_handler, NULL);
}

static void main_window_load(Window *window) {
  
  //create effect layer for transparent mask
  mask.background_color = GColorBlack;
  mask.mask_color = GColorWhite;
  mask.text_align = GTextAlignmentCenter;
  mask.font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FUTURA_60));
  
  effect_layer = effect_layer_create(GRect(10,15,130,125));
  effect_layer_add_effect(effect_layer, effect_mask, &mask);
  layer_add_child(window_get_root_layer(window), effect_layer_get_layer(effect_layer));
  
  
  // begin APNG animation sequence
  s_sequence = gbitmap_sequence_create_with_resource(RESOURCE_ID_ANIMATION);
  s_bitmap = gbitmap_create_blank(gbitmap_sequence_get_bitmap_size(s_sequence), GBitmapFormat8Bit);
  app_timer_register(1, timer_handler, NULL);
}

static void main_window_unload(Window *window) {
  
    gbitmap_sequence_destroy(s_sequence);
    gbitmap_destroy(s_bitmap);
  
    effect_layer_destroy(effect_layer);
}

static void init() {
  s_main_window = window_create();
  window_set_background_color(s_main_window, GColorBlack);
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload,
  });
  window_stack_push(s_main_window, true);
}

static void deinit() {
  window_destroy(s_main_window);
}

int main() {
  init();
  app_event_loop();
  deinit();
}
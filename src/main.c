#include <pebble.h>
#include "effect_layer.h"  

static Window *s_main_window;

static GBitmapSequence *s_sequence = NULL;

EffectLayer *effect_layer;
static EffectMask mask;

char hhmmm[] = "1234";

void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  
   if (!clock_is_24h_style()) {
    
        if( tick_time->tm_hour > 11 ) {   // YG Jun-25-2014: 0..11 - am 12..23 - pm
            if( tick_time->tm_hour > 12 ) tick_time->tm_hour -= 12;
        } else {
            if( tick_time->tm_hour == 0 ) tick_time->tm_hour = 12;
        }        
    }
  
    strftime(hhmmm, sizeof(hhmmm), "%H%M", tick_time);
    mask.text = hhmmm;
  
}


static void timer_handler(void *context) {
  uint32_t next_delay;

  // Advance to the next APNG frame
  if(gbitmap_sequence_update_bitmap_next_frame(s_sequence,  mask.bitmap_background, &next_delay)) {
    
    layer_mark_dirty(window_get_root_layer(s_main_window));
    app_timer_register(next_delay, timer_handler, NULL);

  } else {
    // Start again
    gbitmap_sequence_restart(s_sequence);
    
    app_timer_register(100, timer_handler, NULL);
  }
  
}

static void main_window_load(Window *window) {
  
  
  //create effect layer for transparent mask
  mask.background_color = GColorBlack;
  mask.mask_color = GColorWhite;
  mask.text_align = GTextAlignmentCenter;
  mask.font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FUTURA_59));
  
  effect_layer = effect_layer_create(GRect(10,15,130,125));
  effect_layer_add_effect(effect_layer, effect_mask, &mask);
  layer_add_child(window_get_root_layer(window), effect_layer_get_layer(effect_layer));
  
  
  // begin APNG animation sequence
  s_sequence = gbitmap_sequence_create_with_resource(RESOURCE_ID_ANIMATION);
  mask.bitmap_background  = gbitmap_create_blank(gbitmap_sequence_get_bitmap_size(s_sequence), GBitmapFormat8Bit);
  app_timer_register(100, timer_handler, NULL);
}

static void main_window_unload(Window *window) {
  
    gbitmap_sequence_destroy(s_sequence);
    gbitmap_destroy(mask.bitmap_background );
  
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
  
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  
   //Get a time structure so that the face doesn't start blank
  time_t temp = time(NULL);
  struct tm *t = localtime(&temp);
 
  //Manually call the tick handler when the window is loading
  tick_handler(t, MINUTE_UNIT);

}

static void deinit() {
  window_destroy(s_main_window);
  tick_timer_service_unsubscribe();
}

int main() {
  init();
  app_event_loop();
  deinit();
}
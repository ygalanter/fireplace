#include <pebble.h>
#include "crackle.h"

#if defined(PBL_SPEAKER)

#define POP_SAMPLES 320       // 40ms at 8kHz
#define POP_BASE_NOTE 60
#define VOLUME 6              // speaker volume, 0-100: raise or lower to taste

static int8_t s_pop[POP_SAMPLES];
static AppTimer *s_timer;

// One sharp burst of noise with a fast exponential decay
static void generate_pop(void) {
  int32_t envelope = 127 << 8;
  for (int i = 0; i < POP_SAMPLES; i++) {
    int noise = (rand() % 511) - 255;
    s_pop[i] = (int8_t)((noise * (envelope >> 8)) / 256);
    envelope -= envelope / 24;
  }
}

static void play_pop(void) {
  // Respects both the system-wide speaker mute and Quiet Time; skips if something else is playing
  if (speaker_is_muted() || quiet_time_is_active() || speaker_get_status() != SpeakerStatusIdle) {
    return;
  }
  static const SpeakerSample sample = {
    .data = s_pop,
    .num_bytes = POP_SAMPLES,
    .format = SpeakerPcmFormat_8kHz_8bit,
    .base_midi_note = POP_BASE_NOTE,
    .loop = false,
  };
  static SpeakerNote note;  // must outlive the call: playback continues after it returns
  note = (SpeakerNote) {
    .midi_note = POP_BASE_NOTE - 12 + (rand() % 30),  // varied pitch: from dull thuds to sharp snaps
    .duration_ms = 40,
    .velocity = 30 + (rand() % 98),                   // and varied loudness
  };
  static const SpeakerTrack track = { .notes = &note, .num_notes = 1, .sample = &sample };
  speaker_play_tracks(&track, 1, VOLUME);
}

static void timer_handler(void *context) {
  play_pop();
  // Crackles come in small clusters separated by longer pauses
  uint32_t delay = (rand() % 3 == 0) ? 60 + (rand() % 120) : 400 + (rand() % 2200);
  s_timer = app_timer_register(delay, timer_handler, NULL);
}

void crackle_start(void) {
  if (s_timer) {
    return;
  }
  generate_pop();
  s_timer = app_timer_register(500, timer_handler, NULL);
}

void crackle_stop(void) {
  if (s_timer) {
    app_timer_cancel(s_timer);
    s_timer = NULL;
    speaker_stop();
  }
}

#else

void crackle_start(void) {}
void crackle_stop(void) {}

#endif

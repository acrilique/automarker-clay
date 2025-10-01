#ifndef AUDIO_STATE_H
#define AUDIO_STATE_H

#include <stdbool.h>
#include <SDL3/SDL.h>
#include <SDL3_sound/SDL_sound.h>
#include "../libs/BTT/BTT.h"

// Status enum (moved from main.c)
typedef enum {
    STATUS_IDLE,
    STATUS_DECODE,
    STATUS_BEAT_ANALYSIS,
    STATUS_COMPLETED
} AudioStatus;

// Playback state (NEW - preparing for sound playback)
typedef enum {
    PLAYBACK_STOPPED,
    PLAYBACK_PLAYING,
    PLAYBACK_PAUSED
} PlaybackState;

// Main audio state structure (consolidates AudioTrack + adds playback)
typedef struct {
    // File and decoding
    const char *file_path;
    Sound_Sample *sample;
    
    // Beat detection
    unsigned int *beat_positions;
    int beat_count;
    int beats_buffer_size;
    
    // Processing state
    AudioStatus status;
    SDL_Thread *processing_thread;
    SDL_Mutex *data_mutex;
    SDL_AtomicInt request_stop;
    float processing_progress;
    
    // Playback state (NEW - for future sound playback)
    PlaybackState playback_state;
    unsigned int playback_position;  // Current sample position during playback
    bool follow_playback;    // Auto-scroll during playback
    
} AudioState;

// Function declarations
AudioState* audio_state_create(void);
void audio_state_destroy(AudioState *state);
void audio_state_load_file(AudioState *state, const char *file_path);
void audio_state_request_stop(AudioState *state);
void audio_state_cleanup_processing(AudioState *state);

// Beat callback (moved from main.c)
void audio_beat_callback(void *user_data, unsigned long long sample_time);

#endif // AUDIO_STATE_H

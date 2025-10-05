#ifndef AUDIO_STATE_H
#define AUDIO_STATE_H

#include <stdbool.h>
#include <SDL3/SDL.h>
#include <SDL3_sound/SDL_sound.h>
#include "audio_tools/beat_track.h"
#include "audio_tools/audio_io.h"

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
    
    // Playback state (NEW - for real-time sound playback)
    PlaybackState playback_state;
    SDL_AtomicInt playback_position;  // Current sample position during playback (atomic for thread safety)
    bool follow_playback;    // Auto-scroll during playback
    
    // Audio streaming
    SDL_AudioStream *audio_stream;
    SDL_AudioDeviceID audio_device;
    float *playback_buffer;  // Copy of audio data for playback
    size_t playback_buffer_size;

    // Selection
    unsigned int selection_start;
    unsigned int selection_end;
    
} AudioState;

// Function declarations
AudioState* audio_state_create(void);
void audio_state_destroy(AudioState *state);
void audio_state_load_file(AudioState *state, const char *file_path);
void audio_state_request_stop(AudioState *state);
void audio_state_cleanup_processing(AudioState *state);

// Playback functions
bool audio_state_start_playback(AudioState *state);
void audio_state_stop_playback(AudioState *state);
void audio_state_pause_playback(AudioState *state);
void audio_state_resume_playback(AudioState *state);
void audio_state_set_playback_position(AudioState *state, unsigned int position);
unsigned int audio_state_get_playback_position(AudioState *state);

// Audio conversion functions
audio_data* sdl_sound_to_cara_audio(Sound_Sample *sample);
void free_cara_audio(audio_data *audio);

#endif // AUDIO_STATE_H

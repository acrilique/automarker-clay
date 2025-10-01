#include "audio_state.h"
#include "SDL3/SDL_atomic.h"
#include <stdio.h>
#include <stdlib.h>

// Desired audio format (moved from main.c)
static Sound_AudioInfo desired = {
    .format = SDL_AUDIO_F32,
    .channels = 1,
    .rate = 44100,
};

// Beat callback (moved from main.c, renamed)
void audio_beat_callback(void *user_data, unsigned long long sample_time) {
    AudioState *state = (AudioState *)user_data;
    
    SDL_LockMutex(state->data_mutex);
    if (state->beat_count < state->beats_buffer_size) {
        state->beat_positions[state->beat_count++] = sample_time;
    } else {
        state->beats_buffer_size *= 2;
        state->beat_positions = realloc(state->beat_positions,
                                       sizeof(unsigned int) * state->beats_buffer_size);
        state->beat_positions[state->beat_count++] = sample_time;
    }
    SDL_UnlockMutex(state->data_mutex);
}

// Process audio file (moved from main.c, made static)
static void process_audio_file(AudioState *state) {
  // Clean up previous processing if any - protected by mutex
  SDL_LockMutex(state->data_mutex);
  if (state->sample) {
    Sound_FreeSample(state->sample);
    state->sample = NULL;
  }
  if (state->beat_positions) {
    free(state->beat_positions);
    state->beat_positions = NULL;
  }
  state->beats_buffer_size = 1024;
  state->beat_positions =
      malloc(sizeof(unsigned int) * state->beats_buffer_size);
  state->beat_count = 0;
  state->status = STATUS_DECODE;
  SDL_UnlockMutex(state->data_mutex);

  // Initial file setup
  state->sample =
      Sound_NewSampleFromFile(state->file_path, &desired, 16384);
  if (!state->sample) {
    printf("Error: Could not open audio file: %s\n", state->file_path);
    return;
  }

  // File decoding
  Uint32 decoded_bytes = Sound_DecodeAll(state->sample);
  if (decoded_bytes == 0) {
    printf("Error: Could not decode audio file: %s\n",
           state->file_path);
    Sound_FreeSample(state->sample);
    state->sample = NULL;
    return;
  }

  SDL_LockMutex(state->data_mutex);
  state->status = STATUS_BEAT_ANALYSIS;
  SDL_UnlockMutex(state->data_mutex);

  // TODO check this: At this point sample.flag shall be EOF

  Uint32 total_samples = state->sample->buffer_size / sizeof(float);
  Uint32 processed_samples = 0;

  const Uint32 buffer_size = 4096;
  float *current_buffer = state->sample->buffer;

  BTT *btt = btt_new_default();
  if (!btt) {
    printf("Error: Could not create beat tracking object\n");
    Sound_FreeSample(state->sample);
    state->sample = NULL;
    return;
  }

  btt_set_beat_tracking_callback(btt, &audio_beat_callback, state);

  while (processed_samples < total_samples) {
    if (SDL_GetAtomicInt(&state->request_stop)) {
      btt_destroy(btt);
      return;
    }
    Uint32 samples_to_process =
        (processed_samples + buffer_size <= total_samples)
            ? buffer_size
            : (total_samples - processed_samples);

    btt_process(btt, current_buffer, samples_to_process);
    current_buffer += buffer_size;
    processed_samples += buffer_size;
  }

  SDL_LockMutex(state->data_mutex);
  state->status = STATUS_COMPLETED;
  SDL_UnlockMutex(state->data_mutex);

  btt_destroy(btt);
}

// Processing thread (moved from main.c, made static)
static int audio_processing_thread(void *data) {
    AudioState *state = (AudioState *)data;
    
    process_audio_file(state);
    
    SDL_LockMutex(state->data_mutex);
    if (state->status != STATUS_COMPLETED) {
        state->status = STATUS_IDLE;
    }
    SDL_UnlockMutex(state->data_mutex);
    
    return 0;
}

// Create new audio state
AudioState* audio_state_create(void) {
    AudioState *state = SDL_calloc(1, sizeof(AudioState));
    if (!state) return NULL;
    
    state->data_mutex = SDL_CreateMutex();
    if (!state->data_mutex) {
        SDL_free(state);
        return NULL;
    }
    
    SDL_SetAtomicInt(&state->request_stop, 0);
    state->status = STATUS_IDLE;
    state->playback_state = PLAYBACK_STOPPED;
    state->playback_position = 0;
    state->follow_playback = false;
    
    return state;
}

// Load and process audio file
void audio_state_load_file(AudioState *state, const char *file_path) {
    if (!state || !file_path) return;
    
    // Wait for any existing processing to finish
    SDL_LockMutex(state->data_mutex);
    bool is_processing = (state->status == STATUS_DECODE || 
                         state->status == STATUS_BEAT_ANALYSIS);
    SDL_UnlockMutex(state->data_mutex);
    
    if (is_processing) return;
    
    if (state->processing_thread) {
        SDL_WaitThread(state->processing_thread, NULL);
        state->processing_thread = NULL;
    }
    
    state->file_path = file_path;
    
    // Start processing thread
    state->processing_thread = SDL_CreateThread(
        audio_processing_thread, "AudioProcessing", state);
    
    if (!state->processing_thread) {
        printf("Error: Could not create audio processing thread: %s\n",
               SDL_GetError());
    }
}

// Request to stop ongoing processing gracefully
void audio_state_request_stop(AudioState *state) {
    if (!state) return;
    
    // Set the stop flag
    SDL_SetAtomicInt(&state->request_stop, 1);
    
    // Wait for the processing thread to finish
    if (state->processing_thread) {
        SDL_WaitThread(state->processing_thread, NULL);
        state->processing_thread = NULL;
    }
    
    // Reset the stop flag for future operations
    SDL_SetAtomicInt(&state->request_stop, 0);
    
    // Clean up resources
    SDL_LockMutex(state->data_mutex);
    if (state->sample) {
        Sound_FreeSample(state->sample);
        state->sample = NULL;
    }
    if (state->beat_positions) {
        free(state->beat_positions);
        state->beat_positions = NULL;
        state->beat_count = 0;
        state->beats_buffer_size = 0;
    }
    state->status = STATUS_IDLE;
    SDL_UnlockMutex(state->data_mutex);
}

// Clean up processing resources
void audio_state_cleanup_processing(AudioState *state) {
    if (!state) return;
    
    if (state->processing_thread) {
        SDL_WaitThread(state->processing_thread, NULL);
        state->processing_thread = NULL;
    }
}

// Destroy audio state
void audio_state_destroy(AudioState *state) {
    if (!state) return;
    
    if (state->processing_thread) {
        SDL_SetAtomicInt(&state->request_stop, 1);
        SDL_WaitThread(state->processing_thread, NULL);
        state->processing_thread = NULL;
    }
    
    if (state->data_mutex) {
        SDL_DestroyMutex(state->data_mutex);
    }
    
    if (state->sample) {
        Sound_FreeSample(state->sample);
    }
    
    if (state->beat_positions) {
        free(state->beat_positions);
    }
    
    SDL_free(state);
}

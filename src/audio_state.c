#include "audio_state.h"
#include "SDL3/SDL_atomic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Desired audio format (moved from main.c)
static Sound_AudioInfo desired = {
    .format = SDL_AUDIO_F32,
    .channels = 2,
    .rate = 44100,
};

// Convert SDL_Sound sample to CARA audio_data structure
audio_data* sdl_sound_to_cara_audio(Sound_Sample *sample) {
    if (!sample || !sample->buffer || sample->buffer_size == 0) {
        return NULL;
    }
    
    audio_data *audio = malloc(sizeof(audio_data));
    if (!audio) return NULL;
    
    // Calculate number of samples
    size_t sample_size = sizeof(float); // CARA expects float samples
    size_t total_samples = sample->buffer_size / sample_size;
    
    // Allocate and copy sample data
    audio->samples = malloc(sample->buffer_size);
    if (!audio->samples) {
        free(audio);
        return NULL;
    }
    
    memcpy(audio->samples, sample->buffer, sample->buffer_size);
    
    // Set audio properties
    audio->num_samples = total_samples;
    audio->channels = sample->actual.channels;
    audio->sample_rate = sample->actual.rate;
    audio->file_size = sample->buffer_size;
    
    return audio;
}

// Free CARA audio_data structure
void free_cara_audio(audio_data *audio) {
    if (!audio) return;
    
    if (audio->samples) {
        free(audio->samples);
    }
    free(audio);
}

// Audio callback function for SDL3 streaming
static void audio_callback(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount) {
    AudioState *state = (AudioState *)userdata;
    
    const int total_bytes_needed = total_amount;

    if (!state || !state->playback_buffer || state->playback_state != PLAYBACK_PLAYING) {
        Uint8* silence = SDL_calloc(1, total_bytes_needed);
        if (silence) {
            SDL_PutAudioStreamData(stream, silence, total_bytes_needed);
            SDL_free(silence);
        }
        return;
    }
    
    int current_pos_samples = SDL_GetAtomicInt(&state->playback_position);
    Uint8* temp_buffer = malloc(total_bytes_needed);
    int bytes_provided = 0;

    while (bytes_provided < total_bytes_needed) {
        if (current_pos_samples < state->selection_start || current_pos_samples >= state->selection_end) {
            current_pos_samples = state->selection_start;
        }

        int samples_left_in_loop = state->selection_end - current_pos_samples;
        int bytes_left_in_loop = samples_left_in_loop * sizeof(float);

        int bytes_to_copy_this_iteration = total_bytes_needed - bytes_provided;

        if (bytes_to_copy_this_iteration > bytes_left_in_loop) {
            bytes_to_copy_this_iteration = bytes_left_in_loop;
        }

        if (bytes_to_copy_this_iteration > 0) {
            memcpy(temp_buffer + bytes_provided, &state->playback_buffer[current_pos_samples], bytes_to_copy_this_iteration);
            bytes_provided += bytes_to_copy_this_iteration;
            current_pos_samples += bytes_to_copy_this_iteration / sizeof(float);
        } else {
            current_pos_samples = state->selection_start;
        }
    }

    SDL_PutAudioStreamData(stream, temp_buffer, total_bytes_needed);
    SDL_SetAtomicInt(&state->playback_position, current_pos_samples);
    free(temp_buffer);
}

// Process audio file using CARA beat tracking
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

  // Check for stop request after decoding
  if (SDL_GetAtomicInt(&state->request_stop)) {
    return;
  }

  SDL_LockMutex(state->data_mutex);
  state->status = STATUS_BEAT_ANALYSIS;
  SDL_UnlockMutex(state->data_mutex);

  // Convert SDL_Sound data to CARA format
  audio_data *cara_audio = sdl_sound_to_cara_audio(state->sample);
  if (!cara_audio) {
    printf("Error: Could not convert audio data for CARA processing\n");
    Sound_FreeSample(state->sample);
    state->sample = NULL;
    return;
  }
  

  // Set default selection to the entire track
  state->selection_start = 0;
  state->selection_end = state->sample->buffer_size / sizeof(float);

  // CARA beat tracking parameters
  const size_t window_size = 2048;
  const size_t hop_length = 512;
  const size_t n_mels = 128;
  
  beat_params_t params = get_default_beat_params();
  
  // Perform beat tracking using CARA
  beat_result_t beat_result = beat_track_audio(
    cara_audio, 
    window_size, 
    hop_length, 
    n_mels,
    &params, 
    BEAT_UNITS_SAMPLES  // Get results in sample positions
  );

  // Check for stop request after beat tracking
  if (SDL_GetAtomicInt(&state->request_stop)) {
    free_beat_result(&beat_result);
    free_cara_audio(cara_audio);
    return;
  }

  // Convert CARA results to our format
  SDL_LockMutex(state->data_mutex);
  if (beat_result.num_beats > 0 && beat_result.beat_times) {
    // Ensure we have enough space
    if (beat_result.num_beats > state->beats_buffer_size) {
      state->beats_buffer_size = beat_result.num_beats * 2;
      state->beat_positions = realloc(state->beat_positions,
                                     sizeof(unsigned int) * state->beats_buffer_size);
    }

    // Copy beat positions - CARA returns sample positions when using BEAT_UNITS_SAMPLES
    state->beat_count = beat_result.num_beats;
    for (size_t i = 0; i < beat_result.num_beats; i++) {
      // CARA returns sample positions in terms of frames, but we need to convert to 
      // interleaved sample positions for stereo audio
      unsigned int frame_position = (unsigned int)beat_result.beat_times[i];

      // Apply offset to account for STFT centering behavior
      // CARA currently implements center=False behavior, but we need center=True alignment
      // Add half the window size to center the frame positions correctly
      const int center_offset = window_size / 2;
      frame_position += center_offset;

      // For stereo audio, multiply by channel count to get the correct sample position
      state->beat_positions[i] = frame_position * state->sample->actual.channels;
    }
    
    printf("CARA beat tracking completed: %d beats found, tempo: %.2f BPM\n", 
           state->beat_count, beat_result.tempo_bpm);
    printf("Total audio samples: %d\n", (int)(state->sample->buffer_size / sizeof(float)));
    printf("Audio duration: %.2f seconds\n", (float)(state->sample->buffer_size / sizeof(float)) / state->sample->actual.rate);
    printf("First few beat positions: ");
    for (int i = 0; i < (state->beat_count < 5 ? state->beat_count : 5); i++) {
      printf("%u ", state->beat_positions[i]);
    }
    printf("\n");
    printf("Last few beat positions: ");
    int start_idx = state->beat_count > 5 ? state->beat_count - 5 : 0;
    for (int i = start_idx; i < state->beat_count; i++) {
      printf("%u ", state->beat_positions[i]);
    }
    printf("\n");
    printf("Beat positions as time (seconds): ");
    for (int i = 0; i < (state->beat_count < 5 ? state->beat_count : 5); i++) {
      printf("%.2f ", (float)state->beat_positions[i] / state->sample->actual.rate);
    }
    printf("\n");
  } else {
    printf("CARA beat tracking found no beats\n");
    state->beat_count = 0;
  }

  state->status = STATUS_COMPLETED;

  // Create playback buffer
  if (!state->playback_buffer) {
    state->playback_buffer_size = state->sample->buffer_size / sizeof(float);
    state->playback_buffer = malloc(state->sample->buffer_size);
    if (state->playback_buffer) {
      memcpy(state->playback_buffer, state->sample->buffer,
             state->sample->buffer_size);
    }
  }

  // Set up audio stream
  if (!state->audio_stream) {
    SDL_AudioSpec spec = {.format = SDL_AUDIO_F32,
                          .channels = state->sample->actual.channels,
                          .freq = state->sample->actual.rate};

    state->audio_device =
        SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
    if (state->audio_device) {
      state->audio_stream = SDL_CreateAudioStream(&spec, &spec);
      if (state->audio_stream) {
        SDL_SetAudioStreamGetCallback(state->audio_stream, audio_callback,
                                      state);
        SDL_BindAudioStream(state->audio_device, state->audio_stream);
        SDL_PauseAudioDevice(state->audio_device); // Start paused
      } else {
        SDL_CloseAudioDevice(state->audio_device);
        state->audio_device = 0;
      }
    }
  }

  SDL_UnlockMutex(state->data_mutex);

  // Cleanup CARA resources
  free_beat_result(&beat_result);
  free_cara_audio(cara_audio);
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
    SDL_SetAtomicInt(&state->playback_position, 0);
    state->status = STATUS_IDLE;
    state->playback_state = PLAYBACK_STOPPED;
    state->follow_playback = false;
    
    // Initialize audio streaming components
    state->audio_stream = NULL;
    state->audio_device = 0;
    state->playback_buffer = NULL;
    state->playback_buffer_size = 0;
    
    return state;
}

// Load and process audio file
void audio_state_load_file(AudioState *state, const char *file_path) {
    if (!state || !file_path) return;

    audio_state_stop_playback(state);
    if (state->audio_stream) {
        SDL_DestroyAudioStream(state->audio_stream);
        state->audio_stream = NULL;
    }
    if (state->audio_device) {
        SDL_CloseAudioDevice(state->audio_device);
        state->audio_device = 0;
    }
    
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
    
    // Stop any ongoing playback
    audio_state_stop_playback(state);

    if (state->audio_stream) {
        SDL_DestroyAudioStream(state->audio_stream);
        state->audio_stream = NULL;
    }
    if (state->audio_device) {
        SDL_CloseAudioDevice(state->audio_device);
        state->audio_device = 0;
    }
    
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
    
    if (state->playback_buffer) {
        free(state->playback_buffer);
    }
    
    SDL_free(state);
}

// Start audio playback
bool audio_state_start_playback(AudioState *state) {
    if (!state || !state->sample || state->playback_state == PLAYBACK_PLAYING) {
        return false;
    }

    if (state->playback_state == PLAYBACK_STOPPED && audio_state_get_playback_position(state) == 0) {
        SDL_SetAtomicInt(&state->playback_position, state->selection_start);
    }
    
    if (!state->audio_stream) {
        return false;
    }
    
    state->playback_state = PLAYBACK_PLAYING;
    SDL_ResumeAudioDevice(state->audio_device);
    
    printf("Audio playback started\n");
    return true;
}

// Stop audio playback
void audio_state_stop_playback(AudioState *state) {
    if (!state) return;
    
    if (state->audio_device) {
        SDL_PauseAudioDevice(state->audio_device);
    }
    
    state->playback_state = PLAYBACK_STOPPED;
    
    printf("Audio playback stopped\n");
}

// Pause audio playback
void audio_state_pause_playback(AudioState *state) {
    if (!state || state->playback_state != PLAYBACK_PLAYING) return;
    
    if (state->audio_device) {
        SDL_PauseAudioDevice(state->audio_device);
    }
    
    state->playback_state = PLAYBACK_PAUSED;
    printf("Audio playback paused\n");
}

// Resume audio playback
void audio_state_resume_playback(AudioState *state) {
    if (!state || state->playback_state != PLAYBACK_PAUSED) return;
    
    if (state->audio_device) {
        SDL_ResumeAudioDevice(state->audio_device);
    }
    
    state->playback_state = PLAYBACK_PLAYING;
    printf("Audio playback resumed\n");
}

// Set playback position
void audio_state_set_playback_position(AudioState *state, unsigned int position) {
    if (!state) return;
    
    // Clamp position to valid range
    if (position > state->playback_buffer_size) {
        position = state->playback_buffer_size;
    }
    
    SDL_SetAtomicInt(&state->playback_position, position);

    // If we are playing, we need to clear the stream to seek correctly
    if (state->audio_stream) {
        SDL_ClearAudioStream(state->audio_stream);
    }
}

// Get current playback position
unsigned int audio_state_get_playback_position(AudioState *state) {
    if (!state) return 0;
    return SDL_GetAtomicInt(&state->playback_position);
}

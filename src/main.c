#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <stdio.h>

#include "../libs/tinyfiledialogs.h"

#define CLAY_IMPLEMENTATION
#include "../libs/clay.h"

#include "./clay_renderer_SDL3.c"

#include "aubio.h"

// Font IDs
static const Uint32 FONT_REGULAR = 0;

static const Uint32 CORNER_RADIUS = 8;

// General colors
static const Clay_Color COLOR_BG_DARK = (Clay_Color){43, 41, 51, 255};
static const Clay_Color COLOR_BG_LIGHT = (Clay_Color){90, 90, 90, 255};
static const Clay_Color COLOR_WHITE = (Clay_Color){255, 244, 244, 255};
static const Clay_Color COLOR_ACCENT = (Clay_Color){140, 140, 140, 255};

// Button colors
static const Clay_Color COLOR_BUTTON_BG = (Clay_Color){140, 140, 140, 255};
static const Clay_Color COLOR_BUTTON_BG_HOVER =
    (Clay_Color){160, 160, 160, 255};
static const Clay_Color COLOR_BUTTON_SVG = (Clay_Color){255, 255, 255, 255};

// Waveform colors
static const Clay_Color COLOR_WAVEFORM_BG = (Clay_Color){60, 60, 60, 255};
static const Clay_Color COLOR_WAVEFORM_LINE = (Clay_Color){255, 255, 255, 255};

typedef enum _status {
  STATUS_IDLE,     // Never processed or failed
  STATUS_ACTIVE,   // Currently processing
  STATUS_COMPLETED // Successfully completed
} Status;

typedef struct audio_track { // Ordered by size
  const char *selectedFile;
  float *beat_positions;
  float *all_samples;       // All audio samples for waveform display
  fvec_t *in_fvec;          // Audio samples (processing buffer)
  fvec_t *out_fvec;         // Beat detection result
  SDL_Thread *processing_thread;
  SDL_Mutex *data_mutex;
  int beat_count;
  int total_samples;        // Total number of samples in all_samples
  float processing_progress;
  Status status; // Replaces is_processing and processing_complete
} AudioTrack;

// Waveform display state
typedef struct {
  float zoom;         // Zoom level (1.0 = normal)
  float scroll;       // Scroll position (0.0 = start, 1.0 = end)
} WaveformViewState;

typedef struct app_state {
  SDL_Window *window;
  SDL_Surface *file_icon;
  SDL_Surface *play_icon;
  SDL_Surface *send_icon;
  SDL_Surface *remove_icon;
  SDL_Surface *help_icon;

  Clay_SDL3RendererData rendererData;
  AudioTrack audio_track;
  WaveformViewState waveform_view;
} AppState;

static struct {
  bool visible;
  int x, y;
} context_menu;

// Process audio file to detect beats
static void process_audio_file(AudioTrack *track_state) {
  // Initialize aubio objects
  uint_t win_size = 1024;
  uint_t hop_size = 512;
  uint_t samplerate = 0; // Will be set by aubio_source

  // Clean up previous processing if any - protected by mutex
  SDL_LockMutex(track_state->data_mutex);
  if (track_state->in_fvec) {
    del_fvec(track_state->in_fvec);
    track_state->in_fvec = NULL;
  }
  if (track_state->out_fvec) {
    del_fvec(track_state->out_fvec);
    track_state->out_fvec = NULL;
  }
  if (track_state->beat_positions) {
    free(track_state->beat_positions);
    track_state->beat_positions = NULL;
  }
  if (track_state->all_samples) {
    free(track_state->all_samples);
    track_state->all_samples = NULL;
  }
  track_state->total_samples = 0;
  SDL_UnlockMutex(track_state->data_mutex);

  // Create source
  aubio_source_t *source =
      new_aubio_source(track_state->selectedFile, samplerate, hop_size);
  if (!source) {
    printf("Error: Could not open audio file: %s\n", track_state->selectedFile);

    // Update state to indicate processing failed
    SDL_LockMutex(track_state->data_mutex);
    track_state->status = STATUS_IDLE;
    SDL_UnlockMutex(track_state->data_mutex);

    return;
  }

  // Get actual samplerate
  samplerate = aubio_source_get_samplerate(source);

  // Create tempo detection object
  aubio_tempo_t *tempo =
      new_aubio_tempo("default", win_size, hop_size, samplerate);
  if (!tempo) {
    printf("Error: Could not create tempo detection object\n");
    del_aubio_source(source);

    // Update state to indicate processing failed
    SDL_LockMutex(track_state->data_mutex);
    track_state->status = STATUS_IDLE;
    SDL_UnlockMutex(track_state->data_mutex);

    return;
  }

  // Create input/output vectors
  fvec_t *in_fvec = new_fvec(hop_size);
  fvec_t *out_fvec = new_fvec(1);

  // Initialize beat positions storage
  int beat_capacity = 1000; // Initial capacity
  float *beat_positions = malloc(beat_capacity * sizeof(float));
  int beat_count = 0;

  // Get file duration for progress calculation
  uint_t duration = aubio_source_get_duration(source);
  uint_t total_frames = duration / hop_size;
  uint_t frames_processed = 0;

  uint_t read = 0;

  // Allocate buffer for all samples
  float *all_samples = (float *)calloc(duration, sizeof(float));
  if (!all_samples) {
    printf("Error: Could not allocate memory for audio samples\n");
    del_aubio_source(source);
    del_aubio_tempo(tempo);
    del_fvec(in_fvec);
    del_fvec(out_fvec);
    free(beat_positions);

    // Update state to indicate processing failed
    SDL_LockMutex(track_state->data_mutex);
    track_state->status = STATUS_IDLE;
    SDL_UnlockMutex(track_state->data_mutex);

    return;
  }

  uint_t sample_index = 0;
  frames_processed = 0;
  
  do {
    // Read from source
    aubio_source_do(source, in_fvec, &read);

    // Copy samples to buffer
    for (uint_t i = 0; i < read; i++) {
      if (sample_index < duration) {
        all_samples[sample_index++] = in_fvec->data[i];
      }
    }

    // Execute tempo detection
    aubio_tempo_do(tempo, in_fvec, out_fvec);

    // Check if beat detected
    if (out_fvec->data[0] != 0) {
      // Store beat position
      if (beat_count >= beat_capacity) {
        // Resize array if needed
        beat_capacity *= 2;
        beat_positions = realloc(beat_positions, beat_capacity * sizeof(float));
      }

      // Store beat position in seconds
      beat_positions[beat_count] = aubio_tempo_get_last_s(tempo);
      beat_count++;
    }

    // Update progress
    frames_processed++;
    if (total_frames > 0) {
      SDL_LockMutex(track_state->data_mutex);
      track_state->processing_progress = (float)frames_processed / total_frames;
      SDL_UnlockMutex(track_state->data_mutex);
    }
  } while (read == hop_size);

  // Update state with results - protected by mutex
  SDL_LockMutex(track_state->data_mutex);
  track_state->in_fvec = in_fvec;
  track_state->out_fvec = out_fvec;
  track_state->beat_positions = beat_positions;
  track_state->beat_count = beat_count;
  track_state->all_samples = all_samples;
  track_state->total_samples = duration;
  track_state->status = STATUS_COMPLETED;
  track_state->processing_progress = 1.0f;
  SDL_UnlockMutex(track_state->data_mutex);

  // Print some debug info
  printf("Processed audio file: %s\n", track_state->selectedFile);
  printf("Total samples: %u\n", duration);
  printf("Detected %d beats\n", beat_count);
  if (beat_count > 0) {
    printf("First beat at: %.3f s\n", beat_positions[0]);
    printf("Last beat at: %.3f s\n", beat_positions[beat_count - 1]);
  }
  
  // Clean up aubio source
  del_aubio_source(source);
  del_aubio_tempo(tempo);
}

// Thread function for audio processing
static int audio_processing_thread(void *data) {
  AudioTrack *track_state = (AudioTrack *)data;

  // Process the audio file
  process_audio_file(track_state);

  // Update processing state if not already completed (in case of error)
  SDL_LockMutex(track_state->data_mutex);
  if (track_state->status == STATUS_ACTIVE) {
    track_state->status = STATUS_IDLE;
  }
  SDL_UnlockMutex(track_state->data_mutex);

  return 0;
}

static inline Clay_Dimensions SDL_MeasureText(Clay_StringSlice text,
                                              Clay_TextElementConfig *config,
                                              void *userData) {
  TTF_Font **fonts = userData;
  TTF_Font *font = fonts[config->fontId];
  int width, height;

  if (!TTF_GetStringSize(font, text.chars, text.length, &width, &height)) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to measure text: %s",
                 SDL_GetError());
  }

  return (Clay_Dimensions){(float)width, (float)height};
}

static void
headerButton(Clay_ElementId buttonId, Clay_ElementId iconId, SDL_Surface *icon,
             void (*callback)(Clay_ElementId, Clay_PointerData, intptr_t),
             intptr_t userData) {

  CLAY({
      .id = buttonId,
      .backgroundColor =
          Clay_Hovered() ? COLOR_BUTTON_BG_HOVER : COLOR_BUTTON_BG,
      .layout = {.padding = CLAY_PADDING_ALL(4)},
      .cornerRadius = CLAY_CORNER_RADIUS(5),
  }) {
    Clay_OnHover(callback, userData);
    CLAY({.id = iconId,
          .layout = {.sizing = {.width = CLAY_SIZING_FIXED(50),
                                .height = CLAY_SIZING_GROW(0)}},
          .image = {
              .imageData = icon,
              .sourceDimensions = {1, 1},
          }});
  }
}

static void handleFileSelection(Clay_ElementId elementId,
                                Clay_PointerData pointerData,
                                intptr_t userData) {
  if (pointerData.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
    AudioTrack *track_state = (AudioTrack *)userData;

    // If already processing, don't start another thread
    SDL_LockMutex(track_state->data_mutex);
    if (track_state->status == STATUS_ACTIVE) {
      SDL_UnlockMutex(track_state->data_mutex);
      return;
    }
    SDL_UnlockMutex(track_state->data_mutex);

    if (track_state->processing_thread) {
      SDL_WaitThread(track_state->processing_thread, NULL);
      track_state->processing_thread = NULL;
    }

    const char *filterPatterns[] = {"*.wav", "*.mp3"};
    track_state->selectedFile =
        tinyfd_openFileDialog("Select Audio File", // title
                              "",                  // default path
                              2,                   // number of filter patterns
                              filterPatterns,      // filter patterns array
                              "Audio Files",       // filter description
                              0                    // allow multiple selections
        );

    if (track_state->selectedFile) {
      // Initialize processing state
      SDL_LockMutex(track_state->data_mutex);
      track_state->beat_count = 0;
      track_state->status = STATUS_ACTIVE;
      track_state->processing_progress = 0.0f;
      SDL_UnlockMutex(track_state->data_mutex);

      // Start processing thread
      track_state->processing_thread = SDL_CreateThread(
          audio_processing_thread, "AudioProcessing", track_state);

      if (!track_state->processing_thread) {
        printf("Error: Could not create audio processing thread: %s\n",
               SDL_GetError());

        // Reset processing state
        SDL_LockMutex(track_state->data_mutex);
        track_state->status = STATUS_IDLE;
        SDL_UnlockMutex(track_state->data_mutex);
      }
    }
  }
}

void HandleClayErrors(Clay_ErrorData errorData) {
  printf("%s", errorData.errorText.chars);
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  if (!TTF_Init()) {
    return SDL_APP_FAILURE;
  }

  AppState *state = SDL_calloc(1, sizeof(AppState));
  if (!state) {
    return SDL_APP_FAILURE;
  }
  *appstate = state;

  if (!SDL_CreateWindowAndRenderer("automarker", 640, 480, 0, &state->window,
                                   &state->rendererData.renderer)) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR,
                 "Failed to create window and renderer: %s", SDL_GetError());
    return SDL_APP_FAILURE;
  }
  SDL_SetWindowResizable(state->window, true);

  state->rendererData.textEngine =
      TTF_CreateRendererTextEngine(state->rendererData.renderer);
  if (!state->rendererData.textEngine) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR,
                 "Failed to create text engine from renderer: %s",
                 SDL_GetError());
    return SDL_APP_FAILURE;
  }

  state->rendererData.fonts = SDL_calloc(1, sizeof(TTF_Font *));
  if (!state->rendererData.fonts) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR,
                 "Failed to allocate memory for the font array: %s",
                 SDL_GetError());
    return SDL_APP_FAILURE;
  }

  TTF_Font *font = TTF_OpenFont("resources/Roboto-Regular.ttf", 24);
  if (!font) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to load font: %s",
                 SDL_GetError());
    return SDL_APP_FAILURE;
  }

  state->rendererData.fonts[FONT_REGULAR] = font;

  /* Initialize Clay */
  uint64_t totalMemorySize = Clay_MinMemorySize();
  Clay_Arena clayMemory = Clay_CreateArenaWithCapacityAndMemory(
      totalMemorySize, malloc(totalMemorySize));

  int width, height;
  SDL_GetWindowSize(state->window, &width, &height);
  Clay_Initialize(clayMemory, (Clay_Dimensions){(float)width, (float)height},
                  (Clay_ErrorHandler){HandleClayErrors});
  Clay_SetMeasureTextFunction(SDL_MeasureText, state->rendererData.fonts);

  state->file_icon = IMG_Load("resources/file.svg");
  state->play_icon = IMG_Load("resources/play_pause.svg");
  state->send_icon = IMG_Load("resources/send.svg");
  state->remove_icon = IMG_Load("resources/remove.svg");
  state->help_icon = IMG_Load("resources/help.svg");

  state->audio_track.data_mutex = SDL_CreateMutex();
  
  // Initialize waveform view state
  state->waveform_view.zoom = 1.0f;
  state->waveform_view.scroll = 0.0f;

  *appstate = state;
  return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
  SDL_AppResult ret_val = SDL_APP_CONTINUE;

  switch (event->type) {
  case SDL_EVENT_QUIT:
    ret_val = SDL_APP_SUCCESS;
    break;
  case SDL_EVENT_WINDOW_RESIZED:
    Clay_SetLayoutDimensions((Clay_Dimensions){(float)event->window.data1,
                                               (float)event->window.data2});
    break;
  case SDL_EVENT_MOUSE_MOTION:
    Clay_SetPointerState((Clay_Vector2){event->motion.x, event->motion.y},
                         event->motion.state & SDL_BUTTON_LMASK);
    break;
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
    Clay_SetPointerState((Clay_Vector2){event->button.x, event->button.y},
                         event->button.button == SDL_BUTTON_LEFT);
    if (event->button.button == SDL_BUTTON_RIGHT) {
      context_menu.x = event->button.x;
      context_menu.y = event->button.y;
      context_menu.visible = true;
    } else
      context_menu.visible = false;
    break;
  case SDL_EVENT_MOUSE_WHEEL:
    {
      AppState *state = (AppState *)appstate;
      
      // Handle vertical wheel for zoom (y)
      if (event->wheel.y != 0) {
        // Adjust zoom level - positive y means zoom in, negative means zoom out
        float zoom_delta = event->wheel.y * 0.1f; // Adjust sensitivity as needed
        state->waveform_view.zoom += zoom_delta;
        
        // Clamp zoom to reasonable values
        if (state->waveform_view.zoom < 1.0f) state->waveform_view.zoom = 1.0f;
        if (state->waveform_view.zoom > 50.0f) state->waveform_view.zoom = 50.0f;
      }
      
      // Handle horizontal wheel for scroll (x)
      if (event->wheel.x != 0) {
        // Adjust scroll position - inverted direction: positive x means scroll left, negative means scroll right
        // Scale scroll sensitivity inversely with zoom level for smoother scrolling when zoomed in
        float base_sensitivity = 0.05f;
        float zoom_factor = 1.0f / state->waveform_view.zoom;
        float scroll_delta = event->wheel.x * base_sensitivity * zoom_factor; // Removed negative sign to invert direction
        
        state->waveform_view.scroll += scroll_delta;
        
        // Clamp scroll to valid range [0, 1]
        if (state->waveform_view.scroll < 0.0f) state->waveform_view.scroll = 0.0f;
        if (state->waveform_view.scroll > 1.0f) state->waveform_view.scroll = 1.0f;
      }
      
      // Also update Clay scroll containers
      Clay_UpdateScrollContainers(
          true, (Clay_Vector2){event->wheel.x, event->wheel.y}, 0.01f);
    }
    break;
  default:
    break;
  };

  return ret_val;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
  AppState *state = appstate;

  Clay_BeginLayout();

  Clay_Sizing layoutExpand = {.width = CLAY_SIZING_GROW(0),
                              .height = CLAY_SIZING_GROW(0)};

  // Build main UI layout
  CLAY({.id = CLAY_ID("MainContainer"),
        .backgroundColor = COLOR_BG_DARK,
        .layout = {.layoutDirection = CLAY_TOP_TO_BOTTOM,
                   .sizing = layoutExpand,
                   .padding = CLAY_PADDING_ALL(16),
                   .childGap = 16}}) {
    if (context_menu.visible) {
      CLAY({.floating = {.attachTo = CLAY_ATTACH_TO_PARENT,
                         .attachPoints = {.parent = CLAY_ATTACH_POINT_LEFT_TOP},
                         .offset = {context_menu.x, context_menu.y}},
            .layout = {.padding = CLAY_PADDING_ALL(8)},
            .backgroundColor = COLOR_BG_LIGHT,
            .cornerRadius = CLAY_CORNER_RADIUS(8)}) {
        CLAY({.layout = {.layoutDirection = CLAY_TOP_TO_BOTTOM},
              .backgroundColor = COLOR_BG_DARK,
              .cornerRadius = CLAY_CORNER_RADIUS(8)}) {
          CLAY({.layout = {.padding = CLAY_PADDING_ALL(16)},
                .backgroundColor =
                    Clay_Hovered() ? COLOR_ACCENT : COLOR_BG_DARK}) {
            CLAY_TEXT(CLAY_STRING("Option 1"),
                      CLAY_TEXT_CONFIG({.fontId = FONT_REGULAR,
                                        .fontSize = 16,
                                        .textColor = COLOR_WHITE}));
          }
        }
      }
    }

    // Header bar
    CLAY({.id = CLAY_ID("HeaderBar"),
          .layout = {.sizing = {.width = CLAY_SIZING_GROW(0)},
                     .padding = CLAY_PADDING_ALL(16),
                     .childGap = 16,
                     .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = COLOR_BG_LIGHT,
          .cornerRadius = CLAY_CORNER_RADIUS(8)}) {
      headerButton(CLAY_ID("FileButton"), CLAY_ID("FileIcon"), state->file_icon,
                   handleFileSelection, (intptr_t)&state->audio_track);

      headerButton(CLAY_ID("PlayButton"), CLAY_ID("PlayIcon"), state->play_icon,
                   NULL, 0);

      headerButton(CLAY_ID("SendButton"), CLAY_ID("SendIcon"), state->send_icon,
                   NULL, 0);

      headerButton(CLAY_ID("RemoveButton"), CLAY_ID("RemoveIcon"),
                   state->remove_icon, NULL, 0);

      headerButton(CLAY_ID("HelpButton"), CLAY_ID("HelpIcon"), state->help_icon,
                   NULL, 0);
    }

    // Main content area
    CLAY({.id = CLAY_ID("MainContent"),
          .backgroundColor = COLOR_BG_LIGHT,
          .layout = {.layoutDirection = CLAY_LEFT_TO_RIGHT,
                     .sizing = layoutExpand,
                     .padding = CLAY_PADDING_ALL(16),
                     .childGap = 16},
          .cornerRadius = CLAY_CORNER_RADIUS(8)}) {
      // Create waveform data with current zoom and scroll values
      WaveformData waveformData = {
          .samples = NULL,
          .sampleCount = 0,
          .currentZoom = state->waveform_view.zoom,
          .currentScroll = state->waveform_view.scroll,
          .lineColor = COLOR_WAVEFORM_LINE
      };

      // If we have audio data, use it
      SDL_LockMutex(state->audio_track.data_mutex);
      if (state->audio_track.all_samples && state->audio_track.total_samples > 0) {
          waveformData.samples = state->audio_track.all_samples;
          waveformData.sampleCount = state->audio_track.total_samples;
          
          // Debug info
          static bool logged_waveform = false;
          if (!logged_waveform) {
              printf("Waveform display using %d samples\n", waveformData.sampleCount);
              logged_waveform = true;
          }
      } else if (state->audio_track.in_fvec) {
          // Fallback to in_fvec if all_samples not available
          waveformData.samples = state->audio_track.in_fvec->data;
          waveformData.sampleCount = state->audio_track.in_fvec->length;
          printf("Fallback to in_fvec with %d samples\n", waveformData.sampleCount);
      }
      SDL_UnlockMutex(state->audio_track.data_mutex);

      // Create custom element in the UI
      CLAY({.id = CLAY_ID("WaveformDisplay"),
            .backgroundColor = COLOR_WAVEFORM_BG,
            .layout = {.sizing = {.width = CLAY_SIZING_GROW(0),
                                  .height = CLAY_SIZING_GROW(0)}},
            .cornerRadius = CLAY_CORNER_RADIUS(8),
            .custom = {.customData = &waveformData}}) {}
    }
  }

  Clay_RenderCommandArray render_commands = Clay_EndLayout();

  SDL_SetRenderDrawColor(state->rendererData.renderer, 0, 0, 0, 255);
  SDL_RenderClear(state->rendererData.renderer);

  SDL_Clay_RenderClayCommands(&state->rendererData, &render_commands);

  SDL_RenderPresent(state->rendererData.renderer);

  return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
  (void)result;

  if (result != SDL_APP_SUCCESS) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Application failed to run");
  }

  AppState *state = appstate;
  AudioTrack *track_state = &state->audio_track;

  if (state) {
    // Wait for processing thread to finish if it's running
    if (track_state->processing_thread) {
      SDL_WaitThread(track_state->processing_thread, NULL);
      track_state->processing_thread =
          NULL; // SDL will throw a warning if we don't do this, so let's do it
                // with everything!
    }
    // Destroy mutex
    if (track_state->data_mutex) {
      SDL_DestroyMutex(track_state->data_mutex);
      track_state->data_mutex = NULL;
    }

    // Clean up aubio objects
    if (track_state->in_fvec) {
      del_fvec(track_state->in_fvec);
      track_state->in_fvec = NULL;
    }
    if (track_state->out_fvec) {
      del_fvec(track_state->out_fvec);
      track_state->out_fvec = NULL;
    }
    if (track_state->beat_positions) {
      free(track_state->beat_positions);
      track_state->beat_positions = NULL;
    }

    // Clean up SDL resources
    if (state->rendererData.renderer)
      SDL_DestroyRenderer(state->rendererData.renderer);

    if (state->window)
      SDL_DestroyWindow(state->window);

    if (state->rendererData.fonts) {
      TTF_CloseFont(state->rendererData.fonts[FONT_REGULAR]);

      SDL_free(state->rendererData.fonts);
    }

    if (state->rendererData.textEngine)
      TTF_DestroyRendererTextEngine(state->rendererData.textEngine);

    SDL_free(state);
  }
  TTF_Quit();
}

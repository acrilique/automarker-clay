#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_sound/SDL_sound.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <stdio.h>

#include "../libs/tinyfiledialogs.h"

#define CLAY_IMPLEMENTATION
#include "../libs/clay/clay.h"

#include "./clay_renderer_SDL3.c"
#include "audio_state.h"
#include "connections/process_utils.h"
#include "connections/premiere_pro.h"
#include "connections/after_effects.h"
#include "connections/resolve.h"

// Font IDs
static const Uint32 FONT_REGULAR = 0;

// General colors
static const Clay_Color COLOR_BG_DARK = (Clay_Color){43, 41, 51, 255};
static const Clay_Color COLOR_BG_LIGHT = (Clay_Color){90, 90, 90, 255};
static const Clay_Color COLOR_WHITE = (Clay_Color){255, 244, 244, 255};
static const Clay_Color COLOR_ACCENT = (Clay_Color){140, 140, 140, 255};

// Button colors
static const Clay_Color COLOR_BUTTON_BG = (Clay_Color){140, 140, 140, 255};
static const Clay_Color COLOR_BUTTON_BG_HOVER =
    (Clay_Color){160, 160, 160, 255};

// Waveform colors
static const Clay_Color COLOR_WAVEFORM_BG = (Clay_Color){60, 60, 60, 255};
static const Clay_Color COLOR_WAVEFORM_LINE = (Clay_Color){255, 255, 255, 255};
static const Clay_Color COLOR_WAVEFORM_BEAT =
    (Clay_Color){255, 255, 0, 255};

typedef struct {
  float zoom;   // Zoom level (1.0 = normal)
  float scroll; // Scroll position (0.0 = start, 1.0 = end)
} WaveformViewState;

typedef enum {
  APP_NONE,
  APP_PREMIERE,
  APP_AE,
  APP_RESOLVE
} ConnectedApp;

typedef struct app_state {
  SDL_Window *window;
  ConnectedApp connected_app;
  SDL_Thread *app_status_thread;
  SDL_Surface *file_icon;
  SDL_Surface *play_icon;
  SDL_Surface *send_icon;
  SDL_Surface *remove_icon;
  SDL_Surface *help_icon;

  Clay_SDL3RendererData rendererData;
  AudioState *audio_state;
  WaveformViewState waveform_view;
  struct {
    bool visible;
    int x, y;
  } context_menu;
} AppState;


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

  CLAY(buttonId, {
      .backgroundColor =
          Clay_Hovered() ? COLOR_BUTTON_BG_HOVER : COLOR_BUTTON_BG,
      .layout = {.padding = CLAY_PADDING_ALL(4)},
      .cornerRadius = CLAY_CORNER_RADIUS(5),
  }) {
    Clay_OnHover(callback, userData);
    CLAY(iconId, {
          .layout = {.sizing = {.width = CLAY_SIZING_FIXED(50),
                                .height = CLAY_SIZING_GROW(0)}},
          .aspectRatio = {.aspectRatio = 1.0f},
          .image = {
              .imageData = icon,
          }});
  }
}

static void sendMarkers(Clay_ElementId elementId, Clay_PointerData pointerData,
                        intptr_t userData) {
  (void)elementId;
  if (pointerData.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
    AppState *app_state = (AppState *)userData;
    AudioState *audio_state = app_state->audio_state;

    if (audio_state->status == STATUS_COMPLETED) {
      double *beats_in_seconds =
          malloc(sizeof(double) * audio_state->beat_count);
      for (int i = 0; i < audio_state->beat_count; i++) {
        beats_in_seconds[i] =
            (double)audio_state->beat_positions[i] / audio_state->sample->actual.rate;
      }

      switch (app_state->connected_app) {
      case APP_PREMIERE:
        premiere_pro_add_markers(beats_in_seconds, audio_state->beat_count);
        break;
      case APP_AE:
        after_effects_add_markers(beats_in_seconds, audio_state->beat_count);
        break;
      case APP_RESOLVE:
        resolve_add_markers(beats_in_seconds, audio_state->beat_count);
        break;
      default:
        break;
      }
      free(beats_in_seconds);
    }
  }
}

static void removeMarkers(Clay_ElementId elementId,
                         Clay_PointerData pointerData, intptr_t userData) {
  (void)elementId;
  if (pointerData.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
    AppState *app_state = (AppState *)userData;
    switch (app_state->connected_app) {
    case APP_PREMIERE:
      premiere_pro_clear_all_markers();
      break;
    case APP_AE:
      after_effects_clear_all_markers();
      break;
    case APP_RESOLVE:
      resolve_clear_all_markers();
      break;
    default:
      break;
    }
  }
}

static void handleFileSelection(Clay_ElementId elementId,
                                Clay_PointerData pointerData,
                                intptr_t userData) {
  (void)elementId;
  if (pointerData.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
    AppState *app_state = (AppState *)userData;
    AudioState *audio_state = app_state->audio_state;

    // Always allow opening the file dialog
    const char *filterPatterns[] = {
        "*.wav",
        "*.mp3"}; // This should depend on the result of Sound_AvailableDecoders
    const char * selectedFile =
        tinyfd_openFileDialog("Select Audio File", // title
                              "",                  // default path
                              2,                   // number of filter patterns
                              filterPatterns,      // filter patterns array
                              "Audio Files",       // filter description
                              0                    // allow multiple selections
        );

    // If no file was selected (dialog was cancelled), do nothing
    if (!selectedFile) {
      return;
    }

    // If a file was selected, check if we need to stop ongoing processing
    SDL_LockMutex(audio_state->data_mutex);
    bool is_processing = (audio_state->status == STATUS_DECODE ||
                         audio_state->status == STATUS_BEAT_ANALYSIS);
    SDL_UnlockMutex(audio_state->data_mutex);

    if (is_processing) {
      // Gracefully stop the ongoing processing
      audio_state_request_stop(audio_state);
    } else if (audio_state->processing_thread) {
      // Wait for any completed thread to finish
      SDL_WaitThread(audio_state->processing_thread, NULL);
      audio_state->processing_thread = NULL;
    }

    // Load the new file
    audio_state_load_file(audio_state, selectedFile);
  }
}

void HandleClayErrors(Clay_ErrorData errorData) {
  printf("%s", errorData.errorText.chars);
}

int check_app_status(void *data) {
    AppState *app_state = (AppState *)data;
    while (true) {
        if (is_process_running(PREMIERE_PROCESS_NAME)) {
            app_state->connected_app = APP_PREMIERE;
        } else if (is_process_running(AFTERFX_PROCESS_NAME)) {
            app_state->connected_app = APP_AE;
        } else if (is_process_running(RESOLVE_PROCESS_NAME)) {
            app_state->connected_app = APP_RESOLVE;
        } else {
            app_state->connected_app = APP_NONE;
        }
        SDL_Delay(1000); // Check every second
    }
    return 0;
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  if (!TTF_Init()) {
    return SDL_APP_FAILURE;
  }

  if (!Sound_Init()) {
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
                  (Clay_ErrorHandler){HandleClayErrors, 0});
  Clay_SetMeasureTextFunction(SDL_MeasureText, state->rendererData.fonts);

  state->file_icon = IMG_Load("resources/file.svg");
  state->play_icon = IMG_Load("resources/play_pause.svg");
  state->send_icon = IMG_Load("resources/send.svg");
  state->remove_icon = IMG_Load("resources/remove.svg");
  state->help_icon = IMG_Load("resources/help.svg");

  state->audio_state = audio_state_create();
  if (!state->audio_state) {
    SDL_free(state);
    return SDL_APP_FAILURE;
  }
  state->context_menu.visible = false;
  state->context_menu.x = 0;
  state->context_menu.y = 0;

  // Initialize waveform view state
  state->waveform_view.zoom = 50.0f;
  state->waveform_view.scroll = 0.0f;

  state->connected_app = APP_NONE;
  state->app_status_thread = SDL_CreateThread(check_app_status, "AppStatusThread", (void *)state);

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
    AppState *state = (AppState *)appstate;
    Clay_SetPointerState((Clay_Vector2){event->button.x, event->button.y},
                         event->button.button == SDL_BUTTON_LEFT);
    if (event->button.button == SDL_BUTTON_RIGHT) {
      state->context_menu.x = event->button.x;
      state->context_menu.y = event->button.y;
      state->context_menu.visible = true;
    } else
      state->context_menu.visible = false;
    break;
  case SDL_EVENT_MOUSE_WHEEL: {
    AppState *state = (AppState *)appstate;

    // Handle vertical wheel for zoom (y)
    if (event->wheel.y != 0) {
      // Adjust zoom level - positive y means zoom in, negative means zoom out
      float zoom_delta = state->waveform_view.zoom * event->wheel.y /
                         10.0f; // Adjust sensitivity as needed
      state->waveform_view.zoom += zoom_delta;

      // Clamp zoom to reasonable values
      if (state->waveform_view.zoom < 1.0f)
        state->waveform_view.zoom = 1.0f;
      if (state->waveform_view.zoom > 1000.0f)
        state->waveform_view.zoom = 1000.0f;
    }

    // Handle horizontal wheel for scroll (x)
    if (event->wheel.x != 0) {
      float base_sensitivity = 0.10f;
      float zoom_factor = 1.0f / state->waveform_view.zoom;
      float scroll_delta = event->wheel.x * base_sensitivity * zoom_factor;

      state->waveform_view.scroll += scroll_delta;

      // Clamp scroll to valid range [0, 1]
      if (state->waveform_view.scroll < 0.0f)
        state->waveform_view.scroll = 0.0f;
      if (state->waveform_view.scroll > 1.0f)
        state->waveform_view.scroll = 1.0f;
    }

    // Also update Clay scroll containers
    Clay_UpdateScrollContainers(
        true, (Clay_Vector2){event->wheel.x, event->wheel.y}, 0.01f);
  } break;
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
  CLAY(CLAY_ID("MainContainer"), {
        .backgroundColor = COLOR_BG_DARK,
        .layout = {.layoutDirection = CLAY_TOP_TO_BOTTOM,
                   .sizing = layoutExpand,
                   .padding = CLAY_PADDING_ALL(16),
                   .childGap = 16}}) {

    // Context menu
    if (state->context_menu.visible) {
      CLAY_AUTO_ID({.floating = {.attachTo = CLAY_ATTACH_TO_PARENT,
                         .attachPoints = {.parent = CLAY_ATTACH_POINT_LEFT_TOP},
                         .offset = {state->context_menu.x, state->context_menu.y}},
            .layout = {.padding = CLAY_PADDING_ALL(8)},
            .backgroundColor = COLOR_BG_LIGHT,
            .cornerRadius = CLAY_CORNER_RADIUS(8)}) {
        CLAY_AUTO_ID({.layout = {.layoutDirection = CLAY_TOP_TO_BOTTOM},
              .backgroundColor = COLOR_BG_DARK,
              .cornerRadius = CLAY_CORNER_RADIUS(8)}) {
          CLAY_AUTO_ID({.layout = {.padding = CLAY_PADDING_ALL(16)},
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
    CLAY(CLAY_ID("HeaderBar"), {
          .layout = {.sizing = {.width = CLAY_SIZING_GROW(0)},
                     .padding = CLAY_PADDING_ALL(16),
                     .childGap = 16,
                     .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}},
          .backgroundColor = COLOR_BG_LIGHT,
          .cornerRadius = CLAY_CORNER_RADIUS(8)}) {
      headerButton(CLAY_ID("FileButton"), CLAY_ID("FileIcon"), state->file_icon,
                   handleFileSelection, (intptr_t)state);

      headerButton(CLAY_ID("PlayButton"), CLAY_ID("PlayIcon"), state->play_icon,
                   NULL, 0);

      headerButton(CLAY_ID("SendButton"), CLAY_ID("SendIcon"), state->send_icon,
                   sendMarkers, (intptr_t)state);

      headerButton(CLAY_ID("RemoveButton"), CLAY_ID("RemoveIcon"),
                   state->remove_icon, removeMarkers, (intptr_t)state);

      headerButton(CLAY_ID("HelpButton"), CLAY_ID("HelpIcon"), state->help_icon,
                   NULL, 0);

      switch (state->connected_app) {
      case APP_PREMIERE:
        CLAY_TEXT(CLAY_STRING("Premiere Pro Connected"),
                  CLAY_TEXT_CONFIG(
                      {.fontId = FONT_REGULAR, .fontSize = 16, .textColor = COLOR_WHITE}));
        break;
      case APP_AE:
        CLAY_TEXT(CLAY_STRING("After Effects Connected"),
                  CLAY_TEXT_CONFIG(
                      {.fontId = FONT_REGULAR, .fontSize = 16, .textColor = COLOR_WHITE}));
        break;
      case APP_RESOLVE:
        CLAY_TEXT(CLAY_STRING("DaVinci Resolve Connected"),
                  CLAY_TEXT_CONFIG(
                      {.fontId = FONT_REGULAR, .fontSize = 16, .textColor = COLOR_WHITE}));
        break;
      default:
        CLAY_TEXT(CLAY_STRING("No App Connected"),
                  CLAY_TEXT_CONFIG(
                      {.fontId = FONT_REGULAR, .fontSize = 16, .textColor = COLOR_WHITE}));
        break;
      }
    }

    // Main content area
    CLAY(CLAY_ID("MainContent"), {
          .backgroundColor = COLOR_BG_LIGHT,
          .layout = {.layoutDirection = CLAY_LEFT_TO_RIGHT,
                     .sizing = layoutExpand,
                     .padding = CLAY_PADDING_ALL(16),
                     .childGap = 16},
          .cornerRadius = CLAY_CORNER_RADIUS(8)}) {
      // Create waveform data with current zoom and scroll values
      WaveformData waveformData = {.samples = NULL,
                                   .sampleCount = 0,
                                   .beat_positions = NULL,
                                   .beat_count = 0,
                                   .currentZoom = state->waveform_view.zoom,
                                   .currentScroll = state->waveform_view.scroll,
                                   .lineColor = COLOR_WAVEFORM_LINE,
                                   .beatColor = COLOR_WAVEFORM_BEAT};

      // If we have audio data, use it
      SDL_LockMutex(state->audio_state->data_mutex);
      if (state->audio_state->status >= STATUS_BEAT_ANALYSIS &&
          state->audio_state->sample->buffer &&
          state->audio_state->sample->buffer_size > 0) {
        waveformData.samples = state->audio_state->sample->buffer;
        waveformData.sampleCount =
            state->audio_state->sample->buffer_size / sizeof(float);

        // Add beat positions if available
        if (state->audio_state->beat_positions &&
            state->audio_state->beat_count > 0) {
          waveformData.beat_positions = state->audio_state->beat_positions;
          waveformData.beat_count = state->audio_state->beat_count;

          // Debug info for beats
          static bool logged_beats = false;
          if (!logged_beats) {
            printf("Waveform display using %d beats\n",
                   waveformData.beat_count);
            logged_beats = true;
          }
        }

        // Debug info
        static bool logged_waveform = false;
        if (!logged_waveform) {
          printf("Waveform display using %d samples\n",
                 waveformData.sampleCount);
          logged_waveform = true;
        }
      }
      SDL_UnlockMutex(state->audio_state->data_mutex);

      // Create custom element in the UI
      CLAY(CLAY_ID("WaveformDisplay"), {
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

  if (state) {
    SDL_DetachThread(state->app_status_thread);
    audio_state_destroy(state->audio_state);

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

  Sound_Quit();
  TTF_Quit();
}

#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <stdio.h>

#include "../libs/tinyfiledialogs.h"

#define CLAY_IMPLEMENTATION
#include "../libs/clay.h"

#include "../libs/clay_renderer_SDL3.c"

#include "aubio.h"

// Font IDs
static const Uint32 FONT_REGULAR = 0;

// Colors
static const Clay_Color COLOR_BG_DARK   = (Clay_Color) {43, 41, 51, 255};
static const Clay_Color COLOR_BG_LIGHT  = (Clay_Color) {90, 90, 90, 255};
static const Clay_Color COLOR_WHITE     = (Clay_Color) {255, 255, 255, 255};
static const Clay_Color COLOR_ACCENT    = (Clay_Color) {140, 140, 140, 255};

typedef enum {
    STATUS_IDLE,       // Never processed or failed
    STATUS_ACTIVE,     // Currently processing
    STATUS_COMPLETED   // Successfully completed
} Status;

typedef struct audio_track { // Ordered by size
    const char* selectedFile;
    float* beat_positions;
    fvec_t* in_fvec; // Audio samples
    fvec_t* out_fvec; // Beat detection result
    SDL_Thread* processing_thread;
    SDL_Mutex* data_mutex;
    int beat_count;
    float processing_progress;
    Status status; // Replaces is_processing and processing_complete
} AudioTrack;

typedef struct app_state {
    SDL_Window *window; // I do manually need to call a set of functions on app exit to free this
    Clay_SDL3RendererData rendererData; // And this

    AudioTrack audio_track;
} AppState;

// Process audio file to detect beats
void process_audio_file(AudioTrack *track_state) {
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
    SDL_UnlockMutex(track_state->data_mutex);
    
    // Create source
    aubio_source_t *source = new_aubio_source(track_state->selectedFile, samplerate, hop_size);
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
    aubio_tempo_t *tempo = new_aubio_tempo("default", win_size, hop_size, samplerate);
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
    
    // Process the file
    uint_t read = 0;
    do {
        // Read from source
        aubio_source_do(source, in_fvec, &read);
        
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
    track_state->status = STATUS_COMPLETED;
    track_state->processing_progress = 1.0f;
    SDL_UnlockMutex(track_state->data_mutex);
    
    // Print some debug info
    printf("Processed audio file: %s\n", track_state->selectedFile);
    printf("Detected %d beats\n", beat_count);
    if (beat_count > 0) {
        printf("First beat at: %.3f s\n", beat_positions[0]);
        printf("Last beat at: %.3f s\n", beat_positions[beat_count - 1]);
    }
}

// Thread function for audio processing
int audio_processing_thread(void *data) {
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

static inline Clay_Dimensions SDL_MeasureText(Clay_StringSlice text, Clay_TextElementConfig *config, void *userData)
{
    TTF_Font **fonts = userData;
    TTF_Font *font = fonts[config->fontId];
    int width, height;

    if (!TTF_GetStringSize(font, text.chars, text.length, &width, &height)) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to measure text: %s", SDL_GetError());
    }

    return (Clay_Dimensions) { (float) width, (float) height };
}

void handleFileSelection(Clay_ElementId elementId, Clay_PointerData pointerData, intptr_t userData) {
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
        
        const char *filterPatterns[] = { "*.wav", "*.mp3" };
        track_state->selectedFile = tinyfd_openFileDialog(
            "Select Audio File",  // title
            "",                   // default path
            2,                    // number of filter patterns
            filterPatterns,       // filter patterns array
            "Audio Files",        // filter description
            0                     // allow multiple selections
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
                audio_processing_thread,
                "AudioProcessing",
                track_state
            );
            
            if (!track_state->processing_thread) {
                printf("Error: Could not create audio processing thread: %s\n", SDL_GetError());
                
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

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    (void) argc;
    (void) argv;

    if (!TTF_Init()) {
        return SDL_APP_FAILURE;
    }

    AppState *state = SDL_calloc(1, sizeof(AppState));
    if (!state) {
        return SDL_APP_FAILURE;
    }
    *appstate = state;

    if (!SDL_CreateWindowAndRenderer("automarker", 640, 480, 0, &state->window, &state->rendererData.renderer)) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to create window and renderer: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetWindowResizable(state->window, true);

    state->rendererData.textEngine = TTF_CreateRendererTextEngine(state->rendererData.renderer);
    if (!state->rendererData.textEngine) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to create text engine from renderer: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    state->rendererData.fonts = SDL_calloc(1, sizeof(TTF_Font *));
    if (!state->rendererData.fonts) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to allocate memory for the font array: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    TTF_Font *font = TTF_OpenFont("resources/Roboto-Regular.ttf", 24);
    if (!font) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to load font: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    state->rendererData.fonts[FONT_REGULAR] = font;

    /* Initialize Clay */
    uint64_t totalMemorySize = Clay_MinMemorySize();
    Clay_Arena clayMemory = Clay_CreateArenaWithCapacityAndMemory(totalMemorySize, malloc(totalMemorySize));

    int width, height;
    SDL_GetWindowSize(state->window, &width, &height);
    Clay_Initialize(clayMemory, (Clay_Dimensions) { (float) width, (float) height }, (Clay_ErrorHandler) { HandleClayErrors });
    Clay_SetMeasureTextFunction(SDL_MeasureText, state->rendererData.fonts);

    state->audio_track.data_mutex = SDL_CreateMutex();

    *appstate = state;
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    SDL_AppResult ret_val = SDL_APP_CONTINUE;

    switch (event->type) {
        case SDL_EVENT_QUIT:
            ret_val = SDL_APP_SUCCESS;
            break;
        case SDL_EVENT_WINDOW_RESIZED:
            Clay_SetLayoutDimensions((Clay_Dimensions) { (float) event->window.data1, (float) event->window.data2 });
            break;
        case SDL_EVENT_MOUSE_MOTION:
            Clay_SetPointerState((Clay_Vector2) { event->motion.x, event->motion.y },
                                 event->motion.state & SDL_BUTTON_LMASK);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            Clay_SetPointerState((Clay_Vector2) { event->button.x, event->button.y },
                                 event->button.button == SDL_BUTTON_LEFT);
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            Clay_UpdateScrollContainers(true, (Clay_Vector2) { event->wheel.x, event->wheel.y }, 0.01f);
            break;
        default:
            break;
    };

    return ret_val;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    AppState *state = appstate;

    Clay_BeginLayout();

    Clay_Sizing layoutExpand = {
        .width = CLAY_SIZING_GROW(0),
        .height = CLAY_SIZING_GROW(0)
    };

    // Build main UI layout
    CLAY({ 
        .id = CLAY_ID("MainContainer"),
        .backgroundColor = COLOR_BG_DARK,
        .layout = {
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
            .sizing = layoutExpand,
            .padding = CLAY_PADDING_ALL(16),
            .childGap = 16
        }
    }) {
        // Header bar
        CLAY({ 
            .id = CLAY_ID("HeaderBar"),
            .layout = {
                .sizing = {
                    .height = CLAY_SIZING_FIXED(80),
                    .width = CLAY_SIZING_GROW(0)
                },
                .padding = CLAY_PADDING_ALL(16),
                .childGap = 16,
                .childAlignment = {
                    .y = CLAY_ALIGN_Y_CENTER
                }
            },
            .backgroundColor = COLOR_BG_LIGHT,
            .cornerRadius = CLAY_CORNER_RADIUS(8)
        }) {
            // File menu
            CLAY({ 
                .id = CLAY_ID("FileButton"),
                .layout = { 
                    .padding = { 16, 16, 8, 8 },
                    .sizing = {
                        .width = CLAY_SIZING_FIT()
                    },
                    .childAlignment = {
                        .x = CLAY_ALIGN_X_CENTER
                    },                    
                },
                .backgroundColor = COLOR_ACCENT,
                .cornerRadius = CLAY_CORNER_RADIUS(5),
            }) {
                CLAY_TEXT(CLAY_STRING("File"), CLAY_TEXT_CONFIG({
                    .fontId = FONT_REGULAR,
                    .fontSize = 16,
                    .textColor = COLOR_WHITE,
                }));

                bool fileMenuVisible = 
                    Clay_PointerOver(Clay_GetElementId(CLAY_STRING("FileButton"))) ||
                    Clay_PointerOver(Clay_GetElementId(CLAY_STRING("FileMenu")));

                if (fileMenuVisible) {
                    CLAY({ 
                        .id = CLAY_ID("FileMenu"),
                        .floating = {
                            .attachTo = CLAY_ATTACH_TO_PARENT,
                            .attachPoints = {
                                .parent = CLAY_ATTACH_POINT_LEFT_BOTTOM
                            },
                        },
                        .layout = {
                            .padding = {0, 0, 8, 8 }
                        }
                    }) {
                        CLAY({
                            .layout = {
                                .layoutDirection = CLAY_TOP_TO_BOTTOM,
                                .sizing = {
                                    .width = CLAY_SIZING_FIXED(200)
                                },
                            },
                            .backgroundColor = COLOR_BG_DARK,
                            .cornerRadius = CLAY_CORNER_RADIUS(8)
                        }) {
                            CLAY({
                                .layout = { .padding = CLAY_PADDING_ALL(16)},
                                .backgroundColor = Clay_Hovered() ? COLOR_ACCENT : COLOR_BG_DARK
                            }) {
                                Clay_OnHover(handleFileSelection, (intptr_t)&state->audio_track);
                                CLAY_TEXT(CLAY_STRING("Select audio file"), CLAY_TEXT_CONFIG({
                                    .fontId = FONT_REGULAR,
                                    .fontSize = 16,
                                    .textColor = COLOR_WHITE
                                }));
                            }
                        }
                    }
                }
            }

            // Help menu
            CLAY({ 
                .id = CLAY_ID("HelpButton"),
                .layout = { 
                    .padding = { 16, 16, 8, 8 },
                    .sizing = {
                        .width = CLAY_SIZING_FIT()
                    },
                    .childAlignment = {
                        .x = CLAY_ALIGN_X_CENTER
                    },    
                },
                .backgroundColor = COLOR_ACCENT,
                .cornerRadius = CLAY_CORNER_RADIUS(5)
            }) {
                CLAY_TEXT(CLAY_STRING("Help"), CLAY_TEXT_CONFIG({
                    .fontId = FONT_REGULAR,
                    .fontSize = 16,
                    .textColor = COLOR_WHITE
                }));

                bool helpMenuVisible = 
                    Clay_PointerOver(Clay_GetElementId(CLAY_STRING("HelpButton"))) ||
                    Clay_PointerOver(Clay_GetElementId(CLAY_STRING("HelpMenu")));

                if (helpMenuVisible) {
                    CLAY({ 
                        .id = CLAY_ID("HelpMenu"),
                        .floating = {
                            .attachTo = CLAY_ATTACH_TO_PARENT,
                            .attachPoints = {
                                .parent = CLAY_ATTACH_POINT_LEFT_BOTTOM
                            },
                        },
                        .layout = {
                            .padding = {0, 0, 8, 8 }
                        }
                    }) {
                        CLAY({
                            .layout = {
                                .layoutDirection = CLAY_TOP_TO_BOTTOM,
                                .sizing = {
                                    .width = CLAY_SIZING_FIT()
                                },
                            },
                            .backgroundColor = COLOR_BG_DARK,
                            .cornerRadius = CLAY_CORNER_RADIUS(8)
                        }) {
                            CLAY({
                                .layout = { .padding = CLAY_PADDING_ALL(16)},
                                .backgroundColor = Clay_Hovered() ? COLOR_ACCENT : COLOR_BG_DARK
                            }) {
                                CLAY_TEXT(CLAY_STRING("Readme"), CLAY_TEXT_CONFIG({
                                    .fontId = FONT_REGULAR,
                                    .fontSize = 16,
                                    .textColor = COLOR_WHITE
                                }));
                            }
                        }
                    }
                }
            }
        }

        // Main content area
        CLAY({ 
            .id = CLAY_ID("MainContent"),
            .backgroundColor = COLOR_BG_LIGHT,
            .layout = {
                .layoutDirection = CLAY_TOP_TO_BOTTOM,
                .sizing = layoutExpand,
                .padding = CLAY_PADDING_ALL(16),
                .childGap = 16
            },
            .cornerRadius = CLAY_CORNER_RADIUS(8)
        }) {
            AudioTrack *track_state = &state->audio_track;
            // Display file path or default message
            const char* text = track_state->selectedFile ? track_state->selectedFile : "Select an audio file to begin";
            Clay_String str = { .length = strlen(text), .chars = text };
            CLAY_TEXT(str, CLAY_TEXT_CONFIG({
                    .fontId = FONT_REGULAR,
                    .fontSize = 16,
                    .textColor = COLOR_WHITE
                }));
            
            // Display processing status
            SDL_LockMutex(track_state->data_mutex);
            Status status = track_state->status;
            float progress = track_state->processing_progress;
            int beat_count = track_state->beat_count;
            SDL_UnlockMutex(track_state->data_mutex);

            if (status == STATUS_ACTIVE) {
                // Show processing status
                char progress_text[64];
                snprintf(progress_text, sizeof(progress_text), 
                         "Processing audio file... %.0f%%", progress * 100.0f);
                Clay_String progress_str = { .length = strlen(progress_text), .chars = progress_text };
                CLAY_TEXT(progress_str, CLAY_TEXT_CONFIG({
                        .fontId = FONT_REGULAR,
                        .fontSize = 16,
                        .textColor = COLOR_WHITE
                    }));
                
                // Progress bar
                CLAY({
                    .layout = {
                        .sizing = {
                            .width = CLAY_SIZING_GROW(0),
                            .height = CLAY_SIZING_FIXED(20)
                        }
                    },
                    .backgroundColor = COLOR_BG_DARK,
                    .cornerRadius = CLAY_CORNER_RADIUS(4)
                }) {
                    CLAY({
                        .layout = {
                            .sizing = {
                                .width = CLAY_SIZING_PERCENT(progress),
                                .height = CLAY_SIZING_GROW(0)
                            }
                        },
                        .backgroundColor = COLOR_ACCENT
                    }) {}
                }
            } else if (status == STATUS_COMPLETED && track_state->selectedFile) {
                // Show results
                char result_text[256];
                snprintf(result_text, sizeof(result_text), 
                         "Detected %d beats in the audio file", beat_count);
                Clay_String result_str = { .length = strlen(result_text), .chars = result_text };
                CLAY_TEXT(result_str, CLAY_TEXT_CONFIG({
                        .fontId = FONT_REGULAR,
                        .fontSize = 16,
                        .textColor = COLOR_WHITE
                    }));
            }
        }
    }

    Clay_RenderCommandArray render_commands = Clay_EndLayout();

    SDL_SetRenderDrawColor(state->rendererData.renderer, 0, 0, 0, 255);
    SDL_RenderClear(state->rendererData.renderer);

    SDL_Clay_RenderClayCommands(&state->rendererData, &render_commands);

    SDL_RenderPresent(state->rendererData.renderer);

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    (void) result;

    if (result != SDL_APP_SUCCESS) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Application failed to run");
    }

    AppState *state = appstate;
    AudioTrack *track_state = &state->audio_track;

    if (state) {
        // Wait for processing thread to finish if it's running
        if (track_state->processing_thread) {
            SDL_WaitThread(track_state->processing_thread, NULL);
            track_state->processing_thread = NULL; // SDL will throw a warning if we don't do this, so let's do it with everything!
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
            for(size_t i = 0; i < sizeof(state->rendererData.fonts) / sizeof(*state->rendererData.fonts); i++) {
                TTF_CloseFont(state->rendererData.fonts[i]);
            }

            SDL_free(state->rendererData.fonts);
        }

        if (state->rendererData.textEngine)
            TTF_DestroyRendererTextEngine(state->rendererData.textEngine);

        SDL_free(state);
    }
    TTF_Quit();
}

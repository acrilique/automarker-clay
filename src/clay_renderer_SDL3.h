#ifndef CLAY_RENDERER_SDL3_H
#define CLAY_RENDERER_SDL3_H

#include "../libs/clay/clay.h"
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <stdbool.h>

// Waveform data structure
typedef struct {
    float* samples;      // Audio samples
    int sampleCount;     // Number of samples
    unsigned int* beat_positions; // Beat positions (sample indices)
    int beat_count;      // Number of beats
    float currentZoom;   // Zoom level (1.0 = normal)
    float currentScroll; // Scroll position (0.0 = start)
    Clay_Color lineColor; // Color of the waveform line
    Clay_Color beatColor; // Color of the beat markers
    
    // Playback cursor
    bool showPlaybackCursor;     // Whether to show playback cursor
    unsigned int playbackPosition; // Current playback position in samples
    Clay_Color cursorColor;      // Color of the playback cursor

    // Selection
    unsigned int selection_start;
    unsigned int selection_end;
    bool is_hovering_selection_start;
    bool is_hovering_selection_end;
} WaveformData;


typedef struct {
    SDL_Renderer *renderer;
    TTF_TextEngine *textEngine;
    TTF_Font **fonts;
} Clay_SDL3RendererData;

// Function to draw a waveform
void DrawWaveform(Clay_SDL3RendererData *rendererData, SDL_FRect rect, WaveformData *data);

void SDL_Clay_RenderClayCommands(Clay_SDL3RendererData *rendererData, Clay_RenderCommandArray *rcommands);

#endif // CLAY_RENDERER_SDL3_H

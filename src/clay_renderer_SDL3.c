/**
 * This file initially came from Clay, then got adapted to our needs.
 * 
 * Copyright (C) 2025 Lluc Simó Margalef
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "../libs/clay/clay.h"
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3_image/SDL_image.h>
#include <math.h>
#include <stdlib.h>

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
static void DrawWaveform(Clay_SDL3RendererData *rendererData, SDL_FRect rect, WaveformData *data) {
    // If no data or samples, draw a placeholder
    if (!data || !data->samples || data->sampleCount <= 0) {
        // Draw a placeholder line to indicate no data
        SDL_SetRenderDrawColor(rendererData->renderer, 255, 0, 0, 255); // Red color for placeholder
        const float centerY = rect.y + rect.h / 2.0f;
        SDL_RenderLine(rendererData->renderer, rect.x, centerY, rect.x + rect.w, centerY);
        return;
    }
    
    // Calculate drawing parameters
    const int width = rect.w;
    const int height = rect.h;
    const float centerY = rect.y + height / 2.0f;
    
    // Draw center line for reference
    SDL_SetRenderDrawColor(rendererData->renderer, 100, 100, 100, 255); // Gray color for center line
    SDL_RenderLine(rendererData->renderer, rect.x, centerY, rect.x + width, centerY);

    // Fix: Calculate visible samples correctly for zoom levels
    // When zoom = 1.0, show all samples
    // When zoom > 1.0, show fewer samples (zoomed in)
    // When zoom < 1.0, still show all samples but with different sampling
    unsigned int visibleSamples;
    if (data->currentZoom >= 1.0f) {
        visibleSamples = (unsigned int)(data->sampleCount / data->currentZoom);
    } else {
        visibleSamples = data->sampleCount; // Show all samples when zoomed out
    }
    
    int maxStartSample = (data->sampleCount > (int)visibleSamples) ? (data->sampleCount - visibleSamples) : 0;
    unsigned int startSample = (maxStartSample > 0) ? (unsigned int)(data->currentScroll * maxStartSample) : 0;
    
    // Ensure we're within bounds
    if (visibleSamples > (unsigned int)data->sampleCount) visibleSamples = data->sampleCount;
    if (startSample + visibleSamples > (unsigned int)data->sampleCount) {
        visibleSamples = data->sampleCount - startSample;
    }

    SDL_SetRenderDrawColor(rendererData->renderer, 
        data->lineColor.r, 
        data->lineColor.g, 
        data->lineColor.b, 
        data->lineColor.a);
    
    for (int x = 0; x < width; x++) {
        // Map x position to sample index
        float samplePos = (float)x / width * visibleSamples;
        int sampleIndex = startSample + (int)samplePos;
        
        // Ensure we're within bounds
        if (sampleIndex >= 0 && sampleIndex < data->sampleCount) {
            // Get sample value and normalize it
            float sampleValue = data->samples[sampleIndex];
            
            float lineHeight = (sampleValue) * (height / 2.0f);
            
            // Draw vertical line representing the sample
            SDL_RenderLine(rendererData->renderer, 
                          rect.x + x, centerY, 
                          rect.x + x, centerY - lineHeight);
        }
    }
    
    // Draw beat positions if available
    if (data->beat_positions && data->beat_count > 0) {
        // Set beat marker color (use beatColor if set, otherwise use a default bright color)
        if (data->beatColor.a > 0) {
            SDL_SetRenderDrawColor(rendererData->renderer, 
                                  data->beatColor.r, 
                                  data->beatColor.g, 
                                  data->beatColor.b, 
                                  data->beatColor.a);
        } else {
            // Default to a bright yellow if beatColor not set
            SDL_SetRenderDrawColor(rendererData->renderer, 255, 255, 0, 255);
        }
        
        // Draw a vertical line for each beat position
        for (int i = 0; i < data->beat_count; i++) {
            unsigned int beatPos = data->beat_positions[i];
            
            // Check if this beat is within the visible range
            if (beatPos >= startSample && beatPos < startSample + visibleSamples) {
                // Calculate x position for this beat
                float relativePos = (float)(beatPos - startSample) / visibleSamples;
                int x = rect.x + (relativePos * width);
                
                // Draw a vertical line for the beat
                SDL_RenderLine(rendererData->renderer, 
                              x, rect.y, 
                              x, rect.y + height);
            }
        }
    }
    
    // Draw selection
    if (data->selection_end > data->selection_start) {
        float start_x, end_x;

        // Calculate screen x for selection start, clamped to view
        if (data->selection_start <= startSample) {
            start_x = rect.x;
        } else if (data->selection_start >= startSample + visibleSamples) {
            start_x = rect.x + width;
        } else {
            start_x = rect.x + ((float)(data->selection_start - startSample) / visibleSamples) * width;
        }

        // Calculate screen x for selection end, clamped to view
        if (data->selection_end <= startSample) {
            end_x = rect.x;
        } else if (data->selection_end >= startSample + visibleSamples) {
            end_x = rect.x + width;
        } else {
            end_x = rect.x + ((float)(data->selection_end - startSample) / visibleSamples) * width;
        }

        SDL_SetRenderDrawBlendMode(rendererData->renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(rendererData->renderer, 0, 0, 0, 128); // Semi-transparent black

        if (start_x > rect.x) {
            SDL_FRect pre_selection_rect = {rect.x, rect.y, start_x - rect.x, height};
            SDL_RenderFillRect(rendererData->renderer, &pre_selection_rect);
        }

        if (end_x < rect.x + width) {
            SDL_FRect post_selection_rect = {end_x, rect.y, (rect.x + width) - end_x, height};
            SDL_RenderFillRect(rendererData->renderer, &post_selection_rect);
        }

        // Draw selection handles
        if (data->selection_start > 0 && data->selection_start >= startSample && data->selection_start < startSample + visibleSamples) {
            if (data->is_hovering_selection_start) {
                SDL_SetRenderDrawColor(rendererData->renderer, 100, 100, 255, 255);
            } else {
                SDL_SetRenderDrawColor(rendererData->renderer, 0, 160, 255, 255);
            }
            SDL_RenderLine(rendererData->renderer, start_x, rect.y, start_x, rect.y + height);
        }
        if (data->selection_end < (unsigned int)data->sampleCount && data->selection_end > startSample && data->selection_end <= startSample + visibleSamples) {
            if (data->is_hovering_selection_end) {
                SDL_SetRenderDrawColor(rendererData->renderer, 100, 100, 255, 255);
            } else {
                SDL_SetRenderDrawColor(rendererData->renderer, 0, 160, 255, 255);
            }
            SDL_RenderLine(rendererData->renderer, end_x, rect.y, end_x, rect.y + height);
        }
    }

    // Draw playback cursor if enabled and visible
    if (data->showPlaybackCursor && data->playbackPosition >= startSample && 
        data->playbackPosition < startSample + visibleSamples) {
        
        // Set cursor color (use cursorColor if set, otherwise use a default bright color)
        if (data->cursorColor.a > 0) {
            SDL_SetRenderDrawColor(rendererData->renderer, 
                                  data->cursorColor.r, 
                                  data->cursorColor.g, 
                                  data->cursorColor.b, 
                                  data->cursorColor.a);
        } else {
            // Default to a bright magenta/purple if cursorColor not set
            SDL_SetRenderDrawColor(rendererData->renderer, 196, 94, 206, 255);
        }
        
        // Calculate x position for the playback cursor
        float relativePos = (float)(data->playbackPosition - startSample) / visibleSamples;
        int x = rect.x + (relativePos * width);
        
        // Draw a thicker vertical line for the playback cursor
        SDL_RenderLine(rendererData->renderer, x, rect.y, x, rect.y + height);
        SDL_RenderLine(rendererData->renderer, x + 1, rect.y, x + 1, rect.y + height);
    }
}

/* Global for convenience. Even in 4K this is enough for smooth curves (low radius or rect size coupled with
 * no AA or low resolution might make it appear as jagged curves) */
static int NUM_CIRCLE_SEGMENTS = 16;

//all rendering is performed by a single SDL call, avoiding multiple RenderRect + plumbing choice for circles.
static void SDL_Clay_RenderFillRoundedRect(Clay_SDL3RendererData *rendererData, const SDL_FRect rect, const float cornerRadius, const Clay_Color _color) {
    const SDL_FColor color = { _color.r/255, _color.g/255, _color.b/255, _color.a/255 };

    int indexCount = 0, vertexCount = 0;

    const float minRadius = SDL_min(rect.w, rect.h) / 2.0f;
    const float clampedRadius = SDL_min(cornerRadius, minRadius);

    const int numCircleSegments = SDL_max(NUM_CIRCLE_SEGMENTS, (int) clampedRadius * 0.5f);

    int totalVertices = 4 + (4 * (numCircleSegments * 2)) + 2*4;
    int totalIndices = 6 + (4 * (numCircleSegments * 3)) + 6*4;

    SDL_Vertex *vertices = (SDL_Vertex *)malloc(totalVertices * sizeof(SDL_Vertex));
    if (!vertices) return;
    int *indices = (int *)malloc(totalIndices * sizeof(int));
    if (!indices) {
        free(vertices);
        return;
    }

    //define center rectangle
    vertices[vertexCount++] = (SDL_Vertex){ {rect.x + clampedRadius, rect.y + clampedRadius}, color, {0, 0} }; //0 center TL
    vertices[vertexCount++] = (SDL_Vertex){ {rect.x + rect.w - clampedRadius, rect.y + clampedRadius}, color, {1, 0} }; //1 center TR
    vertices[vertexCount++] = (SDL_Vertex){ {rect.x + rect.w - clampedRadius, rect.y + rect.h - clampedRadius}, color, {1, 1} }; //2 center BR
    vertices[vertexCount++] = (SDL_Vertex){ {rect.x + clampedRadius, rect.y + rect.h - clampedRadius}, color, {0, 1} }; //3 center BL

    indices[indexCount++] = 0;
    indices[indexCount++] = 1;
    indices[indexCount++] = 3;
    indices[indexCount++] = 1;
    indices[indexCount++] = 2;
    indices[indexCount++] = 3;

    //define rounded corners as triangle fans
    const float step = (SDL_PI_F/2) / numCircleSegments;
    for (int i = 0; i < numCircleSegments; i++) {
        const float angle1 = (float)i * step;
        const float angle2 = ((float)i + 1.0f) * step;

        for (int j = 0; j < 4; j++) {  // Iterate over four corners
            float cx, cy, signX, signY;

            switch (j) {
                case 0: cx = rect.x + clampedRadius; cy = rect.y + clampedRadius; signX = -1; signY = -1; break; // Top-left
                case 1: cx = rect.x + rect.w - clampedRadius; cy = rect.y + clampedRadius; signX = 1; signY = -1; break; // Top-right
                case 2: cx = rect.x + rect.w - clampedRadius; cy = rect.y + rect.h - clampedRadius; signX = 1; signY = 1; break; // Bottom-right
                case 3: cx = rect.x + clampedRadius; cy = rect.y + rect.h - clampedRadius; signX = -1; signY = 1; break; // Bottom-left
                default: return;
            }

            vertices[vertexCount++] = (SDL_Vertex){ {cx + SDL_cosf(angle1) * clampedRadius * signX, cy + SDL_sinf(angle1) * clampedRadius * signY}, color, {0, 0} };
            vertices[vertexCount++] = (SDL_Vertex){ {cx + SDL_cosf(angle2) * clampedRadius * signX, cy + SDL_sinf(angle2) * clampedRadius * signY}, color, {0, 0} };

            indices[indexCount++] = j;  // Connect to corresponding central rectangle vertex
            indices[indexCount++] = vertexCount - 2;
            indices[indexCount++] = vertexCount - 1;
        }
    }

    //Define edge rectangles
    // Top edge
    vertices[vertexCount++] = (SDL_Vertex){ {rect.x + clampedRadius, rect.y}, color, {0, 0} }; //TL
    vertices[vertexCount++] = (SDL_Vertex){ {rect.x + rect.w - clampedRadius, rect.y}, color, {1, 0} }; //TR

    indices[indexCount++] = 0;
    indices[indexCount++] = vertexCount - 2; //TL
    indices[indexCount++] = vertexCount - 1; //TR
    indices[indexCount++] = 1;
    indices[indexCount++] = 0;
    indices[indexCount++] = vertexCount - 1; //TR
    // Right edge
    vertices[vertexCount++] = (SDL_Vertex){ {rect.x + rect.w, rect.y + clampedRadius}, color, {1, 0} }; //RT
    vertices[vertexCount++] = (SDL_Vertex){ {rect.x + rect.w, rect.y + rect.h - clampedRadius}, color, {1, 1} }; //RB

    indices[indexCount++] = 1;
    indices[indexCount++] = vertexCount - 2; //RT
    indices[indexCount++] = vertexCount - 1; //RB
    indices[indexCount++] = 2;
    indices[indexCount++] = 1;
    indices[indexCount++] = vertexCount - 1; //RB
    // Bottom edge
    vertices[vertexCount++] = (SDL_Vertex){ {rect.x + rect.w - clampedRadius, rect.y + rect.h}, color, {1, 1} }; //BR
    vertices[vertexCount++] = (SDL_Vertex){ {rect.x + clampedRadius, rect.y + rect.h}, color, {0, 1} }; //BL

    indices[indexCount++] = 2;
    indices[indexCount++] = vertexCount - 2; //BR
    indices[indexCount++] = vertexCount - 1; //BL
    indices[indexCount++] = 3;
    indices[indexCount++] = 2;
    indices[indexCount++] = vertexCount - 1; //BL
    // Left edge
    vertices[vertexCount++] = (SDL_Vertex){ {rect.x, rect.y + rect.h - clampedRadius}, color, {0, 1} }; //LB
    vertices[vertexCount++] = (SDL_Vertex){ {rect.x, rect.y + clampedRadius}, color, {0, 0} }; //LT

    indices[indexCount++] = 3;
    indices[indexCount++] = vertexCount - 2; //LB
    indices[indexCount++] = vertexCount - 1; //LT
    indices[indexCount++] = 0;
    indices[indexCount++] = 3;
    indices[indexCount++] = vertexCount - 1; //LT

    // Render everything
    SDL_RenderGeometry(rendererData->renderer, NULL, vertices, vertexCount, indices, indexCount);

    free(vertices);
    free(indices);
}

static void SDL_Clay_RenderArc(Clay_SDL3RendererData *rendererData, const SDL_FPoint center, const float radius, const float startAngle, const float endAngle, const float thickness, const Clay_Color color) {
    SDL_SetRenderDrawColor(rendererData->renderer, color.r, color.g, color.b, color.a);

    const float radStart = startAngle * (SDL_PI_F / 180.0f);
    const float radEnd = endAngle * (SDL_PI_F / 180.0f);

    const int numCircleSegments = SDL_max(NUM_CIRCLE_SEGMENTS, (int)(radius * 1.5f)); //increase circle segments for larger circles, 1.5 is arbitrary.

    const float angleStep = (radEnd - radStart) / (float)numCircleSegments;
    const float thicknessStep = 0.4f; //arbitrary value to avoid overlapping lines. Changing THICKNESS_STEP or numCircleSegments might cause artifacts.

    for (float t = thicknessStep; t < thickness - thicknessStep; t += thicknessStep) {
        SDL_FPoint *points = (SDL_FPoint *)malloc((numCircleSegments + 1) * sizeof(SDL_FPoint));
        if (!points) continue;
        const float clampedRadius = SDL_max(radius - t, 1.0f);

        for (int i = 0; i <= numCircleSegments; i++) {
            const float angle = radStart + i * angleStep;
            points[i] = (SDL_FPoint){
                    SDL_roundf(center.x + SDL_cosf(angle) * clampedRadius),
                    SDL_roundf(center.y + SDL_sinf(angle) * clampedRadius) };
        }
        SDL_RenderLines(rendererData->renderer, points, numCircleSegments + 1);
        free(points);
    }
}

SDL_Rect currentClippingRectangle;

static void SDL_Clay_RenderClayCommands(Clay_SDL3RendererData *rendererData, Clay_RenderCommandArray *rcommands)
{
    for (int32_t i = 0; i < rcommands->length; i++) {
        Clay_RenderCommand *rcmd = Clay_RenderCommandArray_Get(rcommands, i);
        const Clay_BoundingBox bounding_box = rcmd->boundingBox;
        const SDL_FRect rect = { (int)bounding_box.x, (int)bounding_box.y, (int)bounding_box.width, (int)bounding_box.height };

        switch (rcmd->commandType) {
            case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
                Clay_RectangleRenderData *config = &rcmd->renderData.rectangle;
                SDL_SetRenderDrawBlendMode(rendererData->renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(rendererData->renderer, config->backgroundColor.r, config->backgroundColor.g, config->backgroundColor.b, config->backgroundColor.a);
                if (config->cornerRadius.topLeft > 0) {
                    SDL_Clay_RenderFillRoundedRect(rendererData, rect, config->cornerRadius.topLeft, config->backgroundColor);
                } else {
                    SDL_RenderFillRect(rendererData->renderer, &rect);
                }
            } break;
            case CLAY_RENDER_COMMAND_TYPE_TEXT: {
                Clay_TextRenderData *config = &rcmd->renderData.text;
                TTF_Font *font = rendererData->fonts[config->fontId];
                TTF_Text *text = TTF_CreateText(rendererData->textEngine, font, config->stringContents.chars, config->stringContents.length);
                TTF_SetTextColor(text, config->textColor.r, config->textColor.g, config->textColor.b, config->textColor.a);
                TTF_DrawRendererText(text, rect.x, rect.y);
                TTF_DestroyText(text);
            } break;
            case CLAY_RENDER_COMMAND_TYPE_BORDER: {
                Clay_BorderRenderData *config = &rcmd->renderData.border;

                const float minRadius = SDL_min(rect.w, rect.h) / 2.0f;
                const Clay_CornerRadius clampedRadii = {
                    .topLeft = SDL_min(config->cornerRadius.topLeft, minRadius),
                    .topRight = SDL_min(config->cornerRadius.topRight, minRadius),
                    .bottomLeft = SDL_min(config->cornerRadius.bottomLeft, minRadius),
                    .bottomRight = SDL_min(config->cornerRadius.bottomRight, minRadius)
                };
                //edges
                SDL_SetRenderDrawColor(rendererData->renderer, config->color.r, config->color.g, config->color.b, config->color.a);
                if (config->width.left > 0) {
                    const float starting_y = rect.y + clampedRadii.topLeft;
                    const float length = rect.h - clampedRadii.topLeft - clampedRadii.bottomLeft;
                    SDL_FRect line = { rect.x, starting_y, config->width.left, length };
                    SDL_RenderFillRect(rendererData->renderer, &line);
                }
                if (config->width.right > 0) {
                    const float starting_x = rect.x + rect.w - (float)config->width.right;
                    const float starting_y = rect.y + clampedRadii.topRight;
                    const float length = rect.h - clampedRadii.topRight - clampedRadii.bottomRight;
                    SDL_FRect line = { starting_x, starting_y, config->width.right, length };
                    SDL_RenderFillRect(rendererData->renderer, &line);
                }
                if (config->width.top > 0) {
                    const float starting_x = rect.x + clampedRadii.topLeft;
                    const float length = rect.w - clampedRadii.topLeft - clampedRadii.topRight;
                    SDL_FRect line = { starting_x, rect.y, length, config->width.top };
                    SDL_RenderFillRect(rendererData->renderer, &line);
                }
                if (config->width.bottom > 0) {
                    const float starting_x = rect.x + clampedRadii.bottomLeft;
                    const float starting_y = rect.y + rect.h - (float)config->width.bottom;
                    const float length = rect.w - clampedRadii.bottomLeft - clampedRadii.bottomRight;
                    SDL_FRect line = { starting_x, starting_y, length, config->width.bottom };
                    SDL_SetRenderDrawColor(rendererData->renderer, config->color.r, config->color.g, config->color.b, config->color.a);
                    SDL_RenderFillRect(rendererData->renderer, &line);
                }
                //corners
                if (config->cornerRadius.topLeft > 0) {
                    const float centerX = rect.x + clampedRadii.topLeft -1;
                    const float centerY = rect.y + clampedRadii.topLeft;
                    SDL_Clay_RenderArc(rendererData, (SDL_FPoint){centerX, centerY}, clampedRadii.topLeft,
                        180.0f, 270.0f, config->width.top, config->color);
                }
                if (config->cornerRadius.topRight > 0) {
                    const float centerX = rect.x + rect.w - clampedRadii.topRight -1;
                    const float centerY = rect.y + clampedRadii.topRight;
                    SDL_Clay_RenderArc(rendererData, (SDL_FPoint){centerX, centerY}, clampedRadii.topRight,
                        270.0f, 360.0f, config->width.top, config->color);
                }
                if (config->cornerRadius.bottomLeft > 0) {
                    const float centerX = rect.x + clampedRadii.bottomLeft -1;
                    const float centerY = rect.y + rect.h - clampedRadii.bottomLeft -1;
                    SDL_Clay_RenderArc(rendererData, (SDL_FPoint){centerX, centerY}, clampedRadii.bottomLeft,
                        90.0f, 180.0f, config->width.bottom, config->color);
                }
                if (config->cornerRadius.bottomRight > 0) {
                    const float centerX = rect.x + rect.w - clampedRadii.bottomRight -1; //TODO: why need to -1 in all calculations???
                    const float centerY = rect.y + rect.h - clampedRadii.bottomRight -1;
                    SDL_Clay_RenderArc(rendererData, (SDL_FPoint){centerX, centerY}, clampedRadii.bottomRight,
                        0.0f, 90.0f, config->width.bottom, config->color);
                }

            } break;
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START: {
                Clay_BoundingBox boundingBox = rcmd->boundingBox;
                currentClippingRectangle = (SDL_Rect) {
                        .x = boundingBox.x,
                        .y = boundingBox.y,
                        .w = boundingBox.width,
                        .h = boundingBox.height,
                };
                SDL_SetRenderClipRect(rendererData->renderer, &currentClippingRectangle);
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END: {
                SDL_SetRenderClipRect(rendererData->renderer, NULL);
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_IMAGE: {
                SDL_Surface *image = (SDL_Surface *)rcmd->renderData.image.imageData;
                SDL_Texture *texture = SDL_CreateTextureFromSurface(rendererData->renderer, image);
                const SDL_FRect dest = { rect.x, rect.y, rect.w, rect.h };

                SDL_RenderTexture(rendererData->renderer, texture, NULL, &dest);
                SDL_DestroyTexture(texture);
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_CUSTOM: {
                Clay_CustomRenderData *config = &rcmd->renderData.custom;
                
                // Check if this is a waveform
                WaveformData *waveformData = (WaveformData*)config->customData;
                if (waveformData) {
                    // Draw the waveform
                    DrawWaveform(rendererData, rect, waveformData);
                }
                break;
            }
            default:
                SDL_Log("Unknown render command type: %d", rcmd->commandType);
        }
    }
}

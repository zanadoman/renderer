#ifndef RENDERER_H
#define RENDERER_H

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>

typedef struct FFP_Renderer FFP_Renderer;
typedef struct SDL_GPUShader FFP_Shader;

extern FFP_Renderer *ffp_create_renderer(SDL_Window *window, float fov);

extern float ffp_get_renderer_fov(const FFP_Renderer *renderer);

extern void ffp_set_renderer_fov(FFP_Renderer *renderer, float fov);

extern bool ffp_renderer_draw(FFP_Renderer *renderer);

extern void ffp_destroy_renderer(FFP_Renderer *renderer);

#endif

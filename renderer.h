#ifndef RENDERER_H
#define RENDERER_H

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>

#define FFP_NEAR 0.1f
#define FFP_FAR  100.0f

typedef struct FFP_Renderer  FFP_Renderer;
typedef struct SDL_GPUShader FFP_Shader;

typedef struct {
    struct {
        float x;
        float y;
        float z;
    } pos;
    struct {
        float r;
        float g;
        float b;
        float a;
    } color;
} FFP_Vertex;

typedef struct {
    FFP_Vertex a;
    FFP_Vertex b;
    FFP_Vertex c;
} FFP_Triangle;

extern FFP_Renderer * ffp_create_renderer(SDL_Window *window, float fov);
extern float          ffp_get_renderer_fov(const FFP_Renderer *renderer);
extern void           ffp_set_renderer_fov(FFP_Renderer *renderer, float fov);
extern bool           ffp_renderer_upload_vertices(FFP_Renderer *renderer, const FFP_Triangle *triangle);
extern bool           ffp_renderer_draw(FFP_Renderer *renderer);
extern void           ffp_destroy_renderer(FFP_Renderer *renderer);

#endif

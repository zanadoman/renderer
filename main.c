#include "renderer.h"

#include <SDL3/SDL.h>

Sint32 main(void)
{
    SDL_Window         *window;
    FFP_Renderer       *renderer;
    const FFP_Quad  quad = {
        { { -0.5f,  0.5f, -1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        { {  0.5f,  0.5f, -1.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
        { { -0.5f, -0.5f, -1.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } },
        { {  0.5f, -0.5f, -1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } }
    };
    SDL_Event           event;
    bool                running = true;

    SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
    window   = SDL_CreateWindow("SDL_GPU", 800, 600, SDL_WINDOW_RESIZABLE);
    renderer = ffp_create_renderer(window, 60.0f / 180.0f * 3.14159f);
    ffp_renderer_upload_quad(renderer, &quad);

    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
                break;
            }
        }

        ffp_renderer_draw(renderer);
    }

    ffp_destroy_renderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

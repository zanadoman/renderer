#include "renderer.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>

struct FFP_Renderer {
    SDL_Window              *window;
    SDL_GPUDevice           *device;
    SDL_GPUShader           *vert_spv;
    SDL_GPUShader           *frag_spv;
    SDL_GPUGraphicsPipeline *pipeline;
    float                    fov;
    float                    matrix[16];
};

bool set_projection_matrix(FFP_Renderer *renderer);

FFP_Renderer *ffp_create_renderer(SDL_Window *window, float fov)
{
    FFP_Renderer                      *renderer;
    SDL_GPUColorTargetDescription      target;
    SDL_GPUGraphicsPipelineCreateInfo  info;

    renderer = SDL_calloc(sizeof(FFP_Renderer), 1);
    if (!renderer) return NULL;

    renderer->window = window;

    renderer->device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, NULL);
    if (!renderer->device) {
        SDL_free(renderer);
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "%s\n", SDL_GetError());
        return NULL;
    }

    if (!SDL_ClaimWindowForGPUDevice(renderer->device, renderer->window)) {
        SDL_DestroyGPUDevice(renderer->device);
        SDL_free(renderer);
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "%s\n", SDL_GetError());
        return NULL;
    }

    renderer->vert_spv = load_shader(renderer, "./triangle.vert.spv", 1);
    if (!renderer->vert_spv) SDL_LogError(SDL_LOG_CATEGORY_GPU, "%s\n", SDL_GetError());
    renderer->frag_spv = load_shader(renderer, "./triangle.frag.spv", 1);
    if (!renderer->frag_spv) SDL_LogError(SDL_LOG_CATEGORY_GPU, "%s\n", SDL_GetError());
    if (!renderer->vert_spv || !renderer->frag_spv) {
        SDL_ReleaseWindowFromGPUDevice(renderer->device, renderer->window);
        SDL_DestroyGPUDevice(renderer->device);
        SDL_free(renderer);
    }

    SDL_memset(&target, 0, sizeof(target));
    target.format = SDL_GetGPUSwapchainTextureFormat(renderer->device, renderer->window);
    SDL_memset(&info, 0, sizeof(info));
    info.vertex_shader                         = renderer->vert_spv;
    info.fragment_shader                       = renderer->frag_spv;
    info.target_info.color_target_descriptions = &target;
    info.target_info.num_color_targets         = 1;

    renderer->pipeline = SDL_CreateGPUGraphicsPipeline(renderer->device, &info);
    if (!renderer->pipeline) {
        SDL_ReleaseGPUShader(renderer->device, renderer->frag_spv);
        SDL_ReleaseGPUShader(renderer->device, renderer->vert_spv);
        SDL_ReleaseWindowFromGPUDevice(renderer->device, renderer->window);
        SDL_DestroyGPUDevice(renderer->device);
        SDL_free(renderer);
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "%s\n", SDL_GetError());
        return NULL;
    }

    renderer->fov = fov;

    return renderer;
}

float ffp_get_renderer_fov(const FFP_Renderer *renderer)
{
    return renderer->fov;
}

void ffp_set_renderer_fov(FFP_Renderer *renderer, float fov)
{
    renderer->fov = fov;
}

bool ffp_renderer_draw(FFP_Renderer *renderer)
{
    SDL_GPUCommandBuffer   *commands = SDL_AcquireGPUCommandBuffer(renderer->device);
    SDL_GPUColorTargetInfo  target;
    SDL_GPURenderPass      *pass;

    if (!set_projection_matrix(renderer)) return false;

    if (!commands) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "%s\n", SDL_GetError());
        return false;
    }

    SDL_memset(&target, 0, sizeof(target));
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(commands, renderer->window,
                                               &target.texture, NULL, NULL)) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "%s\n", SDL_GetError());
        return false;
    }

    pass = SDL_BeginGPURenderPass(commands, &target, 1, NULL);
    SDL_BindGPUGraphicsPipeline(pass, renderer->pipeline);
    SDL_PushGPUVertexUniformData(commands, 0, renderer->matrix, sizeof(renderer->matrix));
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(pass);

    if (!SDL_SubmitGPUCommandBuffer(commands)) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "%s\n", SDL_GetError());
        return false;
    }

    return true;
}

void ffp_destroy_renderer(FFP_Renderer *renderer)
{
    SDL_ReleaseGPUGraphicsPipeline(renderer->device, renderer->pipeline);
    SDL_ReleaseGPUShader(renderer->device, renderer->frag_spv);
    SDL_ReleaseGPUShader(renderer->device, renderer->vert_spv);
    SDL_ReleaseWindowFromGPUDevice(renderer->device, renderer->window);
    SDL_DestroyGPUDevice(renderer->device);
    SDL_free(renderer);
}

FFP_Shader *load_shader(FFP_Renderer *renderer, const char *path, Uint32 uniforms)
{
    SDL_GPUShaderCreateInfo  info;
    void                    *code = SDL_LoadFile(path, &info.code_size);
    SDL_GPUShader           *shader;

    if (!code) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "%s\n", SDL_GetError());
        return NULL;
    }

    if (strstr(path, ".vert"))      info.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    else if (strstr(path, ".frag")) info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    else {
        SDL_free(code);
        return NULL;
    }

    info.code                 = code;
    info.entrypoint           = "main";
    info.format               = SDL_GPU_SHADERFORMAT_SPIRV;
    info.num_samplers         = 0;
    info.num_storage_textures = 0;
    info.num_storage_buffers  = 0;
    info.num_uniform_buffers  = uniforms;
    shader = SDL_CreateGPUShader(renderer->device, &info);
    if (!shader) {
        SDL_free(code);
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "%s\n", SDL_GetError());
        return NULL;
    }

    SDL_free(code);
    return shader;
}

bool set_projection_matrix(FFP_Renderer *renderer)
{
    int32_t width, height;
    float S = 1.0f / SDL_tanf(renderer->fov / 2.0f);
    if (!SDL_GetWindowSize(renderer->window, &width, &height)) return false;
    renderer->matrix[0]  = S / ((float)width / (float)height);
    renderer->matrix[1]  = 0.0f;
    renderer->matrix[2]  = 0.0f;
    renderer->matrix[3]  = 0.0f;
    renderer->matrix[4]  = 0.0f;
    renderer->matrix[5]  = S;
    renderer->matrix[6]  = 0.0f;
    renderer->matrix[7]  = 0.0f;
    renderer->matrix[8]  = 0.0f;
    renderer->matrix[9]  = 0.0f;
    renderer->matrix[10] = (0.1f + 100.0f) / (0.1f - 100.0f);
    renderer->matrix[11] = 2.0f * 0.1f * 100.0f / (0.1f - 100.0f);
    renderer->matrix[12] = 0.0f;
    renderer->matrix[13] = 0.0f;
    renderer->matrix[14] = -1.0f;
    renderer->matrix[15] = 0.0f;
    return true;
}

#include "renderer.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>

struct FFP_Renderer {
    SDL_Window              *window;
    SDL_GPUDevice           *device;
    SDL_GPUShader           *vert_shader;
    SDL_GPUShader           *frag_shader;
    SDL_GPUGraphicsPipeline *pipeline;
    float                    fov;
    float                    matrix[16];
};

static FFP_Shader * load_shader(SDL_GPUDevice *device, const char *path, Uint32 uniforms);
static bool         set_projection_matrix(FFP_Renderer *renderer);

FFP_Renderer * ffp_create_renderer(SDL_Window *window, float fov)
{
    FFP_Renderer                      *renderer      = SDL_calloc(sizeof(FFP_Renderer), 1);
    SDL_GPUColorTargetDescription      target_desc;
    SDL_GPUGraphicsPipelineCreateInfo  pipeline_info;

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

    renderer->vert_shader = load_shader(renderer->device, "./shader.vert.spv", 1);
    if (!renderer->vert_shader) {
        SDL_ReleaseWindowFromGPUDevice(renderer->device, renderer->window);
        SDL_DestroyGPUDevice(renderer->device);
        SDL_free(renderer);
        return NULL;
    }

    renderer->frag_shader = load_shader(renderer->device, "./shader.frag.spv", 1);
    if (!renderer->frag_shader) {
        SDL_ReleaseGPUShader(renderer->device, renderer->vert_shader);
        SDL_ReleaseWindowFromGPUDevice(renderer->device, renderer->window);
        SDL_DestroyGPUDevice(renderer->device);
        SDL_free(renderer);
        return NULL;
    }

    SDL_memset(&target_desc, 0, sizeof(target_desc));
    target_desc.format = SDL_GetGPUSwapchainTextureFormat(renderer->device, renderer->window);

    SDL_memset(&pipeline_info, 0, sizeof(pipeline_info));
    pipeline_info.vertex_shader                         = renderer->vert_shader;
    pipeline_info.fragment_shader                       = renderer->frag_shader;
    pipeline_info.target_info.color_target_descriptions = &target_desc;
    pipeline_info.target_info.num_color_targets         = 1;

    renderer->pipeline = SDL_CreateGPUGraphicsPipeline(renderer->device, &pipeline_info);
    if (!renderer->pipeline) {
        SDL_ReleaseGPUShader(renderer->device, renderer->frag_shader);
        SDL_ReleaseGPUShader(renderer->device, renderer->vert_shader);
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
    SDL_GPUCommandBuffer   *command_buffer = SDL_AcquireGPUCommandBuffer(renderer->device);
    SDL_GPUColorTargetInfo  target_info;
    SDL_GPURenderPass      *render_pass;

    if (!command_buffer) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "%s\n", SDL_GetError());
        return false;
    }

    if (!set_projection_matrix(renderer)) {
        SDL_CancelGPUCommandBuffer(command_buffer);
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "%s\n", SDL_GetError());
        return false;
    }

    SDL_memset(&target_info, 0, sizeof(target_info));
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(command_buffer, renderer->window, &target_info.texture, NULL, NULL)) {
        SDL_CancelGPUCommandBuffer(command_buffer);
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "%s\n", SDL_GetError());
        return false;
    }

    render_pass = SDL_BeginGPURenderPass(command_buffer, &target_info, 1, NULL);
    SDL_BindGPUGraphicsPipeline(render_pass, renderer->pipeline);
    SDL_PushGPUVertexUniformData(command_buffer, 0, renderer->matrix, sizeof(renderer->matrix));
    SDL_DrawGPUPrimitives(render_pass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(render_pass);

    if (!SDL_SubmitGPUCommandBuffer(command_buffer)) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "%s\n", SDL_GetError());
        return false;
    }

    return true;
}

void ffp_destroy_renderer(FFP_Renderer *renderer)
{
    SDL_ReleaseGPUGraphicsPipeline(renderer->device, renderer->pipeline);
    SDL_ReleaseGPUShader(renderer->device, renderer->frag_shader);
    SDL_ReleaseGPUShader(renderer->device, renderer->vert_shader);
    SDL_ReleaseWindowFromGPUDevice(renderer->device, renderer->window);
    SDL_DestroyGPUDevice(renderer->device);
    SDL_free(renderer);
}

FFP_Shader * load_shader(SDL_GPUDevice *device, const char *path, Uint32 uniforms)
{
    SDL_GPUShaderCreateInfo  info;
    void                    *code;
    SDL_GPUShader           *shader;

    SDL_memset(&info, 0, sizeof(info));

    code = SDL_LoadFile(path, &info.code_size);
    if (!code) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "%s\n", SDL_GetError());
        return NULL;
    }

    if      (strstr(path, ".vert.spv")) info.stage = SDL_GPU_SHADERSTAGE_VERTEX;
    else if (strstr(path, ".frag.spv")) info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    else {
        SDL_free(code);
        return NULL;
    }

    info.code                = code;
    info.entrypoint          = "main";
    info.format              = SDL_GPU_SHADERFORMAT_SPIRV;
    info.num_uniform_buffers = uniforms;

    shader = SDL_CreateGPUShader(device, &info);
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
    Sint32 width, height;
    float focal = 1.0f / SDL_tanf(renderer->fov / 2.0f);
    if (!SDL_GetWindowSize(renderer->window, &width, &height)) return false;
    renderer->matrix[0]  = focal / ((float)width / (float)height);
    renderer->matrix[1]  = 0.0f;
    renderer->matrix[2]  = 0.0f;
    renderer->matrix[3]  = 0.0f;
    renderer->matrix[4]  = 0.0f;
    renderer->matrix[5]  = focal;
    renderer->matrix[6]  = 0.0f;
    renderer->matrix[7]  = 0.0f;
    renderer->matrix[8]  = 0.0f;
    renderer->matrix[9]  = 0.0f;
    renderer->matrix[10] = (FFP_FAR + FFP_NEAR) / (FFP_NEAR - FFP_FAR);
    renderer->matrix[11] = 2.0f * FFP_FAR * FFP_NEAR / (FFP_NEAR - FFP_FAR);
    renderer->matrix[12] = 0.0f;
    renderer->matrix[13] = 0.0f;
    renderer->matrix[14] = -1.0f;
    renderer->matrix[15] = 0.0f;
    return true;
}

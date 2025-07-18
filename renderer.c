#include "renderer.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>

#include <stddef.h>

struct FFP_Renderer {
    SDL_Window              *window;
    SDL_GPUDevice           *device;
    SDL_GPUBufferBinding     vertbuf;
    SDL_GPUBufferBinding     indbuf;
    SDL_GPUTransferBuffer   *transbuf;
    SDL_GPUGraphicsPipeline *pipeline;
    float                    fov;
    float                    matrix[16];
};

static FFP_Shader * load_shader(SDL_GPUDevice *device, const char *path, Uint32 uniforms);
static bool         set_projection_matrix(FFP_Renderer *renderer);

FFP_Renderer * ffp_create_renderer(SDL_Window *window, float fov)
{
    FFP_Renderer                      *renderer         = SDL_calloc(1, sizeof(FFP_Renderer));
    SDL_GPUBufferCreateInfo            vertbuf_info;
    SDL_GPUBufferCreateInfo            indbuf_info;
    SDL_GPUTransferBufferCreateInfo    transbuf_info;
    SDL_GPUVertexBufferDescription     vertbuf_desc;
    SDL_GPUVertexAttribute             vertbuf_attrs[2];
    SDL_GPUColorTargetDescription      target_desc;
    SDL_GPUGraphicsPipelineCreateInfo  pipeline_info;

    if (!renderer) return NULL;

    SDL_memset(&vertbuf_info,  0, sizeof(vertbuf_info));
    SDL_memset(&indbuf_info,   0, sizeof(indbuf_info));
    SDL_memset(&transbuf_info, 0, sizeof(transbuf_info));
    SDL_memset(&vertbuf_desc,  0, sizeof(vertbuf_desc));
    SDL_memset( vertbuf_attrs, 0, sizeof(vertbuf_attrs));
    SDL_memset(&target_desc,   0, sizeof(target_desc));
    SDL_memset(&pipeline_info, 0, sizeof(pipeline_info));

    renderer->window = window;

    renderer->device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, NULL);
    if (!renderer->device) {
        ffp_destroy_renderer(renderer);
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "%s\n", SDL_GetError());
        return NULL;
    }

    if (!SDL_ClaimWindowForGPUDevice(renderer->device, renderer->window)) {
        ffp_destroy_renderer(renderer);
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "%s\n", SDL_GetError());
        return NULL;
    }

    vertbuf_info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    vertbuf_info.size = sizeof(FFP_Quad);
    renderer->vertbuf.buffer = SDL_CreateGPUBuffer(renderer->device, &vertbuf_info);
    if (!renderer->vertbuf.buffer) {
        ffp_destroy_renderer(renderer);
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "%s\n", SDL_GetError());
        return NULL;
    }

    indbuf_info.usage = SDL_GPU_BUFFERUSAGE_INDEX;
    indbuf_info.size  = 6 * sizeof(Uint16);
    renderer->indbuf.buffer = SDL_CreateGPUBuffer(renderer->device, &indbuf_info);
    if (!renderer->indbuf.buffer) {
        ffp_destroy_renderer(renderer);
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "%s\n", SDL_GetError());
        return NULL;
    }

    transbuf_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transbuf_info.size = vertbuf_info.size + indbuf_info.size;
    renderer->transbuf = SDL_CreateGPUTransferBuffer(renderer->device, &transbuf_info);
    if (!renderer->transbuf) {
        ffp_destroy_renderer(renderer);
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "%s\n", SDL_GetError());
        return NULL;
    }

    vertbuf_desc.pitch        = sizeof(FFP_VertexRGBA);
    vertbuf_attrs[0].location = 0;
    vertbuf_attrs[0].format   = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    vertbuf_attrs[0].offset   = offsetof(FFP_VertexRGBA, position);
    vertbuf_attrs[1].location = 1;
    vertbuf_attrs[1].format   = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    vertbuf_attrs[1].offset   = offsetof(FFP_VertexRGBA, color);
    target_desc.format        = SDL_GetGPUSwapchainTextureFormat(renderer->device, renderer->window);
    pipeline_info.vertex_shader                                 = load_shader(renderer->device, "./shader.vert.spv", 1);
    pipeline_info.fragment_shader                               = load_shader(renderer->device, "./shader.frag.spv", 0);
    pipeline_info.vertex_input_state.vertex_buffer_descriptions = &vertbuf_desc;
    pipeline_info.vertex_input_state.num_vertex_buffers         = 1;
    pipeline_info.vertex_input_state.vertex_attributes          = vertbuf_attrs;
    pipeline_info.vertex_input_state.num_vertex_attributes      = sizeof(vertbuf_attrs) / sizeof(SDL_GPUVertexAttribute);
    pipeline_info.target_info.color_target_descriptions         = &target_desc;
    pipeline_info.target_info.num_color_targets                 = 1;
    if (!pipeline_info.fragment_shader || !pipeline_info.fragment_shader) {
        SDL_ReleaseGPUShader(renderer->device, pipeline_info.fragment_shader);
        SDL_ReleaseGPUShader(renderer->device, pipeline_info.vertex_shader);
        ffp_destroy_renderer(renderer);
        return NULL;
    }

    renderer->pipeline = SDL_CreateGPUGraphicsPipeline(renderer->device, &pipeline_info);
    SDL_ReleaseGPUShader(renderer->device, pipeline_info.fragment_shader);
    SDL_ReleaseGPUShader(renderer->device, pipeline_info.vertex_shader);
    if (!renderer->pipeline) {
        ffp_destroy_renderer(renderer);
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

bool ffp_renderer_upload_quad(FFP_Renderer *renderer, const FFP_Quad *quad)
{
    Uint8                         *transmem     = NULL;
    Uint16                         indices[6]   = { 0, 1, 2, 2, 1, 3 };
    SDL_GPUCommandBuffer          *cmdbuf       = NULL;
    SDL_GPUTransferBufferLocation  transbuf_loc;
    SDL_GPUBufferRegion            dstbuf_reg;
    SDL_GPUCopyPass               *copy_pass    = NULL;

    SDL_memset(&transbuf_loc, 0, sizeof(transbuf_loc));
    SDL_memset(&dstbuf_reg,   0, sizeof(dstbuf_reg));

    transmem = SDL_MapGPUTransferBuffer(renderer->device, renderer->transbuf, false);
    if (!transmem) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "%s\n", SDL_GetError());
        return false;
    }

    SDL_memcpy(transmem,                        quad, sizeof(FFP_Quad));
    SDL_memcpy(transmem + sizeof(FFP_Quad), indices,  sizeof(indices));
    SDL_UnmapGPUTransferBuffer(renderer->device, renderer->transbuf);

    cmdbuf = SDL_AcquireGPUCommandBuffer(renderer->device);
    if (!cmdbuf) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "%s\n", SDL_GetError());
        return false;
    }

    copy_pass = SDL_BeginGPUCopyPass(cmdbuf);
    transbuf_loc.transfer_buffer = renderer->transbuf;
    dstbuf_reg.buffer = renderer->vertbuf.buffer;
    dstbuf_reg.size   = sizeof(FFP_Quad);
    SDL_UploadToGPUBuffer(copy_pass, &transbuf_loc, &dstbuf_reg, false);
    transbuf_loc.offset += dstbuf_reg.size;
    dstbuf_reg.buffer    = renderer->indbuf.buffer;
    dstbuf_reg.size      = sizeof(indices);
    SDL_UploadToGPUBuffer(copy_pass, &transbuf_loc, &dstbuf_reg, false);
    SDL_EndGPUCopyPass(copy_pass);

    if (!SDL_SubmitGPUCommandBuffer(cmdbuf)) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "%s\n", SDL_GetError());
        return false;
    }

    return true;
}

SDL_GPUTexture * ffp_renderer_upload_surface(FFP_Renderer *renderer, const SDL_Surface *surface)
{
    SDL_GPUTextureCreateInfo         texture_info;
    SDL_GPUTextureRegion             texture_reg;
    SDL_GPUTransferBufferCreateInfo  transbuf_info;
    SDL_GPUTextureTransferInfo       transfer_info;
    void                            *transmem      = NULL;
    SDL_GPUCommandBuffer            *cmdbuf        = NULL;
    SDL_GPUCopyPass                 *copy_pass     = NULL;

    SDL_memset(&texture_info,  0, sizeof(texture_info));
    SDL_memset(&texture_reg,   0, sizeof(texture_reg));
    SDL_memset(&transbuf_info, 0, sizeof(transbuf_info));
    SDL_memset(&transfer_info, 0, sizeof(transfer_info));

    texture_info.format               = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    texture_info.usage                = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    texture_info.width                = (Uint32)surface->w;
    texture_info.height               = (Uint32)surface->h;
    texture_info.layer_count_or_depth = 1;
    texture_info.num_levels           = 1;
    texture_reg.texture               = SDL_CreateGPUTexture(renderer->device, &texture_info);
    texture_reg.w                     = texture_info.width;
    texture_reg.h                     = texture_info.height;
    texture_reg.d                     = texture_info.layer_count_or_depth;
    if (!texture_reg.texture) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "%s\n", SDL_GetError());
        return NULL;
    }

    transbuf_info.usage           = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transbuf_info.size            = texture_info.width * texture_info.height * 4;
    transfer_info.transfer_buffer = SDL_CreateGPUTransferBuffer(renderer->device, &transbuf_info);
    if (!transfer_info.transfer_buffer) {
        SDL_ReleaseGPUTexture(renderer->device, texture_reg.texture);
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "%s\n", SDL_GetError());
        return NULL;
    }

    transmem = SDL_MapGPUTransferBuffer(renderer->device, transfer_info.transfer_buffer, false);
    if (!transmem) {
        SDL_ReleaseGPUTransferBuffer(renderer->device, transfer_info.transfer_buffer);
        SDL_ReleaseGPUTexture(renderer->device, texture_reg.texture);
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "%s\n", SDL_GetError());
        return NULL;
    }

    SDL_memcpy(transmem, surface->pixels, transbuf_info.size);
    SDL_UnmapGPUTransferBuffer(renderer->device, transfer_info.transfer_buffer);

    cmdbuf = SDL_AcquireGPUCommandBuffer(renderer->device);
    if (!cmdbuf) {
        SDL_ReleaseGPUTransferBuffer(renderer->device, transfer_info.transfer_buffer);
        SDL_ReleaseGPUTexture(renderer->device, texture_reg.texture);
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "%s\n", SDL_GetError());
        return NULL;
    }

    copy_pass = SDL_BeginGPUCopyPass(cmdbuf);
    SDL_UploadToGPUTexture(copy_pass, &transfer_info, &texture_reg, false);
    SDL_ReleaseGPUTransferBuffer(renderer->device, transfer_info.transfer_buffer);
    SDL_EndGPUCopyPass(copy_pass);

    if (!SDL_SubmitGPUCommandBuffer(cmdbuf)) {
        SDL_ReleaseGPUTexture(renderer->device, texture_reg.texture);
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "%s\n", SDL_GetError());
        return NULL;
    }

    return texture_reg.texture;
}

bool ffp_renderer_draw(FFP_Renderer *renderer)
{
    SDL_GPUCommandBuffer   *cmdbuf      = SDL_AcquireGPUCommandBuffer(renderer->device);
    SDL_GPUColorTargetInfo  target_info;
    SDL_GPURenderPass      *render_pass = NULL;

    if (!cmdbuf) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "%s\n", SDL_GetError());
        return false;
    }

    SDL_memset(&target_info, 0, sizeof(target_info));

    if (!set_projection_matrix(renderer)) {
        SDL_CancelGPUCommandBuffer(cmdbuf);
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "%s\n", SDL_GetError());
        return false;
    }

    target_info.clear_color.r = 0.25f;
    target_info.clear_color.g = 0.25f;
    target_info.clear_color.b = 0.25f;
    target_info.clear_color.a = 1.0f;
    target_info.load_op = SDL_GPU_LOADOP_CLEAR;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmdbuf, renderer->window, &target_info.texture, NULL, NULL)) {
        SDL_CancelGPUCommandBuffer(cmdbuf);
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "%s\n", SDL_GetError());
        return false;
    }

    render_pass = SDL_BeginGPURenderPass(cmdbuf, &target_info, 1, NULL);
    SDL_BindGPUGraphicsPipeline(render_pass, renderer->pipeline);
    SDL_BindGPUVertexBuffers(render_pass, 0, &renderer->vertbuf, 1);
    SDL_BindGPUIndexBuffer(render_pass, &renderer->indbuf, SDL_GPU_INDEXELEMENTSIZE_16BIT);
    SDL_PushGPUVertexUniformData(cmdbuf, 0, renderer->matrix, sizeof(renderer->matrix));
    SDL_DrawGPUIndexedPrimitives(render_pass, 6, 1, 0, 0, 0);
    SDL_EndGPURenderPass(render_pass);

    if (!SDL_SubmitGPUCommandBuffer(cmdbuf)) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "%s\n", SDL_GetError());
        return false;
    }

    return true;
}

void ffp_destroy_renderer(FFP_Renderer *renderer)
{
    SDL_ReleaseGPUGraphicsPipeline(renderer->device, renderer->pipeline);
    SDL_ReleaseGPUTransferBuffer(renderer->device, renderer->transbuf);
    SDL_ReleaseGPUBuffer(renderer->device, renderer->indbuf.buffer);
    SDL_ReleaseGPUBuffer(renderer->device, renderer->vertbuf.buffer);
    SDL_ReleaseWindowFromGPUDevice(renderer->device, renderer->window);
    SDL_DestroyGPUDevice(renderer->device);
    SDL_free(renderer);
}

FFP_Shader * load_shader(SDL_GPUDevice *device, const char *path, Uint32 uniforms)
{
    SDL_GPUShaderCreateInfo  info;
    void                    *code   = NULL;
    SDL_GPUShader           *shader = NULL;

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

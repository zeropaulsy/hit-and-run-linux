//=============================================================================
// Linux port: Vulkan renderer pipeline (see vkpipeline.hpp; the state handling follows glpipeline.cpp)
//=============================================================================

#include <pddi/vk/vkpipeline.hpp>
#include <pddi/vk/vktex.hpp>
#include <pddi/gl/glcustom.hpp>

#include <pddi/pdditype.hpp>
#include <shar_glslang.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int kSamplerBindings = 4;     // bindings 1..4: the most samplers a program declares

static void Identity(float* m)
{
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

pvkPipeline::pvkPipeline()
{
    memset(this, 0, sizeof(*this));     // plain data and handles only
    frameWidth = frameHeight = 1.0f;
    Identity(projection);
    Identity(modelView);
    for(int i = 0; i < pvkMaxLights; i++)
    {
        lights[i].atten[0] = 1.0f;
        lights[i].range = 1000000.0f;
    }
    ambient[0] = ambient[1] = ambient[2] = 0.2f;
    ambient[3] = 1.0f;
    fogEnd = 1.0f;
    material.alphaCompare = -1;
    material.custom = -1;
    current.normal[2] = 1.0f;
    current.colour[0] = current.colour[1] = current.colour[2] = current.colour[3] = 255;
    filterMode = PDDI_FILTER_BILINEAR;

    // GL's initial state
    state.depthTest = false;
    state.depthWrite = true;
    state.depthCompare = PDDI_COMPARE_LESS;
    state.stencilCompare = PDDI_COMPARE_ALWAYS;
    state.stencilMask = state.stencilWriteMask = 0xff;
    state.colourWrite = 0xf;
    state.blend = PDDI_BLEND_NONE;
    state.depthMax = 1.0f;
}

pvkPipeline::~pvkPipeline()
{
    free(streamVerts);
}

//-----------------------------------------------------------------------------
// Programs
//-----------------------------------------------------------------------------

static char* Join(const char** parts)
{
    size_t size = 1;
    for(int i = 0; parts[i]; i++)
        size += strlen(parts[i]);
    char* s = (char*)malloc(size);
    if(!s)
        return NULL;
    s[0] = 0;
    for(int i = 0; parts[i]; i++)
        strcat(s, parts[i]);
    return s;
}

bool pvkPipeline::BuildProgram(Program& p, const char* name, const char* header, const char* fragmentBody, bool custom,
                               const char* fragmentHeader, const char* vertexBody)
{
    const char* vertexParts[] = { pglVertexDeclarations, vertexBody ? "#line 1\n" : "", vertexBody ? vertexBody : "",
                                  vertexBody ? "\n" : "", pglVertexMain, NULL };
    const char* fragmentParts[] = { pglFragmentPrelude, custom ? "#line 1\n" : "", fragmentBody,
                                    custom ? pglCustomFragmentMain : "", NULL };
    const char* headerParts[] = { header, "#define PVK 1\n", NULL };
    const char* fragmentHeaderParts[] = { fragmentHeader ? fragmentHeader : header, "#define PVK 1\n", NULL };
    char* vertex = Join(vertexParts);
    char* fragment = Join(fragmentParts);
    char* vh = Join(headerParts);
    char* fh = Join(fragmentHeaderParts);
    shar_glsl_program compiled;
    bool ok = vertex && fragment && vh && fh && shar_glsl_compile(name, vh, vertex, fh, fragment, &compiled);
    free(vertex);
    free(fragment);
    free(vh);
    free(fh);
    if(!ok)
    {
        fprintf(stderr, "PDDI: %s program does not build:\n%s\n", name, compiled.log ? compiled.log : "");
        shar_glsl_free(&compiled);
        return false;
    }

    memset(&p, 0, sizeof(p));
    p.vertex = pvkCreateShaderModule(compiled.vertex, compiled.vertexWords);
    p.fragment = pvkCreateShaderModule(compiled.fragment, compiled.fragmentWords);
    p.blockSize = compiled.blockSize;
    for(int u = 0; u < U_COUNT; u++)
        p.offset[u] = -1;
    for(int s = 0; s < SAMPLER_COUNT; s++)
        p.binding[s] = -1;
    for(int i = 0; i < compiled.uniformCount; i++)
    {
        const shar_glsl_uniform& cu = compiled.uniforms[i];
        if(strcmp(cu.name, "uTexture") == 0)
            p.binding[SAMPLER_TEXTURE] = cu.binding;
        else if(strcmp(cu.name, "uReflection") == 0)
            p.binding[SAMPLER_REFLECTION] = cu.binding;
        else if(strcmp(cu.name, "uScreen") == 0)
            p.binding[SAMPLER_SCREEN] = cu.binding;
        if(cu.offset < 0)
            continue;
        for(int u = 0; u < U_COUNT; u++)
        {
            if(strcmp(cu.name, pglUniformNames[u]) == 0)
            {
                p.offset[u] = cu.offset;
                p.stride[u] = cu.arraySize > 1 ? cu.arrayStride : 0;
                p.glType[u] = cu.glType;
            }
        }
    }
    for(int s = 0; s < SAMPLER_COUNT; s++)
    {
        if(p.binding[s] > kSamplerBindings)
        {
            fprintf(stderr, "PDDI: %s: sampler binding %d past the layout's\n", name, p.binding[s]);
            ok = false;
        }
    }
    shar_glsl_free(&compiled);
    if(!ok || !p.vertex || !p.fragment)
    {
        ReleaseProgram(p);
        return false;
    }
    p.block = (unsigned char*)calloc(p.blockSize > 0 ? p.blockSize : 16, 1);
    p.blockChanged = true;
    for(int g = 0; g < GROUP_COUNT; g++)
        p.uploaded[g] = version[g] - 1;
    p.timeFrame = frameCount - 1;
    p.built = true;
    return true;
}

void pvkPipeline::ReleaseProgram(Program& p)
{
    if(p.vertex)
        vkDestroyShaderModule(g_pvk->device, p.vertex, NULL);
    if(p.fragment)
        vkDestroyShaderModule(g_pvk->device, p.fragment, NULL);
    free(p.block);
    memset(&p, 0, sizeof(p));
}

bool pvkPipeline::Init()
{
    if(ready)
        return true;
    for(int i = 0; i < PGL_PROGRAM_COUNT; i++)
    {
        if(!BuildProgram(programs[i], pglProgramSources[i].name, pglProgramSources[i].header, pglFragmentMain, false,
                         NULL, NULL))
            return false;
    }

    VkDescriptorSetLayoutBinding bindings[1 + kSamplerBindings];
    memset(bindings, 0, sizeof(bindings));
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    for(int i = 1; i <= kSamplerBindings; i++)
    {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo set = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    set.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    set.bindingCount = 1 + kSamplerBindings;
    set.pBindings = bindings;
    if(vkCreateDescriptorSetLayout(g_pvk->device, &set, NULL, &setLayout) != VK_SUCCESS)
        return false;
    VkPipelineLayoutCreateInfo pl = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &setLayout;
    if(vkCreatePipelineLayout(g_pvk->device, &pl, NULL, &layout) != VK_SUCCESS)
        return false;
    VkPipelineCacheCreateInfo pc = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
    vkCreatePipelineCache(g_pvk->device, &pc, NULL, &cache);

    pipelineCapacity = 1024;
    pipelines = (CachedPipeline*)calloc(pipelineCapacity, sizeof(CachedPipeline));

    // a white texel for samplers without a texture (shaders test uTextured, but the descriptor must be valid)
    if(!g_pvk->CreateImage(1, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT,
                           VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT, &white))
        return false;
    VkCommandBuffer cmd = g_pvk->UploadCmd();
    g_pvk->Transition(cmd, white, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkClearColorValue one;
    one.float32[0] = one.float32[1] = one.float32[2] = one.float32[3] = 1.0f;
    VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdClearColorImage(cmd, white.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &one, 1, &range);
    g_pvk->Transition(cmd, white, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    whiteSampler = GetSampler(PDDI_FILTER_NONE, PDDI_UV_CLAMP, false);

    ready = true;
    return true;
}

void pvkPipeline::Shutdown()
{
    if(!g_pvk || !ready)
        return;
    g_pvk->WaitIdle();
    for(int i = 0; i < pipelineCapacity; i++)
        if(pipelines[i].pipeline)
            vkDestroyPipeline(g_pvk->device, pipelines[i].pipeline, NULL);
    free(pipelines);
    pipelines = NULL;
    for(int i = 0; i < samplerCount; i++)
        vkDestroySampler(g_pvk->device, samplers[i].sampler, NULL);
    samplerCount = 0;
    for(int i = 0; i < PGL_PROGRAM_COUNT + customCount; i++)
        ReleaseProgram(programs[i]);
    g_pvk->DestroyNow(white);
    g_pvk->DestroyNow(screen);
    vkDestroyPipelineCache(g_pvk->device, cache, NULL);
    vkDestroyPipelineLayout(g_pvk->device, layout, NULL);
    vkDestroyDescriptorSetLayout(g_pvk->device, setLayout, NULL);
    ready = false;
}

static int CullDirective(const char* source)
{
    const char* p = strstr(source, "#pragma shar_cull");
    if(!p)
        return PVK_CULL_GAME;
    p += strlen("#pragma shar_cull");
    while(*p == ' ' || *p == '\t')
        p++;
    return strncmp(p, "none", 4) == 0 ? PVK_CULL_NONE : PVK_CULL_GAME;
}

int pvkPipeline::BuildCustomProgram(const char* name, const char* source)
{
    if(!ready || customCount == pvkMaxCustomPrograms)
        return -1;
    Program& p = programs[PGL_PROGRAM_COUNT + customCount];
    bool vertexPart = strstr(source, "MaterialVertex") != NULL;
    char vertexHeader[160], fragmentHeader[160];
    snprintf(vertexHeader, sizeof(vertexHeader),
             "#version 330 core\n#define REFLECTION 1\n#define REFRACTION 0\n#define CUSTOM 1\n#define MATERIAL_VERTEX %d\n#define VERTEX_STAGE 1\n",
             vertexPart ? 1 : 0);
    snprintf(fragmentHeader, sizeof(fragmentHeader),
             "#version 330 core\n#define REFLECTION 1\n#define REFRACTION 0\n#define CUSTOM 1\n#define FRAGMENT_STAGE 1\n");
    if(!BuildProgram(p, name, vertexHeader, source, true, fragmentHeader, vertexPart ? source : NULL))
        return -1;
    p.cull = CullDirective(source);
    return customCount++;
}

int pvkPipeline::CustomCull(int slot) const
{
    return slot >= 0 && slot < customCount ? programs[PGL_PROGRAM_COUNT + slot].cull : PVK_CULL_GAME;
}

// with the pipelines made from them (retired: frames in flight may use them)
void pvkPipeline::ReleaseCustomPrograms()
{
    if(!customCount)
        return;
    for(int i = 0; i < pipelineCapacity; i++)
    {
        if(pipelines[i].pipeline && (int)(pipelines[i].key & 63) >= PGL_PROGRAM_COUNT)
        {
            g_pvk->Retire(pipelines[i].pipeline);
            pipelines[i].pipeline = VK_NULL_HANDLE;
            pipelines[i].key = 0;
            pipelineCount--;
        }
    }
    // the table is open-addressed: rehash what is left so lookups still find it
    CachedPipeline* old = pipelines;
    pipelines = (CachedPipeline*)calloc(pipelineCapacity, sizeof(CachedPipeline));
    for(int i = 0; i < pipelineCapacity; i++)
    {
        if(!old[i].pipeline)
            continue;
        unsigned h = (old[i].key * 2654435761u) % pipelineCapacity;
        while(pipelines[h].pipeline)
            h = (h + 1) % pipelineCapacity;
        pipelines[h] = old[i];
    }
    free(old);
    for(int i = 0; i < customCount; i++)
        ReleaseProgram(programs[PGL_PROGRAM_COUNT + i]);
    customCount = 0;
    boundPipeline = VK_NULL_HANDLE;
    boundProgram = NULL;
    material.custom = -1;
    Changed(GROUP_MATERIAL);
}

// glcustom.cpp builds through this (pvkPipeline is plain data: no virtual functions of its own)
class pvkCustomTarget : public pglCustomTarget
{
public:
    pvkPipeline* pipeline;
    int BuildCustomProgram(const char* name, const char* source) { return pipeline->BuildCustomProgram(name, source); }
    void ReleaseCustomPrograms() { pipeline->ReleaseCustomPrograms(); }
};

void pvkCustomUpdate(pvkPipeline* pipeline)
{
    static pvkCustomTarget s_target;
    s_target.pipeline = pipeline;
    pglCustomUpdate(&s_target);
}

//-----------------------------------------------------------------------------
// State (as glpipeline.cpp)
//-----------------------------------------------------------------------------

void pvkPipeline::NewFrame(int width, int height)
{
    frameCount++;
    frameTime = SDL_GetTicks() / 1000.0f;
    frameWidth = (float)width;
    frameHeight = (float)height;
}

void pvkPipeline::SetProjection(const float* m)
{
    if(memcmp(projection, m, sizeof(projection)) == 0)
        return;
    memcpy(projection, m, sizeof(projection));
    Changed(GROUP_MATRICES);
}

void pvkPipeline::SetModelView(const float* m)
{
    if(memcmp(modelView, m, sizeof(modelView)) == 0)
        return;
    memcpy(modelView, m, sizeof(modelView));
    Changed(GROUP_MATRICES);
}

void pvkPipeline::LoadIdentity()
{
    Identity(projection);
    Identity(modelView);
    Changed(GROUP_MATRICES);
}

void pvkPipeline::SetLight(int i, bool enabled, const float* p, const float* colour, float attenA, float attenB,
                           float attenC, float range)
{
    if(i < 0 || i >= pvkMaxLights)
        return;
    Light& l = lights[i];
    l.enabled = enabled;
    for(int r = 0; r < 4; r++)
        l.position[r] = modelView[r] * p[0] + modelView[4 + r] * p[1] + modelView[8 + r] * p[2] + modelView[12 + r] * p[3];
    memcpy(l.colour, colour, sizeof(l.colour));
    l.atten[0] = attenA;
    l.atten[1] = attenB;
    l.atten[2] = attenC;
    l.range = range;
    Changed(GROUP_LIGHTS);
}

void pvkPipeline::SetAmbient(const float* colour)
{
    memcpy(ambient, colour, sizeof(ambient));
    Changed(GROUP_LIGHTS);
}

void pvkPipeline::EnableFog(bool enable)
{
    fogEnabled = enable;
    Changed(GROUP_FOG);
}

void pvkPipeline::SetFog(const float* colour, float start, float end)
{
    memcpy(fogColour, colour, sizeof(fogColour));
    fogStart = start;
    fogEnd = end;
    Changed(GROUP_FOG);
}

void pvkPipeline::SetMaterial(const Material& m)
{
    if(memcmp(&material, &m, sizeof(material)) == 0)
        return;
    material = m;
    Changed(GROUP_MATERIAL);
}

void pvkPipeline::SetTextures(pvkTexture* base, pvkTexture* refl, int filter, int uv)
{
    texture = base;
    reflection = refl;
    filterMode = filter;
    uvMode = uv;
}

void pvkPipeline::SetTextureFiltering(bool mipmaps, float anisotropy)
{
    float value = mipmaps ? (anisotropy < 1.0f ? 1.0f : anisotropy) : 0.0f;
    if(value == mipAlpha)
        return;
    mipAlpha = value;
    Changed(GROUP_MATERIAL);
}

bool pvkPipeline::AlphaToCoverage() const
{
    static int s_allowed = -1;      // SHAR_ALPHA_TO_COVERAGE=0: plain alpha test even with MSAA
    if(s_allowed < 0)
    {
        const char* e = getenv("SHAR_ALPHA_TO_COVERAGE");
        s_allowed = !(e && e[0] == '0');
    }
    return s_allowed && g_pvk->ActiveSamples() > 1 && !material.blended &&
           (material.alphaCompare == 4 || material.alphaCompare == 5);
}

void pvkPipeline::SetFloats(Program& p, int u, const float* v, int count, int components)
{
    if(p.offset[u] < 0)
        return;
    int stride = p.stride[u] ? p.stride[u] : components * 4;
    for(int i = 0; i < count; i++)
        memcpy(p.block + p.offset[u] + i * stride, v + i * components, components * 4);
    p.blockChanged = true;
}

void pvkPipeline::SetInt(Program& p, int u, int v)
{
    if(p.offset[u] < 0)
        return;
    memcpy(p.block + p.offset[u], &v, 4);
    p.blockChanged = true;
}

void pvkPipeline::UploadUniforms(Program& p)
{
    if(p.uploaded[GROUP_MATRICES] != version[GROUP_MATRICES])
    {
        SetFloats(p, U_PROJECTION, projection, 1, 16);
        SetFloats(p, U_MODELVIEW, modelView, 1, 16);
        const float* a0 = &modelView[0];
        const float* a1 = &modelView[4];
        const float* a2 = &modelView[8];
        float n[9];
        n[0] = a1[1] * a2[2] - a1[2] * a2[1];
        n[1] = a1[2] * a2[0] - a1[0] * a2[2];
        n[2] = a1[0] * a2[1] - a1[1] * a2[0];
        n[3] = a2[1] * a0[2] - a2[2] * a0[1];
        n[4] = a2[2] * a0[0] - a2[0] * a0[2];
        n[5] = a2[0] * a0[1] - a2[1] * a0[0];
        n[6] = a0[1] * a1[2] - a0[2] * a1[1];
        n[7] = a0[2] * a1[0] - a0[0] * a1[2];
        n[8] = a0[0] * a1[1] - a0[1] * a1[0];
        float det = a0[0] * n[0] + a0[1] * n[1] + a0[2] * n[2];
        if(det != 0.0f)
        {
            for(int i = 0; i < 9; i++)
                n[i] /= det;
        }
        // std140 mat3: three columns, each padded to a vec4
        if(p.offset[U_NORMALMATRIX] >= 0)
        {
            for(int c = 0; c < 3; c++)
                memcpy(p.block + p.offset[U_NORMALMATRIX] + c * 16, &n[c * 3], 12);
            p.blockChanged = true;
        }
        p.uploaded[GROUP_MATRICES] = version[GROUP_MATRICES];
    }

    if(p.uploaded[GROUP_LIGHTS] != version[GROUP_LIGHTS])
    {
        float position[pvkMaxLights][4], colour[pvkMaxLights][3], atten[pvkMaxLights][3], range[pvkMaxLights];
        int count = 0;
        for(int i = 0; i < pvkMaxLights; i++)
        {
            if(!lights[i].enabled)
                continue;
            memcpy(position[count], lights[i].position, sizeof(position[0]));
            memcpy(colour[count], lights[i].colour, sizeof(colour[0]));
            memcpy(atten[count], lights[i].atten, sizeof(atten[0]));
            range[count] = lights[i].range;
            count++;
        }
        SetInt(p, U_LIGHTCOUNT, count);
        if(count)
        {
            SetFloats(p, U_LIGHTPOSITION, &position[0][0], count, 4);
            SetFloats(p, U_LIGHTCOLOUR, &colour[0][0], count, 3);
            SetFloats(p, U_LIGHTATTEN, &atten[0][0], count, 3);
            SetFloats(p, U_LIGHTRANGE, range, count, 1);
        }
        SetFloats(p, U_GLOBALAMBIENT, ambient, 1, 4);
        p.uploaded[GROUP_LIGHTS] = version[GROUP_LIGHTS];
    }

    if(p.uploaded[GROUP_FOG] != version[GROUP_FOG])
    {
        SetInt(p, U_FOG, fogEnabled);
        SetFloats(p, U_FOGCOLOUR, fogColour, 1, 3);
        SetFloats(p, U_FOGEND, &fogEnd, 1, 1);
        float scale = fogEnd != fogStart ? 1.0f / (fogEnd - fogStart) : 1.0f;
        SetFloats(p, U_FOGSCALE, &scale, 1, 1);
        p.uploaded[GROUP_FOG] = version[GROUP_FOG];
    }

    if(p.uploaded[GROUP_MATERIAL] != version[GROUP_MATERIAL])
    {
        SetInt(p, U_TEXTURED, material.textured);
        SetInt(p, U_LIT, material.lit);
        SetFloats(p, U_MATAMBIENT, material.ambient, 1, 4);
        SetFloats(p, U_MATDIFFUSE, material.diffuse, 1, 4);
        SetFloats(p, U_MATSPECULAR, material.specular, 1, 4);
        SetFloats(p, U_MATEMISSIVE, material.emissive, 1, 4);
        SetFloats(p, U_SHININESS, &material.shininess, 1, 1);
        SetInt(p, U_ALPHACOMPARE, material.alphaCompare < 0 ? 1 : material.alphaCompare);
        SetFloats(p, U_ALPHAREF, &material.alphaRef, 1, 1);
        SetInt(p, U_ALPHATOCOVERAGE, AlphaToCoverage());
        float mip = material.blended ? 0.0f : mipAlpha;
        SetFloats(p, U_MIPALPHA, &mip, 1, 1);
        SetInt(p, U_REFLECTIONTEXTURED, material.reflectionTextured);
        SetFloats(p, U_ENVBLEND, material.envBlend, 1, 4);
        SetFloats(p, U_REFRACTCOLOUR, material.refractColour, 1, 4);
        SetFloats(p, U_REFRACTINDEX, &material.refractIndex, 1, 1);
        SetFloats(p, U_REFRACTBLEND, &material.refractBlend, 1, 1);
        p.uploaded[GROUP_MATERIAL] = version[GROUP_MATERIAL];
    }

    if(p.timeFrame != frameCount)
    {
        SetFloats(p, U_TIME, &frameTime, 1, 1);
        float resolution[2] = { frameWidth, frameHeight };
        SetFloats(p, U_RESOLUTION, resolution, 1, 2);
        p.timeFrame = frameCount;
    }
}

//-----------------------------------------------------------------------------
// Pipeline objects and samplers
//-----------------------------------------------------------------------------

static const VkCompareOp s_compare[8] =
{
    VK_COMPARE_OP_NEVER, VK_COMPARE_OP_ALWAYS, VK_COMPARE_OP_LESS, VK_COMPARE_OP_LESS_OR_EQUAL,
    VK_COMPARE_OP_GREATER, VK_COMPARE_OP_GREATER_OR_EQUAL, VK_COMPARE_OP_EQUAL, VK_COMPARE_OP_NOT_EQUAL
};

static const VkStencilOp s_stencil[6] =
{
    VK_STENCIL_OP_KEEP, VK_STENCIL_OP_ZERO, VK_STENCIL_OP_REPLACE, VK_STENCIL_OP_INCREMENT_AND_CLAMP,
    VK_STENCIL_OP_DECREMENT_AND_CLAMP, VK_STENCIL_OP_INVERT
};

// Direct3D 8's (dx8/state.cpp alphaBlendTable), as glmat.cpp
static const struct { VkBlendOp op; VkBlendFactor src, dest; } s_blend[8] =
{
    { VK_BLEND_OP_ADD,              VK_BLEND_FACTOR_ONE,       VK_BLEND_FACTOR_ZERO },                  // NONE
    { VK_BLEND_OP_ADD,              VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA },   // ALPHA
    { VK_BLEND_OP_ADD,              VK_BLEND_FACTOR_ONE,       VK_BLEND_FACTOR_ONE },                   // ADD
    { VK_BLEND_OP_REVERSE_SUBTRACT, VK_BLEND_FACTOR_ONE,       VK_BLEND_FACTOR_ONE },                   // SUBTRACT
    { VK_BLEND_OP_ADD,              VK_BLEND_FACTOR_DST_COLOR, VK_BLEND_FACTOR_ZERO },                  // MODULATE
    { VK_BLEND_OP_ADD,              VK_BLEND_FACTOR_DST_COLOR, VK_BLEND_FACTOR_SRC_COLOR },             // MODULATE2
    { VK_BLEND_OP_ADD,              VK_BLEND_FACTOR_DST_COLOR, VK_BLEND_FACTOR_SRC_ALPHA },             // ADDMODULATEALPHA
    { VK_BLEND_OP_REVERSE_SUBTRACT, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_SRC_ALPHA }              // SUBMODULATEALPHA
};

static int TopologyClass(int primType)
{
    return primType == PVK_LINES || primType == PVK_LINESTRIP ? 1 : primType == PVK_POINTS ? 2 : 0;
}

static const VkPrimitiveTopology s_topology[5] =
{
    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
    VK_PRIMITIVE_TOPOLOGY_LINE_STRIP, VK_PRIMITIVE_TOPOLOGY_POINT_LIST
};

VkPipeline pvkPipeline::GetPipeline(int programIndex, int topologyClass)
{
    int samples = g_pvk->ActiveSamples();
    int samplesLog2 = 0;
    while((1 << samplesLog2) < samples)
        samplesLog2++;
    int blend = state.blend >= 0 && state.blend < 8 ? state.blend : 0;
    int fill = g_pvk->fillModeNonSolid ? (state.fill & 3) : 0;
    unsigned key = (unsigned)programIndex | (blend << 6) | ((state.colourWrite & 15) << 10) | (fill << 14) |
                   ((AlphaToCoverage() ? 1 : 0) << 16) | (samplesLog2 << 17) | (topologyClass << 20) | (1u << 31);

    unsigned h = (key * 2654435761u) % pipelineCapacity;
    while(pipelines[h].pipeline)
    {
        if(pipelines[h].key == key)
            return pipelines[h].pipeline;
        h = (h + 1) % pipelineCapacity;
    }
    if(pipelineCount * 2 >= pipelineCapacity)
    {
        static bool s_reported = false;
        if(!s_reported)
            fprintf(stderr, "PDDI: Vulkan pipeline table full\n");
        s_reported = true;
        return VK_NULL_HANDLE;
    }

    Program& p = programs[programIndex];
    VkPipelineShaderStageCreateInfo stages[2] = { { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO },
                                                  { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO } };
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = p.vertex;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = p.fragment;
    stages[1].pName = "main";

    VkVertexInputBindingDescription bindings[PVK_ATTRIB_COUNT];
    VkVertexInputAttributeDescription attributes[PVK_ATTRIB_COUNT];
    static const VkFormat formats[PVK_ATTRIB_COUNT] = { VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32B32_SFLOAT,
                                                        VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R32G32_SFLOAT };
    for(int i = 0; i < PVK_ATTRIB_COUNT; i++)
    {
        bindings[i].binding = i;
        bindings[i].stride = 0;     // dynamic
        bindings[i].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        attributes[i].location = i;
        attributes[i].binding = i;
        attributes[i].format = formats[i];
        attributes[i].offset = 0;
    }
    VkPipelineVertexInputStateCreateInfo vertex = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vertex.vertexBindingDescriptionCount = PVK_ATTRIB_COUNT;
    vertex.pVertexBindingDescriptions = bindings;
    vertex.vertexAttributeDescriptionCount = PVK_ATTRIB_COUNT;
    vertex.pVertexAttributeDescriptions = attributes;
    VkPipelineInputAssemblyStateCreateInfo assembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    assembly.topology = topologyClass == 1 ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST :
                        topologyClass == 2 ? VK_PRIMITIVE_TOPOLOGY_POINT_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    raster.polygonMode = fill == PDDI_FILL_WIRE ? VK_POLYGON_MODE_LINE : fill == PDDI_FILL_POINT ?
                         VK_POLYGON_MODE_POINT : VK_POLYGON_MODE_FILL;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = (VkSampleCountFlagBits)samples;
    ms.alphaToCoverageEnable = AlphaToCoverage();
    VkPipelineDepthStencilStateCreateInfo depth = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    depth.maxDepthBounds = 1.0f;
    VkPipelineColorBlendAttachmentState attachment;
    memset(&attachment, 0, sizeof(attachment));
    attachment.blendEnable = blend != PDDI_BLEND_NONE;
    attachment.srcColorBlendFactor = attachment.srcAlphaBlendFactor = s_blend[blend].src;
    attachment.dstColorBlendFactor = attachment.dstAlphaBlendFactor = s_blend[blend].dest;
    attachment.colorBlendOp = attachment.alphaBlendOp = s_blend[blend].op;
    attachment.colorWriteMask = state.colourWrite & 15;
    VkPipelineColorBlendStateCreateInfo colourBlend = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    colourBlend.attachmentCount = 1;
    colourBlend.pAttachments = &attachment;
    static const VkDynamicState dynamics[] =
    {
        VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_CULL_MODE, VK_DYNAMIC_STATE_FRONT_FACE,
        VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY, VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE, VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_COMPARE_OP, VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE, VK_DYNAMIC_STATE_STENCIL_OP,
        VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK, VK_DYNAMIC_STATE_STENCIL_WRITE_MASK, VK_DYNAMIC_STATE_STENCIL_REFERENCE,
        VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE, VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE, VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE,
        VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE
    };
    VkPipelineDynamicStateCreateInfo dynamic = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynamic.dynamicStateCount = sizeof(dynamics) / sizeof(dynamics[0]);
    dynamic.pDynamicStates = dynamics;
    VkFormat colourFormat = VK_FORMAT_R8G8B8A8_UNORM;
    VkPipelineRenderingCreateInfo rendering = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &colourFormat;
    rendering.depthAttachmentFormat = g_pvk->depthFormat;
    rendering.stencilAttachmentFormat = g_pvk->depthFormat;

    VkGraphicsPipelineCreateInfo info = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    info.pNext = &rendering;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertex;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &ms;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &colourBlend;
    info.pDynamicState = &dynamic;
    info.layout = layout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    if(vkCreateGraphicsPipelines(g_pvk->device, cache, 1, &info, NULL, &pipeline) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    pipelines[h].key = key;
    pipelines[h].pipeline = pipeline;
    pipelineCount++;
    stats.pipelines++;
    return pipeline;
}

VkSampler pvkPipeline::GetSampler(int filter, int uv, bool mipmapped)
{
    float anisotropy = mipmapped && g_pvk->anisotropy ? g_pglAnisotropy : 1.0f;
    if(anisotropy > g_pvk->properties.limits.maxSamplerAnisotropy)
        anisotropy = g_pvk->properties.limits.maxSamplerAnisotropy;
    if(anisotropy != samplerAnisotropy && samplerCount)
    {
        // the setting changed (options menu): new samplers; the old ones go once the frames using them are done
        for(int i = 0; i < samplerCount; i++)
            g_pvk->Retire(samplers[i].sampler);
        samplerCount = 0;
        whiteSampler = VK_NULL_HANDLE;
        descriptorsDirty = true;
    }
    samplerAnisotropy = anisotropy;

    unsigned key = (filter & 7) | ((uv & 3) << 3) | ((mipmapped ? 1 : 0) << 5);
    for(int i = 0; i < samplerCount; i++)
        if(samplers[i].key == key)
            return samplers[i].sampler;
    if(samplerCount == 64)
        return samplers[0].sampler;

    // as glmat.cpp's tables: point for PDDI_FILTER_NONE and _MIPMAP (2), linear otherwise; GL's clamp is edge clamp
    bool linear = filter != PDDI_FILTER_NONE && filter != 2;
    VkSamplerCreateInfo info = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    info.magFilter = info.minFilter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    if(mipmapped)
        info.minFilter = VK_FILTER_LINEAR;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    info.addressModeU = info.addressModeV = info.addressModeW =
        uv == PDDI_UV_TILE ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.maxLod = mipmapped ? VK_LOD_CLAMP_NONE : 0.0f;
    info.anisotropyEnable = anisotropy > 1.0f;
    info.maxAnisotropy = anisotropy;
    VkSampler sampler = VK_NULL_HANDLE;
    vkCreateSampler(g_pvk->device, &info, NULL, &sampler);
    samplers[samplerCount].key = key;
    samplers[samplerCount].sampler = sampler;
    samplerCount++;
    return sampler;
}

//-----------------------------------------------------------------------------
// Drawing
//-----------------------------------------------------------------------------

// binds what the draw needs: rendering, the pipeline object, dynamic state that changed, the uniform block and
// samplers. NULL when nothing can be drawn.
VkCommandBuffer pvkPipeline::Bind(int primType)
{
    if(!ready)
        return VK_NULL_HANDLE;

    // textures first: their uploads go to the upload command buffer, before rendering starts
    VkImageView views[SAMPLER_COUNT];
    VkSampler samplersUsed[SAMPLER_COUNT];
    int index = material.program >= 0 && material.program < PGL_PROGRAM_COUNT ? material.program : PGL_PROGRAM_SIMPLE;
    if(material.custom >= 0 && material.custom < customCount)
        index = PGL_PROGRAM_COUNT + material.custom;
    Program& p = programs[index];
    pvkTexture* textures[2] = { material.textured ? texture : NULL, material.reflectionTextured ? reflection : NULL };
    for(int s = 0; s < 2; s++)
    {
        views[s] = white.view;
        samplersUsed[s] = VK_NULL_HANDLE;
        if(p.binding[s] < 0 || !textures[s])
            continue;
        VkImageView view = textures[s]->Prepare();
        if(view)
        {
            views[s] = view;
            samplersUsed[s] = GetSampler(filterMode, uvMode, g_pglMipmaps && textures[s]->HasMipmaps() &&
                                         filterMode != PDDI_FILTER_NONE);
        }
    }
    views[SAMPLER_SCREEN] = screen.view && screen.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ? screen.view : white.view;
    samplersUsed[SAMPLER_SCREEN] = VK_NULL_HANDLE;
    if(!whiteSampler)
        whiteSampler = GetSampler(PDDI_FILTER_NONE, PDDI_UV_CLAMP, false);
    for(int s = 0; s < SAMPLER_COUNT; s++)
    {
        if(!samplersUsed[s])
            samplersUsed[s] = s == SAMPLER_SCREEN ? GetSampler(PDDI_FILTER_BILINEAR, PDDI_UV_CLAMP, false) : whiteSampler;
    }

    g_pvk->BeginRendering();
    VkCommandBuffer cmd = g_pvk->Cmd();
    if(boundSerial != g_pvk->FrameSerial())
    {
        // a new command buffer: nothing bound
        boundSerial = g_pvk->FrameSerial();
        boundPipeline = VK_NULL_HANDLE;
        boundProgram = NULL;
        appliedTopology = -1;
        memset(&applied, 0xff, sizeof(applied));
        descriptorsDirty = true;
        vkCmdSetFrontFace(cmd, VK_FRONT_FACE_COUNTER_CLOCKWISE);
        vkCmdSetDepthBiasEnable(cmd, VK_FALSE);
        vkCmdSetDepthBoundsTestEnable(cmd, VK_FALSE);
        vkCmdSetRasterizerDiscardEnable(cmd, VK_FALSE);
        vkCmdSetPrimitiveRestartEnable(cmd, VK_FALSE);
    }

    VkPipeline pipeline = GetPipeline(index, TopologyClass(primType));
    if(!pipeline)
        return VK_NULL_HANDLE;
    if(pipeline != boundPipeline)
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        boundPipeline = pipeline;
    }

    // dynamic state
    const RenderState& s = state;
    if(memcmp(s.viewport, applied.viewport, sizeof(s.viewport)) != 0 || s.depthMin != applied.depthMin ||
       s.depthMax != applied.depthMax)
    {
        // GL's y up: a negative height flips it (the same image as GL's, rows top first)
        VkViewport v = { (float)s.viewport[0], (float)(s.viewport[1] + s.viewport[3]), (float)s.viewport[2],
                         -(float)s.viewport[3], s.depthMin, s.depthMax };
        if(s.viewport[2] <= 0 || s.viewport[3] <= 0)
        {
            v.x = 0.0f;
            v.y = 1.0f;
            v.width = 1.0f;
            v.height = -1.0f;
        }
        vkCmdSetViewport(cmd, 0, 1, &v);
        memcpy(applied.viewport, s.viewport, sizeof(s.viewport));
        applied.depthMin = s.depthMin;
        applied.depthMax = s.depthMax;
    }
    if(s.scissor != applied.scissor || memcmp(s.scissorRect, applied.scissorRect, sizeof(s.scissorRect)) != 0 ||
       applied.colourWrite == 0xffffffffu)
    {
        int w = g_pvk->Width(), h = g_pvk->Height();
        int x0 = 0, y0 = 0, x1 = w, y1 = h;
        if(s.scissor)
        {
            x0 = s.scissorRect[0];
            y0 = s.scissorRect[1];
            x1 = x0 + s.scissorRect[2];
            y1 = y0 + s.scissorRect[3];
            x0 = x0 < 0 ? 0 : x0 > w ? w : x0;
            y0 = y0 < 0 ? 0 : y0 > h ? h : y0;
            x1 = x1 < x0 ? x0 : x1 > w ? w : x1;
            y1 = y1 < y0 ? y0 : y1 > h ? h : y1;
        }
        VkRect2D r = { { x0, y0 }, { (unsigned)(x1 - x0), (unsigned)(y1 - y0) } };
        vkCmdSetScissor(cmd, 0, 1, &r);
        applied.scissor = s.scissor;
        memcpy(applied.scissorRect, s.scissorRect, sizeof(s.scissorRect));
        applied.colourWrite = s.colourWrite;
    }
    if(s.cull != applied.cull || s.cullFront != applied.cullFront)
    {
        vkCmdSetCullMode(cmd, !s.cull ? VK_CULL_MODE_NONE : s.cullFront ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT);
        applied.cull = s.cull;
        applied.cullFront = s.cullFront;
    }
    if(s.depthTest != applied.depthTest)
    {
        vkCmdSetDepthTestEnable(cmd, s.depthTest);
        applied.depthTest = s.depthTest;
    }
    if(s.depthWrite != applied.depthWrite)
    {
        vkCmdSetDepthWriteEnable(cmd, s.depthWrite);
        applied.depthWrite = s.depthWrite;
    }
    if(s.depthCompare != applied.depthCompare)
    {
        vkCmdSetDepthCompareOp(cmd, s_compare[s.depthCompare & 7]);
        applied.depthCompare = s.depthCompare;
    }
    if(s.stencilTest != applied.stencilTest)
    {
        vkCmdSetStencilTestEnable(cmd, s.stencilTest);
        applied.stencilTest = s.stencilTest;
    }
    if(s.stencilCompare != applied.stencilCompare || s.stencilFail != applied.stencilFail ||
       s.stencilZFail != applied.stencilZFail || s.stencilZPass != applied.stencilZPass)
    {
        vkCmdSetStencilOp(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, s_stencil[s.stencilFail % 6], s_stencil[s.stencilZPass % 6],
                          s_stencil[s.stencilZFail % 6], s_compare[s.stencilCompare & 7]);
        applied.stencilCompare = s.stencilCompare;
        applied.stencilFail = s.stencilFail;
        applied.stencilZFail = s.stencilZFail;
        applied.stencilZPass = s.stencilZPass;
    }
    if(s.stencilMask != applied.stencilMask)
    {
        vkCmdSetStencilCompareMask(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, s.stencilMask);
        applied.stencilMask = s.stencilMask;
    }
    if(s.stencilWriteMask != applied.stencilWriteMask)
    {
        vkCmdSetStencilWriteMask(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, s.stencilWriteMask);
        applied.stencilWriteMask = s.stencilWriteMask;
    }
    if(s.stencilRef != applied.stencilRef)
    {
        vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, s.stencilRef);
        applied.stencilRef = s.stencilRef;
    }
    if(primType != appliedTopology)
    {
        vkCmdSetPrimitiveTopology(cmd, s_topology[primType]);
        appliedTopology = primType;
    }

    // the uniform block, copied to the ring when it changed (or the frame is new: the old copy's ring was reset)
    UploadUniforms(p);
    if(p.blockChanged || p.blockSerial != g_pvk->FrameSerial())
    {
        VkDeviceSize align = g_pvk->properties.limits.minUniformBufferOffsetAlignment;
        void* dst = g_pvk->Transient(p.blockSize, align < 16 ? 16 : align, &p.blockBuffer, &p.blockOffset);
        if(!dst)
            return VK_NULL_HANDLE;
        memcpy(dst, p.block, p.blockSize);
        p.blockChanged = false;
        p.blockSerial = g_pvk->FrameSerial();
        stats.uniformCopies++;
    }

    bool push = descriptorsDirty || &p != boundProgram || p.blockBuffer != pushedBlock || p.blockOffset != pushedOffset;
    for(int i = 0; i < SAMPLER_COUNT && !push; i++)
        push = p.binding[i] >= 0 && (views[i] != pushedViews[i] || samplersUsed[i] != pushedSamplers[i]);
    if(push)
    {
        VkDescriptorBufferInfo blockInfo = { p.blockBuffer, p.blockOffset, (VkDeviceSize)p.blockSize };
        VkDescriptorImageInfo imageInfo[SAMPLER_COUNT];
        VkWriteDescriptorSet writes[1 + SAMPLER_COUNT];
        memset(writes, 0, sizeof(writes));
        int n = 0;
        writes[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[n].dstBinding = 0;
        writes[n].descriptorCount = 1;
        writes[n].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[n].pBufferInfo = &blockInfo;
        n++;
        for(int i = 0; i < SAMPLER_COUNT; i++)
        {
            pushedViews[i] = views[i];
            pushedSamplers[i] = samplersUsed[i];
            if(p.binding[i] < 0)
                continue;
            imageInfo[i].sampler = samplersUsed[i];
            imageInfo[i].imageView = views[i];
            imageInfo[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            writes[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[n].dstBinding = p.binding[i];
            writes[n].descriptorCount = 1;
            writes[n].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[n].pImageInfo = &imageInfo[i];
            n++;
        }
        vkCmdPushDescriptorSetKHR(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, n, writes);
        pushedBlock = p.blockBuffer;
        pushedOffset = p.blockOffset;
        boundProgram = &p;
        descriptorsDirty = false;
    }
    stats.draws++;
    return cmd;
}

VkCommandBuffer pvkPipeline::Prepare(int primType, unsigned missing)
{
    return Bind(primType);
}

// attributes a prim buffer lacks: the current values, in the ring, read with stride 0
void pvkPipeline::BindCurrent(VkCommandBuffer cmd, unsigned missing, VkBuffer* buffers, VkDeviceSize* offsets,
                              VkDeviceSize* strides)
{
    if(!missing)
        return;
    VkBuffer buffer;
    VkDeviceSize offset;
    unsigned char* dst = (unsigned char*)g_pvk->Transient(32, 16, &buffer, &offset);
    if(!dst)
        return;
    memcpy(dst, current.normal, 12);
    memcpy(dst + 12, current.colour, 4);
    memcpy(dst + 16, current.uv, 8);
    static const int where[PVK_ATTRIB_COUNT] = { 0, 0, 12, 16 };
    for(int a = PVK_ATTRIB_NORMAL; a < PVK_ATTRIB_COUNT; a++)
    {
        if(missing & (1u << a))
        {
            buffers[a] = buffer;
            offsets[a] = offset + where[a];
            strides[a] = 0;
        }
    }
}

void pvkPipeline::BeginStream(int primType)
{
    streamPrim = primType;
    streamCount = 0;
}

void pvkPipeline::Emit(float x, float y, float z)
{
    if(streamCount == streamCapacity)
    {
        int capacity = streamCapacity ? streamCapacity * 2 : 1024;
        pvkVertex* v = (pvkVertex*)realloc(streamVerts, capacity * sizeof(pvkVertex));
        if(!v)
            return;
        streamVerts = v;
        streamCapacity = capacity;
    }
    pvkVertex& v = streamVerts[streamCount++];
    v = current;
    v.position[0] = x;
    v.position[1] = y;
    v.position[2] = z;
}

void pvkPipeline::EndStream()
{
    if(!ready || streamCount == 0)
        return;
    VkCommandBuffer cmd = Bind(streamPrim);
    if(!cmd)
        return;
    VkBuffer buffer;
    VkDeviceSize offset;
    unsigned size = streamCount * sizeof(pvkVertex);
    void* dst = g_pvk->Transient(size, 16, &buffer, &offset);
    if(!dst)
        return;
    memcpy(dst, streamVerts, size);
    VkBuffer buffers[PVK_ATTRIB_COUNT] = { buffer, buffer, buffer, buffer };
    VkDeviceSize offsets[PVK_ATTRIB_COUNT] = { offset + offsetof(pvkVertex, position), offset + offsetof(pvkVertex, normal),
                                               offset + offsetof(pvkVertex, colour), offset + offsetof(pvkVertex, uv) };
    VkDeviceSize strides[PVK_ATTRIB_COUNT] = { sizeof(pvkVertex), sizeof(pvkVertex), sizeof(pvkVertex), sizeof(pvkVertex) };
    vkCmdBindVertexBuffers2(cmd, 0, PVK_ATTRIB_COUNT, buffers, offsets, NULL, strides);
    vkCmdDraw(cmd, streamCount, 1, 0, 0);
    stats.streamDraws++;
}

//-----------------------------------------------------------------------------
// Refraction's screen copy
//-----------------------------------------------------------------------------

static bool s_recaptureScreen = false;

void pvkRecaptureScreen()
{
    s_recaptureScreen = true;
}

void pvkPipeline::CaptureScreen()
{
    if(screenFrame == frameCount && screen.image && !s_recaptureScreen)
        return;
    screenFrame = frameCount;
    s_recaptureScreen = false;
    int w = g_pvk->Width(), h = g_pvk->Height();
    if(!screen.image || screen.width != w || screen.height != h)
    {
        g_pvk->Retire(screen);
        if(!g_pvk->CreateImage(w, h, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT,
                               VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT, &screen))
            return;
    }
    g_pvk->BeginRendering();        // the frame must exist
    g_pvk->EndRendering();
    VkCommandBuffer cmd = g_pvk->Cmd();
    pvkImage& colour = g_pvk->Target().colour;
    g_pvk->Transition(cmd, colour, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    g_pvk->Transition(cmd, screen, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageSubresourceLayers layers = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    VkExtent3D extent = { (unsigned)w, (unsigned)h, 1 };
    if(colour.samples > 1)
    {
        VkImageResolve r;
        memset(&r, 0, sizeof(r));
        r.srcSubresource = r.dstSubresource = layers;
        r.extent = extent;
        vkCmdResolveImage(cmd, colour.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, screen.image,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
    }
    else
    {
        VkImageCopy c;
        memset(&c, 0, sizeof(c));
        c.srcSubresource = c.dstSubresource = layers;
        c.extent = extent;
        vkCmdCopyImage(cmd, colour.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, screen.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
    }
    g_pvk->Transition(cmd, screen, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    descriptorsDirty = true;
}

//-----------------------------------------------------------------------------
// Statistics (SHAR_GL_STATS=1, as the GL pipeline's)
//-----------------------------------------------------------------------------

static int s_statsEnabled = -1;
static Uint64 s_frameStart;

void pvkPipeline::FrameStats()
{
    static Uint32 last = 0;
    if(s_statsEnabled < 0)
    {
        const char* e = getenv("SHAR_GL_STATS");
        s_statsEnabled = e && e[0] == '1';
        last = SDL_GetTicks();
    }
    if(!s_statsEnabled)
        return;
    s_frameStart = SDL_GetPerformanceCounter();
    stats.frames++;
    if(SDL_GetTicks() - last < 5000)
        return;
    last = SDL_GetTicks();
    float f = (float)stats.frames;
    fprintf(stderr, "[vk] per frame: %.0f draws (%.0f streamed), %.0f uniform copies; %u new pipelines (%d cached); "
            "render time %.3f ms CPU\n", stats.draws / f, stats.streamDraws / f, stats.uniformCopies / f, stats.pipelines,
            pipelineCount, stats.cpuMs / f);
    memset(&stats, 0, sizeof(stats));
}

void pvkPipeline::FrameEnd()
{
    if(s_statsEnabled <= 0)
        return;
    stats.cpuMs += (SDL_GetPerformanceCounter() - s_frameStart) * 1000.0 / SDL_GetPerformanceFrequency();
}

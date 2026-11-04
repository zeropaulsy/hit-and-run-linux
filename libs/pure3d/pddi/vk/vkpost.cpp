//=============================================================================
// Linux port: user post-processing shaders on the Vulkan renderer (as gl/glpost.cpp; format: shaders/README.md).
// post.ini, the pragmas and the uniforms are the GL renderer's: keep the two in step.
//=============================================================================

// The passes see what they see in GL. Their images are kept bottom row first, as GL's textures: the frame (stored top
// row first in Vulkan) is copied in flipped, depth through a small pass into an R32F image (uDepth, flipped too), and
// each pass draws a screen triangle with vUV = 0,0 at the bottom left, so texture(), gl_FragCoord and ViewPosition()
// agree with GL. The last pass draws back into the frame with a flipped viewport (its gl_FragCoord.y counts from the
// top: only dither patterns notice).
//
// A pass named *.spv is SPIR-V written for Vulkan (shaders/README.md, Vulkan passes): loaded as it is, no header, a
// fixed interface (s_spirvVertexSource's vUV, the block at binding 0 laid out as below, the samplers at bindings 1 up
// in U_* order); its pragmas come from the source beside it (same name, .frag), when there is one.
//
// Vulkan only (GL's multisampled depth is a renderbuffer): a pass with #pragma shar_depth_samples gets, while the
// multisampled depth can be read, SHAR_DEPTH_SAMPLES, uDepthMS and DepthSample(): built again when that changes.

#include <pddi/vk/vkpost.hpp>
#include <pddi/vk/vkpipeline.hpp>
#include <pddi/gl/glcustom.hpp>

#include <SDL2/SDL.h>
#include <shar_glslang.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <png.h>

static const char* s_passVertexSource =
    "#version 330 core\n"
    "out vec2 vUV;\n"
    "void main()\n"
    "{\n"
    "    vec2 p = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);\n"
    "    vUV = p * 0.5 + 0.5;\n"
    "    gl_Position = vec4(p, 0.0, 1.0);\n"
    "}\n";

// as glpost.cpp's s_passHeader
static const char* s_passHeader =
    "#version 330 core\n"
    "in vec2 vUV;\n"
    "out vec4 fragColour;\n"
    "uniform sampler2D uScene;\n"
    "uniform sampler2D uOriginal;\n"
    "uniform sampler2D uDepth;\n"
    "uniform vec2 uResolution;\n"
    "uniform float uTime;\n"
    "uniform int uFrame;\n"
    "uniform float uNear;\n"
    "uniform float uFar;\n"
    "uniform mat4 uProjection;\n"
    "uniform mat4 uInverseProjection;\n"
    "vec3 ViewPosition(vec2 uv)\n"
    "{\n"
    "    vec4 p = uInverseProjection * vec4(uv * 2.0 - 1.0, texture(uDepth, uv).r * 2.0 - 1.0, 1.0);\n"
    "    return p.xyz / p.w;\n"
    "}\n"
    "float LinearDepth(vec2 uv)\n"
    "{\n"
    "    float d = texture(uDepth, uv).r;\n"
    "    if(uFar <= 0.0)\n"
    "        return d;\n"
    "    float z = d * 2.0 - 1.0;\n"
    "    return 2.0 * uNear * uFar / (uFar + uNear - z * (uFar - uNear));\n"
    "}\n"
    "#line 1\n";

// #pragma shar_depth_samples: raw depth of one sample of a pixel, the pixel counted as vUV * uResolution counts it
// (the image is stored top row first); pixels outside the picture are clamped
static const char* s_depthSamplesHeader =
    "#define SHAR_DEPTH_SAMPLES %d\n"
    "uniform sampler2DMS uDepthMS;\n"
    "float DepthSample(ivec2 pixel, int s)\n"
    "{\n"
    "    ivec2 size = textureSize(uDepthMS);\n"
    "    pixel = clamp(pixel, ivec2(0), size - 1);\n"
    "    return texelFetch(uDepthMS, ivec2(pixel.x, size.y - 1 - pixel.y), s).r;\n"
    "}\n";

// the screen triangle for SPIR-V passes: vUV at location 0
static const char* s_spirvVertexSource =
    "#version 450\n"
    "layout(location = 0) out vec2 vUV;\n"
    "void main()\n"
    "{\n"
    "    vec2 p = vec2(gl_VertexIndex == 1 ? 3.0 : -1.0, gl_VertexIndex == 2 ? 3.0 : -1.0);\n"
    "    vUV = p * 0.5 + 0.5;\n"
    "    gl_Position = vec4(p, 0.0, 1.0);\n"
    "}\n";

// the depth buffer, flipped into an R32F image for uDepth
static const char* s_depthFragmentSource =
    "#version 420 core\n"
    "in vec2 vUV;\n"
    "out vec4 fragColour;\n"
    "layout(binding = 1) uniform sampler2D uDepthBuffer;   // no uniform block here: keep binding 0 for it\n"
    "void main()\n"
    "{\n"
    "    fragColour = vec4(texture(uDepthBuffer, vec2(vUV.x, 1.0 - vUV.y)).r);\n"
    "}\n";

const int MAX_PASSES = 16;
const int MAX_PASS_TEXTURES = 4;
const int SAMPLER_BINDINGS = 4 + MAX_PASS_TEXTURES;     // uScene, uOriginal, uDepth, the pass's images, uDepthMS

enum { U_SCENE, U_ORIGINAL, U_DEPTH, U_TEXTURE0, U_DEPTH_MS = U_TEXTURE0 + MAX_PASS_TEXTURES };      // samplers
enum { P_RESOLUTION, P_TIME, P_FRAME, P_NEAR, P_FAR, P_PROJECTION, P_INVERSE, P_COUNT };    // block members
static const char* s_blockNames[P_COUNT] = { "uResolution", "uTime", "uFrame", "uNear", "uFar", "uProjection",
                                             "uInverseProjection" };
static const int s_spirvOffsets[P_COUNT] = { 0, 8, 12, 16, 20, 32, 96 };    // std140, as the block's declaration
const int SPIRV_BLOCK_SIZE = 160;

struct Image
{
    char path[600];
    time_t mtime;
    pvkImage image;             // no image: could not be read
};
static Image s_images[MAX_PASSES * 2 * MAX_PASS_TEXTURES];
static int s_imageCount;

struct Program
{
    VkShaderModule vertex, fragment;
    VkPipeline pipeline;
    int blockSize;
    int offset[P_COUNT];
    int binding[SAMPLER_BINDINGS];
};

struct Pass
{
    char file[256];
    time_t mtime;
    Program program;
    bool ok;
    bool usesDepth;
    bool wantsDepthSamples;     // #pragma shar_depth_samples
    int depthSamples;           // what it was built for (0: without uDepthMS)
    bool beginsEffect;
    float scale;
    pvkImage target;            // its own target when scaled
    int textureCount;
    char textureName[MAX_PASS_TEXTURES][64];
    int textureImage[MAX_PASS_TEXTURES];
};

static Pass s_passes[PGL_POST_STAGES][MAX_PASSES];
static int s_passCount[PGL_POST_STAGES];
static char s_folder[512];
static time_t s_iniTime;
static Uint32 s_lastCheck;
static int s_state = 0;         // 0 not set up, 1 set up, -1 off (SHAR_POST=0)

static VkDescriptorSetLayout s_setLayout;
static VkPipelineLayout s_layout;
static VkSampler s_linear, s_nearest;
static Program s_depthProgram;
static pvkImage s_original, s_pingPong[2], s_depth;     // bottom row first
static VkImageView s_depthView;                         // the depth buffer's depth aspect, for sampling
static VkImage s_depthViewImage;
static VkImageView s_msDepthView;                       // the same for the multisampled depth buffer
static VkImage s_msDepthViewImage;
static VkShaderModule s_spirvVertex;                    // SPIR-V passes' vertex stage

static time_t FileTime(const char* path)
{
    struct stat st;
    return stat(path, &st) == 0 ? st.st_mtime : 0;
}

// NUL-terminated; size (if given) gets the bytes read
static char* ReadFile(const char* path, long* sizeOut = NULL)
{
    FILE* f = fopen(path, "rb");
    if(!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* text = (char*)malloc(size + 1);
    if(text)
    {
        size = (long)fread(text, 1, size, f);
        text[size] = 0;
        if(sizeOut)
            *sizeOut = size;
    }
    fclose(f);
    return text;
}

//-----------------------------------------------------------------------------
// Programs
//-----------------------------------------------------------------------------

static void ReleaseProgram(Program& p)
{
    if(p.pipeline)
        g_pvk->RetirePipeline(p.pipeline);
    if(p.vertex)
        vkDestroyShaderModule(g_pvk->device, p.vertex, NULL);
    if(p.fragment)
        vkDestroyShaderModule(g_pvk->device, p.fragment, NULL);
    memset(&p, 0, sizeof(p));
}

static VkPipeline CreatePipeline(Program& p)
{
    VkPipelineShaderStageCreateInfo stages[2] = { { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO },
                                                  { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO } };
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = p.vertex;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = p.fragment;
    stages[1].pName = "main";
    VkPipelineVertexInputStateCreateInfo vertex = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo assembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState attachment;
    memset(&attachment, 0, sizeof(attachment));
    attachment.colorWriteMask = 0xf;
    VkPipelineColorBlendStateCreateInfo blend = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    blend.attachmentCount = 1;
    blend.pAttachments = &attachment;
    VkDynamicState dynamics[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamics;
    VkFormat format = &p == &s_depthProgram ? VK_FORMAT_R32_SFLOAT : VK_FORMAT_R8G8B8A8_UNORM;
    VkPipelineRenderingCreateInfo rendering = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &format;
    VkGraphicsPipelineCreateInfo info = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    info.pNext = &rendering;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertex;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &ms;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = s_layout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    vkCreateGraphicsPipelines(g_pvk->device, g_pvk->pipelineCache, 1, &info, NULL, &pipeline);
    return pipeline;
}

// samplerNames: the extra samplers' names (the pass's images), in binding order after the three standard ones
static bool BuildProgram(Program& p, const char* name, const char* fragmentHeader, const char* fragment,
                         char (*samplerNames)[64], int samplerCount)
{
    memset(&p, 0, sizeof(p));
    shar_glsl_program compiled;
    if(!shar_glsl_compile(name, NULL, s_passVertexSource, fragmentHeader, fragment, &compiled))
    {
        fprintf(stderr, "PDDI: post-processing: %s\n", compiled.log ? compiled.log : name);
        shar_glsl_free(&compiled);
        return false;
    }
    p.vertex = pvkCreateShaderModule(compiled.vertex, compiled.vertexWords);
    p.fragment = pvkCreateShaderModule(compiled.fragment, compiled.fragmentWords);
    p.blockSize = compiled.blockSize;
    for(int i = 0; i < P_COUNT; i++)
        p.offset[i] = -1;
    for(int i = 0; i < SAMPLER_BINDINGS; i++)
        p.binding[i] = -1;
    bool ok = p.vertex && p.fragment;
    for(int i = 0; i < compiled.uniformCount; i++)
    {
        const shar_glsl_uniform& u = compiled.uniforms[i];
        if(u.offset >= 0)
        {
            for(int b = 0; b < P_COUNT; b++)
                if(strcmp(u.name, s_blockNames[b]) == 0)
                    p.offset[b] = u.offset;
            continue;
        }
        int slot = -1;
        if(strcmp(u.name, "uScene") == 0 || strcmp(u.name, "uDepthBuffer") == 0)
            slot = U_SCENE;
        else if(strcmp(u.name, "uOriginal") == 0)
            slot = U_ORIGINAL;
        else if(strcmp(u.name, "uDepth") == 0)
            slot = U_DEPTH;
        else if(strcmp(u.name, "uDepthMS") == 0)
            slot = U_DEPTH_MS;
        for(int t = 0; t < samplerCount && slot < 0; t++)
            if(strcmp(u.name, samplerNames[t]) == 0)
                slot = U_TEXTURE0 + t;
        if(slot >= 0)
            p.binding[slot] = u.binding;
        if(u.binding > SAMPLER_BINDINGS)
            ok = false;
    }
    shar_glsl_free(&compiled);
    if(ok)
        p.pipeline = CreatePipeline(p);
    if(!ok || !p.pipeline)
    {
        ReleaseProgram(p);
        return false;
    }
    return true;
}

// a SPIR-V fragment stage (words: count of them): checked for what the fixed interface allows, bindings read from its
// decorations
static bool BuildSpirvProgram(Program& p, const char* name, const unsigned* words, size_t count)
{
    memset(&p, 0, sizeof(p));
    if(!s_spirvVertex)
        return false;
    if(count < 5 || words[0] != 0x07230203)
    {
        fprintf(stderr, "PDDI: post-processing: %s is not SPIR-V\n", name);
        return false;
    }
    bool fragmentMain = false;
    for(int i = 0; i < SAMPLER_BINDINGS; i++)
        p.binding[i] = -1;
    for(size_t i = 5; i < count;)
    {
        unsigned op = words[i] & 0xffff, length = words[i] >> 16;
        if(length == 0 || i + length > count)
        {
            fprintf(stderr, "PDDI: post-processing: %s: damaged SPIR-V\n", name);
            return false;
        }
        if(op == 15 && length >= 4 && words[i + 1] == 4 && strcmp((const char*)&words[i + 3], "main") == 0)
            fragmentMain = true;                            // OpEntryPoint Fragment ... "main"
        if(op == 71 && length >= 4 && words[i + 2] == 34 && words[i + 3] != 0)
        {
            fprintf(stderr, "PDDI: post-processing: %s: only descriptor set 0\n", name);    // OpDecorate DescriptorSet
            return false;
        }
        if(op == 71 && length >= 4 && words[i + 2] == 33)  // OpDecorate Binding
        {
            unsigned b = words[i + 3];
            if(b > (unsigned)SAMPLER_BINDINGS)
            {
                fprintf(stderr, "PDDI: post-processing: %s: binding %u (0 block, 1-%d samplers)\n", name, b,
                        SAMPLER_BINDINGS);
                return false;
            }
            if(b > 0)
                p.binding[b - 1] = b;
        }
        i += length;
    }
    if(!fragmentMain)
    {
        fprintf(stderr, "PDDI: post-processing: %s has no fragment entry point \"main\"\n", name);
        return false;
    }
    p.blockSize = SPIRV_BLOCK_SIZE;
    memcpy(p.offset, s_spirvOffsets, sizeof(p.offset));
    p.fragment = pvkCreateShaderModule(words, count);
    p.vertex = s_spirvVertex;
    p.pipeline = p.fragment ? CreatePipeline(p) : VK_NULL_HANDLE;
    p.vertex = VK_NULL_HANDLE;                          // shared, not the pass's to release
    if(!p.pipeline)
    {
        ReleaseProgram(p);
        return false;
    }
    return true;
}

//-----------------------------------------------------------------------------
// Images (#pragma shar_texture)
//-----------------------------------------------------------------------------

static void PngError(png_structp png, png_const_charp message)
{
    fprintf(stderr, "PDDI: post-processing: PNG error: %s\n", message);
    longjmp(*(jmp_buf*)png_get_error_ptr(png), 1);
}

static void PngWarning(png_structp, png_const_charp)
{
}

// rows as stored (the image's top row at v = 0, as glpost.cpp uploads them)
static bool LoadPng(const char* path, pvkImage* out)
{
    memset(out, 0, sizeof(*out));
    FILE* f = fopen(path, "rb");
    if(!f)
        return false;
    jmp_buf failed;
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, &failed, PngError, PngWarning);
    png_infop info = png ? png_create_info_struct(png) : NULL;
    unsigned char* pixels = NULL;
    png_bytep* rows = NULL;
    bool ok = false;
    if(png && info && setjmp(failed) == 0)
    {
        png_init_io(png, f);
        png_read_info(png, info);
        int depth = png_get_bit_depth(png, info);
        int type = png_get_color_type(png, info);
        if(type == PNG_COLOR_TYPE_PALETTE || depth < 8 || png_get_valid(png, info, PNG_INFO_tRNS))
            png_set_expand(png);
        if(type == PNG_COLOR_TYPE_GRAY || type == PNG_COLOR_TYPE_GRAY_ALPHA)
            png_set_gray_to_rgb(png);
        if(depth == 16)
            png_set_strip_16(png);
        if(!(type & PNG_COLOR_MASK_ALPHA) && !png_get_valid(png, info, PNG_INFO_tRNS))
            png_set_filler(png, 0xff, PNG_FILLER_AFTER);
        png_set_interlace_handling(png);
        png_read_update_info(png, info);
        int w = png_get_image_width(png, info);
        int h = png_get_image_height(png, info);
        pixels = (unsigned char*)malloc((size_t)w * h * 4);
        rows = (png_bytep*)malloc(h * sizeof(png_bytep));
        if(pixels && rows && png_get_rowbytes(png, info) == (png_uint_32)w * 4)
        {
            for(int y = 0; y < h; y++)
                rows[y] = pixels + (size_t)y * w * 4;
            png_read_image(png, rows);
            VkBuffer staging;
            VkDeviceSize offset;
            void* dst = g_pvk->Transient((VkDeviceSize)w * h * 4, 16, &staging, &offset);
            if(dst && g_pvk->CreateImage(w, h, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT,
                                         VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                         VK_IMAGE_ASPECT_COLOR_BIT, out))
            {
                memcpy(dst, pixels, (size_t)w * h * 4);
                VkCommandBuffer cmd = g_pvk->UploadCmd();
                g_pvk->Transition(cmd, *out, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                VkBufferImageCopy copy;
                memset(&copy, 0, sizeof(copy));
                copy.bufferOffset = offset;
                copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                copy.imageSubresource.layerCount = 1;
                copy.imageExtent.width = w;
                copy.imageExtent.height = h;
                copy.imageExtent.depth = 1;
                vkCmdCopyBufferToImage(cmd, staging, out->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
                g_pvk->Transition(cmd, *out, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                ok = true;
            }
        }
    }
    if(png)
        png_destroy_read_struct(&png, info ? &info : NULL, NULL);
    free(rows);
    free(pixels);
    fclose(f);
    return ok;
}

static int UseImage(const char* passFile, const char* file)
{
    char folder[300];
    snprintf(folder, sizeof(folder), "%s", passFile);
    char* slash = strrchr(folder, '/');
    if(slash)
        slash[1] = 0;
    else
        folder[0] = 0;
    char path[600];
    snprintf(path, sizeof(path), "%s/%s%s", s_folder, folder, file);
    time_t mtime = FileTime(path);
    for(int i = 0; i < s_imageCount; i++)
    {
        Image& image = s_images[i];
        if(strcmp(image.path, path) != 0)
            continue;
        if(image.mtime != mtime)
        {
            g_pvk->Retire(image.image);
            LoadPng(path, &image.image);
            image.mtime = mtime;
        }
        return i;
    }
    if(s_imageCount == (int)(sizeof(s_images) / sizeof(s_images[0])))
        return -1;
    Image& image = s_images[s_imageCount];
    snprintf(image.path, sizeof(image.path), "%s", path);
    image.mtime = mtime;
    if(!LoadPng(path, &image.image))
        fprintf(stderr, "PDDI: post-processing: can't load %s (PNG)\n", path);
    return s_imageCount++;
}

//-----------------------------------------------------------------------------
// post.ini and the passes (as glpost.cpp)
//-----------------------------------------------------------------------------

static void BuildPass(Pass& p)
{
    char path[800];
    snprintf(path, sizeof(path), "%s/%s", s_folder, p.file);
    p.mtime = FileTime(path);
    ReleaseProgram(p.program);
    p.ok = false;
    size_t length = strlen(p.file);
    bool spirv = length > 4 && strcmp(p.file + length - 4, ".spv") == 0;
    char* binary = NULL;
    long binarySize = 0;
    if(spirv)
    {
        binary = ReadFile(path, &binarySize);
        if(!binary)
        {
            fprintf(stderr, "PDDI: post-processing: can't read %s\n", path);
            return;
        }
        snprintf(path + strlen(path) - 4, 5, ".frag");     // the pragmas' source, if any
    }
    char* source = ReadFile(path);
    if(spirv && !source)
        source = (char*)calloc(1, 1);
    if(!source)
    {
        fprintf(stderr, "PDDI: post-processing: can't read %s\n", path);
        return;
    }
    char header[4096];
    snprintf(header, sizeof(header), "%s", s_passHeader);
    char* lineMark = strstr(header, "#line 1\n");
    if(lineMark)
        *lineMark = 0;
    p.textureCount = 0;
    for(const char* t = strstr(source, "#pragma shar_texture"); t && p.textureCount < MAX_PASS_TEXTURES;
        t = strstr(t + 1, "#pragma shar_texture"))
    {
        char sampler[64], file[256];
        if(sscanf(t + strlen("#pragma shar_texture"), " %63s %255s", sampler, file) != 2)
            continue;
        int image = UseImage(p.file, file);
        if(image < 0)
            continue;
        if(strlen(header) + 100 < sizeof(header))
            snprintf(header + strlen(header), sizeof(header) - strlen(header), "uniform sampler2D %s;\n", sampler);
        snprintf(p.textureName[p.textureCount], sizeof(p.textureName[0]), "%s", sampler);
        p.textureImage[p.textureCount] = image;
        p.textureCount++;
    }
    p.wantsDepthSamples = !spirv && strstr(source, "#pragma shar_depth_samples") != NULL;
    p.depthSamples = p.wantsDepthSamples ? g_pvk->DepthSamples() : 0;
    if(p.depthSamples > 0)
        snprintf(header + strlen(header), sizeof(header) - strlen(header), s_depthSamplesHeader, p.depthSamples);
    snprintf(header + strlen(header), sizeof(header) - strlen(header), "#line 1\n");
    if(spirv)
    {
        snprintf(path, sizeof(path), "%s/%s", s_folder, p.file);
        p.ok = BuildSpirvProgram(p.program, path, (const unsigned*)binary, (size_t)binarySize / 4);
        free(binary);
    }
    else
        p.ok = BuildProgram(p.program, path, header, source, p.textureName, p.textureCount);
    p.usesDepth = p.ok && p.program.binding[U_DEPTH] >= 0;
    p.beginsEffect = strstr(source, "#pragma shar_original") != NULL;
    p.scale = 1.0f;
    const char* scale = strstr(source, "#pragma shar_scale");
    if(scale && sscanf(scale + strlen("#pragma shar_scale"), " %f", &p.scale) == 1)
        p.scale = p.scale < 0.1f ? 0.1f : p.scale > 1.0f ? 1.0f : p.scale;
    else
        p.scale = 1.0f;
    free(source);
}

static void LoadConfig()
{
    for(int s = 0; s < PGL_POST_STAGES; s++)
    {
        for(int i = 0; i < s_passCount[s]; i++)
            ReleaseProgram(s_passes[s][i].program);
        s_passCount[s] = 0;
    }
    char path[600];
    snprintf(path, sizeof(path), "%s/post.ini", s_folder);
    s_iniTime = FileTime(path);
    char* text = ReadFile(path);
    if(!text)
        return;
    int stage = -1;
    for(char* line = strtok(text, "\r\n"); line; line = strtok(NULL, "\r\n"))
    {
        char* c = strpbrk(line, "#;");
        if(c)
            *c = 0;
        while(*line == ' ' || *line == '\t')
            line++;
        char* end = line + strlen(line);
        while(end > line && (end[-1] == ' ' || end[-1] == '\t'))
            *--end = 0;
        if(!*line)
            continue;
        if(strcmp(line, "[scene]") == 0)
            stage = PGL_POST_SCENE;
        else if(strcmp(line, "[final]") == 0)
            stage = PGL_POST_FINAL;
        else if(line[0] == '[')
        {
            fprintf(stderr, "PDDI: post-processing: unknown section %s in %s\n", line, path);
            stage = -1;
        }
        else if(stage < 0)
            fprintf(stderr, "PDDI: post-processing: %s is outside [scene] or [final] in %s\n", line, path);
        else if(s_passCount[stage] == MAX_PASSES)
            fprintf(stderr, "PDDI: post-processing: more than %d passes in a stage, %s left out\n", MAX_PASSES, line);
        else
        {
            Pass& p = s_passes[stage][s_passCount[stage]++];
            pvkImage target = p.target;     // kept (resized as needed)
            memset(&p, 0, sizeof(p));
            p.target = target;
            snprintf(p.file, sizeof(p.file), "%s", line);
            BuildPass(p);
        }
    }
    free(text);
    for(int s = 0; s < PGL_POST_STAGES; s++)
    {
        if(!s_passCount[s])
            continue;
        fprintf(stderr, "PDDI: post-processing, %s stage:", s == PGL_POST_SCENE ? "scene" : "final");
        for(int i = 0; i < s_passCount[s]; i++)
            fprintf(stderr, " %s%s", s_passes[s][i].file, s_passes[s][i].ok ? "" : " (failed)");
        fprintf(stderr, "\n");
    }
}

static bool Setup()
{
    VkDescriptorSetLayoutBinding bindings[1 + SAMPLER_BINDINGS];
    memset(bindings, 0, sizeof(bindings));
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    for(int i = 1; i <= SAMPLER_BINDINGS; i++)
    {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo set = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    set.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    set.bindingCount = 1 + SAMPLER_BINDINGS;
    set.pBindings = bindings;
    VkPipelineLayoutCreateInfo layout = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &s_setLayout;
    VkSamplerCreateInfo s = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    s.addressModeU = s.addressModeV = s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    s.magFilter = s.minFilter = VK_FILTER_LINEAR;
    VkSamplerCreateInfo n = s;
    n.magFilter = n.minFilter = VK_FILTER_NEAREST;
    if(vkCreateDescriptorSetLayout(g_pvk->device, &set, NULL, &s_setLayout) != VK_SUCCESS ||
       vkCreatePipelineLayout(g_pvk->device, &layout, NULL, &s_layout) != VK_SUCCESS ||
       vkCreateSampler(g_pvk->device, &s, NULL, &s_linear) != VK_SUCCESS ||
       vkCreateSampler(g_pvk->device, &n, NULL, &s_nearest) != VK_SUCCESS)
        return false;
    if(!BuildProgram(s_depthProgram, "post-processing depth copy", NULL, s_depthFragmentSource, NULL, 0))
        return false;
    shar_glsl_program compiled;
    if(shar_glsl_compile("post-processing SPIR-V vertex", NULL, s_spirvVertexSource, NULL,
                         "#version 450\nvoid main()\n{\n}\n", &compiled))
        s_spirvVertex = pvkCreateShaderModule(compiled.vertex, compiled.vertexWords);
    shar_glsl_free(&compiled);
    return true;
}

static void CheckForChanges()
{
    Uint32 now = SDL_GetTicks();
    if(s_state == 1 && now - s_lastCheck < 1000)
        return;
    s_lastCheck = now;
    if(s_state == 0)
    {
        const char* e = getenv("SHAR_POST");
        if(e && e[0] == '0')
        {
            s_state = -1;
            return;
        }
        if(!Setup())
        {
            fprintf(stderr, "PDDI: post-processing unavailable on Vulkan\n");
            s_state = -1;
            return;
        }
        pglShaderFolder(s_folder, sizeof(s_folder));
        s_state = 1;
        LoadConfig();
        return;
    }
    char folder[512];
    pglShaderFolder(folder, sizeof(folder));
    if(strcmp(folder, s_folder) != 0)       // another pack
    {
        snprintf(s_folder, sizeof(s_folder), "%s", folder);
        LoadConfig();
        return;
    }
    char path[600];
    snprintf(path, sizeof(path), "%s/post.ini", s_folder);
    if(FileTime(path) != s_iniTime)
    {
        LoadConfig();
        return;
    }
    for(int s = 0; s < PGL_POST_STAGES; s++)
    {
        for(int i = 0; i < s_passCount[s]; i++)
        {
            Pass& p = s_passes[s][i];
            char file[800];
            snprintf(file, sizeof(file), "%s/%s", s_folder, p.file);
            if(FileTime(file) != p.mtime)
            {
                BuildPass(p);
                fprintf(stderr, "PDDI: post-processing: reloaded %s%s\n", p.file, p.ok ? "" : " (failed)");
            }
        }
    }
}

//-----------------------------------------------------------------------------
// Running a stage
//-----------------------------------------------------------------------------

static void Invert(const float* m, float* out)
{
    float inv[16];
    inv[0] = m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8] = m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    inv[1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5] = m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9] = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] = m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    inv[2] = m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
    inv[6] = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
    inv[10] = m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];
    inv[3] = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
    inv[7] = m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11] - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
    inv[15] = m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10] + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];
    float det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
    if(det == 0.0f)
    {
        memset(out, 0, 16 * sizeof(float));
        out[0] = out[5] = out[10] = out[15] = 1.0f;
        return;
    }
    for(int i = 0; i < 16; i++)
        out[i] = inv[i] / det;
}

static bool Fit(pvkImage& image, int w, int h, VkFormat format)
{
    if(image.image && image.width == w && image.height == h)
        return true;
    g_pvk->Retire(image);
    return g_pvk->CreateImage(w, h, 1, format, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                              VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                              VK_IMAGE_ASPECT_COLOR_BIT, &image);
}

// the frame's depth buffer as a depth-only view (sampling takes one aspect)
static VkImageView DepthView(pvkImage& depth, VkImageView& cached, VkImage& cachedImage)
{
    if(cachedImage == depth.image && cached)
        return cached;
    // views are cheap; the old one may be in a frame in flight, so it is left (the depth buffer changes with the window
    // size and anti-aliasing only)
    cached = VK_NULL_HANDLE;
    VkImageViewCreateInfo view = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    view.image = depth.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = depth.format;
    view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    view.subresourceRange.levelCount = 1;
    view.subresourceRange.layerCount = 1;
    vkCreateImageView(g_pvk->device, &view, NULL, &cached);
    cachedImage = depth.image;
    return cached;
}

// one screen triangle into target (flipped: the frame itself, stored top row first)
static void Draw(VkCommandBuffer cmd, Program& p, VkImageView target, int w, int h, bool flipped, const float* block,
                 VkImageView* views, VkSampler* samplers)
{
    if(!p.pipeline)
        return;
    VkRenderingAttachmentInfo colour = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    colour.imageView = target;
    colour.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colour.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colour.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo info = { VK_STRUCTURE_TYPE_RENDERING_INFO };
    info.renderArea.extent.width = w;
    info.renderArea.extent.height = h;
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &colour;
    vkCmdBeginRendering(cmd, &info);
    VkViewport viewport = { 0.0f, flipped ? (float)h : 0.0f, (float)w, flipped ? -(float)h : (float)h, 0.0f, 1.0f };
    VkRect2D scissor = { { 0, 0 }, { (unsigned)w, (unsigned)h } };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p.pipeline);

    VkWriteDescriptorSet writes[1 + SAMPLER_BINDINGS];
    VkDescriptorImageInfo images[SAMPLER_BINDINGS];
    VkDescriptorBufferInfo blockInfo;
    memset(writes, 0, sizeof(writes));
    int n = 0;
    if(p.blockSize > 0)
    {
        VkBuffer buffer;
        VkDeviceSize offset;
        VkDeviceSize align = g_pvk->properties.limits.minUniformBufferOffsetAlignment;
        unsigned char* dst = (unsigned char*)g_pvk->Transient(p.blockSize, align < 16 ? 16 : align, &buffer, &offset);
        if(!dst)
        {
            vkCmdEndRendering(cmd);
            return;
        }
        memset(dst, 0, p.blockSize);
        static const int sizes[P_COUNT] = { 8, 4, 4, 4, 4, 64, 64 };
        static const int at[P_COUNT] = { 0, 2, 3, 4, 5, 6, 22 };   // in block[]
        for(int i = 0; i < P_COUNT; i++)
            if(p.offset[i] >= 0)
                memcpy(dst + p.offset[i], block + at[i], sizes[i]);
        blockInfo.buffer = buffer;
        blockInfo.offset = offset;
        blockInfo.range = p.blockSize;
        writes[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[n].dstBinding = 0;
        writes[n].descriptorCount = 1;
        writes[n].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[n].pBufferInfo = &blockInfo;
        n++;
    }
    for(int i = 0; i < SAMPLER_BINDINGS; i++)
    {
        if(p.binding[i] < 0 || !views[i])
            continue;
        images[i].sampler = samplers[i];
        images[i].imageView = views[i];
        images[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        writes[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[n].dstBinding = p.binding[i];
        writes[n].descriptorCount = 1;
        writes[n].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[n].pImageInfo = &images[i];
        n++;
    }
    if(n)
        vkCmdPushDescriptorSetKHR(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s_layout, 0, n, writes);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRendering(cmd);
}

static int ActivePasses(int stage, Pass** passes, bool& needDepth)
{
    int n = 0;
    needDepth = false;
    for(int i = 0; i < s_passCount[stage]; i++)
    {
        if(!s_passes[stage][i].ok)
            continue;
        passes[n++] = &s_passes[stage][i];
        needDepth |= s_passes[stage][i].usesDepth;
    }
    return n;
}

void pvkPostRun(int stage, pvkPipeline* pipeline)
{
    if(!g_pvk)
        return;
    CheckForChanges();
    if(s_state != 1 || !g_pglUserShaders)
        return;
    Pass* passes[MAX_PASSES];
    bool needDepth = false;
    if(ActivePasses(stage, passes, needDepth) == 0)
        return;

    g_pvk->ResolveTarget();
    g_pvk->BeginRendering();        // the frame exists
    g_pvk->EndRendering();

    // anti-aliasing changed (the targets are made by now): passes with shar_depth_samples build for the new count
    for(int i = 0; i < s_passCount[stage]; i++)
    {
        Pass& p = s_passes[stage][i];
        if(p.wantsDepthSamples && p.depthSamples != g_pvk->DepthSamples())
            BuildPass(p);
    }
    int n = ActivePasses(stage, passes, needDepth);
    if(n == 0)
        return;
    VkImageView msDepthView = VK_NULL_HANDLE;
    for(int i = 0; i < n; i++)
        if(passes[i]->program.binding[U_DEPTH_MS] >= 0)
            msDepthView = DepthView(g_pvk->MultisampledDepth(), s_msDepthView, s_msDepthViewImage);

    VkCommandBuffer cmd = g_pvk->Cmd();
    if(msDepthView)
        g_pvk->Transition(cmd, g_pvk->MultisampledDepth(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    pvkTarget& scene = g_pvk->Scene();
    int w = scene.colour.width, h = scene.colour.height;
    if(!Fit(s_original, w, h, VK_FORMAT_R8G8B8A8_UNORM) || !Fit(s_pingPong[0], w, h, VK_FORMAT_R8G8B8A8_UNORM) ||
       !Fit(s_pingPong[1], w, h, VK_FORMAT_R8G8B8A8_UNORM) || (needDepth && !Fit(s_depth, w, h, VK_FORMAT_R32_SFLOAT)))
        return;

    // the frame so far, flipped into uOriginal
    g_pvk->Transition(cmd, scene.colour, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    g_pvk->Transition(cmd, s_original, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageBlit blit;
    memset(&blit, 0, sizeof(blit));
    blit.srcSubresource.aspectMask = blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.layerCount = blit.dstSubresource.layerCount = 1;
    blit.srcOffsets[0].y = h;
    blit.srcOffsets[1].x = w;
    blit.srcOffsets[1].z = 1;
    blit.dstOffsets[1].x = w;
    blit.dstOffsets[1].y = h;
    blit.dstOffsets[1].z = 1;
    vkCmdBlitImage(cmd, scene.colour.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, s_original.image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);
    g_pvk->Transition(cmd, s_original, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    float block[38];        // uResolution, uTime, uFrame, uNear, uFar, uProjection, uInverseProjection
    memset(block, 0, sizeof(block));
    VkImageView views[SAMPLER_BINDINGS];
    VkSampler samplers[SAMPLER_BINDINGS];
    if(needDepth)
    {
        g_pvk->Transition(cmd, scene.depth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        g_pvk->Transition(cmd, s_depth, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        memset(views, 0, sizeof(views));
        views[U_SCENE] = DepthView(scene.depth, s_depthView, s_depthViewImage);
        samplers[U_SCENE] = s_nearest;
        Draw(cmd, s_depthProgram, s_depth.view, w, h, false, block, views, samplers);
        g_pvk->Transition(cmd, s_depth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    float nearPlane = 0.0f, farPlane = 0.0f;
    const float* m = pipeline->GetProjection();
    if(m[11] == -1.0f && m[10] != 1.0f && m[10] != -1.0f)
    {
        nearPlane = m[14] / (m[10] - 1.0f);
        farPlane = m[14] / (m[10] + 1.0f);
    }
    static int s_frame = 0;
    s_frame++;
    block[2] = SDL_GetTicks() / 1000.0f;
    int frame = s_frame;
    memcpy(&block[3], &frame, 4);
    block[4] = nearPlane;
    block[5] = farPlane;
    memcpy(&block[6], m, 64);
    Invert(m, &block[22]);

    pvkImage* input = &s_original;
    for(int i = 0; i < n; i++)
    {
        Pass& p = *passes[i];
        bool last = i == n - 1;
        pvkImage* output = &s_pingPong[i & 1];
        int outW = w, outH = h;
        if(!last && p.scale < 1.0f)
        {
            outW = (int)(w * p.scale + 0.5f);
            outH = (int)(h * p.scale + 0.5f);
            outW = outW < 1 ? 1 : outW;
            outH = outH < 1 ? 1 : outH;
            if(!Fit(p.target, outW, outH, VK_FORMAT_R8G8B8A8_UNORM))
                return;
            output = &p.target;
        }
        if(last)
            output = &scene.colour;

        // a pass that begins an effect makes its input the effect's uOriginal
        if(p.beginsEffect && input != &s_original)
        {
            g_pvk->Transition(cmd, *input, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            g_pvk->Transition(cmd, s_original, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkImageBlit b;
            memset(&b, 0, sizeof(b));
            b.srcSubresource.aspectMask = b.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            b.srcSubresource.layerCount = b.dstSubresource.layerCount = 1;
            b.srcOffsets[1].x = input->width;
            b.srcOffsets[1].y = input->height;
            b.srcOffsets[1].z = 1;
            b.dstOffsets[1].x = w;
            b.dstOffsets[1].y = h;
            b.dstOffsets[1].z = 1;
            vkCmdBlitImage(cmd, input->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, s_original.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &b,
                           input->width == w && input->height == h ? VK_FILTER_NEAREST : VK_FILTER_LINEAR);
            g_pvk->Transition(cmd, s_original, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        g_pvk->Transition(cmd, *input, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        g_pvk->Transition(cmd, *output, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        memset(views, 0, sizeof(views));
        views[U_SCENE] = input->view;
        views[U_ORIGINAL] = s_original.view;
        views[U_DEPTH] = needDepth ? s_depth.view : s_original.view;
        views[U_DEPTH_MS] = msDepthView;
        samplers[U_SCENE] = samplers[U_ORIGINAL] = s_linear;
        samplers[U_DEPTH] = samplers[U_DEPTH_MS] = s_nearest;
        for(int t = 0; t < p.textureCount; t++)
        {
            pvkImage& image = s_images[p.textureImage[t]].image;
            views[U_TEXTURE0 + t] = image.view ? image.view : s_original.view;
            samplers[U_TEXTURE0 + t] = s_linear;
        }
        block[0] = (float)outW;
        block[1] = (float)outH;
        Draw(cmd, p.program, output->view, outW, outH, last, block, views, samplers);
        input = output;
    }
    pipeline->Invalidate();     // the frame's draws bind everything again
}

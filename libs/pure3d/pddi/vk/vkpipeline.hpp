//=============================================================================
// Linux port: Vulkan renderer pipeline (as pglPipeline, glpipeline.hpp)
//=============================================================================

// The same CPU-side fixed-function state as the GL pipeline (matrices, lights, fog, material, current vertex values)
// drawn with the same programs (glshaders.cpp, PVK defined). The programs' loose uniforms become one uniform block
// (GL_EXT_vulkan_glsl_relaxed); each program keeps a CPU copy of its block, written as GL took glUniform* calls, and
// copied to the frame's transient ring when it changed. Descriptors are pushed (VK_KHR_push_descriptor): the block and
// the program's samplers. The rest of the GL state the pddi set (blending, depth, stencil, culling, colour writes,
// viewport, scissor) is RenderState: its dynamic parts are set on the command buffer as they change, the others
// choose the pipeline object (cached by key).

#ifndef _VKPIPELINE_HPP_
#define _VKPIPELINE_HPP_

#include <pddi/vk/vkcore.hpp>
#include <pddi/gl/glshaders.hpp>

class pvkTexture;

// one vertex of an immediate-mode stream (as pglVertex)
struct pvkVertex
{
    float position[3];
    float normal[3];
    float uv[2];
    unsigned char colour[4];
};

const int pvkMaxLights = 8;
const int pvkMaxCustomPrograms = 32;
enum { PVK_CULL_GAME, PVK_CULL_NONE };      // as PGL_CULL_*

// the vertex attributes' bindings (one attribute per binding: streams interleave, prim buffers do not)
enum { PVK_ATTRIB_POSITION, PVK_ATTRIB_NORMAL, PVK_ATTRIB_COLOUR, PVK_ATTRIB_UV, PVK_ATTRIB_COUNT };

enum pvkTopology { PVK_TRIANGLES, PVK_TRISTRIP, PVK_LINES, PVK_LINESTRIP, PVK_POINTS };   // pddiPrimType order

class pvkPipeline
{
public:
    // as pglPipeline::Material
    struct Material
    {
        int program;
        int custom;
        bool textured;
        bool reflectionTextured;
        bool lit;
        int alphaCompare;
        float alphaRef;
        bool blended;
        float ambient[4];
        float diffuse[4];
        float specular[4];
        float emissive[4];
        float shininess;
        float envBlend[4];
        float refractColour[4];
        float refractIndex;
        float refractBlend;
    };

    // the GL state of the GL pddi, as the context and materials set it
    struct RenderState
    {
        bool cull;
        bool cullFront;             // else back (PDDI_CULL_INVERTED)
        bool depthTest, depthWrite;
        int depthCompare;           // pddiCompareMode
        bool stencilTest;
        int stencilCompare;
        unsigned stencilRef, stencilMask, stencilWriteMask;
        int stencilFail, stencilZFail, stencilZPass;    // pddiStencilOp
        unsigned colourWrite;       // RGBA bits
        int blend;                  // pddiBlendMode, PDDI_BLEND_NONE = off
        int fill;                   // pddiFillMode
        int viewport[4];            // x, y (from the top), width, height in pixels
        float depthMin, depthMax;
        bool scissor;
        int scissorRect[4];         // x, y (from the top), width, height
    } state;

    pvkPipeline();
    ~pvkPipeline();

    bool Init();
    void Shutdown();

    void SetProjection(const float* m);
    const float* GetProjection() const { return projection; }
    void SetModelView(const float* m);
    void LoadIdentity();
    void SetLight(int i, bool enabled, const float* position, const float* colour, float attenA, float attenB, float attenC,
                  float range);
    void SetAmbient(const float* colour);
    void EnableFog(bool enable);
    void SetFog(const float* colour, float start, float end);
    void SetMaterial(const Material& m);

    // the material's textures and how they are sampled (pddiFilterMode, pddiUVMode); NULL: none
    void SetTextures(pvkTexture* base, pvkTexture* reflection, int filterMode, int uvMode);

    pvkVertex current;
    void BeginStream(int primType);
    void Emit(float x, float y, float z);
    void EndStream();

    // prim buffers: the material's state bound, then the caller binds its buffers and draws (Draw*); the
    // attributes it lacks take the current values
    VkCommandBuffer Prepare(int primType, unsigned missingAttributes);
    void BindCurrent(VkCommandBuffer cmd, unsigned missingAttributes, VkBuffer* buffers, VkDeviceSize* offsets,
                     VkDeviceSize* strides);

    int BuildCustomProgram(const char* name, const char* source);
    int CustomCull(int slot) const;
    void ReleaseCustomPrograms();
    int CustomProgramCount() const { return customCount; }

    void NewFrame(int width, int height);
    void SetTextureFiltering(bool mipmaps, float anisotropy);
    void CaptureScreen();       // refraction: the frame so far, sampled as uScreen

    void FrameStats();
    void FrameEnd();
    struct Stats { unsigned frames, draws, streamDraws, pipelines, uniformCopies, uploads; double cpuMs; } stats;

private:
    enum { GROUP_MATRICES, GROUP_LIGHTS, GROUP_FOG, GROUP_MATERIAL, GROUP_COUNT };
    enum { SAMPLER_TEXTURE, SAMPLER_REFLECTION, SAMPLER_SCREEN, SAMPLER_COUNT };

    struct Program
    {
        bool built;
        VkShaderModule vertex, fragment;
        int blockSize;
        int offset[U_COUNT];        // in the block, -1: not in this program
        int stride[U_COUNT];        // array element stride
        int glType[U_COUNT];
        int binding[SAMPLER_COUNT]; // descriptor binding, -1: not used
        unsigned char* block;       // CPU copy of the uniform block
        bool blockChanged;
        VkBuffer blockBuffer;       // its copy in this frame's ring
        VkDeviceSize blockOffset;
        unsigned long long blockSerial;
        unsigned uploaded[GROUP_COUNT];
        unsigned timeFrame;
        int cull;
    };

    void Changed(int group) { version[group]++; }
    bool AlphaToCoverage() const;
    bool BuildProgram(Program& p, const char* name, const char* header, const char* fragmentBody, bool custom,
                      const char* fragmentHeader, const char* vertexBody);
    void ReleaseProgram(Program& p);
    void UploadUniforms(Program& p);
    void SetFloats(Program& p, int u, const float* v, int count, int components);
    void SetInt(Program& p, int u, int v);
    VkPipeline GetPipeline(int programIndex, int topologyClass);
    VkCommandBuffer Bind(int primType);
    VkSampler GetSampler(int filterMode, int uvMode, bool mipmapped);

    bool ready;
    float mipAlpha;
    Program programs[PGL_PROGRAM_COUNT + pvkMaxCustomPrograms];
    int customCount;
    unsigned frameCount;
    float frameTime, frameWidth, frameHeight;
    unsigned version[GROUP_COUNT];

    VkDescriptorSetLayout setLayout;
    VkPipelineLayout layout;
    VkPipelineCache cache;

    // pipeline objects by key
    struct CachedPipeline { unsigned key; VkPipeline pipeline; };
    CachedPipeline* pipelines;
    int pipelineCapacity, pipelineCount;

    // samplers by filter, address mode, mipmapping
    struct CachedSampler { unsigned key; VkSampler sampler; };
    CachedSampler samplers[64];
    int samplerCount;
    float samplerAnisotropy;    // the setting the cached samplers were made with

    pvkImage white;             // for samplers a draw does not set
    VkSampler whiteSampler;

    // what the command buffer has (per frame: reset when the serial changes)
    unsigned long long boundSerial;
    VkPipeline boundPipeline;
    Program* boundProgram;
    RenderState applied;
    int appliedTopology;
    bool descriptorsDirty;
    VkBuffer pushedBlock;
    VkDeviceSize pushedOffset;
    VkImageView pushedViews[SAMPLER_COUNT];
    VkSampler pushedSamplers[SAMPLER_COUNT];

    pvkTexture* texture;
    pvkTexture* reflection;
    int filterMode, uvMode;

    // the refraction programs' screen copy
    pvkImage screen;
    unsigned screenFrame;

    pvkVertex* streamVerts;
    int streamCount, streamCapacity;
    int streamPrim;

    float projection[16];
    float modelView[16];
    struct Light
    {
        bool enabled;
        float position[4];
        float colour[3];
        float atten[3];
        float range;
    } lights[pvkMaxLights];
    float ambient[4];
    bool fogEnabled;
    float fogColour[3];
    float fogStart, fogEnd;
    Material material;
};

void pvkRecaptureScreen();

// user material shaders (gl/glcustom.cpp, shared): from BeginFrame
void pvkCustomUpdate(pvkPipeline* pipeline);

#endif

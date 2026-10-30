//=============================================================================
// Linux port: Vulkan renderer core (pddi/vk): instance, device, memory, frames, render targets, swapchain.
//=============================================================================

// The Vulkan pddi mirrors the OpenGL one (pddi/gl): the same pddi classes, the same GLSL (glshaders.cpp, compiled to
// SPIR-V by libshar_glslang.so), the same D3D8 parity rules. It needs Vulkan 1.3 (dynamic rendering, synchronization2,
// extended dynamic state) and VK_KHR_push_descriptor; without them the platform layer falls back to OpenGL.
//
// Frames: the game draws into offscreen targets (colour + depth/stencil, multisampled ones too with anti-aliasing)
// that keep their contents between frames, as GL's window did. SwapBuffers draws the colour target into a swapchain
// image through the gamma pass and presents it. pvkFrames frames are in flight; each has a command buffer for
// uploads (submitted first) and one for drawing, a transient ring (streamed vertices, uniforms, staging) and a fence.
// GPU objects freed by the game (any thread) are destroyed once the frames that might use them have finished.
//
// Pure3D creates textures and prim buffers on its loader threads: as in the GL pddi they keep a CPU copy and are
// uploaded on the render thread when first drawn. Only memory allocation and Retire are called from other threads.

#ifndef _VKCORE_HPP_
#define _VKCORE_HPP_

#include <pddi/vk/vkfuncs.hpp>
#include <pthread.h>
#include <SDL2/SDL.h>

const int pvkFrames = 2;

struct pvkMemory
{
    VkDeviceMemory memory;
    VkDeviceSize offset, size;
    unsigned char* mapped;      // host-visible allocations: mapped for good
    int block;                  // the allocator's block, -1 for a dedicated allocation
};

struct pvkBuffer
{
    VkBuffer buffer;
    pvkMemory memory;
    VkDeviceSize size;
};

struct pvkImage
{
    VkImage image;
    VkImageView view;
    pvkMemory memory;
    VkFormat format;
    VkImageAspectFlags aspect;
    int width, height, mips;
    VkSampleCountFlagBits samples;
    VkImageLayout layout;       // as last transitioned (Transition)
};

// where draws go: colour + depth/stencil, single or multisampled
struct pvkTarget
{
    pvkImage colour;
    pvkImage depth;
};

class pvkCore
{
public:
    pvkCore();

    // everything up to the swapchain; false (with the reason on stderr) when Vulkan cannot draw into this window
    bool Init(SDL_Window* window);
    void Shutdown();

    VkInstance instance;
    VkPhysicalDevice physical;
    VkDevice device;
    VkQueue queue;
    unsigned queueFamily;
    VkPhysicalDeviceProperties properties;
    VkPhysicalDeviceMemoryProperties memoryProperties;
    bool bcTextures;            // textureCompressionBC (DXT)
    bool anisotropy;            // samplerAnisotropy
    bool fillModeNonSolid;
    VkFormat depthFormat;       // D24S8, else D32S8
    VkSampleCountFlags sampleCounts;    // usable for colour and depth targets

    // memory: suballocated from large blocks (thread safe)
    bool Allocate(const VkMemoryRequirements& req, VkMemoryPropertyFlags flags, bool linear, pvkMemory* out);
    void Free(pvkMemory& m);
    bool CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible, pvkBuffer* out);
    bool CreateImage(int width, int height, int mips, VkFormat format, VkSampleCountFlagBits samples,
                     VkImageUsageFlags usage, VkImageAspectFlags aspect, pvkImage* out);
    void DestroyNow(pvkBuffer& b);
    void DestroyNow(pvkImage& i);

    // destroyed once the GPU is past the frame being recorded (any thread)
    void Retire(pvkBuffer& b);
    void Retire(pvkImage& i);
    void Retire(VkPipeline pipeline);
    void Retire(VkSampler sampler);

    // the frame being recorded (render thread)
    VkCommandBuffer Cmd();
    // copies that run before the frame's draws (their results are visible to everything after it)
    VkCommandBuffer UploadCmd();
    // space for this frame only, host-visible: vertices, uniforms, staging. NULL when out of memory.
    void* Transient(VkDeviceSize size, VkDeviceSize align, VkBuffer* buffer, VkDeviceSize* offset);
    unsigned long long FrameSerial() const { return serial; }

    // layout transition with a full barrier (the game's frame needs few: correctness over overlap)
    void Transition(VkCommandBuffer cmd, pvkImage& image, VkImageLayout layout);

    // draw targets: the frame's (multisampled when anti-aliasing is on), resized with the window
    void SetSize(int width, int height);
    void SetSamples(int samples);       // 1 = off; clamped to what the device has
    int Samples() const { return targetSamples; }
    int Width() const { return scene.colour.image ? scene.colour.width : width; }      // of the targets as made
    int Height() const { return scene.colour.image ? scene.colour.height : height; }
    pvkTarget& Target() { return msActive ? msTarget : scene; }
    int ActiveSamples() const { return msActive ? targetSamples : 1; }  // of Target(): pipelines are made for it
    pvkTarget& Scene() { return scene; }        // the single-sampled one (what is presented)
    // dynamic rendering into the current target (contents kept); EndRendering before transfers
    void BeginRendering();
    void EndRendering();
    bool Rendering() const { return rendering; }
    unsigned RenderingGeneration() const { return renderingGeneration; }   // dynamic state is lost between instances
    void ResolveTarget();                       // multisampled -> scene (colour, depth, stencil); then draws go to scene

    // submits the frame and presents the scene colour through the gamma pass; starts the next frame
    void Present(float gamma, SDL_Window* window);
    void BeginFrame();
    void WaitIdle();

    // SHAR_SHOT_EVERY and pddiDisplay::Screenshot: the scene colour, read back at the next Present (RGBA, top row
    // first); delivered to the callback when the frame completes
    void RequestReadback(void (*done)(const unsigned char* rgba, int width, int height));

private:
    struct Frame
    {
        VkCommandPool pool;
        VkCommandBuffer cmd, upload;
        VkFence fence;
        VkSemaphore acquired;
        unsigned long long serial;
        // transient ring: chunks of host-visible memory, reused each time the frame comes round
        struct Chunk { pvkBuffer buffer; VkDeviceSize used; } chunks[16];
        int chunkCount;
        pvkBuffer readback;
        void (*readbackDone)(const unsigned char*, int, int);
        int readbackWidth, readbackHeight;
    };

    bool CreateInstance(SDL_Window* window);
    bool PickDevice();
    bool CreateDevice();
    bool CreateSwapchain(SDL_Window* window);
    void DestroySwapchain();
    bool CreateTargets();
    void DestroyTargets();
    bool CreatePresentPass();
    void FlushRetired(unsigned long long completed);

    VkDebugUtilsMessengerEXT messenger;
    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    VkFormat swapchainFormat;
    VkPresentModeKHR presentMode;
    VkImage swapchainImages[8];
    VkImageView swapchainViews[8];
    VkSemaphore swapchainDone[8];
    unsigned swapchainCount;
    int swapchainWidth, swapchainHeight;
    bool swapchainStale;

    Frame frames[pvkFrames];
    int frame;
    unsigned long long serial;
    bool uploadsRecorded;
    bool frameOpen;

    int width, height;              // draw targets' size
    int requestedSamples, targetSamples;
    pvkTarget scene, msTarget;
    bool msActive;                  // this frame draws into msTarget (until ResolveTarget)
    bool rendering;
    unsigned renderingGeneration;
    bool targetsDirty;

    // the present (gamma) pass
    VkDescriptorSetLayout presentSetLayout;
    VkPipelineLayout presentLayout;
    VkPipeline presentPipeline;
    VkFormat presentPipelineFormat;
    VkSampler presentSampler;
    VkShaderModule presentVS, presentFS;
    int presentBinding;

    void (*pendingReadback)(const unsigned char*, int, int);

    // memory blocks
    struct Block
    {
        VkDeviceMemory memory;
        VkDeviceSize size;
        unsigned type;
        bool linear;
        unsigned char* mapped;
        struct Range { VkDeviceSize offset, size; } *free;
        int freeCount, freeCapacity;
    };
    Block* blocks;
    int blockCount, blockCapacity;
    pthread_mutex_t memoryLock;

    // retired objects
    struct Retired
    {
        unsigned long long serial;
        int kind;                   // 0 buffer, 1 image, 2 pipeline, 3 sampler
        pvkBuffer buffer;
        pvkImage image;
        VkPipeline pipeline;
        VkSampler sampler;
    };
    Retired* retired;
    int retiredCount, retiredCapacity;
    pthread_mutex_t retireLock;
    void PushRetired(const Retired& r);
};

extern pvkCore* g_pvk;      // set while the Vulkan renderer runs

// the platform layer (linuxplatform_sdl.cpp): starts Vulkan for an SDL_WINDOW_VULKAN window; false = use OpenGL
bool pvkStart(SDL_Window* window);
bool pvkActive();

// SPIR-V from the renderer's GLSL (libshar_glslang.so); NULL on failure (log printed)
VkShaderModule pvkCreateShaderModule(const unsigned* words, size_t count);

#endif

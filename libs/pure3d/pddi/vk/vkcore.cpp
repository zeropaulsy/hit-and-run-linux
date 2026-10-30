//=============================================================================
// Linux port: Vulkan renderer core (see vkcore.hpp)
//=============================================================================

#include <pddi/vk/vkcore.hpp>

#include <SDL2/SDL_vulkan.h>
#include <shar_glslang.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

pvkCore* g_pvk = NULL;

static const VkDeviceSize kBlockSize = 64 * 1024 * 1024;       // device memory is suballocated from these
static const VkDeviceSize kDedicatedSize = 32 * 1024 * 1024;   // larger requests get their own allocation
static const VkDeviceSize kChunkSize = 8 * 1024 * 1024;        // transient ring chunks

static int EnvInt(const char* name, int fallback)
{
    const char* e = getenv(name);
    return e ? atoi(e) : fallback;
}

pvkCore::pvkCore()
{
    memset(this, 0, sizeof(*this));     // plain data and handles only
    pthread_mutex_init(&memoryLock, NULL);
    pthread_mutex_init(&retireLock, NULL);
    requestedSamples = targetSamples = 1;
}

//-----------------------------------------------------------------------------
// Instance and device
//-----------------------------------------------------------------------------

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                   VkDebugUtilsMessageTypeFlagsEXT type,
                                                   const VkDebugUtilsMessengerCallbackDataEXT* data, void* user)
{
    static int s_count = 0;
    if(s_count++ < 300)
        fprintf(stderr, "Vulkan %s: %s\n", severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ? "error" : "warning",
                data->pMessage);
    return VK_FALSE;
}

bool pvkCore::CreateInstance(SDL_Window* window)
{
    unsigned version = VK_API_VERSION_1_0;
    vkEnumerateInstanceVersion(&version);
    if(version < VK_API_VERSION_1_3)
    {
        fprintf(stderr, "PDDI: the Vulkan loader is %u.%u, 1.3 is needed\n", VK_API_VERSION_MAJOR(version),
                VK_API_VERSION_MINOR(version));
        return false;
    }

    const char* extensions[16];
    unsigned count = 0;
    if(!SDL_Vulkan_GetInstanceExtensions(window, &count, NULL) || count > 14 ||
       !SDL_Vulkan_GetInstanceExtensions(window, &count, extensions))
    {
        fprintf(stderr, "PDDI: SDL has no Vulkan surface for this window (%s)\n", SDL_GetError());
        return false;
    }

    // SHAR_VK_DEBUG=1: the validation layer (package vulkan-validationlayers) and its messages on stderr
    bool debug = EnvInt("SHAR_VK_DEBUG", 0) != 0;
    const char* layers[1] = { "VK_LAYER_KHRONOS_validation" };
    unsigned layerCount = 0;
    if(debug)
    {
        unsigned n = 0;
        vkEnumerateInstanceLayerProperties(&n, NULL);
        VkLayerProperties* props = (VkLayerProperties*)calloc(n ? n : 1, sizeof(VkLayerProperties));
        vkEnumerateInstanceLayerProperties(&n, props);
        for(unsigned i = 0; i < n; i++)
            if(strcmp(props[i].layerName, layers[0]) == 0)
                layerCount = 1;
        free(props);
        if(!layerCount)
            fprintf(stderr, "PDDI: SHAR_VK_DEBUG: no validation layer installed (vulkan-validationlayers)\n");
        extensions[count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    }

    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = "The Simpsons: Hit & Run";
    app.pEngineName = "Pure3D";
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo info = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = count;
    info.ppEnabledExtensionNames = extensions;
    info.enabledLayerCount = layerCount;
    info.ppEnabledLayerNames = layers;
    VkResult r = vkCreateInstance(&info, NULL, &instance);
    if(r != VK_SUCCESS && debug)
    {
        info.enabledExtensionCount = count - 1;     // no debug utils after all
        info.enabledLayerCount = 0;
        r = vkCreateInstance(&info, NULL, &instance);
        debug = false;
    }
    if(r != VK_SUCCESS)
    {
        fprintf(stderr, "PDDI: vkCreateInstance failed (%d)\n", r);
        return false;
    }
    if(!pvkLoadInstance(instance))
        return false;

    if(debug && vkCreateDebugUtilsMessengerEXT)
    {
        VkDebugUtilsMessengerCreateInfoEXT m = { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
        m.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        m.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        m.pfnUserCallback = DebugMessage;
        vkCreateDebugUtilsMessengerEXT(instance, &m, NULL, &messenger);
        fprintf(stderr, "PDDI: Vulkan validation on\n");
    }

    if(!SDL_Vulkan_CreateSurface(window, instance, &surface))
    {
        fprintf(stderr, "PDDI: SDL_Vulkan_CreateSurface failed (%s)\n", SDL_GetError());
        return false;
    }
    return true;
}

static bool HasExtension(VkPhysicalDevice device, const char* name)
{
    unsigned n = 0;
    vkEnumerateDeviceExtensionProperties(device, NULL, &n, NULL);
    VkExtensionProperties* props = (VkExtensionProperties*)calloc(n ? n : 1, sizeof(VkExtensionProperties));
    vkEnumerateDeviceExtensionProperties(device, NULL, &n, props);
    bool found = false;
    for(unsigned i = 0; i < n && !found; i++)
        found = strcmp(props[i].extensionName, name) == 0;
    free(props);
    return found;
}

// a 1.3 device with a queue that draws and presents to the window, the swapchain and push descriptors; discrete GPUs
// first. SHAR_VK_DEVICE=n picks the n-th device (vulkaninfo --summary order) when it qualifies.
bool pvkCore::PickDevice()
{
    unsigned n = 0;
    vkEnumeratePhysicalDevices(instance, &n, NULL);
    if(n == 0)
    {
        fprintf(stderr, "PDDI: no Vulkan devices\n");
        return false;
    }
    VkPhysicalDevice* devices = (VkPhysicalDevice*)calloc(n, sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(instance, &n, devices);
    int wanted = EnvInt("SHAR_VK_DEVICE", -1);
    int bestScore = -1;
    for(unsigned d = 0; d < n; d++)
    {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(devices[d], &p);
        const char* why = NULL;
        unsigned family = ~0u;
        if(p.apiVersion < VK_API_VERSION_1_3)
            why = "Vulkan 1.3 needed";
        else if(!HasExtension(devices[d], VK_KHR_SWAPCHAIN_EXTENSION_NAME))
            why = "no swapchain";
        else if(!HasExtension(devices[d], VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME))
            why = "no push descriptors";
        else
        {
            unsigned qn = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &qn, NULL);
            VkQueueFamilyProperties* q = (VkQueueFamilyProperties*)calloc(qn ? qn : 1, sizeof(VkQueueFamilyProperties));
            vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &qn, q);
            for(unsigned i = 0; i < qn && family == ~0u; i++)
            {
                VkBool32 present = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(devices[d], i, surface, &present);
                if((q[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present)
                    family = i;
            }
            free(q);
            if(family == ~0u)
                why = "cannot present to the window";
        }
        if(why)
        {
            fprintf(stderr, "PDDI: Vulkan device %u (%s): %s\n", d, p.deviceName, why);
            continue;
        }
        int score = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 4 :
                    p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 3 :
                    p.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU ? 0 : 1;
        if((int)d == wanted)
            score = 100;
        if(score > bestScore)
        {
            bestScore = score;
            physical = devices[d];
            queueFamily = family;
            properties = p;
        }
    }
    free(devices);
    if(bestScore < 0)
        return false;
    vkGetPhysicalDeviceMemoryProperties(physical, &memoryProperties);
    return true;
}

bool pvkCore::CreateDevice()
{
    VkPhysicalDeviceFeatures available;
    vkGetPhysicalDeviceFeatures(physical, &available);
    bcTextures = available.textureCompressionBC && !getenv("SHAR_GL_NO_S3TC");
    anisotropy = available.samplerAnisotropy != 0;
    fillModeNonSolid = available.fillModeNonSolid != 0;

    VkPhysicalDeviceVulkan13Features f13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    VkPhysicalDeviceFeatures2 query = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    query.pNext = &f13;
    vkGetPhysicalDeviceFeatures2(physical, &query);
    if(!f13.dynamicRendering || !f13.synchronization2)
    {
        fprintf(stderr, "PDDI: %s lacks dynamic rendering or synchronization2\n", properties.deviceName);
        return false;
    }

    VkPhysicalDeviceVulkan13Features enable13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    enable13.dynamicRendering = VK_TRUE;
    enable13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceFeatures2 features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    features.pNext = &enable13;
    features.features.textureCompressionBC = bcTextures;
    features.features.samplerAnisotropy = anisotropy;
    features.features.fillModeNonSolid = fillModeNonSolid;

    float priority = 1.0f;
    VkDeviceQueueCreateInfo q = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    q.queueFamilyIndex = queueFamily;
    q.queueCount = 1;
    q.pQueuePriorities = &priority;
    const char* extensions[2] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME };
    VkDeviceCreateInfo info = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    info.pNext = &features;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &q;
    info.enabledExtensionCount = 2;
    info.ppEnabledExtensionNames = extensions;
    VkResult r = vkCreateDevice(physical, &info, NULL, &device);
    if(r != VK_SUCCESS)
    {
        fprintf(stderr, "PDDI: vkCreateDevice failed (%d)\n", r);
        return false;
    }
    if(!pvkLoadDevice(device))
        return false;
    vkGetDeviceQueue(device, queueFamily, 0, &queue);

    // depth/stencil as the GL window's (24/8) where the device has it (not AMD), else 32-bit float depth
    depthFormat = VK_FORMAT_UNDEFINED;
    VkFormat candidates[2] = { VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT };
    for(int i = 0; i < 2 && depthFormat == VK_FORMAT_UNDEFINED; i++)
    {
        VkFormatProperties fp;
        vkGetPhysicalDeviceFormatProperties(physical, candidates[i], &fp);
        if(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
            depthFormat = candidates[i];
    }
    if(depthFormat == VK_FORMAT_UNDEFINED)
        return false;
    sampleCounts = properties.limits.framebufferColorSampleCounts & properties.limits.framebufferDepthSampleCounts &
                   properties.limits.framebufferStencilSampleCounts;
    return true;
}

bool pvkCore::Init(SDL_Window* window)
{
    if(!pvkLoadGlobal() || !CreateInstance(window) || !PickDevice() || !CreateDevice())
        return false;

    for(int f = 0; f < pvkFrames; f++)
    {
        Frame& fr = frames[f];
        VkCommandPoolCreateInfo pool = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        pool.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pool.queueFamilyIndex = queueFamily;
        if(vkCreateCommandPool(device, &pool, NULL, &fr.pool) != VK_SUCCESS)
            return false;
        VkCommandBufferAllocateInfo alloc = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        alloc.commandPool = fr.pool;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 2;
        VkCommandBuffer cmds[2];
        if(vkAllocateCommandBuffers(device, &alloc, cmds) != VK_SUCCESS)
            return false;
        fr.cmd = cmds[0];
        fr.upload = cmds[1];
        VkFenceCreateInfo fence = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VkSemaphoreCreateInfo sem = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        if(vkCreateFence(device, &fence, NULL, &fr.fence) != VK_SUCCESS ||
           vkCreateSemaphore(device, &sem, NULL, &fr.acquired) != VK_SUCCESS)
            return false;
    }

    int w = 0, h = 0;
    SDL_Vulkan_GetDrawableSize(window, &w, &h);
    width = w > 0 ? w : 640;
    height = h > 0 ? h : 480;
    if(!CreateSwapchain(window) || !CreatePresentPass())
        return false;
    targetsDirty = true;

    fprintf(stderr, "PDDI: Vulkan %u.%u.%u, %s (driver %u)\n", VK_API_VERSION_MAJOR(properties.apiVersion),
            VK_API_VERSION_MINOR(properties.apiVersion), VK_API_VERSION_PATCH(properties.apiVersion),
            properties.deviceName, properties.driverVersion);
    return true;
}

void pvkCore::WaitIdle()
{
    if(device)
        vkDeviceWaitIdle(device);
}

void pvkCore::Shutdown()
{
    if(!device)
    {
        if(surface)
            vkDestroySurfaceKHR(instance, surface, NULL);
        if(instance)
            vkDestroyInstance(instance, NULL);
        surface = VK_NULL_HANDLE;
        instance = VK_NULL_HANDLE;
        return;
    }
    vkDeviceWaitIdle(device);
    FlushRetired(~0ull);
    DestroyTargets();
    DestroySwapchain();
    for(int f = 0; f < pvkFrames; f++)
    {
        Frame& fr = frames[f];
        for(int c = 0; c < fr.chunkCount; c++)
            DestroyNow(fr.chunks[c].buffer);
        if(fr.readback.buffer)
            DestroyNow(fr.readback);
        vkDestroyFence(device, fr.fence, NULL);
        vkDestroySemaphore(device, fr.acquired, NULL);
        vkDestroyCommandPool(device, fr.pool, NULL);
    }
    vkDestroyPipeline(device, presentPipeline, NULL);
    vkDestroyPipelineLayout(device, presentLayout, NULL);
    vkDestroyDescriptorSetLayout(device, presentSetLayout, NULL);
    vkDestroySampler(device, presentSampler, NULL);
    vkDestroyShaderModule(device, presentVS, NULL);
    vkDestroyShaderModule(device, presentFS, NULL);
    for(int b = 0; b < blockCount; b++)
    {
        vkFreeMemory(device, blocks[b].memory, NULL);
        free(blocks[b].free);
    }
    free(blocks);
    blocks = NULL;
    blockCount = 0;
    vkDestroyDevice(device, NULL);
    device = VK_NULL_HANDLE;
    vkDestroySurfaceKHR(instance, surface, NULL);
    if(messenger)
        vkDestroyDebugUtilsMessengerEXT(instance, messenger, NULL);
    vkDestroyInstance(instance, NULL);
    instance = VK_NULL_HANDLE;
}

//-----------------------------------------------------------------------------
// Memory
//-----------------------------------------------------------------------------

static VkDeviceSize AlignUp(VkDeviceSize v, VkDeviceSize a)
{
    return a > 1 ? (v + a - 1) / a * a : v;
}

bool pvkCore::Allocate(const VkMemoryRequirements& req, VkMemoryPropertyFlags flags, bool linear, pvkMemory* out)
{
    memset(out, 0, sizeof(*out));
    unsigned type = ~0u;
    for(unsigned i = 0; i < memoryProperties.memoryTypeCount && type == ~0u; i++)
        if((req.memoryTypeBits & (1u << i)) && (memoryProperties.memoryTypes[i].propertyFlags & flags) == flags)
            type = i;
    if(type == ~0u)
        return false;
    bool host = (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;

    if(req.size >= kDedicatedSize)
    {
        VkMemoryAllocateInfo info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        info.allocationSize = req.size;
        info.memoryTypeIndex = type;
        if(vkAllocateMemory(device, &info, NULL, &out->memory) != VK_SUCCESS)
            return false;
        out->size = req.size;
        out->block = -1;
        if(host)
            vkMapMemory(device, out->memory, 0, VK_WHOLE_SIZE, 0, (void**)&out->mapped);
        return true;
    }

    pthread_mutex_lock(&memoryLock);
    for(int pass = 0; pass < 2; pass++)
    {
        for(int b = 0; b < blockCount; b++)
        {
            Block& block = blocks[b];
            if(block.type != type || block.linear != linear)
                continue;
            for(int r = 0; r < block.freeCount; r++)
            {
                Block::Range range = block.free[r];
                VkDeviceSize start = AlignUp(range.offset, req.alignment);
                if(start + req.size > range.offset + range.size)
                    continue;
                // the range becomes what is left before and after the allocation
                Block::Range before = { range.offset, start - range.offset };
                Block::Range after = { start + req.size, range.offset + range.size - start - req.size };
                int pieces = (before.size ? 1 : 0) + (after.size ? 1 : 0);
                if(block.freeCount + pieces - 1 > block.freeCapacity)
                {
                    block.freeCapacity = block.freeCapacity * 2 + 8;
                    block.free = (Block::Range*)realloc(block.free, block.freeCapacity * sizeof(Block::Range));
                }
                memmove(&block.free[r + pieces], &block.free[r + 1], (block.freeCount - r - 1) * sizeof(Block::Range));
                int at = r;
                if(before.size) block.free[at++] = before;
                if(after.size) block.free[at++] = after;
                block.freeCount += pieces - 1;
                out->memory = block.memory;
                out->offset = start;
                out->size = req.size;
                out->block = b;
                out->mapped = block.mapped ? block.mapped + start : NULL;
                pthread_mutex_unlock(&memoryLock);
                return true;
            }
        }
        if(pass == 1)
            break;
        // no room: a new block
        VkMemoryAllocateInfo info = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        info.allocationSize = kBlockSize;
        info.memoryTypeIndex = type;
        VkDeviceMemory memory;
        if(vkAllocateMemory(device, &info, NULL, &memory) != VK_SUCCESS)
            break;
        if(blockCount == blockCapacity)
        {
            blockCapacity = blockCapacity * 2 + 8;
            blocks = (Block*)realloc(blocks, blockCapacity * sizeof(Block));
        }
        Block& block = blocks[blockCount++];
        memset(&block, 0, sizeof(block));
        block.memory = memory;
        block.size = kBlockSize;
        block.type = type;
        block.linear = linear;
        if(host)
            vkMapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, (void**)&block.mapped);
        block.freeCapacity = 8;
        block.free = (Block::Range*)malloc(block.freeCapacity * sizeof(Block::Range));
        block.free[0].offset = 0;
        block.free[0].size = kBlockSize;
        block.freeCount = 1;
    }
    pthread_mutex_unlock(&memoryLock);
    return false;
}

void pvkCore::Free(pvkMemory& m)
{
    if(!m.memory)
        return;
    if(m.block < 0)
    {
        vkFreeMemory(device, m.memory, NULL);
        memset(&m, 0, sizeof(m));
        return;
    }
    pthread_mutex_lock(&memoryLock);
    Block& block = blocks[m.block];
    // insert in offset order, merged with the neighbours it touches
    int r = 0;
    while(r < block.freeCount && block.free[r].offset < m.offset)
        r++;
    bool joinBefore = r > 0 && block.free[r - 1].offset + block.free[r - 1].size == m.offset;
    bool joinAfter = r < block.freeCount && m.offset + m.size == block.free[r].offset;
    if(joinBefore && joinAfter)
    {
        block.free[r - 1].size += m.size + block.free[r].size;
        memmove(&block.free[r], &block.free[r + 1], (block.freeCount - r - 1) * sizeof(Block::Range));
        block.freeCount--;
    }
    else if(joinBefore)
        block.free[r - 1].size += m.size;
    else if(joinAfter)
    {
        block.free[r].offset = m.offset;
        block.free[r].size += m.size;
    }
    else
    {
        if(block.freeCount == block.freeCapacity)
        {
            block.freeCapacity = block.freeCapacity * 2 + 8;
            block.free = (Block::Range*)realloc(block.free, block.freeCapacity * sizeof(Block::Range));
        }
        memmove(&block.free[r + 1], &block.free[r], (block.freeCount - r) * sizeof(Block::Range));
        block.free[r].offset = m.offset;
        block.free[r].size = m.size;
        block.freeCount++;
    }
    pthread_mutex_unlock(&memoryLock);
    memset(&m, 0, sizeof(m));
}

bool pvkCore::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible, pvkBuffer* out)
{
    memset(out, 0, sizeof(*out));
    VkBufferCreateInfo info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if(vkCreateBuffer(device, &info, NULL, &out->buffer) != VK_SUCCESS)
        return false;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device, out->buffer, &req);
    VkMemoryPropertyFlags flags = hostVisible ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                                              : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    if(!Allocate(req, flags, true, &out->memory) ||
       vkBindBufferMemory(device, out->buffer, out->memory.memory, out->memory.offset) != VK_SUCCESS)
    {
        DestroyNow(*out);
        return false;
    }
    out->size = size;
    return true;
}

bool pvkCore::CreateImage(int w, int h, int mips, VkFormat format, VkSampleCountFlagBits samples,
                          VkImageUsageFlags usage, VkImageAspectFlags aspect, pvkImage* out)
{
    memset(out, 0, sizeof(*out));
    VkImageCreateInfo info = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent.width = w;
    info.extent.height = h;
    info.extent.depth = 1;
    info.mipLevels = mips;
    info.arrayLayers = 1;
    info.samples = samples;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if(vkCreateImage(device, &info, NULL, &out->image) != VK_SUCCESS)
        return false;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device, out->image, &req);
    if(!Allocate(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false, &out->memory) ||
       vkBindImageMemory(device, out->image, out->memory.memory, out->memory.offset) != VK_SUCCESS)
    {
        DestroyNow(*out);
        return false;
    }
    VkImageViewCreateInfo view = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    view.image = out->image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange.aspectMask = aspect & ~VK_IMAGE_ASPECT_STENCIL_BIT ? aspect & ~VK_IMAGE_ASPECT_STENCIL_BIT : aspect;
    view.subresourceRange.levelCount = mips;
    view.subresourceRange.layerCount = 1;
    if(aspect & VK_IMAGE_ASPECT_DEPTH_BIT)
        view.subresourceRange.aspectMask = aspect;      // attachment views take depth and stencil together
    if(vkCreateImageView(device, &view, NULL, &out->view) != VK_SUCCESS)
    {
        DestroyNow(*out);
        return false;
    }
    out->format = format;
    out->aspect = aspect;
    out->width = w;
    out->height = h;
    out->mips = mips;
    out->samples = samples;
    out->layout = VK_IMAGE_LAYOUT_UNDEFINED;
    return true;
}

void pvkCore::DestroyNow(pvkBuffer& b)
{
    if(b.buffer)
        vkDestroyBuffer(device, b.buffer, NULL);
    Free(b.memory);
    memset(&b, 0, sizeof(b));
}

void pvkCore::DestroyNow(pvkImage& i)
{
    if(i.view)
        vkDestroyImageView(device, i.view, NULL);
    if(i.image)
        vkDestroyImage(device, i.image, NULL);
    Free(i.memory);
    memset(&i, 0, sizeof(i));
}

void pvkCore::PushRetired(const Retired& r)
{
    pthread_mutex_lock(&retireLock);
    if(retiredCount == retiredCapacity)
    {
        retiredCapacity = retiredCapacity * 2 + 64;
        retired = (Retired*)realloc(retired, retiredCapacity * sizeof(Retired));
    }
    retired[retiredCount++] = r;
    pthread_mutex_unlock(&retireLock);
}

// the frame being recorded may still use it, and on another thread the render thread may already be recording the
// next: the next frame's serial covers both
void pvkCore::Retire(pvkBuffer& b)
{
    if(!b.buffer && !b.memory.memory)
        return;
    Retired r;
    memset(&r, 0, sizeof(r));
    r.serial = serial + 1;
    r.kind = 0;
    r.buffer = b;
    PushRetired(r);
    memset(&b, 0, sizeof(b));
}

void pvkCore::Retire(pvkImage& i)
{
    if(!i.image && !i.memory.memory)
        return;
    Retired r;
    memset(&r, 0, sizeof(r));
    r.serial = serial + 1;
    r.kind = 1;
    r.image = i;
    PushRetired(r);
    memset(&i, 0, sizeof(i));
}

void pvkCore::Retire(VkPipeline pipeline)
{
    if(!pipeline)
        return;
    Retired r;
    memset(&r, 0, sizeof(r));
    r.serial = serial + 1;
    r.kind = 2;
    r.pipeline = pipeline;
    PushRetired(r);
}

void pvkCore::Retire(VkSampler sampler)
{
    if(!sampler)
        return;
    Retired r;
    memset(&r, 0, sizeof(r));
    r.serial = serial + 1;
    r.kind = 3;
    r.sampler = sampler;
    PushRetired(r);
}

void pvkCore::FlushRetired(unsigned long long completed)
{
    pthread_mutex_lock(&retireLock);
    int kept = 0;
    for(int i = 0; i < retiredCount; i++)
    {
        Retired& r = retired[i];
        if(r.serial > completed)
        {
            retired[kept++] = r;
            continue;
        }
        if(r.kind == 0)
            DestroyNow(r.buffer);
        else if(r.kind == 1)
            DestroyNow(r.image);
        else if(r.kind == 2)
            vkDestroyPipeline(device, r.pipeline, NULL);
        else
            vkDestroySampler(device, r.sampler, NULL);
    }
    retiredCount = kept;
    pthread_mutex_unlock(&retireLock);
}

//-----------------------------------------------------------------------------
// Frames
//-----------------------------------------------------------------------------

VkCommandBuffer pvkCore::Cmd()
{
    if(!frameOpen)
        BeginFrame();
    return frames[frame].cmd;
}

VkCommandBuffer pvkCore::UploadCmd()
{
    if(!frameOpen)
        BeginFrame();
    uploadsRecorded = true;
    return frames[frame].upload;
}

void* pvkCore::Transient(VkDeviceSize size, VkDeviceSize align, VkBuffer* buffer, VkDeviceSize* offset)
{
    if(!frameOpen)
        BeginFrame();
    Frame& fr = frames[frame];
    for(int c = 0; c < fr.chunkCount; c++)
    {
        Frame::Chunk& chunk = fr.chunks[c];
        VkDeviceSize start = AlignUp(chunk.used, align);
        if(start + size <= chunk.buffer.size)
        {
            chunk.used = start + size;
            *buffer = chunk.buffer.buffer;
            *offset = start;
            return chunk.buffer.memory.mapped + start;
        }
    }
    if(fr.chunkCount == 16)
        return NULL;
    Frame::Chunk& chunk = fr.chunks[fr.chunkCount];
    VkDeviceSize chunkSize = size > kChunkSize ? AlignUp(size, 1024 * 1024) : kChunkSize;
    if(!CreateBuffer(chunkSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                     VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, &chunk.buffer))
        return NULL;
    fr.chunkCount++;
    chunk.used = size;
    *buffer = chunk.buffer.buffer;
    *offset = 0;
    return chunk.buffer.memory.mapped;
}

void pvkCore::Transition(VkCommandBuffer cmd, pvkImage& image, VkImageLayout layout)
{
    if(image.layout == layout || !image.image)
        return;
    VkImageMemoryBarrier2 b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
    b.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    b.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    b.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    b.oldLayout = image.layout;
    b.newLayout = layout;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image.image;
    b.subresourceRange.aspectMask = image.aspect;
    b.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    b.subresourceRange.layerCount = 1;
    VkDependencyInfo dep = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &dep);
    image.layout = layout;
}

static void SwapchainBarrier(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to)
{
    VkImageMemoryBarrier2 b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
    b.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    b.srcAccessMask = from == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    b.dstStageMask = to == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR ? VK_PIPELINE_STAGE_2_NONE : VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    b.dstAccessMask = to == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR ? 0 : VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.layerCount = 1;
    VkDependencyInfo dep = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &dep);
}

void pvkCore::BeginFrame()
{
    if(frameOpen)
        return;
    Frame& fr = frames[frame];
    vkWaitForFences(device, 1, &fr.fence, VK_TRUE, ~0ull);
    vkResetFences(device, 1, &fr.fence);
    if(fr.readbackDone)
    {
        fr.readbackDone(fr.readback.memory.mapped, fr.readbackWidth, fr.readbackHeight);
        fr.readbackDone = NULL;
    }
    FlushRetired(fr.serial);        // the queue runs in order: everything up to this frame is done
    vkResetCommandPool(device, fr.pool, 0);
    for(int c = 0; c < fr.chunkCount; c++)
        fr.chunks[c].used = 0;
    VkCommandBufferBeginInfo begin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(fr.upload, &begin);
    vkBeginCommandBuffer(fr.cmd, &begin);
    serial++;
    fr.serial = serial;
    uploadsRecorded = false;
    frameOpen = true;
    rendering = false;

    if(targetsDirty)
    {
        DestroyTargets();
        if(!CreateTargets())
            fprintf(stderr, "PDDI: cannot create %dx%d draw targets\n", width, height);
        targetsDirty = false;
    }
    msActive = targetSamples > 1;
}

//-----------------------------------------------------------------------------
// Draw targets
//-----------------------------------------------------------------------------

void pvkCore::SetSize(int w, int h)
{
    if(w <= 0 || h <= 0 || (w == width && h == height))
        return;
    width = w;
    height = h;
    targetsDirty = true;
}

void pvkCore::SetSamples(int samples)
{
    if(samples < 1)
        samples = 1;
    if(samples == requestedSamples && !targetsDirty)
        return;
    requestedSamples = samples;
    targetsDirty = true;
}

static VkSampleCountFlagBits SampleBits(int samples, VkSampleCountFlags available)
{
    int s = 64;
    while(s > 1 && (s > samples || !(available & s)))
        s >>= 1;
    return (VkSampleCountFlagBits)s;
}

// targets are retired, not destroyed: frames in flight may still read them
void pvkCore::DestroyTargets()
{
    Retire(scene.colour);
    Retire(scene.depth);
    Retire(msTarget.colour);
    Retire(msTarget.depth);
}

bool pvkCore::CreateTargets()
{
    const VkImageUsageFlags colourUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                          VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    const VkImageUsageFlags depthUsage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                         VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    const VkImageAspectFlags depthAspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    if(!CreateImage(width, height, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, colourUsage,
                    VK_IMAGE_ASPECT_COLOR_BIT, &scene.colour) ||
       !CreateImage(width, height, 1, depthFormat, VK_SAMPLE_COUNT_1_BIT, depthUsage, depthAspect, &scene.depth))
        return false;

    int previous = targetSamples;
    targetSamples = 1;
    VkSampleCountFlagBits samples = SampleBits(requestedSamples, sampleCounts);
    if(samples > 1)
    {
        if(CreateImage(width, height, 1, VK_FORMAT_R8G8B8A8_UNORM, samples, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                       VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT, &msTarget.colour) &&
           CreateImage(width, height, 1, depthFormat, samples, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                       VK_IMAGE_USAGE_TRANSFER_DST_BIT, depthAspect, &msTarget.depth))
            targetSamples = samples;
        else
        {
            fprintf(stderr, "PDDI: %dx anti-aliasing unavailable at %dx%d: off\n", (int)samples, width, height);
            Retire(msTarget.colour);
            Retire(msTarget.depth);
        }
    }
    if(targetSamples != previous)
    {
        if(targetSamples > 1)
            fprintf(stderr, "PDDI: %dx anti-aliasing\n", targetSamples);
        else if(previous > 1)
            fprintf(stderr, "PDDI: anti-aliasing off\n");
    }

    // new targets start black, depth 1, stencil 0
    VkCommandBuffer cmd = frames[frame].cmd;
    pvkImage* colours[2] = { &scene.colour, &msTarget.colour };
    pvkImage* depths[2] = { &scene.depth, &msTarget.depth };
    VkClearColorValue black;
    memset(&black, 0, sizeof(black));
    VkClearDepthStencilValue far = { 1.0f, 0 };
    for(int i = 0; i < 2; i++)
    {
        if(colours[i]->image)
        {
            Transition(cmd, *colours[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            vkCmdClearColorImage(cmd, colours[i]->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &range);
        }
        if(depths[i]->image)
        {
            Transition(cmd, *depths[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkImageSubresourceRange range = { depthAspect, 0, 1, 0, 1 };
            vkCmdClearDepthStencilImage(cmd, depths[i]->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &far, 1, &range);
        }
    }
    return true;
}

void pvkCore::BeginRendering()
{
    if(!frameOpen)
        BeginFrame();
    if(rendering)
        return;
    VkCommandBuffer cmd = frames[frame].cmd;
    pvkTarget& t = Target();
    Transition(cmd, t.colour, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    Transition(cmd, t.depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo colour = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    colour.imageView = t.colour.view;
    colour.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colour.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colour.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingAttachmentInfo depth = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    depth.imageView = t.depth.view;
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingInfo info = { VK_STRUCTURE_TYPE_RENDERING_INFO };
    info.renderArea.extent.width = t.colour.width;
    info.renderArea.extent.height = t.colour.height;
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &colour;
    info.pDepthAttachment = &depth;
    info.pStencilAttachment = &depth;
    vkCmdBeginRendering(cmd, &info);
    rendering = true;
    renderingGeneration++;
}

void pvkCore::EndRendering()
{
    if(!rendering)
        return;
    vkCmdEndRendering(frames[frame].cmd);
    rendering = false;
}

// a rendering instance with resolve attachments and nothing drawn: the multisampled colour averaged into the
// scene's, depth and stencil from sample 0 (the GUI layer drawn afterwards may test against them, as in GL)
void pvkCore::ResolveTarget()
{
    if(!msActive)
        return;
    EndRendering();
    VkCommandBuffer cmd = frames[frame].cmd;
    Transition(cmd, msTarget.colour, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    Transition(cmd, msTarget.depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    Transition(cmd, scene.colour, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    Transition(cmd, scene.depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfo colour = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    colour.imageView = msTarget.colour.view;
    colour.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colour.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colour.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colour.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
    colour.resolveImageView = scene.colour.view;
    colour.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkRenderingAttachmentInfo depth = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    depth.imageView = msTarget.depth.view;
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.resolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
    depth.resolveImageView = scene.depth.view;
    depth.resolveImageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkRenderingInfo info = { VK_STRUCTURE_TYPE_RENDERING_INFO };
    info.renderArea.extent.width = scene.colour.width;
    info.renderArea.extent.height = scene.colour.height;
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &colour;
    info.pDepthAttachment = &depth;
    info.pStencilAttachment = &depth;
    vkCmdBeginRendering(cmd, &info);
    vkCmdEndRendering(cmd);
    msActive = false;
}

//-----------------------------------------------------------------------------
// Swapchain and present
//-----------------------------------------------------------------------------

bool pvkCore::CreateSwapchain(SDL_Window* window)
{
    VkSurfaceCapabilitiesKHR caps;
    if(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps) != VK_SUCCESS)
        return false;
    int w = 0, h = 0;
    SDL_Vulkan_GetDrawableSize(window, &w, &h);
    VkExtent2D extent = caps.currentExtent;
    if(extent.width == 0xFFFFFFFFu)
    {
        extent.width = w > 0 ? w : width;
        extent.height = h > 0 ? h : height;
    }
    if(extent.width == 0 || extent.height == 0)
        return true;        // minimised: none until it has a size

    if(!swapchainFormat)
    {
        unsigned n = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &n, NULL);
        VkSurfaceFormatKHR* formats = (VkSurfaceFormatKHR*)calloc(n ? n : 1, sizeof(VkSurfaceFormatKHR));
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &n, formats);
        swapchainFormat = n ? formats[0].format : VK_FORMAT_B8G8R8A8_UNORM;
        for(unsigned i = 0; i < n; i++)
        {
            if((formats[i].format == VK_FORMAT_B8G8R8A8_UNORM || formats[i].format == VK_FORMAT_R8G8B8A8_UNORM) &&
               formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                swapchainFormat = formats[i].format;
                break;
            }
        }
        free(formats);

        // vsync (FIFO) as the GL renderer; SHAR_VSYNC=0: mailbox, else immediate
        presentMode = VK_PRESENT_MODE_FIFO_KHR;
        if(EnvInt("SHAR_VSYNC", 1) == 0)
        {
            n = 0;
            vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &n, NULL);
            VkPresentModeKHR* modes = (VkPresentModeKHR*)calloc(n ? n : 1, sizeof(VkPresentModeKHR));
            vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &n, modes);
            for(unsigned i = 0; i < n; i++)
            {
                if(modes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
                    presentMode = modes[i];
                else if(modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR && presentMode == VK_PRESENT_MODE_FIFO_KHR)
                    presentMode = modes[i];
            }
            free(modes);
        }
    }

    unsigned images = caps.minImageCount + 1;
    if(caps.maxImageCount && images > caps.maxImageCount)
        images = caps.maxImageCount;
    VkSwapchainKHR old = swapchain;
    VkSwapchainCreateInfoKHR info = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    info.surface = surface;
    info.minImageCount = images;
    info.imageFormat = swapchainFormat;
    info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) ?
                          VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR : (VkCompositeAlphaFlagBitsKHR)(caps.supportedCompositeAlpha & -caps.supportedCompositeAlpha);
    info.presentMode = presentMode;
    info.clipped = VK_TRUE;
    info.oldSwapchain = old;
    VkResult r = vkCreateSwapchainKHR(device, &info, NULL, &swapchain);
    if(old)
    {
        for(unsigned i = 0; i < swapchainCount; i++)
        {
            vkDestroyImageView(device, swapchainViews[i], NULL);
            vkDestroySemaphore(device, swapchainDone[i], NULL);
        }
        vkDestroySwapchainKHR(device, old, NULL);
        swapchainCount = 0;
    }
    if(r != VK_SUCCESS)
    {
        fprintf(stderr, "PDDI: vkCreateSwapchainKHR failed (%d)\n", r);
        swapchain = VK_NULL_HANDLE;
        return false;
    }
    swapchainCount = 8;
    vkGetSwapchainImagesKHR(device, swapchain, &swapchainCount, swapchainImages);
    for(unsigned i = 0; i < swapchainCount; i++)
    {
        VkImageViewCreateInfo view = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        view.image = swapchainImages[i];
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = swapchainFormat;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view.subresourceRange.levelCount = 1;
        view.subresourceRange.layerCount = 1;
        vkCreateImageView(device, &view, NULL, &swapchainViews[i]);
        VkSemaphoreCreateInfo sem = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        vkCreateSemaphore(device, &sem, NULL, &swapchainDone[i]);
    }
    swapchainWidth = extent.width;
    swapchainHeight = extent.height;
    swapchainStale = false;
    return true;
}

void pvkCore::DestroySwapchain()
{
    for(unsigned i = 0; i < swapchainCount; i++)
    {
        vkDestroyImageView(device, swapchainViews[i], NULL);
        vkDestroySemaphore(device, swapchainDone[i], NULL);
    }
    swapchainCount = 0;
    if(swapchain)
        vkDestroySwapchainKHR(device, swapchain, NULL);
    swapchain = VK_NULL_HANDLE;
}

VkShaderModule pvkCreateShaderModule(const unsigned* words, size_t count)
{
    VkShaderModuleCreateInfo info = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    info.codeSize = count * 4;
    info.pCode = words;
    VkShaderModule module = VK_NULL_HANDLE;
    vkCreateShaderModule(g_pvk->device, &info, NULL, &module);
    return module;
}

// the scene colour drawn over the swapchain image, through out = in^(1/gamma) (the display menu's gamma: the D3D8
// display set the video card's ramp, see gldisplay.cpp ApplyGamma)
bool pvkCore::CreatePresentPass()
{
    static const char* vertexSource =
        "#version 450\n"
        "layout(location = 0) out vec2 uv;\n"
        "void main()\n"
        "{\n"
        "    vec2 p = vec2(gl_VertexIndex == 1 ? 3.0 : -1.0, gl_VertexIndex == 2 ? 3.0 : -1.0);\n"
        "    uv = p * 0.5 + 0.5;\n"
        "    gl_Position = vec4(p, 0.0, 1.0);\n"
        "}\n";
    static const char* fragmentSource =
        "#version 450\n"
        "layout(location = 0) in vec2 uv;\n"
        "layout(binding = 1) uniform sampler2D image;\n"
        "layout(push_constant) uniform Gamma { float invGamma; } gamma;\n"
        "layout(location = 0) out vec4 fragColour;\n"
        "void main()\n"
        "{\n"
        "    vec3 c = texture(image, uv).rgb;\n"
        "    fragColour = vec4(gamma.invGamma == 1.0 ? c : pow(c, vec3(gamma.invGamma)), 1.0);\n"
        "}\n";
    shar_glsl_program program;
    if(!shar_glsl_compile("present", NULL, vertexSource, NULL, fragmentSource, &program))
    {
        fprintf(stderr, "PDDI: %s\n", program.log ? program.log : "present pass does not compile");
        shar_glsl_free(&program);
        return false;
    }
    presentVS = pvkCreateShaderModule(program.vertex, program.vertexWords);
    presentFS = pvkCreateShaderModule(program.fragment, program.fragmentWords);
    int binding = 1;
    for(int i = 0; i < program.uniformCount; i++)
        if(program.uniforms[i].binding >= 0)
            binding = program.uniforms[i].binding;
    shar_glsl_free(&program);

    VkDescriptorSetLayoutBinding b = { (unsigned)binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                       VK_SHADER_STAGE_FRAGMENT_BIT, NULL };
    VkDescriptorSetLayoutCreateInfo set = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    set.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    set.bindingCount = 1;
    set.pBindings = &b;
    vkCreateDescriptorSetLayout(device, &set, NULL, &presentSetLayout);
    VkPushConstantRange push = { VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4 };
    VkPipelineLayoutCreateInfo layout = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &presentSetLayout;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &push;
    vkCreatePipelineLayout(device, &layout, NULL, &presentLayout);

    VkSamplerCreateInfo s = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    s.magFilter = s.minFilter = VK_FILTER_LINEAR;
    s.addressModeU = s.addressModeV = s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(device, &s, NULL, &presentSampler);
    presentBinding = binding;
    return presentVS && presentFS;
}

static VkPipeline CreatePresentPipeline(VkDevice device, VkPipelineLayout layout, VkShaderModule vs, VkShaderModule fs,
                                        VkFormat format)
{
    VkPipelineShaderStageCreateInfo stages[2] = { { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO },
                                                  { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO } };
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
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
    VkPipelineColorBlendAttachmentState blendAttachment;
    memset(&blendAttachment, 0, sizeof(blendAttachment));
    blendAttachment.colorWriteMask = 0xf;
    VkPipelineColorBlendStateCreateInfo blend = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;
    VkDynamicState dynamics[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamics;
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
    info.layout = layout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, NULL, &pipeline);
    return pipeline;
}

void pvkCore::RequestReadback(void (*done)(const unsigned char*, int, int))
{
    pendingReadback = done;
}

void pvkCore::Present(float gamma, SDL_Window* window)
{
    if(!frameOpen)
        BeginFrame();
    ResolveTarget();
    EndRendering();
    Frame& fr = frames[frame];
    VkCommandBuffer cmd = fr.cmd;

    if(pendingReadback && scene.colour.image)
    {
        int width = scene.colour.width, height = scene.colour.height;
        VkDeviceSize size = (VkDeviceSize)width * height * 4;
        if(fr.readback.size < size)
        {
            Retire(fr.readback);
            CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, &fr.readback);
        }
        if(fr.readback.buffer)
        {
            Transition(cmd, scene.colour, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            VkBufferImageCopy copy;
            memset(&copy, 0, sizeof(copy));
            copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy.imageSubresource.layerCount = 1;
            copy.imageExtent.width = width;
            copy.imageExtent.height = height;
            copy.imageExtent.depth = 1;
            vkCmdCopyImageToBuffer(cmd, scene.colour.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, fr.readback.buffer, 1, &copy);
            fr.readbackDone = pendingReadback;
            fr.readbackWidth = width;
            fr.readbackHeight = height;
        }
        pendingReadback = NULL;
    }

    // a swapchain image to present into: none while minimised (presenting would block or fail)
    bool hidden = (SDL_GetWindowFlags(window) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)) != 0;
    int w = 0, h = 0;
    SDL_Vulkan_GetDrawableSize(window, &w, &h);
    if(!hidden && (swapchainStale || !swapchain || w != swapchainWidth || h != swapchainHeight) && w > 0 && h > 0)
    {
        vkDeviceWaitIdle(device);
        CreateSwapchain(window);
    }
    unsigned image = ~0u;
    if(!hidden && swapchain)
    {
        VkResult r = vkAcquireNextImageKHR(device, swapchain, 1000000000ull, fr.acquired, VK_NULL_HANDLE, &image);
        if(r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_TIMEOUT || r == VK_NOT_READY || (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR))
        {
            swapchainStale = r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR;
            image = ~0u;
        }
    }

    if(image != ~0u)
    {
        if(!presentPipeline || presentPipelineFormat != swapchainFormat)
        {
            if(presentPipeline)
                vkDestroyPipeline(device, presentPipeline, NULL);
            presentPipeline = CreatePresentPipeline(device, presentLayout, presentVS, presentFS, swapchainFormat);
            presentPipelineFormat = swapchainFormat;
        }
        Transition(cmd, scene.colour, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        SwapchainBarrier(cmd, swapchainImages[image], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        VkRenderingAttachmentInfo colour = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
        colour.imageView = swapchainViews[image];
        colour.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colour.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colour.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingInfo info = { VK_STRUCTURE_TYPE_RENDERING_INFO };
        info.renderArea.extent.width = swapchainWidth;
        info.renderArea.extent.height = swapchainHeight;
        info.layerCount = 1;
        info.colorAttachmentCount = 1;
        info.pColorAttachments = &colour;
        vkCmdBeginRendering(cmd, &info);
        if(presentPipeline)
        {
            VkViewport viewport = { 0.0f, 0.0f, (float)swapchainWidth, (float)swapchainHeight, 0.0f, 1.0f };
            VkRect2D scissor = { { 0, 0 }, { (unsigned)swapchainWidth, (unsigned)swapchainHeight } };
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            vkCmdSetScissor(cmd, 0, 1, &scissor);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, presentPipeline);
            VkDescriptorImageInfo imageInfo = { presentSampler, scene.colour.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            write.dstBinding = presentBinding;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &imageInfo;
            vkCmdPushDescriptorSetKHR(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, presentLayout, 0, 1, &write);
            float invGamma = gamma > 0.0f ? 1.0f / gamma : 1.0f;
            if(invGamma > 0.999f && invGamma < 1.001f)
                invGamma = 1.0f;
            vkCmdPushConstants(cmd, presentLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4, &invGamma);
            vkCmdDraw(cmd, 3, 1, 0, 0);
        }
        vkCmdEndRendering(cmd);
        SwapchainBarrier(cmd, swapchainImages[image], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    }

    if(uploadsRecorded)
    {
        VkMemoryBarrier2 b = { VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
        b.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        b.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        b.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        VkDependencyInfo dep = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        dep.memoryBarrierCount = 1;
        dep.pMemoryBarriers = &b;
        vkCmdPipelineBarrier2(fr.upload, &dep);
    }
    vkEndCommandBuffer(fr.upload);
    vkEndCommandBuffer(fr.cmd);
    VkCommandBuffer cmds[2] = { fr.upload, fr.cmd };
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit.commandBufferCount = uploadsRecorded ? 2 : 1;
    submit.pCommandBuffers = uploadsRecorded ? cmds : &cmds[1];
    if(image != ~0u)
    {
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &fr.acquired;
        submit.pWaitDstStageMask = &waitStage;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &swapchainDone[image];
    }
    VkResult r = vkQueueSubmit(queue, 1, &submit, fr.fence);
    if(r != VK_SUCCESS)
    {
        static bool s_reported = false;
        if(!s_reported)
            fprintf(stderr, "PDDI: vkQueueSubmit failed (%d)%s\n", r, r == VK_ERROR_DEVICE_LOST ? ": device lost" : "");
        s_reported = true;
    }
    if(image != ~0u)
    {
        VkPresentInfoKHR present = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &swapchainDone[image];
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain;
        present.pImageIndices = &image;
        r = vkQueuePresentKHR(queue, &present);
        if(r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR)
            swapchainStale = true;
    }
    frameOpen = false;
    frame = (frame + 1) % pvkFrames;
}

//-----------------------------------------------------------------------------
// Start (platform layer)
//-----------------------------------------------------------------------------

bool pvkStart(SDL_Window* window)
{
    if(g_pvk)
        return true;
    pvkCore* core = new pvkCore;
    g_pvk = core;       // Init's helpers use it
    if(!core->Init(window))
    {
        core->Shutdown();
        delete core;
        g_pvk = NULL;
        return false;
    }
    return true;
}

bool pvkActive()
{
    return g_pvk != NULL;
}

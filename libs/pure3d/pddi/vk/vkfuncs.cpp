//=============================================================================
// Linux port: Vulkan entry points (see vkfuncs.hpp)
//=============================================================================

#include <pddi/vk/vkfuncs.hpp>

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <stdio.h>

namespace pvkfn
{
#define PVK_DEFINE(name) PFN_##name name = NULL;
    PVK_GLOBAL_FUNCS(PVK_DEFINE)
    PVK_INSTANCE_FUNCS(PVK_DEFINE)
    PVK_DEBUG_FUNCS(PVK_DEFINE)
    PVK_DEVICE_FUNCS(PVK_DEFINE)
#undef PVK_DEFINE
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = NULL;
}

bool pvkLoadGlobal()
{
    if(vkGetInstanceProcAddr)
        return true;
    if(SDL_Vulkan_LoadLibrary(NULL) != 0)
    {
        fprintf(stderr, "PDDI: no Vulkan loader (%s)\n", SDL_GetError());
        return false;
    }
    vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr();
    if(!vkGetInstanceProcAddr)
        return false;
    bool ok = true;
#define PVK_LOAD(name) name = (PFN_##name)vkGetInstanceProcAddr(NULL, #name); \
    if(!name) { fprintf(stderr, "PDDI: Vulkan loader lacks %s\n", #name); ok = false; }
    PVK_GLOBAL_FUNCS(PVK_LOAD)
#undef PVK_LOAD
    return ok;
}

bool pvkLoadInstance(VkInstance instance)
{
    bool ok = true;
#define PVK_LOAD(name) name = (PFN_##name)vkGetInstanceProcAddr(instance, #name); \
    if(!name) { fprintf(stderr, "PDDI: Vulkan instance lacks %s\n", #name); ok = false; }
    PVK_INSTANCE_FUNCS(PVK_LOAD)
#undef PVK_LOAD
    // optional: present with VK_EXT_debug_utils only (SHAR_VK_DEBUG=1)
#define PVK_LOAD_OPTIONAL(name) name = (PFN_##name)vkGetInstanceProcAddr(instance, #name);
    PVK_DEBUG_FUNCS(PVK_LOAD_OPTIONAL)
#undef PVK_LOAD_OPTIONAL
    return ok;
}

bool pvkLoadDevice(VkDevice device)
{
    bool ok = true;
#define PVK_LOAD(name) name = (PFN_##name)vkGetDeviceProcAddr(device, #name); \
    if(!name) { fprintf(stderr, "PDDI: Vulkan device lacks %s\n", #name); ok = false; }
    PVK_DEVICE_FUNCS(PVK_LOAD)
#undef PVK_LOAD
    return ok;
}

//=============================================================================
// Linux port: Vulkan renderer (pddi/vk). The Vulkan library is loaded at run time (SDL_Vulkan_LoadLibrary), so the
// game starts on machines without it and uses OpenGL there; the entry points are fetched into these pointers. They
// live in a namespace (mangled names): nothing can clash with the loader's own exported vk* functions.
//=============================================================================

#ifndef _VKFUNCS_HPP_
#define _VKFUNCS_HPP_

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#define PVK_GLOBAL_FUNCS(X) \
    X(vkCreateInstance) X(vkEnumerateInstanceExtensionProperties) X(vkEnumerateInstanceLayerProperties) \
    X(vkEnumerateInstanceVersion)

#define PVK_INSTANCE_FUNCS(X) \
    X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceProperties) \
    X(vkGetPhysicalDeviceFeatures) X(vkGetPhysicalDeviceFeatures2) X(vkGetPhysicalDeviceQueueFamilyProperties) \
    X(vkGetPhysicalDeviceMemoryProperties) X(vkGetPhysicalDeviceFormatProperties) \
    X(vkEnumerateDeviceExtensionProperties) X(vkGetPhysicalDeviceSurfaceSupportKHR) \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) X(vkGetPhysicalDeviceSurfaceFormatsKHR) \
    X(vkGetPhysicalDeviceSurfacePresentModesKHR) X(vkDestroySurfaceKHR) X(vkCreateDevice) X(vkGetDeviceProcAddr)

#define PVK_DEBUG_FUNCS(X) \
    X(vkCreateDebugUtilsMessengerEXT) X(vkDestroyDebugUtilsMessengerEXT)

#define PVK_DEVICE_FUNCS(X) \
    X(vkDestroyDevice) X(vkGetDeviceQueue) X(vkDeviceWaitIdle) X(vkQueueSubmit) X(vkQueueWaitIdle) \
    X(vkCreateSwapchainKHR) X(vkDestroySwapchainKHR) X(vkGetSwapchainImagesKHR) X(vkAcquireNextImageKHR) \
    X(vkQueuePresentKHR) \
    X(vkCreateCommandPool) X(vkDestroyCommandPool) X(vkResetCommandPool) X(vkAllocateCommandBuffers) \
    X(vkBeginCommandBuffer) X(vkEndCommandBuffer) \
    X(vkCreateFence) X(vkDestroyFence) X(vkWaitForFences) X(vkResetFences) X(vkGetFenceStatus) \
    X(vkCreateSemaphore) X(vkDestroySemaphore) \
    X(vkAllocateMemory) X(vkFreeMemory) X(vkMapMemory) X(vkUnmapMemory) \
    X(vkCreateBuffer) X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements) X(vkBindBufferMemory) \
    X(vkCreateImage) X(vkDestroyImage) X(vkGetImageMemoryRequirements) X(vkBindImageMemory) \
    X(vkCreateImageView) X(vkDestroyImageView) X(vkCreateSampler) X(vkDestroySampler) \
    X(vkCreateShaderModule) X(vkDestroyShaderModule) \
    X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout) \
    X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) \
    X(vkCreateGraphicsPipelines) X(vkDestroyPipeline) X(vkCreatePipelineCache) X(vkDestroyPipelineCache) \
    X(vkGetPipelineCacheData) \
    X(vkCmdBeginRendering) X(vkCmdEndRendering) X(vkCmdPipelineBarrier2) \
    X(vkCmdBindPipeline) X(vkCmdBindVertexBuffers2) X(vkCmdBindIndexBuffer) X(vkCmdDraw) X(vkCmdDrawIndexed) \
    X(vkCmdSetViewport) X(vkCmdSetScissor) X(vkCmdSetCullMode) X(vkCmdSetFrontFace) \
    X(vkCmdSetPrimitiveTopology) X(vkCmdSetDepthTestEnable) X(vkCmdSetDepthWriteEnable) \
    X(vkCmdSetDepthCompareOp) X(vkCmdSetStencilTestEnable) X(vkCmdSetStencilOp) \
    X(vkCmdSetStencilCompareMask) X(vkCmdSetStencilWriteMask) X(vkCmdSetStencilReference) \
    X(vkCmdSetDepthBiasEnable) X(vkCmdSetDepthBoundsTestEnable) X(vkCmdSetRasterizerDiscardEnable) \
    X(vkCmdSetPrimitiveRestartEnable) \
    X(vkCmdClearAttachments) X(vkCmdCopyBuffer) X(vkCmdCopyBufferToImage) X(vkCmdCopyImageToBuffer) \
    X(vkCmdBlitImage) X(vkCmdCopyImage) X(vkCmdResolveImage) X(vkCmdClearColorImage) X(vkCmdClearDepthStencilImage) \
    X(vkCmdPushConstants) \
    X(vkCmdPushDescriptorSetKHR) X(vkCmdWriteTimestamp) \
    X(vkCreateQueryPool) X(vkDestroyQueryPool) X(vkGetQueryPoolResults) X(vkCmdResetQueryPool)

namespace pvkfn
{
#define PVK_DECLARE(name) extern PFN_##name name;
    PVK_GLOBAL_FUNCS(PVK_DECLARE)
    PVK_INSTANCE_FUNCS(PVK_DECLARE)
    PVK_DEBUG_FUNCS(PVK_DECLARE)
    PVK_DEVICE_FUNCS(PVK_DECLARE)
#undef PVK_DECLARE
    extern PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
}
using namespace pvkfn;

// SDL_Vulkan_LoadLibrary and the global entry points; false when there is no Vulkan loader
bool pvkLoadGlobal();
// after vkCreateInstance / vkCreateDevice; false when a required entry point is missing (named on stderr)
bool pvkLoadInstance(VkInstance instance);
bool pvkLoadDevice(VkDevice device);

#endif

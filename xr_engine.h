#pragma once

#include <string>
#include <vector>
#include <vulkan/vulkan.h>

#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>


namespace EcmXr {
    // NEW: Listens to the VR headset state changes (wake, sleep, connect)
    bool PollEvents(XrInstance instance, XrSession session, bool& sessionRunning);

    // Structure to hold our active Vulkan context variables
    struct VulkanContext {
        VkInstance instance = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        uint32_t queueFamilyIndex = 0;
        VkQueue queue = VK_NULL_HANDLE;
    };

    // Structure to hold a single texture buffer from the runtime carousel
    struct SwapchainBuffer {
        VkImage image = VK_NULL_HANDLE;
    };

    // Public API Declarations
    bool CheckXrResult(XrResult result, const std::string& operation);
    bool InitializeOpenXR(XrInstance& instance, XrSystemId& systemId);
    bool CreateVulkanContext(XrInstance xrInstance, XrSystemId xrSystemId, VulkanContext& ctx);
    bool CreateSessionAndSwapchain(
        XrInstance xrInstance, 
        XrSystemId xrSystemId, 
        const VulkanContext& vulkanCtx, 
        uint32_t width, 
        uint32_t height,
        XrSession& session,
        XrSwapchain& swapchain,
        std::vector<SwapchainBuffer>& swapchainBuffers
    );

}
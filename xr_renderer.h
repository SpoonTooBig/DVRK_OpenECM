#pragma once

#include <vulkan/vulkan.h>
#include "xr_engine.h"

namespace EcmXr {

    class XrRenderer {
    public:
        XrRenderer(const VulkanContext& ctx, uint32_t width, uint32_t height);
        ~XrRenderer();

        // High performance copy operation: transfers raw CPU pixels to a live Vulkan swapchain image
        void UploadFrameToImage(VkImage targetImage, const unsigned char* pixelData);

    private:
        const VulkanContext& m_ctx;
        uint32_t m_width;
        uint32_t m_height;
        VkDeviceSize m_imageSize;

        // Vulkan transfer objects
        VkCommandPool m_commandPool = VK_NULL_HANDLE;
        VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
        
        // Staging buffer storage (The loading dock)
        VkBuffer m_stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_stagingMemory = VK_NULL_HANDLE;

        // Internal helper utilities
        void CreateStagingBuffer();
        uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    };

} // namespace EcmXr

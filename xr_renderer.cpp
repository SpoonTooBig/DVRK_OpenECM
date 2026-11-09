#include "xr_renderer.h"
#include <iostream>
#include <cstring>

namespace EcmXr {

    XrRenderer::XrRenderer(const VulkanContext& ctx, uint32_t width, uint32_t height)
        : m_ctx(ctx), m_width(width), m_height(height) {
        
        m_imageSize = width * height * 4; // 4 bytes per pixel (RGBA)

        // 1. Create a Command Pool for managing GPU workflow instructions
        VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolInfo.queueFamilyIndex = m_ctx.queueFamilyIndex;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        if (vkCreateCommandPool(m_ctx.device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
            std::cerr << "[Renderer ERROR] Failed to create Vulkan command pool." << std::endl;
        }

        // 2. Allocate a persistent Command Buffer for recording our copy operations
        VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocInfo.commandPool = m_commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        vkAllocateCommandBuffers(m_ctx.device, &allocInfo, &m_commandBuffer);

        // 3. Spin up our memory staging buffers
        CreateStagingBuffer();
        std::cout << "[SUCCESS] Low-latency Vulkan video renderer pipeline initialized." << std::endl;
    }

    XrRenderer::~XrRenderer() {
        if (m_stagingBuffer != VK_NULL_HANDLE) vkDestroyBuffer(m_ctx.device, m_stagingBuffer, nullptr);
        if (m_stagingMemory != VK_NULL_HANDLE) vkFreeMemory(m_ctx.device, m_stagingMemory, nullptr);
        if (m_commandPool != VK_NULL_HANDLE) vkDestroyCommandPool(m_ctx.device, m_commandPool, nullptr);
    }

    void XrRenderer::CreateStagingBuffer() {
        VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferInfo.size = m_imageSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(m_ctx.device, &bufferInfo, nullptr, &m_stagingBuffer);

        VkMemoryRequirements memReqs;
        vkGetBufferMemoryRequirements(m_ctx.device, m_stagingBuffer, &memReqs);

        VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocInfo.allocationSize = memReqs.size;
        // Request memory that is visible to the CPU so we can map it
        allocInfo.memoryTypeIndex = FindMemoryType(memReqs.memoryTypeBits, 
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        
        vkAllocateMemory(m_ctx.device, &allocInfo, nullptr, &m_stagingMemory);
        vkBindBufferMemory(m_ctx.device, m_stagingBuffer, m_stagingMemory, 0);
    }

    void XrRenderer::UploadFrameToImage(VkImage targetImage, const unsigned char* pixelData) {
        // Step A: Map memory and copy OpenCV raw pixels to the staging buffer
        void* data;
        vkMapMemory(m_ctx.device, m_stagingMemory, 0, m_imageSize, 0, &data);
        std::memcpy(data, pixelData, static_cast<size_t>(m_imageSize));
        vkUnmapMemory(m_ctx.device, m_stagingMemory);

        // Step B: Record the GPU commands to transition formats and copy pixels
        VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(m_commandBuffer, &beginInfo);

        // Transition the OpenXR image layout so it can receive data safely
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.image = targetImage;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        vkCmdPipelineBarrier(m_commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        // Copy pixel regions from buffer to the active image target index
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {m_width, m_height, 1};

        vkCmdCopyBufferToImage(m_commandBuffer, m_stagingBuffer, targetImage, 
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        // Hand the layout configuration state back so the WiVRn compositor engine can read it
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(m_commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, 
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        vkEndCommandBuffer(m_commandBuffer);

        // Step C: Push the box down our active Vulkan conveyor belt queue instantly
        VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &m_commandBuffer;
        vkQueueSubmit(m_ctx.queue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_ctx.queue); // Maintain synchronous pipeline alignment for prototyping
    }

    uint32_t XrRenderer::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(m_ctx.physicalDevice, &memProperties);
        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        return 0;
    }

} // namespace EcmXr

#include "xr_engine.h"
#include <iostream>
#include <cstring>

namespace EcmXr {

    bool CheckXrResult(XrResult result, const std::string& operation) {
        if (XR_FAILED(result)) {
            std::cerr << "[OpenXR ERROR] " << operation << " failed with code: " << result << std::endl;
            return false;
        }
        return true;
    }

    bool InitializeOpenXR(XrInstance& instance, XrSystemId& systemId) {
        XrApplicationInfo appInfo = {};
        std::strncpy(appInfo.applicationName, "DVRK_OpenECM", XR_MAX_APPLICATION_NAME_SIZE);
        appInfo.applicationVersion = 1;
        std::strncpy(appInfo.engineName, "No Engine", XR_MAX_ENGINE_NAME_SIZE);
        appInfo.engineVersion = 1;
        appInfo.apiVersion = XR_CURRENT_API_VERSION;

        const char* enabledExtensions[] = { XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME };

        XrInstanceCreateInfo createInfo = { XR_TYPE_INSTANCE_CREATE_INFO };
        createInfo.applicationInfo = appInfo;
        createInfo.enabledExtensionCount = 1;
        createInfo.enabledExtensionNames = enabledExtensions;

        std::cout << "Creating OpenXR Instance..." << std::endl;
        if (!CheckXrResult(xrCreateInstance(&createInfo, &instance), "xrCreateInstance")) {
            return false;
        }

        XrSystemGetInfo systemInfo = { XR_TYPE_SYSTEM_GET_INFO };
        systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

        std::cout << "Querying OpenXR VR System..." << std::endl;
        if (!CheckXrResult(xrGetSystem(instance, &systemInfo, &systemId), "xrGetSystem")) {
            return false;
        }

        std::cout << "[SUCCESS] OpenXR Instance and System Linked!" << std::endl;
        return true;
    }

    bool CreateSessionAndSwapchain(
        XrInstance xrInstance, 
        XrSystemId xrSystemId, 
        const VulkanContext& vulkanCtx, 
        uint32_t width, 
        uint32_t height,
        XrSession& session,
        XrSwapchain& swapchain,
        std::vector<SwapchainBuffer>& swapchainBuffers
    ) {
        // 1. Bind the Vulkan variables to the OpenXR Session structure
        XrGraphicsBindingVulkanKHR graphicsBinding{XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR};
        graphicsBinding.instance = vulkanCtx.instance;
        graphicsBinding.physicalDevice = vulkanCtx.physicalDevice;
        graphicsBinding.device = vulkanCtx.device;
        graphicsBinding.queueFamilyIndex = vulkanCtx.queueFamilyIndex;
        graphicsBinding.queueIndex = 0;

        XrSessionCreateInfo sessionCreateInfo{XR_TYPE_SESSION_CREATE_INFO};
        sessionCreateInfo.next = &graphicsBinding; // This hooks Vulkan into OpenXR!
        sessionCreateInfo.systemId = xrSystemId;

        std::cout << "Creating OpenXR Session with WiVRn..." << std::endl;
        if (!CheckXrResult(xrCreateSession(xrInstance, &sessionCreateInfo, &session), "xrCreateSession")) {
            return false;
        }

        // 2. Define the configuration for our camera texture swapchain carousel
        XrSwapchainCreateInfo swapchainCreateInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        // Use standard color bits that match OpenCV's RGBA conversion output
        swapchainCreateInfo.format = VK_FORMAT_R8G8B8A8_SRGB; 
        swapchainCreateInfo.sampleCount = 1;
        swapchainCreateInfo.width = width;   // This will be your camera's width (848)
        swapchainCreateInfo.height = height; // This will be your camera's height (480)
        swapchainCreateInfo.faceCount = 1;
        swapchainCreateInfo.arraySize = 1;
        swapchainCreateInfo.mipCount = 1;
        // Tell Vulkan we will be copying camera pixels directly into these images
        swapchainCreateInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;

        std::cout << "Allocating VR Swapchain textures (" << width << "x" << height << ")..." << std::endl;
        if (!CheckXrResult(xrCreateSwapchain(session, &swapchainCreateInfo, &swapchain), "xrCreateSwapchain")) {
            return false;
        }

        // 3. Ask WiVRn how many texture frames it allocated for us in the carousel
        uint32_t imageCount = 0;
        xrEnumerateSwapchainImages(swapchain, 0, &imageCount, nullptr);

        // Fetch the raw underlying Vulkan VkImage handles from OpenXR
        std::vector<XrSwapchainImageVulkanKHR> xrImages(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
        xrEnumerateSwapchainImages(swapchain, imageCount, &imageCount, (XrSwapchainImageBaseHeader*)xrImages.data());

        // Extract and store just the image pointers in our custom lightweight buffer array
        swapchainBuffers.resize(imageCount);
        for (uint32_t i = 0; i < imageCount; i++) {
            swapchainBuffers[i].image = xrImages[i].image;
        }

        std::cout << "[SUCCESS] VR Pipeline active! Allocated " << imageCount << " swapchain textures." << std::endl;
        return true;
    }

    bool CreateVulkanContext(XrInstance xrInstance, XrSystemId xrSystemId, VulkanContext& ctx) {
        PFN_xrGetVulkanGraphicsRequirements2KHR getGraphicsRequirements = nullptr;
        if (!CheckXrResult(xrGetInstanceProcAddr(
                xrInstance, "xrGetVulkanGraphicsRequirements2KHR",
                reinterpret_cast<PFN_xrVoidFunction*>(&getGraphicsRequirements)),
                "xrGetVulkanGraphicsRequirements2KHR") || getGraphicsRequirements == nullptr) {
            return false;
        }

        XrGraphicsRequirementsVulkan2KHR graphicsRequirements{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};
        if (!CheckXrResult(getGraphicsRequirements(xrInstance, xrSystemId, &graphicsRequirements),
                        "Querying Vulkan graphics requirements")) {
            return false;
        }

        PFN_xrCreateVulkanInstanceKHR createVulkanInstance = nullptr;
        if (!CheckXrResult(xrGetInstanceProcAddr(
                xrInstance, "xrCreateVulkanInstanceKHR",
                reinterpret_cast<PFN_xrVoidFunction*>(&createVulkanInstance)),
                "xrCreateVulkanInstanceKHR") || createVulkanInstance == nullptr) {
            return false;
        }

        VkInstanceCreateInfo instanceCreateInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        XrVulkanInstanceCreateInfoKHR xrInstanceCreateInfo{XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
        xrInstanceCreateInfo.systemId = xrSystemId;
        xrInstanceCreateInfo.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
        xrInstanceCreateInfo.vulkanCreateInfo = &instanceCreateInfo;

        VkResult vulkanResult = VK_SUCCESS;
        if (!CheckXrResult(createVulkanInstance(
                xrInstance, &xrInstanceCreateInfo, &ctx.instance, &vulkanResult),
                "Creating Vulkan instance through OpenXR")) {
            return false;
        }
        if (vulkanResult != VK_SUCCESS) {
            std::cerr << "[Vulkan ERROR] xrCreateVulkanInstanceKHR failed with code: "
                    << vulkanResult << std::endl;
            return false;
        }

        PFN_xrGetVulkanGraphicsDevice2KHR getGraphicsDevice = nullptr;
        if (!CheckXrResult(xrGetInstanceProcAddr(
                xrInstance, "xrGetVulkanGraphicsDevice2KHR",
                reinterpret_cast<PFN_xrVoidFunction*>(&getGraphicsDevice)),
                "xrGetVulkanGraphicsDevice2KHR") || getGraphicsDevice == nullptr) {
            return false;
        }

        XrVulkanGraphicsDeviceGetInfoKHR deviceGetInfo{XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
        deviceGetInfo.systemId = xrSystemId;
        deviceGetInfo.vulkanInstance = ctx.instance;
        if (!CheckXrResult(getGraphicsDevice(xrInstance, &deviceGetInfo, &ctx.physicalDevice),
                        "Selecting the OpenXR Vulkan physical device")) {
            return false;
        }

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(ctx.physicalDevice, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(ctx.physicalDevice, &queueFamilyCount, queueFamilies.data());

        bool foundGraphicsQueue = false;
        for (uint32_t index = 0; index < queueFamilyCount; ++index) {
            if (queueFamilies[index].queueCount > 0 &&
                (queueFamilies[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
                ctx.queueFamilyIndex = index;
                foundGraphicsQueue = true;
                break;
            }
        }
        if (!foundGraphicsQueue) {
            std::cerr << "[Vulkan ERROR] The OpenXR physical device has no graphics queue family." << std::endl;
            return false;
        }

        float queuePriority = 1.0f;
        VkDeviceQueueCreateInfo queueCreateInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueCreateInfo.queueFamilyIndex = ctx.queueFamilyIndex;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;

        VkDeviceCreateInfo deviceCreateInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        deviceCreateInfo.queueCreateInfoCount = 1;
        deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;

        PFN_xrCreateVulkanDeviceKHR createVulkanDevice = nullptr;
        if (!CheckXrResult(xrGetInstanceProcAddr(
                xrInstance, "xrCreateVulkanDeviceKHR",
                reinterpret_cast<PFN_xrVoidFunction*>(&createVulkanDevice)),
                "xrCreateVulkanDeviceKHR") || createVulkanDevice == nullptr) {
            return false;
        }

        XrVulkanDeviceCreateInfoKHR xrDeviceCreateInfo{XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
        xrDeviceCreateInfo.systemId = xrSystemId;
        xrDeviceCreateInfo.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
        xrDeviceCreateInfo.vulkanPhysicalDevice = ctx.physicalDevice;
        xrDeviceCreateInfo.vulkanCreateInfo = &deviceCreateInfo;

        vulkanResult = VK_SUCCESS;
        if (!CheckXrResult(createVulkanDevice(
                xrInstance, &xrDeviceCreateInfo, &ctx.device, &vulkanResult),
                "Creating Vulkan device through OpenXR")) {
            return false;
        }
        if (vulkanResult != VK_SUCCESS) {
            std::cerr << "[Vulkan ERROR] xrCreateVulkanDeviceKHR failed with code: "
                    << vulkanResult << std::endl;
            return false;
        }

        vkGetDeviceQueue(ctx.device, ctx.queueFamilyIndex, 0, &ctx.queue);

        std::cout << "[SUCCESS] Vulkan graphics context fully bound to OpenXR tracking!" << std::endl;
        return true;
    }

    bool PollEvents(XrInstance instance, XrSession session, bool& sessionRunning) {
        XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
        
        // Read through all messages sent by WiVRn since the last frame loop
        while (xrPollEvent(instance, &event) == XR_SUCCESS) {
            switch (event.type) {
                case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                    auto* stateChanged = (XrEventDataSessionStateChanged*)&event;
                    
                    // If the headset is ready, start the frame pumps!
                    if (stateChanged->state == XR_SESSION_STATE_READY) {
                        XrSessionBeginInfo beginInfo{XR_TYPE_SESSION_BEGIN_INFO};
                        beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                        
                        std::cout << "OpenXR Session is READY. Transitioning to RUNNING state..." << std::endl;
                        if (CheckXrResult(xrBeginSession(session, &beginInfo), "xrBeginSession")) {
                            sessionRunning = true;
                        }
                    } else if (stateChanged->state == XR_SESSION_STATE_STOPPING) {
                        std::cout << "OpenXR Session is STOPPING..." << std::endl;
                        xrEndSession(session);
                        sessionRunning = false;
                    }
                    break;
                }
                default:
                    break;
            }
            event = {XR_TYPE_EVENT_DATA_BUFFER}; // Clear buffer for next check
        }
        return true;
    }

}

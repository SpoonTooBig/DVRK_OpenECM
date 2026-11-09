#include <iostream>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <opencv2/opencv.hpp>

#include "xr_engine.h"
#include "xr_renderer.h"
// Atomic flag to cleanly exit the main loop when pressing ESC or Ctrl+C
std::atomic<bool> g_running{true};
const int cap_dev = 1; //Capture number on your system

int main(int argc, char* argv[]) {
    // Bring in std and cv namespaces locally to keep the code incredibly clean
    using namespace std;
    using namespace cv;

    cout << "========================================" << endl;
    cout << "Initializing DVRK_OpenECM Multi-Pipeline" << endl;
    cout << "========================================" << endl;

    // 1. Declare all OpenXR, Vulkan, and Swapchain state variables
    XrInstance xrInstance = XR_NULL_HANDLE;
    XrSystemId xrSystemId = XR_NULL_SYSTEM_ID;
    EcmXr::VulkanContext vulkanCtx; // Fixes your "undeclared vulkanCtx" error
    XrSession xrSession = XR_NULL_HANDLE;
    XrSwapchain xrSwapchain = XR_NULL_HANDLE;
    vector<EcmXr::SwapchainBuffer> swapchainBuffers;

    // 2. Initialize the OpenXR Instance and find the VR System
    if (!EcmXr::InitializeOpenXR(xrInstance, xrSystemId)) {
        cerr << "[FATAL] Failed to initialize OpenXR. Is WiVRn/Monado running?" << endl;
        return -1;
    }

    // 3. Create the Vulkan Context handshake requested by OpenXR
    if (!EcmXr::CreateVulkanContext(xrInstance, xrSystemId, vulkanCtx)) {
        cerr << "[FATAL] Failed to establish Vulkan graphics context connection." << endl;
        if (xrInstance != XR_NULL_HANDLE) xrDestroyInstance(xrInstance);
        return -1;
    }

    // 4. Open the video capture device (/dev/video2)
    VideoCapture cap(cap_dev);
    if (!cap.isOpened()) {
        cerr << "[ERROR] Could not open camera device" << endl;
        cerr << "Ensure your webcam is connected or check 'ls /dev/video*'" << endl;
        // Clean up what we have initialized so far before exiting
        if (vulkanCtx.device != VK_NULL_HANDLE) vkDestroyDevice(vulkanCtx.device, nullptr);
        if (vulkanCtx.instance != VK_NULL_HANDLE) vkDestroyInstance(vulkanCtx.instance, nullptr);
        if (xrInstance != XR_NULL_HANDLE) xrDestroyInstance(xrInstance);
        return -1;
    }

    // 5. Configure standard streaming parameters
    cap.set(CAP_PROP_FRAME_WIDTH, 848);
    cap.set(CAP_PROP_FRAME_HEIGHT, 480);
    cap.set(CAP_PROP_FPS, 30);

    double actual_width  = cap.get(CAP_PROP_FRAME_WIDTH);
    double actual_height = cap.get(CAP_PROP_FRAME_HEIGHT);
    double actual_fps    = cap.get(CAP_PROP_FPS);

    cout << "[SUCCESS] Camera initialized successfully!" << endl;
    cout << "Stream Format: " << actual_width << "x" << actual_height 
         << " @ " << actual_fps << " FPS" << endl;

    // 6. Initialize OpenXR Session and create Swapchains using actual camera frame size
    if (!EcmXr::CreateSessionAndSwapchain(xrInstance, xrSystemId, vulkanCtx, 
                                          static_cast<uint32_t>(actual_width), 
                                          static_cast<uint32_t>(actual_height), 
                                          xrSession, xrSwapchain, swapchainBuffers)) {
        cerr << "[FATAL] Failed to establish OpenXR session pipelines." << endl;
        cap.release();
        if (vulkanCtx.device != VK_NULL_HANDLE) vkDestroyDevice(vulkanCtx.device, nullptr);
        if (vulkanCtx.instance != VK_NULL_HANDLE) vkDestroyInstance(vulkanCtx.instance, nullptr);
        if (xrInstance != XR_NULL_HANDLE) xrDestroyInstance(xrInstance);
        return -1;
    }

    cout << "Press 'ESC' in the preview window to exit." << endl;

    // Mats to hold the raw frame and our formatted OpenXR copy
    Mat rawFrame;
    Mat rgbaFrame;

     // 5. Instantiate our memory copy renderer
    EcmXr::XrRenderer renderer(vulkanCtx, actual_width, actual_height);

    // Create a generic space context so OpenXR knows how to track position reference frames
    XrSpace viewSpace = XR_NULL_HANDLE;
    XrReferenceSpaceCreateInfo spaceCreateInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    spaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    spaceCreateInfo.poseInReferenceSpace = {{0,0,0,1}, {0,0,0}}; // Positioned center-front
    xrCreateReferenceSpace(xrSession, &spaceCreateInfo, &viewSpace);

    cout << "[SUCCESS] VR Streaming Active! Put on your headset." << endl;
    
    bool sessionRunning = false;

    // 6. Direct Frame Streaming Loop
    while (g_running) {
        EcmXr::PollEvents(xrInstance, xrSession, sessionRunning);

        if (!sessionRunning) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue; 
        }


        XrFrameState frameState{XR_TYPE_FRAME_STATE};
        XrResult res = xrWaitFrame(xrSession, nullptr, &frameState);        
        xrBeginFrame(xrSession, nullptr);

        // B. Capture camera frame from hardware
        cap >> rawFrame;
        if (rawFrame.empty()) {
            XrFrameEndInfo emptyEndInfo{XR_TYPE_FRAME_END_INFO};
            emptyEndInfo.displayTime = frameState.predictedDisplayTime;
            emptyEndInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
            emptyEndInfo.layerCount = 0;
            emptyEndInfo.layers = nullptr;
            xrEndFrame(xrSession, &emptyEndInfo);
            continue;
        }

        // Convert camera layout format from OpenCV (BGR) to OpenXR (RGBA)
        cvtColor(rawFrame, rgbaFrame, COLOR_BGR2RGBA);

        // C. Request a clear buffer slot index from the WiVRn image carousel
        uint32_t imageIndex = 0;
        XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        xrAcquireSwapchainImage(xrSwapchain, &acquireInfo, &imageIndex);

        XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        waitInfo.timeout = XR_INFINITE_DURATION; // Block until the texture slot is freed by compositor
        xrWaitSwapchainImage(xrSwapchain, &waitInfo);

        // D. UPLOAD PIXELS: Copy raw image data into the assigned swapchain image index texture target
        renderer.UploadFrameToImage(swapchainBuffers[imageIndex].image, rgbaFrame.data);

        // E. Release ownership of the updated texture back to WiVRn
        XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        xrReleaseSwapchainImage(xrSwapchain, &releaseInfo);

         // F. Instruct the compositor to render these texture layers directly across your fields of view
        XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
        endInfo.displayTime = frameState.predictedDisplayTime;
        
        // OPAQUE handles blocking out the passthrough/VR environment, keeping background areas dark
        endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE; 

        // 1. Initialize TWO separate Quad Layers (one for each eye)
        XrCompositionLayerQuad leftLayer{XR_TYPE_COMPOSITION_LAYER_QUAD};
        XrCompositionLayerQuad rightLayer{XR_TYPE_COMPOSITION_LAYER_QUAD};
        
        // 2. CREATE A SOLID BLACK BACKGROUND LAYER:
        // We set this to viewSpace as well, but make it massive and push it far back.
        // If your xrSwapchain texture isn't solid black, you can clear it to black during your frame update loop.
        XrCompositionLayerQuad backgroundLayer{XR_TYPE_COMPOSITION_LAYER_QUAD};
        backgroundLayer.space = viewSpace; 
        backgroundLayer.layerFlags = XR_COMPOSITION_LAYER_CORRECT_CHROMATIC_ABERRATION_BIT;
        backgroundLayer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        backgroundLayer.subImage.swapchain = xrSwapchain; 
        backgroundLayer.subImage.imageRect.offset = {0, 0};
        backgroundLayer.subImage.imageRect.extent = {static_cast<int32_t>(actual_width), static_cast<int32_t>(actual_height)};
        backgroundLayer.subImage.imageArrayIndex = 0; 
        backgroundLayer.size = {100.0f, 100.0f}; // 100 meters wide to swallow the whole view space
        backgroundLayer.pose.position = {0.0f, 0.0f, -10.0f}; // Placed far behind your theater panel
        backgroundLayer.pose.orientation = {0.0f, 0.0f, 0.0f, 1.0f};

        // --- THEATER SCREEN COMMON CONFIGURATION ---
        // HEAD-LOCKED FIX 1: Set reference space to viewSpace so it sticks to the face
        leftLayer.space = viewSpace; 
        leftLayer.layerFlags = XR_COMPOSITION_LAYER_CORRECT_CHROMATIC_ABERRATION_BIT;
        leftLayer.subImage.swapchain = xrSwapchain;
        leftLayer.subImage.imageRect.offset = {0, 0};
        leftLayer.subImage.imageRect.extent = {static_cast<int32_t>(actual_width), static_cast<int32_t>(actual_height)};
        
        // Virtual screen dimensions (1.6 meters wide by 1.2 meters high)
        leftLayer.size = {1.6f, 1.2f}; 
        leftLayer.pose.orientation = {0.0f, 0.0f, 0.0f, 1.0f};

        // HEAD-LOCKED FIX 2: Set the position to dead-center.
        // Because we are in viewSpace, {0, 0, -1.5} keeps it locked exactly 1.5 meters out from the operator's nose
        leftLayer.pose.position  = {0.0f, 0.0f, -0.75f}; 
        
        // Clone the base setup to the right eye
        rightLayer = leftLayer;

        // --- INDIVIDUAL COMPOSITOR EYE ROUTING ---
        leftLayer.eyeVisibility = XR_EYE_VISIBILITY_LEFT;
        rightLayer.eyeVisibility = XR_EYE_VISIBILITY_RIGHT;

        // HEAD-LOCKED FIX 3: Force both eyes to map directly onto your flat 2D texture (Index 0)
        // This stops OpenXR from looking for a non-existent right-eye index array element
        leftLayer.subImage.imageArrayIndex = 0;
        rightLayer.subImage.imageArrayIndex = 0; 

        // 3. PACK ALL THREE POINTERS CORRECTLY INTO THE ARRAY
        const XrCompositionLayerBaseHeader* layerPointers[3];
        layerPointers[0] = (const XrCompositionLayerBaseHeader*)&backgroundLayer;
        layerPointers[1] = (const XrCompositionLayerBaseHeader*)&leftLayer;
        layerPointers[2] = (const XrCompositionLayerBaseHeader*)&rightLayer;

        if (frameState.shouldRender) {
            endInfo.layerCount = 3; 
            endInfo.layers = layerPointers;
        } else {
            endInfo.layerCount = 0;
            endInfo.layers = nullptr;
        }

        xrEndFrame(xrSession, &endInfo);
        
        // Keep local desktop review mirror window active
        imshow("DVRK OpenECM - Local Preview", rawFrame);
        int key = waitKey(1);
        if (key == 27) g_running = false; // Escape key terminates loop cleanly
    }

    // 8. Cleanup resources in the strict reverse order of allocation
    cout << "Stopping camera stream and cleaning up..." << endl;
    cap.release();
    destroyAllWindows();
    
    if (xrSwapchain != XR_NULL_HANDLE) {
        cout << "Destroying OpenXR Swapchain..." << endl;
        xrDestroySwapchain(xrSwapchain);
    }
    
    if (xrSession != XR_NULL_HANDLE) {
        cout << "Destroying OpenXR Session..." << endl;
        xrDestroySession(xrSession);
    }

    if (vulkanCtx.device != VK_NULL_HANDLE) {
        cout << "Destroying Vulkan Logical Device..." << endl;
        vkDestroyDevice(vulkanCtx.device, nullptr);
    }
    
    if (vulkanCtx.instance != VK_NULL_HANDLE) {
        cout << "Destroying Vulkan Instance..." << endl;
        vkDestroyInstance(vulkanCtx.instance, nullptr);
    }

    if (xrInstance != XR_NULL_HANDLE) {
        cout << "Destroying OpenXR Instance..." << endl;
        xrDestroyInstance(xrInstance);
    }

    cout << "Shutdown complete." << endl;
    return 0;
}

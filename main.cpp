#include <iostream>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <opencv2/opencv.hpp>

#include "xr_engine.h"

// Atomic flag to cleanly exit the main loop when pressing ESC or Ctrl+C
std::atomic<bool> g_running{true};

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
    VideoCapture cap(2);
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
    cap.set(CAP_PROP_FRAME_WIDTH, 640);
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

    // 7. Main Execution Loop
    while (g_running) {
        cap >> rawFrame;

        if (rawFrame.empty()) {
            cerr << "[WARNING] Received empty camera frame. Retrying..." << endl;
            continue;
        }

        cvtColor(rawFrame, rgbaFrame, COLOR_BGR2RGBA);

        // --- PIPELINE PLUG ---
        // This is exactly where we will write the Vulkan memory copy function!
        // ---------------------

        imshow("DVRK OpenECM - Local Preview", rawFrame);

        int key = waitKey(1);
        if (key == 27) { // ESC key
            g_running = false;
        }
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

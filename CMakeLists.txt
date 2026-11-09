# 1. Minimum CMake version required to parse this script
cmake_minimum_required(VERSION 3.16)

# 2. Name of your project and the language it uses (CXX means C++)
project(DVRK_OpenECM CXX)

# 3. Use modern C++17 standards (required by modern Vulkan/OpenXR wrappers)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 4. Find External System Packages
# REQUIRED means the build script will stop and throw an error if these aren't installed on Linux
find_package(Vulkan REQUIRED)
find_package(OpenCV REQUIRED)

# 5. Tell CMake where to look for OpenXR headers and binaries
# Since OpenXR was built from source, we create variables you can override from the terminal
set(XR_INCLUDE_DIR "/home/careslab/OpenXR-SDK-Source/build/include" CACHE PATH "Path to OpenXR headers" FORCE)
set(XR_LIBRARY "/home/careslab/OpenXR-SDK-Source/build/src/loader/libopenxr_loader.so" CACHE FILEPATH "Path to libopenxr_loader.so")

# 6. Define the Executable Target
# This tells CMake: Build a binary called "openecm_xr" using the source file "main.cpp"
add_executable(openecm_xr main.cpp xr_engine.cpp xr_renderer.cpp)

# 7. Include Directories (Where the compiler looks for #include <...> statements)
target_include_directories(openecm_xr PRIVATE 
    ${Vulkan_INCLUDE_DIRS} 
    ${OpenCV_INCLUDE_DIRS} 
    ${XR_INCLUDE_DIR}
)

# 8. Link Libraries (Connects the compiled code with external binary engines at runtime)
target_link_libraries(openecm_xr PRIVATE 
    ${Vulkan_LIBRARIES} 
    ${OpenCV_LIBS} 
    ${XR_LIBRARY}
    pthread # Standard Linux multi-threading library
)
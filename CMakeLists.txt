cmake_minimum_required(VERSION 3.20)
project(VulkanRooms CXX)

set(CMAKE_CXX_STANDARD 17)

# Dependencies
include(FetchContent)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

find_package(Vulkan REQUIRED)

# Executable
add_executable(VulkanRooms
    src/main.cpp
    src/app/VulkanApp.cpp
    src/app/pathtracer/PathTracer.cpp
)

target_link_libraries(VulkanRooms Vulkan::Vulkan glfw)

target_include_directories(VulkanRooms PRIVATE
    src
    $ENV{VULKAN_SDK}/Include
    ${glfw_SOURCE_DIR}/include
)

# Shaders
find_program(GLSLC glslc HINTS $ENV{VULKAN_SDK}/Bin)

set(SHADER_DIR ${CMAKE_SOURCE_DIR}/shaders)
set(SPIRV_DIR ${CMAKE_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SPIRV_DIR})

add_custom_command(
    OUTPUT
        ${SPIRV_DIR}/vert.spv
        ${SPIRV_DIR}/frag.spv
        ${SPIRV_DIR}/comp.spv
    COMMAND ${GLSLC} ${SHADER_DIR}/shader.vert -o ${SPIRV_DIR}/vert.spv
    COMMAND ${GLSLC} ${SHADER_DIR}/shader.frag -o ${SPIRV_DIR}/frag.spv
    COMMAND ${GLSLC} ${SHADER_DIR}/pathtracer.comp -o ${SPIRV_DIR}/comp.spv
    DEPENDS
        ${SHADER_DIR}/shader.vert
        ${SHADER_DIR}/shader.frag
        ${SHADER_DIR}/pathtracer.comp
)

add_custom_target(Shaders DEPENDS
    ${SPIRV_DIR}/vert.spv
    ${SPIRV_DIR}/frag.spv
    ${SPIRV_DIR}/comp.spv
)

add_dependencies(VulkanRooms Shaders)

# Copy shaders to binary dir after build
add_custom_command(TARGET VulkanRooms POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_BINARY_DIR}/shaders
    $<TARGET_FILE_DIR:VulkanRooms>/shaders
)

# ImGui
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.89.4
)
FetchContent_MakeAvailable(imgui)

target_sources(VulkanRooms PRIVATE
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)

target_include_directories(VulkanRooms PRIVATE
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
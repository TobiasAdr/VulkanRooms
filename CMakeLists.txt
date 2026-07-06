cmake_minimum_required(VERSION 3.20)
project(NyttProjekt)

set(CMAKE_CXX_STANDARD 17)

find_package(Vulkan REQUIRED)

include(FetchContent)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.3.8
)
FetchContent_MakeAvailable(glfw)

add_executable(VulkanRooms
    src/main.cpp
    src/app/VulkanApp.cpp

)

target_link_libraries(VulkanRooms 
    Vulkan::Vulkan 
    glfw
)

target_include_directories(VulkanRooms PRIVATE
    $ENV{VULKAN_SDK}/Include
)
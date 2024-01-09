project(imgui)

add_library(imgui STATIC)

target_include_directories(imgui SYSTEM PUBLIC
    imgui
    imgui/backends
)

file(GLOB SRC_IMGUI imgui/*.cpp)
set(SRC_IMGUI_BACKEND imgui/backends/imgui_impl_glfw.cpp imgui/backends/imgui_impl_opengl3.cpp)
target_sources(imgui PRIVATE ${SRC_IMGUI} ${SRC_IMGUI_BACKEND})
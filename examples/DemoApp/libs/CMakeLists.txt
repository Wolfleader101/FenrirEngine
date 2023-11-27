set(GLFW_BUILD_DOCS OFF)
set(GLFW_BUILD_TESTS OFF)
set(GLFW_BUILD_EXAMPLES OFF)

add_subdirectory(glad)
add_subdirectory(glfw)

target_include_directories(DemoApp SYSTEM PUBLIC
    glad/include
    glfw/include
    stb_image/include/
)
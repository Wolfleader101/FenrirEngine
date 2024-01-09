set(GLFW_BUILD_DOCS OFF)
set(GLFW_BUILD_TESTS OFF)
set(GLFW_BUILD_EXAMPLES OFF)

set(BUILD_SHARED_LIBS OFF)
set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF)
set(ASSIMP_BUILD_TESTS OFF)
set(ASSIMP_INSTALL OFF)
set(ASSIMP_BUILD_ASSIMP_VIEW  OFF)

set(ASSIMP_BUILD_OBJ_IMPORTER ON)
set(ASSIMP_BUILD_GLTF_IMPORTER ON)
set(ASSIMP_BUILD_FBX_IMPORTER ON)

set(ASSIMP_WARNINGS_AS_ERRORS OFF)


add_subdirectory(glad)
add_subdirectory(glfw)
add_subdirectory(imgui)
add_subdirectory(assimp)
add_subdirectory(glaze)

target_include_directories(imgui PUBLIC glfw/include)

target_include_directories(DemoApp SYSTEM PUBLIC
    glad/include
    glfw/include
    stb_image/include/
    assimp/include/
    glaze/include
)
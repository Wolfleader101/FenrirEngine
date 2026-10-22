# FenrirRenderer (ADR-0016, ADR-0018, ADR-0019). Built with FENRIR_ENABLE_RENDERER. It holds the build-time
# FenrirShaderCompiler (Tools/ShaderCompiler), the engine's shader includes (Shaders/include, the directory
# fenrir_add_shaders resolves #include against), and the renderer's layers, each its own target with headers that are
# never installed:
# - FenrirRendererDevice: the private graphics device, its GraphicsSurface and the null backend (src/Device,
#   src/Backend/Null).
# - FenrirRendererOpenGL: the OpenGL 4.6 backend (src/Backend/OpenGL), the only target that includes GL or links its
#   loader, FenrirGlad (libs/glad), which it links PRIVATE.
# - FenrirRendererCore: the renderer core (src/Core): the render graph, render features and the frame. It has no
#   FenrirApp, FenrirECS, FenrirScene or FenrirWindow dependency, so it is tested with the null backend and no App.
# The ECS facade, FenrirRenderer itself, joins them in a later ticket.
add_subdirectory(Tools/ShaderCompiler)
add_subdirectory(libs/glad)

add_library(FenrirRendererDevice STATIC
    src/Device/CommandList.hpp
    src/Device/GraphicsDevice.hpp
    src/Device/GraphicsSurface.hpp
    src/Device/Handles.hpp
    src/Device/ResourceTable.hpp
    src/Device/Resources.hpp

    src/Backend/Null/NullDevice.hpp
    src/Backend/Null/NullDevice.cpp
)
target_include_directories(FenrirRendererDevice PUBLIC src)
# The shader container's data types only: pipelines are made from its variants.
target_link_libraries(FenrirRendererDevice PUBLIC FenrirShaderContainer)

add_library(FenrirRendererOpenGL STATIC
    src/Backend/OpenGL/OpenGLDevice.hpp
    src/Backend/OpenGL/OpenGLDevice.cpp
)
target_link_libraries(FenrirRendererOpenGL PUBLIC FenrirRendererDevice FenrirLogger PRIVATE FenrirGlad)

add_library(FenrirRendererCore STATIC
    src/Core/FrameRingBuffer.hpp
    src/Core/FrameRingBuffer.cpp
    src/Core/RenderFeature.hpp
    src/Core/RenderGraph.hpp
    src/Core/RenderGraph.cpp
    src/Core/RendererCore.hpp
    src/Core/RendererCore.cpp
    src/Core/TransientPool.hpp
    src/Core/TransientPool.cpp
)
target_link_libraries(FenrirRendererCore PUBLIC FenrirRendererDevice)

# Tests that use Catch2: only with -DFENRIR_BUILD_TESTS=ON (see the root CMakeLists.txt).
if(FENRIR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

# FenrirRenderer (ADR-0016, ADR-0018, ADR-0019). Built with FENRIR_ENABLE_RENDERER. It holds the build-time
# FenrirShaderCompiler (Tools/ShaderCompiler), the engine's shader includes (Shaders/include, the directory
# fenrir_add_shaders resolves #include against), and the renderer's layers, each its own target with headers that are
# never installed:
# - FenrirRendererDevice: the private graphics device, its GraphicsSurface and the null backend (src/Device,
#   src/Backend/Null).
# - FenrirRendererOpenGL: the OpenGL 4.6 backend (src/Backend/OpenGL), the only target that includes GL or links its
#   loader, FenrirGlad (libs/glad), which it links PRIVATE.
# - FenrirRendererCore: the renderer core (src/Core): the render graph, render features, the frame and the scene
#   renderer drawing the meshes and materials it is given, with the built-in shaders (Shaders/Builtin) embedded. It has
#   no FenrirApp, FenrirECS, FenrirScene or FenrirWindow dependency, so it is tested with the null backend and no App.
#   It uses FenrirAssets' plain data types (MeshData, MaterialData, ShaderData) through FenrirAssets' include
#   directory, but never links FenrirAssets or sees its store (ADR-0023).
# - FenrirRenderer: the ECS facade (include/, src/Facade): Renderer::Install, the components and the Render system.
#   The only public target, and the only one that links FenrirWindow, PRIVATE.
add_subdirectory(Tools/ShaderCompiler)
add_subdirectory(libs/glad)
add_subdirectory(libs/stb_truetype)

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
    src/Core/BlockRing.hpp
    src/Core/BlockRing.cpp
    src/Core/BuiltinShaders.hpp
    src/Core/BuiltinShaders.cpp
    src/Core/DebugFeature.hpp
    src/Core/DebugFeature.cpp
    src/Core/DebugFont.hpp
    src/Core/DebugFont.cpp
    src/Core/Draw2DFeature.hpp
    src/Core/Draw2DFeature.cpp
    src/Core/FrameRingBuffer.hpp
    src/Core/FrameRingBuffer.cpp
    src/Core/RenderFeature.hpp
    src/Core/RenderGraph.hpp
    src/Core/RenderGraph.cpp
    src/Core/RendererCore.hpp
    src/Core/RendererCore.cpp
    src/Core/SceneFrame.hpp
    src/Core/SceneRenderer.hpp
    src/Core/SceneRenderer.cpp
    src/Core/Skinning.hpp
    src/Core/Skinning.cpp
    src/Core/Tonemapping.hpp
    src/Core/Tonemapping.cpp
    src/Core/TransientPool.hpp
    src/Core/TransientPool.cpp
)
target_link_libraries(FenrirRendererCore PUBLIC FenrirRendererDevice FenrirMath FenrirLogger PRIVATE FenrirStbTruetype)
# The debug font (ADR-0025), an OFL monospace TrueType font checked in with its licence (Resources/Fonts), embedded as a
# byte array generated at build time, so the renderer runs with no files beside it.
set(debug_font "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Fonts/JetBrainsMono-Regular.ttf")
set(debug_font_header "${CMAKE_CURRENT_BINARY_DIR}/resources/include/FenrirResources/DebugFont.hpp")
add_custom_command(
    OUTPUT "${debug_font_header}"
    COMMAND "${CMAKE_COMMAND}" -DINPUT=${debug_font} -DOUTPUT=${debug_font_header} -DNAME=DebugFont -P
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedBytes.cmake"
    DEPENDS "${debug_font}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedBytes.cmake"
    COMMENT "Embedding the debug font"
    VERBATIM
)
target_sources(FenrirRendererCore PRIVATE "${debug_font_header}")
target_include_directories(FenrirRendererCore PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/resources/include")
# FenrirAssets' data types only: its headers, not its library, so the core cannot reach the store or the App. The same
# for FenrirDraw2D's plain data, which the 2D render feature draws.
target_include_directories(FenrirRendererCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../FenrirAssets/include"
                                                     "${CMAKE_CURRENT_SOURCE_DIR}/../FenrirDraw2D/include")
# The built-in shader programs, embedded, so the renderer runs with no files beside it.
# ALPHA_MASK: the variant a Masked material draws with (ADR-0024). SKINNED: the variant skinned draws use (ADR-0035);
# the fallback has it too, so a skinned draw falling back to it is never drawn unskinned.
fenrir_add_shaders(FenrirRendererCore PROGRAM BuiltinUnlit VERTEX Shaders/Builtin/mesh.vert
                   FRAGMENT Shaders/Builtin/unlit.frag KEYS ALPHA_MASK SKINNED EMBED)
fenrir_add_shaders(FenrirRendererCore PROGRAM BuiltinLit VERTEX Shaders/Builtin/mesh.vert
                   FRAGMENT Shaders/Builtin/lit.frag KEYS ALPHA_MASK SKINNED EMBED)
fenrir_add_shaders(FenrirRendererCore PROGRAM BuiltinFallback VERTEX Shaders/Builtin/mesh.vert
                   FRAGMENT Shaders/Builtin/fallback.frag KEYS SKINNED EMBED)
# CUBEMAP: the variant that draws a Sky cubemap.
fenrir_add_shaders(FenrirRendererCore PROGRAM BuiltinBackground VERTEX Shaders/Builtin/fullscreen.vert
                   FRAGMENT Shaders/Builtin/background.frag KEYS CUBEMAP EMBED)
# MSAA: the variant that resolves a multisampled SceneColor.
fenrir_add_shaders(FenrirRendererCore PROGRAM BuiltinOutput VERTEX Shaders/Builtin/fullscreen.vert
                   FRAGMENT Shaders/Builtin/output.frag KEYS MSAA EMBED)
# Debug drawing (ADR-0025): lines expanded to screen-space quads, solid triangles, and text from the SDF font atlas.
fenrir_add_shaders(FenrirRendererCore PROGRAM BuiltinDebugLines VERTEX Shaders/Builtin/debug_lines.vert
                   FRAGMENT Shaders/Builtin/debug.frag EMBED)
fenrir_add_shaders(FenrirRendererCore PROGRAM BuiltinDebugTriangles VERTEX Shaders/Builtin/debug_triangles.vert
                   FRAGMENT Shaders/Builtin/debug.frag EMBED)
fenrir_add_shaders(FenrirRendererCore PROGRAM BuiltinDebugText VERTEX Shaders/Builtin/debug_text.vert
                   FRAGMENT Shaders/Builtin/debug_text.frag EMBED)
# builtin:2d (ADR-0027), the 2D render feature's own shader on the ImGui vertex layout. The renderer picks the keys from
# the 2D pipeline description: INPUT_SRGB (sRGB vertex colours), PREMULTIPLY (PremultipliedAlpha), GAMMA (the gamma path).
fenrir_add_shaders(FenrirRendererCore PROGRAM Builtin2D VERTEX Shaders/Builtin/2d.vert FRAGMENT Shaders/Builtin/2d.frag
                   KEYS INPUT_SRGB PREMULTIPLY GAMMA EMBED)

add_library(FenrirRenderer STATIC
    include/FenrirRenderer/Components.hpp
    include/FenrirRenderer/Overlay.hpp
    include/FenrirRenderer/Renderer.hpp
    include/FenrirRenderer/SpawnModel.hpp

    src/Facade/Renderer.cpp
    src/Facade/RendererState.hpp
    src/Facade/SpawnModel.cpp
)
target_include_directories(FenrirRenderer PUBLIC include PRIVATE src)
target_link_libraries(FenrirRenderer PUBLIC FenrirAssets FenrirDraw2D FenrirInput FenrirMath PRIVATE FenrirRendererCore FenrirRendererOpenGL
                                                                            FenrirWindow FenrirDebugDraw)

# Tests that use Catch2: only with -DFENRIR_BUILD_TESTS=ON (see the root CMakeLists.txt).
if(FENRIR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

# Its shared types (ADR-0042): `cmake --build <dir> --target fenrir-describe-api` regenerates api/FenrirRenderer.api.json,
# which FenrirRendererScripting's binding binds.
fenrir_describe_api(FenrirRenderer api/FenrirRenderer.api.toml)

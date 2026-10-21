# SPIKE (prototype/fenrir-renderer-spike, throwaway): FenrirRenderer in three layers (ADR-0019). The core and its
# backends are a private target; only src/Core/Backend/OpenGL/ sees GL and its loader. The facade is the public target
# and the only one that links FenrirWindow.
add_subdirectory(ShaderCompiler)

add_library(FenrirRendererCore STATIC
    src/Core/Device/Device.hpp
    src/Core/Device/HandlePool.hpp
    src/Core/Backend/Null/NullDevice.cpp
    src/Core/RenderGraph.hpp
    src/Core/RenderGraph.cpp
    src/Core/RendererCore.hpp
    src/Core/RendererCore.cpp
    src/Core/ShaderContainer.hpp
    src/Core/ShaderContainer.cpp
)
target_include_directories(FenrirRendererCore PUBLIC src/Core)
target_link_libraries(FenrirRendererCore PUBLIC FenrirAssets FenrirDebugDraw FenrirMath PRIVATE FenrirRendererGL)

# The GL backend, alone with the loader: its include path reaches no other translation unit.
add_library(FenrirRendererGL STATIC src/Core/Backend/OpenGL/GLDevice.cpp libs/glad/glad.c)
target_include_directories(FenrirRendererGL PRIVATE src/Core)
target_include_directories(FenrirRendererGL SYSTEM PRIVATE libs/glad/include)
set_source_files_properties(libs/glad/glad.c PROPERTIES COMPILE_OPTIONS "-w")

fenrir_add_shaders(FenrirRendererCore NAME Lit VERT shaders/mesh.vert FRAG shaders/lit.frag)
fenrir_add_shaders(FenrirRendererCore NAME Unlit VERT shaders/mesh.vert FRAG shaders/unlit.frag)
fenrir_add_shaders(FenrirRendererCore NAME Sky VERT shaders/fullscreen.vert FRAG shaders/sky.frag)
fenrir_add_shaders(FenrirRendererCore NAME Output VERT shaders/fullscreen.vert FRAG shaders/output.frag)
fenrir_add_shaders(FenrirRendererCore NAME DebugLine VERT shaders/debug_line.vert FRAG shaders/debug_line.frag)

add_library(FenrirRenderer STATIC include/FenrirRenderer/Renderer.hpp src/Facade/Renderer.cpp)
target_include_directories(FenrirRenderer PUBLIC include)
target_link_libraries(FenrirRenderer PUBLIC FenrirApp FenrirAssets FenrirECS FenrirMath
                                     PRIVATE FenrirRendererCore FenrirWindow FenrirDebugDraw)

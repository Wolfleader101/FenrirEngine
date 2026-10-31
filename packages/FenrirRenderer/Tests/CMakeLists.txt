# FenrirRenderer's tests on the null backend (the spec's seam 1): the renderer core alone, with a fake GraphicsSurface
# and no App, and the ECS facade in an App run one frame at a time, both read back through RendererProbe. They need no
# window or GPU, so they run on every machine and preset.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

# Golden images (ADR-0049): PNGs read and written through FenrirAssets's stb, and frames compared with them by
# CIEDE2000. The null tests check the comparison; the GL tests compare whole frames with OpenGL/goldens.
add_library(FenrirRendererGoldenImages STATIC GoldenImages.cpp GoldenImages.hpp)
target_include_directories(FenrirRendererGoldenImages PUBLIC .)
target_link_libraries(FenrirRendererGoldenImages PUBLIC Catch2::Catch2 PRIVATE FenrirStbImage)

add_executable(FenrirRendererTests PublicHeaders.cpp CoreHeaders.cpp Device.cpp CommandLists.cpp RenderGraph.cpp
                                   FrameRing.cpp Frames.cpp OpenGLStart.cpp Drawing.cpp Preparing.cpp RendererFrames.cpp
                                   Cameras.cpp Picking.cpp Materials.cpp SpawnModel.cpp Sky.cpp DebugFont.cpp
                                   DebugDrawing.cpp Overlay.cpp Draw2D.cpp Skinning.cpp SkinningMaths.cpp
                                   SphericalHarmonics.cpp SkyLighting.cpp Tonemapping.cpp Brdf.cpp Bloom.cpp
                                   GoldenComparison.cpp DeviceTest.hpp DrawLists2D.hpp FakeSurface.hpp RecordingLogger.hpp
                                   RendererProbe.hpp RendererTestApp.hpp SkinningTestApp.hpp)
target_include_directories(FenrirRendererTests PRIVATE ../src)
# The OpenGL backend too, for the cases where it cannot start: they need no GPU.
# FenrirSkeleton as the skinning source the skinned tests draw from: the tests link it, the renderer never does
# (ADR-0041).
target_link_libraries(FenrirRendererTests PRIVATE FenrirRenderer FenrirRendererCore FenrirRendererOpenGL FenrirDebugDraw
                                                  FenrirSkeleton FenrirRendererGoldenImages Catch2::Catch2WithMain)
# A shader whose frame set is not the renderer's, for the debug check of the fixed sets.
fenrir_add_shaders(FenrirRendererTests PROGRAM WrongFrameSet VERTEX fixtures/wrong_frame.vert
                   FRAGMENT fixtures/plain.frag EMBED)
# A material shader sampling one texture, for cameras' image targets sampled by materials (ADR-0026).
fenrir_add_shaders(FenrirRendererTests PROGRAM SampledPicture VERTEX ../Shaders/Builtin/mesh.vert
                   FRAGMENT fixtures/sampled.frag EMBED)
# A 2D shader with a parameter block in set 1, for 2D commands' parameter bytes (ADR-0027).
fenrir_add_shaders(FenrirRendererTests PROGRAM Tinted2D VERTEX ../Shaders/Builtin/2d.vert
                   FRAGMENT fixtures/tinted_2d.frag EMBED)
# A user's own material shader (ADR-0024), built as any user's is and loaded from the container file it writes, with its
# own variant key FANCY, which the renderer leaves off.
fenrir_add_shaders(FenrirRendererTests PROGRAM UserTint VERTEX fixtures/user.vert FRAGMENT fixtures/user.frag KEYS FANCY)
# Khronos's BoxTextured from the demo apps' shared assets, for SpawnModel on a real model, a glTF node with two meshes,
# and FenrirAssets's skinned armatures fixture.
target_compile_definitions(FenrirRendererTests PRIVATE
    FENRIR_TEST_USER_SHADER="${CMAKE_CURRENT_BINARY_DIR}/shaders/UserTint.fshader"
    FENRIR_TEST_BOX_TEXTURED="${CMAKE_CURRENT_SOURCE_DIR}/../../../examples/assets/BoxTextured/BoxTextured.gltf"
    FENRIR_TEST_TWO_PARTS="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/two_parts.gltf"
    FENRIR_TEST_ARMATURES="${CMAKE_CURRENT_SOURCE_DIR}/../../FenrirAssets/tests/fixtures/models/armatures.gltf")
add_test(NAME FenrirRenderer COMMAND FenrirRendererTests)

# The GL pixel probes (the spec's seam 2): the OpenGL backend on a hidden window's OpenGL 4.6 core context, made through
# FenrirWindow as the renderer makes it, with debug validation, each test reading back a small offscreen target through
# GlProbe. Without a display or a 4.6 core context every test reports skipped (Catch2's SKIP, exit code 4), never
# passed. The fixture shaders are built by fenrir_add_shaders and embedded. Labelled gl: Windows CI runs the null
# tests only, and developers run these on their own GPU.
add_executable(FenrirRendererGlTests OpenGL/Clears.cpp OpenGL/Draws.cpp OpenGL/Validation.cpp OpenGL/Resources.cpp
                                     OpenGL/Vsync.cpp OpenGL/Scenes.cpp OpenGL/Targets.cpp OpenGL/Materials.cpp
                                     OpenGL/Sky.cpp OpenGL/Debug.cpp OpenGL/Overlay.cpp OpenGL/Skinning.cpp
                                     OpenGL/Tonemapping.cpp OpenGL/NormalMaps.cpp OpenGL/SkyLighting.cpp OpenGL/Bloom.cpp
                                     OpenGL/GlProbe.hpp OpenGL/GlScene.hpp OpenGL/GlTestApp.hpp
                                     DrawLists2D.hpp RecordingLogger.hpp RendererProbe.hpp RendererTestApp.hpp
                                     SkinningTestApp.hpp)
target_include_directories(FenrirRendererGlTests PRIVATE . ../src)
target_link_libraries(FenrirRendererGlTests PRIVATE FenrirRenderer FenrirRendererCore FenrirRendererOpenGL FenrirWindow
                                                    FenrirApp FenrirDebugDraw FenrirSkeleton FenrirRendererGoldenImages
                                                    Catch2::Catch2WithMain)
# The golden images, committed; a failing frame and its difference image are written into the build tree.
target_compile_definitions(FenrirRendererGlTests PRIVATE
    FENRIR_TEST_GOLDENS="${CMAKE_CURRENT_SOURCE_DIR}/OpenGL/goldens"
    FENRIR_TEST_GOLDEN_FAILURES="${CMAKE_CURRENT_BINARY_DIR}/golden-failures")
fenrir_add_shaders(FenrirRendererGlTests PROGRAM GlFullscreenFill VERTEX OpenGL/fixtures/fullscreen.vert
                   FRAGMENT OpenGL/fixtures/fill.frag EMBED)
fenrir_add_shaders(FenrirRendererGlTests PROGRAM GlPositionFill VERTEX OpenGL/fixtures/position.vert
                   FRAGMENT OpenGL/fixtures/fill.frag EMBED)
fenrir_add_shaders(FenrirRendererGlTests PROGRAM SampledPicture VERTEX ../Shaders/Builtin/mesh.vert
                   FRAGMENT fixtures/sampled.frag EMBED)
# A material set of a block and four textures, for the flattening formula's texture units.
fenrir_add_shaders(FenrirRendererGlTests PROGRAM GlFourTextures VERTEX ../Shaders/Builtin/mesh.vert
                   FRAGMENT OpenGL/fixtures/four_textures.frag EMBED)
add_test(NAME FenrirRenderer-GL COMMAND FenrirRendererGlTests)
set_tests_properties(FenrirRenderer-GL PROPERTIES SKIP_RETURN_CODE 4 TIMEOUT 300 LABELS gl)

# FenrirRenderer's tests on the null backend (the spec's seam 1): the renderer core alone, with a fake GraphicsSurface
# and no App, and the ECS facade in an App run one frame at a time, both read back through RendererProbe. They need no
# window or GPU, so they run on every machine and preset.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

add_executable(FenrirRendererTests PublicHeaders.cpp CoreHeaders.cpp Device.cpp CommandLists.cpp RenderGraph.cpp
                                   FrameRing.cpp Frames.cpp OpenGLStart.cpp Drawing.cpp Preparing.cpp RendererFrames.cpp
                                   DeviceTest.hpp FakeSurface.hpp RecordingLogger.hpp RendererProbe.hpp
                                   RendererTestApp.hpp)
target_include_directories(FenrirRendererTests PRIVATE ../src)
# The OpenGL backend too, for the cases where it cannot start: they need no GPU.
target_link_libraries(FenrirRendererTests PRIVATE FenrirRenderer FenrirRendererCore FenrirRendererOpenGL
                                                  Catch2::Catch2WithMain)
# A shader whose frame set is not the renderer's, for the debug check of the fixed sets.
fenrir_add_shaders(FenrirRendererTests PROGRAM WrongFrameSet VERTEX fixtures/wrong_frame.vert
                   FRAGMENT fixtures/plain.frag EMBED)
add_test(NAME FenrirRenderer COMMAND FenrirRendererTests)

# The GL pixel probes (the spec's seam 2): the OpenGL backend on a hidden window's OpenGL 4.6 core context, made through
# FenrirWindow as the renderer makes it, with debug validation, each test reading back a small offscreen target through
# GlProbe. Without a display or a 4.6 core context every test reports skipped (Catch2's SKIP, exit code 4), never
# passed. The fixture shaders are built by fenrir_add_shaders and embedded. Labelled gl: Windows CI runs the null
# tests only, and developers run these on their own GPU.
add_executable(FenrirRendererGlTests OpenGL/Clears.cpp OpenGL/Draws.cpp OpenGL/Validation.cpp OpenGL/Resources.cpp
                                     OpenGL/Vsync.cpp OpenGL/Scenes.cpp OpenGL/GlProbe.hpp OpenGL/GlTestApp.hpp
                                     RecordingLogger.hpp RendererProbe.hpp RendererTestApp.hpp)
target_include_directories(FenrirRendererGlTests PRIVATE . ../src)
target_link_libraries(FenrirRendererGlTests PRIVATE FenrirRenderer FenrirRendererCore FenrirRendererOpenGL FenrirWindow
                                                    FenrirApp Catch2::Catch2WithMain)
fenrir_add_shaders(FenrirRendererGlTests PROGRAM GlFullscreenFill VERTEX OpenGL/fixtures/fullscreen.vert
                   FRAGMENT OpenGL/fixtures/fill.frag EMBED)
fenrir_add_shaders(FenrirRendererGlTests PROGRAM GlPositionFill VERTEX OpenGL/fixtures/position.vert
                   FRAGMENT OpenGL/fixtures/fill.frag EMBED)
add_test(NAME FenrirRenderer-GL COMMAND FenrirRendererGlTests)
set_tests_properties(FenrirRenderer-GL PROPERTIES SKIP_RETURN_CODE 4 TIMEOUT 300 LABELS gl)

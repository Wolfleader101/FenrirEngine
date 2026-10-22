# FenrirRenderer's tests on the null backend (the spec's seam 1): the renderer core alone, with a fake GraphicsSurface
# and no App, read back through RendererProbe. They need no window or GPU, so they run on every machine and preset.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

add_executable(FenrirRendererTests CoreHeaders.cpp Device.cpp CommandLists.cpp RenderGraph.cpp FrameRing.cpp Frames.cpp
                                   DeviceTest.hpp FakeSurface.hpp RendererProbe.hpp)
target_link_libraries(FenrirRendererTests PRIVATE FenrirRendererCore Catch2::Catch2WithMain)
add_test(NAME FenrirRenderer COMMAND FenrirRendererTests)

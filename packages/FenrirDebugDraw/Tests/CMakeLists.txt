# FenrirDebugDraw's tests: headless, the App driven one frame at a time with a chosen delta. No window, no renderer.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

add_executable(FenrirDebugDrawTests DebugDrawTestApp.hpp Shapes.cpp Lifetimes.cpp Groups.cpp RelativeTo.cpp
                                    Threads.cpp Text.cpp)
target_link_libraries(FenrirDebugDrawTests PRIVATE FenrirDebugDraw Catch2::Catch2WithMain)
add_test(NAME FenrirDebugDraw COMMAND FenrirDebugDrawTests)

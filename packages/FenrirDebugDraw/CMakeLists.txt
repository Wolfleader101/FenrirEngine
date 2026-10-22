# FenrirDebugDraw: debug shapes and text as plain data per scene (ADR-0025). It links no library and has no GPU code, so
# it is built in every configuration; FenrirRenderer draws what it records.
add_library(FenrirDebugDraw STATIC
    include/FenrirDebugDraw/DebugDraw.hpp
    include/FenrirDebugDraw/DebugDrawing.hpp

    src/DebugDraw.cpp
    src/DebugDrawing.cpp
    src/DebugDrawState.hpp
    src/Shapes.cpp
    src/Shapes.hpp
)

target_link_libraries(FenrirDebugDraw PUBLIC FenrirApp FenrirScene FenrirMath)

target_include_directories(FenrirDebugDraw PUBLIC include PRIVATE src)

# Tests that use Catch2: only with -DFENRIR_BUILD_TESTS=ON (see the root CMakeLists.txt).
if(FENRIR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

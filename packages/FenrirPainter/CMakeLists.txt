# FenrirPainter: the painter (ADR-0060), which records analytic shapes, paints and clips into a FenrirDraw2D draw list
# through its own shader, builtin:painter, and fat vertex. The recording depends on FenrirDraw2D (and the FenrirAssets
# types it carries) and FenrirMath; Painting::Install, which registers the shader with the asset store (ADR-0027 lets a
# UI bring its shaders), also needs FenrirApp. Built with FENRIR_ENABLE_RENDERER, whose fenrir_add_shaders builds it.
add_library(FenrirPainter STATIC
    src/Painter.cpp
    src/PainterVertex.cpp
    src/Painting.cpp
    src/StopTexture.cpp
    src/Transform2D.cpp
    src/Colours.hpp
)
target_include_directories(FenrirPainter PUBLIC include)
target_link_libraries(FenrirPainter PUBLIC FenrirDraw2D FenrirMath PRIVATE FenrirApp)
# builtin:painter, embedded so the painter needs no files beside it. GAMMA: the variant for the Gamma blend space.
fenrir_add_shaders(FenrirPainter PROGRAM BuiltinPainter VERTEX Shaders/painter.vert FRAGMENT Shaders/painter.frag
                   KEYS GAMMA EMBED)

# Tests that use Catch2: only with -DFENRIR_BUILD_TESTS=ON (see the root CMakeLists.txt).
if(FENRIR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

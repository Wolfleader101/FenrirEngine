# FenrirUI: UI roots of elements laid out by Yoga flexbox behind the layout bridge and painted every frame through
# FenrirPainter into each root's 2D target (ADR-0063, ADR-0064, ADR-0066). Yoga is linked PRIVATE and included by
# src/LayoutBridge.cpp only, so no Yoga type reaches a public header. A root on a window reads its size from FenrirWindow
# and submits to FenrirRenderer's overlay, both linked PRIVATE too. Built with FENRIR_ENABLE_UI, which needs the renderer.
add_subdirectory(libs)

add_library(FenrirUI STATIC
    include/FenrirUI/Element.hpp
    include/FenrirUI/ElementId.hpp
    include/FenrirUI/Style.hpp
    include/FenrirUI/Stylesheet.hpp
    include/FenrirUI/UI.hpp
    include/FenrirUI/UIRoot.hpp
    include/FenrirUI/UITarget.hpp

    src/BoxPainting.cpp
    src/BoxPainting.hpp
    src/Cascade.cpp
    src/Cascade.hpp
    src/LayoutBridge.cpp
    src/LayoutBridge.hpp
    src/Rects.hpp
    src/Style.cpp
    src/StyleValues.cpp
    src/StyleValues.hpp
    src/Stylesheet.cpp
    src/UI.cpp
    src/UIRoot.cpp
    src/UIRootState.hpp
)
target_include_directories(FenrirUI PUBLIC include PRIVATE src)
target_link_libraries(FenrirUI PUBLIC FenrirPainter FenrirInput FenrirApp FenrirAssets
                               PRIVATE FenrirYoga FenrirWindow FenrirRenderer)

# Tests that use Catch2: only with -DFENRIR_BUILD_TESTS=ON (see the root CMakeLists.txt).
if(FENRIR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

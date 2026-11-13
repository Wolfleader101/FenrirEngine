# FenrirUI: UI roots of elements laid out by Yoga flexbox behind the layout bridge and painted every frame through
# FenrirPainter into each root's 2D target (ADR-0063, ADR-0064, ADR-0066). Yoga is linked PRIVATE and included by
# src/LayoutBridge.cpp only, so no Yoga type reaches a public header. A root on a window reads its size from FenrirWindow
# and submits to FenrirRenderer's overlay, both linked PRIVATE too. Built with FENRIR_ENABLE_UI, which needs the renderer.
add_subdirectory(libs)

# The default stylesheet, embedded as one raw string literal, which MSVC holds to 16380 bytes. Configure reads it again
# when it changes.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/Resources/default.css" FENRIR_UI_DEFAULT_STYLESHEET)
string(LENGTH "${FENRIR_UI_DEFAULT_STYLESHEET}" FENRIR_UI_DEFAULT_STYLESHEET_LENGTH)
if(FENRIR_UI_DEFAULT_STYLESHEET_LENGTH GREATER 16000)
    message(FATAL_ERROR "FenrirUI: Resources/default.css is over 16000 bytes; split its embedded literal")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Resources/default.css")
configure_file(src/DefaultStylesheet.cpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/DefaultStylesheet.cpp" @ONLY)

add_library(FenrirUI STATIC
    include/FenrirUI/Controls.hpp
    include/FenrirUI/Element.hpp
    include/FenrirUI/ElementId.hpp
    include/FenrirUI/Markup.hpp
    include/FenrirUI/Style.hpp
    include/FenrirUI/Stylesheet.hpp
    include/FenrirUI/UIEvent.hpp
    include/FenrirUI/UI.hpp
    include/FenrirUI/UIRoot.hpp
    include/FenrirUI/UITarget.hpp

    src/AttributeValues.hpp
    src/BoxPainting.cpp
    src/BoxPainting.hpp
    src/Cascade.cpp
    src/Cascade.hpp
    src/Controls.cpp
    src/DefaultStylesheet.cpp.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/DefaultStylesheet.cpp"
    src/ElementGeometry.cpp
    src/ElementGeometry.hpp
    src/ElementText.cpp
    src/ElementText.hpp
    src/HotReload.cpp
    src/InputRouting.cpp
    src/InputRouting.hpp
    src/LayoutBridge.cpp
    src/LayoutBridge.hpp
    src/Markup.cpp
    src/Rects.hpp
    src/Scrolling.cpp
    src/Scrolling.hpp
    src/Style.cpp
    src/StyleAnimation.cpp
    src/StyleAnimation.hpp
    src/StyleFiles.cpp
    src/StyleFiles.hpp
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

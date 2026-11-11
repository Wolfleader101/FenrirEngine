# FenrirText: fonts, shaping, paragraph layout and glyph atlases (ADR-0061, ADR-0062), over FreeType, HarfBuzz,
# SheenBidi and libunibreak. Only src/ includes them, and they are linked PRIVATE, so neither their headers nor their
# compile definitions reach FenrirText's users. It knows no renderer, painter or UI. Built with FENRIR_ENABLE_TEXT.
add_subdirectory(libs)

add_library(FenrirText STATIC
    include/FenrirText/FontLibrary.hpp
    include/FenrirText/GlyphAtlas.hpp
    include/FenrirText/Paragraph.hpp
    include/FenrirText/TextStyle.hpp

    src/FontLibrary.cpp
    src/FontLibraryState.hpp
    src/GlyphAtlas.cpp
    src/Paragraph.cpp
    src/Unicode.hpp
    src/Unicode.cpp
)

target_link_libraries(FenrirText PUBLIC FenrirAssets FenrirDraw2D FenrirMath
                                 PRIVATE FenrirHarfBuzz freetype FenrirSheenBidi FenrirUnibreak)

target_include_directories(FenrirText PUBLIC include PRIVATE src)

# The embedded default face (JetBrains Mono under the SIL Open Font License, Resources/Fonts), the debug font's file,
# embedded again here so FenrirText stays independent of the renderer; a byte array generated at build time.
set(default_font "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Fonts/JetBrainsMono-Regular.ttf")
set(default_font_header "${CMAKE_CURRENT_BINARY_DIR}/resources/include/FenrirTextResources/DefaultFont.hpp")
add_custom_command(
    OUTPUT "${default_font_header}"
    COMMAND "${CMAKE_COMMAND}" -DINPUT=${default_font} -DOUTPUT=${default_font_header} -DNAME=DefaultFont -P
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedBytes.cmake"
    DEPENDS "${default_font}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedBytes.cmake"
    COMMENT "Embedding FenrirText's default font"
    VERBATIM
)
target_sources(FenrirText PRIVATE "${default_font_header}")
target_include_directories(FenrirText PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/resources/include")

# Tests that use Catch2: only with -DFENRIR_BUILD_TESTS=ON (see the root CMakeLists.txt).
if(FENRIR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

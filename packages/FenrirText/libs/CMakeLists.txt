# FenrirText's text libraries (ADR-0062), each a pinned submodule built static: FreeType 2.15.0 (faces and rasterising),
# HarfBuzz 15.1.0 (shaping, with its FreeType integration), SheenBidi 2.9.0 (bidi levels, line runs and scripts) and
# libunibreak 8.0 (UAX#14 line breaks and UAX#29 grapheme boundaries). Everything set here stays in this directory, so
# none of it reaches the rest of the engine, as with FenrirWindow's SDL.

# The engine's directory-wide warning flags (-Weverything, /W4) stop here: these libraries' warnings are not ours to fix.
set_directory_properties(PROPERTIES COMPILE_OPTIONS "")

set(BUILD_SHARED_LIBS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)

# FreeType through its own CMake, with no optional library: no system zlib (its own inflate reads compressed fonts), no
# bzip2, PNG (colour bitmaps, out of scope), Brotli (WOFF2) or HarfBuzz (FreeType's auto-hinter can use HarfBuzz for
# its script coverage; the circular dependency is not worth it). Its install rules are skipped.
set(FT_DISABLE_ZLIB ON)
set(FT_DISABLE_BZIP2 ON)
set(FT_DISABLE_PNG ON)
set(FT_DISABLE_HARFBUZZ ON)
set(FT_DISABLE_BROTLI ON)
set(SKIP_INSTALL_ALL ON)
add_subdirectory(freetype EXCLUDE_FROM_ALL)

# HarfBuzz as its single amalgamated source, with the FreeType integration on (hb-ft), so it shapes from FreeType's
# faces directly. FreeType 2.15 has every function hb-ft looks for, so each is declared present.
add_library(FenrirHarfBuzz STATIC harfbuzz/src/harfbuzz.cc)
target_include_directories(FenrirHarfBuzz SYSTEM PUBLIC harfbuzz/src)
target_compile_definitions(FenrirHarfBuzz PUBLIC HAVE_FREETYPE
                           PRIVATE HAVE_FT_GET_TRANSFORM HAVE_FT_GET_VAR_BLEND_COORDINATES
                                   HAVE_FT_SET_VAR_BLEND_COORDINATES HAVE_FT_DONE_MM_VAR)
target_link_libraries(FenrirHarfBuzz PUBLIC freetype)
if(MSVC)
    # The amalgamation passes MSVC's default section limit.
    target_compile_options(FenrirHarfBuzz PRIVATE /bigobj /utf-8)
endif()

# SheenBidi as its unity source.
add_library(FenrirSheenBidi STATIC SheenBidi/Source/SheenBidi.c)
target_include_directories(FenrirSheenBidi SYSTEM PUBLIC SheenBidi/Headers PRIVATE SheenBidi/Source)
target_compile_definitions(FenrirSheenBidi PRIVATE SB_CONFIG_UNITY)

# libunibreak's library sources, as its Makefile.am lists them (the *data.c tables are included by them).
add_library(FenrirUnibreak STATIC
    libunibreak/src/unibreakbase.c
    libunibreak/src/unibreakdef.c
    libunibreak/src/linebreak.c
    libunibreak/src/linebreakdata.c
    libunibreak/src/linebreakdef.c
    libunibreak/src/eastasianwidthdef.c
    libunibreak/src/emojidef.c
    libunibreak/src/graphemebreak.c
    libunibreak/src/wordbreak.c
)
target_include_directories(FenrirUnibreak SYSTEM PUBLIC libunibreak/src)

# FenrirText links them PRIVATE, so no other target sees them; to FenrirText's own sources their headers are system
# headers.
set_target_properties(freetype FenrirHarfBuzz FenrirSheenBidi FenrirUnibreak PROPERTIES SYSTEM TRUE)

# Their sources warn under Clang and GCC (and Clang on Windows about the CRT functions they use); silence them there.
# MSVC builds them at its default level without warnings worth reading, and would warn about /w overriding /W3.
if(NOT MSVC)
    foreach(library freetype FenrirHarfBuzz FenrirSheenBidi FenrirUnibreak)
        target_compile_options(${library} PRIVATE -w)
    endforeach()
endif()

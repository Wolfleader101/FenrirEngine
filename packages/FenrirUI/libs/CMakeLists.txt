# Yoga 3.2.2, FenrirUI's flexbox layout (ADR-0064), a pinned submodule built static. Its own CMake turns warnings into
# errors, RTTI off and interprocedural optimisation on, so its sources are compiled here as a library of FenrirUI's
# instead (FenrirText builds HarfBuzz the same way). Everything set here stays in this directory, so none of it reaches
# the rest of the engine, as with FenrirWindow's SDL.

# The engine's directory-wide warning flags (-Weverything, /W4) stop here: Yoga's warnings are not ours to fix.
set_directory_properties(PROPERTIES COMPILE_OPTIONS "")

file(GLOB_RECURSE fenrir_yoga_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/yoga/yoga/*.cpp")
add_library(FenrirYoga STATIC ${fenrir_yoga_sources})
# FenrirUI links it PRIVATE and includes it from its layout bridge only, where its headers are system headers.
target_include_directories(FenrirYoga SYSTEM PUBLIC yoga)
set_target_properties(FenrirYoga PROPERTIES INTERPROCEDURAL_OPTIMIZATION OFF)
if(MSVC)
    target_compile_options(FenrirYoga PRIVATE /w)
else()
    target_compile_options(FenrirYoga PRIVATE -w)
endif()

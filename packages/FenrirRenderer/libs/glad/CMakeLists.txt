# FenrirGlad: the glad 2 OpenGL loader (GL 4.6 core, multiple contexts), generated once by hand and checked in; README.md
# holds the command. The build runs no Python. Only the OpenGL backend links it, PRIVATE, so its include path reaches no
# other target (ADR-0016). Everything set here stays in this directory (spec: "Build").

# The engine's directory-wide warning flags (-Weverything, /W4) stop here: generated code's warnings are not ours to fix.
set_directory_properties(PROPERTIES COMPILE_OPTIONS "")
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)

add_library(FenrirGlad STATIC include/glad/gl.h include/KHR/khrplatform.h src/gl.c)
target_include_directories(FenrirGlad PUBLIC include)
set_target_properties(FenrirGlad PROPERTIES SYSTEM TRUE)

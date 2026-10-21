# SPIKE (prototype/fenrir-renderer-spike, throwaway): FenrirWindow over SDL3 (ADR-0020, ADR-0022). Only src/ includes
# SDL, linked PRIVATE.
add_subdirectory(libs)

add_library(FenrirWindow STATIC include/FenrirWindow/Window.hpp src/Window.cpp)
target_link_libraries(FenrirWindow PUBLIC FenrirApp FenrirInput PRIVATE SDL3::SDL3-static)
target_include_directories(FenrirWindow PUBLIC include)

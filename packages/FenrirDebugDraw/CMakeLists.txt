# SPIKE (prototype/fenrir-renderer-spike, throwaway): FenrirDebugDraw, no GPU code (ADR-0025).
add_library(FenrirDebugDraw STATIC include/FenrirDebugDraw/DebugDraw.hpp src/DebugDraw.cpp)
target_link_libraries(FenrirDebugDraw PUBLIC FenrirMath PRIVATE FenrirApp)
target_include_directories(FenrirDebugDraw PUBLIC include)

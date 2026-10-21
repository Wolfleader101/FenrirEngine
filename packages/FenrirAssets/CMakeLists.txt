# SPIKE (prototype/fenrir-renderer-spike, throwaway): FenrirAssets, plain data only (ADR-0021).
add_library(FenrirAssets STATIC include/FenrirAssets/Assets.hpp src/Assets.cpp)
target_link_libraries(FenrirAssets PUBLIC FenrirMath PRIVATE FenrirApp)
target_include_directories(FenrirAssets PUBLIC include)

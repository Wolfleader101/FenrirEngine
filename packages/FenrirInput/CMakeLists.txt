# SPIKE (prototype/fenrir-renderer-spike, throwaway): FenrirInput, linking no library (ADR-0017).
add_library(FenrirInput STATIC include/FenrirInput/Input.hpp src/Input.cpp)
target_link_libraries(FenrirInput PUBLIC FenrirApp)
target_include_directories(FenrirInput PUBLIC include)

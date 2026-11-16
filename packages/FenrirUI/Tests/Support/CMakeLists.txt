# FenrirUI's test support, shared by FenrirUI's tests, the UI scripting tests and UIDemo's tests: the capturing test
# target (CaptureTarget.hpp), a UITarget that keeps every draw list a root submits to it. Header only, so an INTERFACE
# library; it needs FenrirUI alone, and no Catch2.
add_library(FenrirUITestSupport INTERFACE CaptureTarget.hpp)
target_include_directories(FenrirUITestSupport INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(FenrirUITestSupport INTERFACE FenrirUI)

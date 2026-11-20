# FenrirMath's own tests. No window, no .NET.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

add_executable(FenrirMathTests Colour.cpp Frustum.cpp)
target_link_libraries(FenrirMathTests PRIVATE FenrirMath Catch2::Catch2WithMain)
add_test(NAME FenrirMath COMMAND FenrirMathTests)

# Its registration against its description: each described type, default-constructed through its registered
# description, holds the defaults api/FenrirMath.api.json records (tools/generate-type-registry writes the test).
target_sources(FenrirMathTests PRIVATE Generated/FenrirMathTypesDefaultsTests.cpp)
target_link_libraries(FenrirMathTests PRIVATE FenrirTypesTesting)

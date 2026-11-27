# FenrirECS's own tests, through EntityList's public interface only. No .NET and no scripting.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

add_executable(FenrirECSTests PublicHeaders.cpp ComponentTypes.cpp ComponentTypeNames.cpp ComponentWatchHeader.cpp
                              ComponentWatches.cpp DynamicComponents.cpp Entities.cpp Queries.cpp SceneComponents.cpp TrackedComponentSets.cpp
                              WorldTransforms.cpp)
target_link_libraries(FenrirECSTests PRIVATE FenrirECS Catch2::Catch2WithMain)
add_test(NAME FenrirECS COMMAND FenrirECSTests --order decl)

# Its registration against its description: each described type, default-constructed through its registered
# description, holds the defaults api/FenrirECS.api.json records (tools/generate-type-registry writes the test).
target_sources(FenrirECSTests PRIVATE Generated/FenrirECSTypesDefaultsTests.cpp)
target_link_libraries(FenrirECSTests PRIVATE FenrirTypesTesting)

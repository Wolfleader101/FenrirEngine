# FenrirECS's own tests, through EntityList's public interface only. No .NET and no scripting.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

add_executable(FenrirECSTests ComponentTypes.cpp ComponentWatchHeader.cpp ComponentWatches.cpp DynamicComponents.cpp Queries.cpp)
target_link_libraries(FenrirECSTests PRIVATE FenrirECS Catch2::Catch2WithMain)
add_test(NAME FenrirECS COMMAND FenrirECSTests --order decl)

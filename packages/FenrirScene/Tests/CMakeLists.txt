# FenrirScene's own tests, through Scene's public interface only. No App, no .NET.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

add_executable(FenrirSceneTests PublicHeaders.cpp SceneResourceNames.cpp)
target_link_libraries(FenrirSceneTests PRIVATE FenrirScene Catch2::Catch2WithMain)
add_test(NAME FenrirScene COMMAND FenrirSceneTests --order decl)

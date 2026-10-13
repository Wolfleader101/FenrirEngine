# FenrirECS's own tests, through EntityList's public interface only. No .NET and no scripting.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../libs/catch2/CMakeLists.txt")
    message(FATAL_ERROR "FENRIR_BUILD_TESTS needs the Catch2 submodule: git submodule update --init packages/FenrirECS/libs/catch2")
endif()

set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)
set(CATCH_BUILD_TESTING OFF CACHE BOOL "" FORCE)
# Static, so the test executable needs no DLL beside it. Set in this directory only.
set(BUILD_SHARED_LIBS OFF)
add_subdirectory(../libs/catch2 catch2)

add_executable(FenrirECSTests DynamicComponents.cpp Queries.cpp)
target_link_libraries(FenrirECSTests PRIVATE FenrirECS Catch2::Catch2WithMain)
add_test(NAME FenrirECS COMMAND FenrirECSTests)

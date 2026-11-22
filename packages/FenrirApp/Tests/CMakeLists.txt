# FenrirApp's own tests: the App driven one frame at a time with a chosen delta. No window, no .NET.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

add_executable(FenrirAppTests AppResources.cpp EventReaders.cpp Frames.cpp Phases.cpp RecordingLogger.hpp
                              SceneResources.cpp Scenes.cpp TypeRegistry.cpp)
target_link_libraries(FenrirAppTests PRIVATE FenrirApp Catch2::Catch2WithMain)
add_test(NAME FenrirApp COMMAND FenrirAppTests --order decl)

# The App's type registry once packages are installed: its own executable, since it links the packages above FenrirApp
# whose Installs register their types (assets, skeletons, animation, physics, and the renderer when it is built).
if(FENRIR_ENABLE_ASSETS AND FENRIR_ENABLE_PHYSICS)
    add_executable(FenrirAppInstalledTypesTests InstalledTypes.cpp RecordingLogger.hpp)
    target_link_libraries(FenrirAppInstalledTypesTests PRIVATE FenrirApp FenrirAssets FenrirSkeleton FenrirAnimation
                                                               FenrirPhysics Catch2::Catch2WithMain)
    if(FENRIR_ENABLE_RENDERER)
        target_link_libraries(FenrirAppInstalledTypesTests PRIVATE FenrirRenderer)
        target_compile_definitions(FenrirAppInstalledTypesTests PRIVATE FENRIR_APP_TESTS_RENDERER=1)
    else()
        target_compile_definitions(FenrirAppInstalledTypesTests PRIVATE FENRIR_APP_TESTS_RENDERER=0)
    endif()
    add_test(NAME FenrirAppInstalledTypes COMMAND FenrirAppInstalledTypesTests)
endif()

# Its registration against its description: each described type, default-constructed through its registered
# description, holds the defaults api/FenrirApp.api.json records (tools/generate-type-registry writes the test).
target_sources(FenrirAppTests PRIVATE Generated/FenrirAppTypesDefaultsTests.cpp)
target_link_libraries(FenrirAppTests PRIVATE FenrirTypesTesting)

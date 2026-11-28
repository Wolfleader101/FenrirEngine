# FenrirAssets's tests: its public interface, headless (the spec's seam 4). The image and model fixtures are small files
# in fixtures/, read where they are; fixtures/models/make_fixtures.py wrote the models.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

add_executable(FenrirAssetsTests PublicHeaders.cpp Owners.cpp Revisions.cpp Loading.cpp Images.cpp ShaderContainers.cpp
                                 Materials.cpp Models.cpp SpawnNodes.cpp Cubemaps.cpp Skeletons.cpp Skins.cpp
                                 Clips.cpp BoneMasks.cpp SkinnedImport.cpp MeshLods.cpp Fonts.cpp ImageUpdates.cpp
                                 DataAssets.cpp AssetPaths.cpp SubAssets.cpp References.cpp Saving.cpp Move.cpp HotReload.cpp ScenePatch.cpp Scenes.cpp Instances.cpp SceneInstances.cpp AssetsTest.hpp
                                 DataAssetsTest.hpp
                                 AssetsProbe.hpp
                                 CountingSource.hpp)
# Models built in code, which FenrirSkeleton's spawning tests also build on.
target_sources(FenrirAssetsTests PRIVATE ProceduralModels.cpp)
target_link_libraries(FenrirAssetsTests PRIVATE FenrirAssets Catch2::Catch2WithMain)
# The asset source seam is private (src/AssetSource.hpp): its tests stand a source in memory in for the directory.
target_include_directories(FenrirAssetsTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_compile_definitions(FenrirAssetsTests PRIVATE FENRIR_ASSETS_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
# The format research's sword, FenrirSerialisation's golden file: the saving tests load it and save it back, read where
# it is committed rather than copied, so there is one golden.
target_compile_definitions(
    FenrirAssetsTests
    PRIVATE FENRIR_SERIALISATION_GOLDEN_SWORD="${CMAKE_CURRENT_SOURCE_DIR}/../../FenrirSerialisation/Tests/fixtures/sword.asset.json5")
add_test(NAME FenrirAssets COMMAND FenrirAssetsTests)

# With the renderer on, FenrirShaderCompiler builds a fixture shader from GLSL for the parameter-table test.
if(FENRIR_ENABLE_RENDERER)
    target_sources(FenrirAssetsTests PRIVATE ShaderParameters.cpp)
    fenrir_add_shaders(FenrirAssetsTests PROGRAM Parameters VERTEX fixtures/shaders/parameters.vert
                       FRAGMENT fixtures/shaders/parameters.frag EMBED)
endif()

# Its registration against its description: each described type, default-constructed through its registered
# description, holds the defaults api/FenrirAssets.api.json records (tools/generate-type-registry writes the test).
target_sources(FenrirAssetsTests PRIVATE Generated/FenrirAssetsTypesDefaultsTests.cpp)
target_link_libraries(FenrirAssetsTests PRIVATE FenrirTypesTesting)

# The data asset test package's registration (Package/), which both real tools wrote, compiled against its header; the
# data asset tests register it into the app's type registry. Its .asset.json5 fixtures are in fixtures/data/.
target_sources(FenrirAssetsTests PRIVATE Package/Generated/DataAssetsTestPackageTypes.hpp
                                         Package/Generated/DataAssetsTestPackageTypes.cpp)
target_link_libraries(FenrirAssetsTests PRIVATE FenrirAssetsDataTestPackage)

# FenrirAssets: plain CPU-side assets behind generational handles, owned by the app or a scene (ADR-0021). Its
# importers sit behind a private seam: only src/Importers/ includes a third-party library, and each is linked PRIVATE,
# so neither its headers nor its compile definitions reach FenrirAssets's users.
# Data assets (ADR-0075, ADR-0079) are read through FenrirSerialisation, whose Diagnostic LoadResult carries, so it
# links FenrirSerialisation PUBLIC too.
# It registers its described types with FenrirTypes' type registry (ADR-0074), through the generated registration
# it compiles, so it links FenrirTypes PUBLIC: the generated header, public, gives a game Describe<T>.
add_subdirectory(libs)
# The shader container format, its reader and writer: a target of its own with no dependencies, so the renderer core
# and FenrirShaderCompiler use it without the store.
add_subdirectory(ShaderContainer)

add_library(FenrirAssets STATIC
    include/FenrirAssets/AssetHandle.hpp
    include/FenrirAssets/Assets.hpp
    include/FenrirAssets/AssetStore.hpp
    include/FenrirAssets/BoneMaskData.hpp
    include/FenrirAssets/ClipData.hpp
    include/FenrirAssets/FontData.hpp
    include/FenrirAssets/ImageData.hpp
    include/FenrirAssets/InstanceListData.hpp
    include/FenrirAssets/MaterialData.hpp
    include/FenrirAssets/MeshData.hpp
    include/FenrirAssets/Model.hpp
    include/FenrirAssets/ModelSpawner.hpp
    include/FenrirAssets/Move.hpp
    include/FenrirAssets/ShaderData.hpp
    include/FenrirAssets/SkeletonData.hpp
    include/FenrirAssets/SkinningSource.hpp
    include/FenrirAssets/SpawnNodes.hpp

    src/Assets.cpp
    src/AssetStore.cpp
    src/AssetStoreState.hpp
    src/AssetSource.hpp
    src/AssetWatcher.hpp
    src/AssetWatcher.cpp
    src/DirectorySource.cpp
    src/Move.cpp
    src/Envelope.hpp
    src/Envelope.cpp
    src/AssetData.cpp
    src/AnimationData.cpp
    src/AnimationEdits.cpp
    src/SkinPreparation.hpp
    src/SkinPreparation.cpp
    src/CubemapLoading.hpp
    src/CubemapLoading.cpp
    src/ImageEdits.cpp
    src/Materials.cpp
    src/ModelLoading.cpp
    src/ModelData.cpp
    src/MeshLods.cpp
    src/SpawnNodes.cpp
    src/SceneInstances.cpp
    src/SceneReader.cpp
    src/SceneWriter.cpp

    src/Importers/ImageImporter.hpp
    src/Importers/Stb/StbImageImporter.cpp
    src/Importers/ModelImporter.hpp
    src/Importers/ModelImporter.cpp
    src/Importers/SkinnedImport.cpp
    src/Importers/Cgltf/CgltfModelImporter.cpp
    src/Importers/Ufbx/UfbxModelImporter.cpp
)

target_link_libraries(FenrirAssets PUBLIC FenrirApp FenrirScene FenrirMath FenrirShaderContainer PRIVATE FenrirCore FenrirStbImage FenrirCgltf FenrirUfbx meshoptimizer)

target_include_directories(FenrirAssets PUBLIC include PRIVATE src)

# The package's API description, api/FenrirAssets.api.json (ADR-0042): its artefact whether or not scripting is built.
fenrir_describe_api(FenrirAssets api/FenrirAssets.api.toml)
# Its type registration (ADR-0074), from that description: `cmake --build <dir> --target
# fenrir-generate-type-registry` regenerates include/FenrirAssets/Generated/ and src/Generated/.
fenrir_generate_type_registry(FenrirAssets api/FenrirAssets.api.toml)
target_sources(FenrirAssets PRIVATE include/FenrirAssets/Generated/FenrirAssetsTypes.hpp
                                    src/Generated/FenrirAssetsTypes.cpp)
target_link_libraries(FenrirAssets PUBLIC FenrirTypes FenrirSerialisation)

# The data asset test package (Tests/Package/): its description and registration regenerate with every package's, so it
# is added whether or not the tests build.
add_subdirectory(Tests/Package)

# Tests that use Catch2: only with -DFENRIR_BUILD_TESTS=ON (see the root CMakeLists.txt).
if(FENRIR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

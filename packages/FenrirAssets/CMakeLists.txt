# FenrirAssets: plain CPU-side assets behind generational handles, owned by the app or a scene (ADR-0021). Its
# importers sit behind a private seam: only src/Importers/ includes a third-party library, and each is linked PRIVATE,
# so neither its headers nor its compile definitions reach FenrirAssets's users.
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
    include/FenrirAssets/ImageData.hpp
    include/FenrirAssets/MaterialData.hpp
    include/FenrirAssets/MeshData.hpp
    include/FenrirAssets/Model.hpp
    include/FenrirAssets/ShaderData.hpp
    include/FenrirAssets/SkeletonData.hpp
    include/FenrirAssets/SkinningSource.hpp
    include/FenrirAssets/SpawnNodes.hpp

    src/Assets.cpp
    src/AssetStore.cpp
    src/AssetStoreState.hpp
    src/AssetData.cpp
    src/AnimationData.cpp
    src/AnimationEdits.cpp
    src/SkinPreparation.hpp
    src/SkinPreparation.cpp
    src/CubemapLoading.hpp
    src/CubemapLoading.cpp
    src/Materials.cpp
    src/ModelLoading.cpp
    src/SpawnNodes.cpp

    src/Importers/ImageImporter.hpp
    src/Importers/Stb/StbImageImporter.cpp
    src/Importers/ModelImporter.hpp
    src/Importers/ModelImporter.cpp
    src/Importers/Cgltf/CgltfModelImporter.cpp
    src/Importers/Ufbx/UfbxModelImporter.cpp
)

target_link_libraries(FenrirAssets PUBLIC FenrirApp FenrirScene FenrirMath FenrirShaderContainer PRIVATE FenrirStbImage FenrirCgltf FenrirUfbx)

target_include_directories(FenrirAssets PUBLIC include PRIVATE src)

# The package's API description, api/FenrirAssets.api.json (ADR-0042): its artefact whether or not scripting is built.
fenrir_describe_api(FenrirAssets api/FenrirAssets.api.toml)

# Tests that use Catch2: only with -DFENRIR_BUILD_TESTS=ON (see the root CMakeLists.txt).
if(FENRIR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

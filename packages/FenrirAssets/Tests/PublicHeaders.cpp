// FenrirAssets hides its importers at compile time (ADR-0021, ADR-0031). This file includes the public headers before
// anything else, so if one of them ever reaches for stb, cgltf or ufbx, or their include paths or definitions reach a
// user of FenrirAssets (this test target is one), the build fails here. The same goes for glslang and SPIRV-Reflect,
// which only FenrirShaderCompiler links (ADR-0018), though FenrirAssets reads the shader containers it writes.
#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirAssets/ImageData.hpp"
#include "FenrirAssets/MaterialData.hpp"
#include "FenrirAssets/MeshData.hpp"
#include "FenrirAssets/Model.hpp"
#include "FenrirAssets/ShaderContainer.hpp"
#include "FenrirAssets/ShaderData.hpp"
#include "FenrirAssets/ShaderGlBindings.hpp"
#include "FenrirAssets/SpawnNodes.hpp"

#if defined(STBI_VERSION) || defined(STBI_INCLUDE_STB_IMAGE_H)
#error "A FenrirAssets public header includes stb_image."
#endif

#if __has_include(<stb_image.h>) || __has_include("stb_image.h")
#error "stb's include path reaches a target outside FenrirAssets: it must be linked PRIVATE."
#endif

#if defined(STBI_ONLY_PNG) || defined(STBI_ONLY_JPEG) || defined(STBI_ONLY_HDR) || defined(STBI_NO_STDIO)
#error "stb_image's compile definitions reach a target outside FenrirAssets: it must be linked PRIVATE."
#endif

#if defined(CGLTF_H_INCLUDED__) || defined(UFBX_UFBX_H_INCLUDED)
#error "A FenrirAssets public header includes cgltf or ufbx."
#endif

#if __has_include(<cgltf.h>) || __has_include("cgltf.h") || __has_include(<ufbx.h>) || __has_include("ufbx.h")
#error "cgltf's or ufbx's include path reaches a target outside FenrirAssets: they must be linked PRIVATE."
#endif

#if __has_include(<glslang/Public/ShaderLang.h>) || __has_include(<spirv_reflect.h>)
#error "FenrirShaderCompiler's libraries reach FenrirAssets's users: only the build-time tool may link them."
#endif

#include <type_traits>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("The public headers compile without the importers", "[assets][seam]")
{
    const Fenrir::AssetHandle<Fenrir::Image> image;

    INFO("a handle made with no arguments is empty");
    REQUIRE(image.IsEmpty());
}

TEST_CASE("Asset handles are plain data that can be copied byte for byte", "[assets][handles]")
{
    INFO("every kind of handle, reserved ones too, is trivially copyable");
    REQUIRE(std::is_trivially_copyable_v<Fenrir::AssetHandle<Fenrir::Mesh>>);
    REQUIRE(std::is_trivially_copyable_v<Fenrir::AssetHandle<Fenrir::Image>>);
    REQUIRE(std::is_trivially_copyable_v<Fenrir::AssetHandle<Fenrir::Material>>);
    REQUIRE(std::is_trivially_copyable_v<Fenrir::AssetHandle<Fenrir::Shader>>);
    REQUIRE(std::is_trivially_copyable_v<Fenrir::AssetHandle<Fenrir::Model>>);
    REQUIRE(std::is_trivially_copyable_v<Fenrir::AssetHandle<Fenrir::Skeleton>>);
    REQUIRE(std::is_trivially_copyable_v<Fenrir::AssetHandle<Fenrir::Clip>>);

    INFO("a handle is an index and a generation, nothing more");
    REQUIRE(sizeof(Fenrir::AssetHandle<Fenrir::Mesh>) == 8);

    INFO("each short name resolves to its data type");
    REQUIRE(std::is_same_v<Fenrir::AssetData<Fenrir::Mesh>, Fenrir::MeshData>);
    REQUIRE(std::is_same_v<Fenrir::AssetData<Fenrir::Image>, Fenrir::ImageData>);
    REQUIRE(std::is_same_v<Fenrir::AssetData<Fenrir::Material>, Fenrir::MaterialData>);
    REQUIRE(std::is_same_v<Fenrir::AssetData<Fenrir::Shader>, Fenrir::ShaderData>);
    REQUIRE(std::is_same_v<Fenrir::AssetData<Fenrir::Model>, Fenrir::Model>);
}

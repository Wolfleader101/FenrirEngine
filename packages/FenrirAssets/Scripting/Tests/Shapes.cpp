// The C# mirrors' shapes (spec seam 7: each extension's layouts): AssetHandle<T> for every asset kind, and the values
// the native functions read and write through pointers (SamplerDescription, MaterialTexture, ShaderParameter), are laid
// out as C++'s, checked field by field, offsets and nested enums included, by the mappings another package's extension
// uses for its components (AssetMappings.hpp).

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "AssetsScriptingApp.hpp"
#include "FenrirAssetsScripting/AssetMappings.hpp"

using namespace Fenrir;
using namespace AssetsScriptingTests;

namespace
{
    template <typename T>
    void RequireSameLayout(AssetsScriptingApp& test, const char* name)
    {
        const auto result = ScriptingProbe::Component(test.scripting)->ValidateStruct<T>();
        INFO("C#'s " << name << " is not laid out as C++'s: " << result.diagnostic);
        REQUIRE(result.Succeeded());
    }
} // namespace

TEST_CASE("AssetHandle<T> has the C++ layout for every asset kind", "[assets][scripting]")
{
    AssetsScriptingApp test;
    RequireSameLayout<AssetHandle<Mesh>>(test, "AssetHandle<Mesh>");
    RequireSameLayout<AssetHandle<Image>>(test, "AssetHandle<Image>");
    RequireSameLayout<AssetHandle<Material>>(test, "AssetHandle<Material>");
    RequireSameLayout<AssetHandle<Shader>>(test, "AssetHandle<Shader>");
    RequireSameLayout<AssetHandle<Model>>(test, "AssetHandle<Model>");
}

TEST_CASE("The sampler, texture value and shader parameter C# passes have the C++ layout", "[assets][scripting]")
{
    AssetsScriptingApp test;
    RequireSameLayout<SamplerDescription>(test, "SamplerDescription");
    RequireSameLayout<MaterialTexture>(test, "MaterialTexture");
    RequireSameLayout<ScriptShaderParameter>(test, "ShaderParameter");
}

// The C# handles' shape (spec seam 7: each extension's layouts): AssetHandle<T> for every asset kind is laid out as
// C++'s, checked field by field by the hand-written mappings another package's binding uses for its components
// (AssetMappings.hpp). The generated mirrors (SamplerDescription, MaterialTexture, ShaderParameter) are checked by the
// generated defaults test (Generated/FenrirAssetsDefaultsTests.cpp) and whenever the extension registers them.

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

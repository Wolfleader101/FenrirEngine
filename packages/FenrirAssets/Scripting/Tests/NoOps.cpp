// The no-op rules (spec: "Scripting: the extensions"): with assets' extension but no asset store, scripts still load
// and every Fenrir.Assets function does nothing and returns false, zero or the empty handle, with out values zeroed;
// a stale handle does the same. Nothing throws, and a debug build warns once per native call site.
//
// A native call site warns once per Scripting given the extension (its state keeps the record), so each case counts
// its own warnings. They still run in a process of their own (CTest's FenrirAssetsScripting-NoOps), in declaration
// order (--order decl), as they did when the record was per process.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>

#include "AssetsScriptingApp.hpp"

using namespace Fenrir;
using namespace AssetsScriptingTests;

namespace
{
    // What NoOpCalls.EveryHandleCall reads when every call does nothing.
    constexpr const char* NothingDone = "IsValid=False\n"
                                        "Unload=False\n"
                                        "ReplaceMesh=False\n"
                                        "ResizeRenderTarget=False\n"
                                        "ReplaceImage=False\n"
                                        "Create=0\n"
                                        "Clone=0\n"
                                        "SetFloat=False\n"
                                        "SetVector2=False\n"
                                        "SetVector3=False\n"
                                        "SetVector4=False\n"
                                        "SetColour=False\n"
                                        "SetTexture=False\n"
                                        "SetBlendMode=False\n"
                                        "SetAlphaCutoff=False\n"
                                        "SetDoubleSided=False\n"
                                        "TryGetFloat=False:0\n"
                                        "TryGetVector2=False:True\n"
                                        "TryGetVector3=False:True\n"
                                        "TryGetVector4=False:True\n"
                                        "TryGetColour=False:True\n"
                                        "TryGetTexture=False:True\n"
                                        "TryGetOverrideFloat=False:0\n"
                                        "TryGetOverrideVector2=False:True\n"
                                        "TryGetOverrideVector3=False:True\n"
                                        "TryGetOverrideVector4=False:True\n"
                                        "TryGetOverrideColour=False:True\n"
                                        "TryGetOverrideTexture=False:True\n"
                                        "IsSet=False\n"
                                        "Reset=False\n"
                                        "GetParameterCount=0\n"
                                        "GetParameter=0:True";

    // Every native function that takes a handle, as its warnings name it.
    constexpr const char* HandleFunctions[] = {
        "Assets.Unload",
        "Assets.ReplaceMesh",
        "Assets.ResizeRenderTarget",
        "Assets.ReplaceImage",
        "Materials.Create",
        "Materials.Clone",
        "Materials.SetFloat",
        "Materials.SetVector2",
        "Materials.SetVector3",
        "Materials.SetVector4",
        "Materials.SetColour",
        "Materials.SetTexture",
        "Materials.SetBlendMode",
        "Materials.SetAlphaCutoff",
        "Materials.SetDoubleSided",
        "Materials.TryGetFloat",
        "Materials.TryGetVector2",
        "Materials.TryGetVector3",
        "Materials.TryGetVector4",
        "Materials.TryGetColour",
        "Materials.TryGetTexture",
        "Materials.IsSet",
        "Materials.TryGetOverrideFloat",
        "Materials.TryGetOverrideVector2",
        "Materials.TryGetOverrideVector3",
        "Materials.TryGetOverrideVector4",
        "Materials.TryGetOverrideColour",
        "Materials.TryGetOverrideTexture",
        "Materials.Reset",
        "Shaders.GetParameterCount",
        "Shaders.GetParameter",
    };
} // namespace

TEST_CASE("With no asset store, every C# asset function does nothing and warns once per native call site",
          "[assets][scripting][noops]")
{
    // Scripting with assets' extension, but no Assets::Install: scripts still load.
    Testing::HeadlessApp headless;
    Scripting scripting = Scripting::Install(headless.Get(), FixtureConfig());
    {
        INFO("The fixture did not load with assets' extension and no asset store");
        REQUIRE(scripting.HasScripts());
    }

    // Handles that would name the first asset of each kind.
    const std::uint64_t first = (std::uint64_t{1} << 32u);
    const std::string expected = std::string("Load=0\nAddMesh=0\nAddRenderTarget=0\nAddImage=0\n") + NothingDone;
    for (int call = 0; call < 2; ++call)
    {
        const std::string results = Text(scripting, NoOpCallsType, "EveryCall", first, first, first, first);
        INFO("Call " << call << ": a function did something, or returned other than false, zero or the empty handle:\n"
                     << results);
        REQUIRE(results == expected);
    }

    for (const char* function :
         {"Assets.Load", "Assets.IsValid", "Assets.AddMesh", "Assets.AddRenderTarget", "Assets.AddImage"})
    {
        INFO(function << " did not warn once in a debug build (never in a release build), though called twice");
        REQUIRE(LoggedWith(headless, Testing::LogLevel::Warn, std::string(function) + ": the app has no asset store") ==
                warningsPerCallSite);
    }
    for (const char* function : HandleFunctions)
    {
        INFO(function << " did not warn once in a debug build (never in a release build), though called twice");
        REQUIRE(LoggedWith(headless, Testing::LogLevel::Warn, std::string(function) + ": the app has no asset store") ==
                warningsPerCallSite);
    }
}

TEST_CASE("A stale handle makes every C# asset function do nothing, warning once per native call site",
          "[assets][scripting][noops]")
{
    AssetsScriptingApp test;
    AssetStore& store = test.Store();
    // One asset of each kind, unloaded, so each handle is stale.
    const auto mesh = HandleFrom<Mesh>(test.Call<std::uint64_t>(AssetCallsType, "AddTriangle", appOwner));
    const auto image = HandleFrom<Image>(test.Call<std::uint64_t>(ImageCallsType, "AddRenderTarget", 4, 4));
    const AssetHandle<Shader> shader = store.Add<Shader>(FixtureShader(), AssetOwner::App());
    const AssetHandle<Material> material = store.CreateMaterial(shader, AssetOwner::App());
    REQUIRE(store.Unload(mesh));
    REQUIRE(store.Unload(image));
    REQUIRE(store.Unload(material));
    REQUIRE(store.Unload(shader));

    for (int call = 0; call < 2; ++call)
    {
        const std::string results =
            test.Text(NoOpCallsType, "EveryHandleCall", BitsOf(mesh), BitsOf(image), BitsOf(material), BitsOf(shader));
        INFO("Call " << call
                     << ": a function did something with a stale handle, or returned other than false, zero "
                        "or the empty handle:\n"
                     << results);
        REQUIRE(results == NothingDone);
    }
    for (const char* function : HandleFunctions)
    {
        INFO(function << " did not warn once in a debug build (never in a release build), though called twice");
        REQUIRE(LoggedWith(test.headless, Testing::LogLevel::Warn, std::string(function) + ": the ") ==
                warningsPerCallSite);
    }
    INFO("Nothing was made from the stale handles");
    REQUIRE_FALSE(store.IsValid(mesh));
    REQUIRE_FALSE(store.IsValid(image));
}

TEST_CASE("The empty handle makes C# asset functions do nothing", "[assets][scripting][noops]")
{
    AssetsScriptingApp test;
    INFO("A function did something with the empty handle");
    REQUIRE(test.Text(NoOpCallsType, "EveryHandleCall", std::uint64_t{0}, std::uint64_t{0}, std::uint64_t{0},
                      std::uint64_t{0}) == NothingDone);
}

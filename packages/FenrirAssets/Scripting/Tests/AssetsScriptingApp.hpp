#pragma once

// The assets scripting tests' part of FenrirScripting's ExtensionTestApp (spec seam 7): the fixture CTest names in
// FENRIR_TEST_ASSETS_FIXTURE, assets installed with its asset root FenrirAssets's test fixtures and the fixture shader
// registered as a built-in, and assets' extension. C++ calls the fixture's static methods through Iris and compares
// what C# did with what the store holds and what the same calls do from C++.

#include "ExtensionTestApp.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "FenrirAssets/Assets.hpp"
#include "FenrirAssets/ShaderContainer.hpp"
#include "FenrirAssetsScripting/AssetsScripting.hpp"

namespace Fenrir::Testing
{
    // The fixture's types the tests call.
    inline constexpr const char* MeshBuilderType = "AssetsScriptFixture.MeshBuilder";
    inline constexpr const char* AssetCallsType = "AssetsScriptFixture.AssetCalls";
    inline constexpr const char* ImageCallsType = "AssetsScriptFixture.ImageCalls";
    inline constexpr const char* MaterialCallsType = "AssetsScriptFixture.MaterialCalls";
    inline constexpr const char* ShaderCallsType = "AssetsScriptFixture.ShaderCalls";

    // C#'s AssetOwner values.
    inline constexpr int sceneOwner = 0;
    inline constexpr int appOwner = 1;

    // The built-in name the fixture shader is registered under, so C# loads it as a game loads `builtin:lit`.
    inline constexpr const char* FixtureShaderName = "builtin:scripting-fixture";

    // How the tests carry a handle across Iris's scalar-only invoke (FindMethod passes numbers and pointers, not
    // structs): as two 32-bit halves in one std::uint64_t, the generation high and the index low, which the fixture's
    // TestHandle (Fixture/Meshes.cs) packs and unpacks the same way. The harness's own convention; not how a native
    // takes one (that is AssetHandle<T> itself, through a pointer).
    template <typename T>
    std::uint64_t BitsOf(AssetHandle<T> handle)
    {
        return (std::uint64_t{handle.generation} << 32u) | handle.index;
    }

    template <typename T>
    AssetHandle<T> HandleFrom(std::uint64_t bits)
    {
        return {static_cast<std::uint32_t>(bits), static_cast<std::uint32_t>(bits >> 32u)};
    }

    // Calls a fixture method that returns text; "<null>" if it returned null.
    template <typename... Args>
    std::string Text(Scripting& scripting, const char* typeName, const char* method, Args... arguments)
    {
        return Call<std::optional<std::string>>(scripting, typeName, method, arguments...).value_or("<null>");
    }

    // The fixture shader's parameters: tint (vec4, a colour), roughness (float), offset (vec2), emissive (vec3, no
    // pragma so zero) and baseColourTexture (default normal), as FenrirShaderCompiler would reflect `#pragma fenrir
    // default` lines for them (FenrirAssets's own material tests use the same).
    inline std::vector<ShaderParameter> FixtureParameters()
    {
        using Type = ShaderParameterType;
        return {
            {"tint", Type::Vector4, {1.0f, 0.5f, 0.25f, 1.0f}},
            {"roughness", Type::Float, {0.5f}},
            {"offset", Type::Vector2, {2.0f, 3.0f}},
            {"emissive", Type::Vector3, {}},
            {"baseColourTexture", Type::Texture, {}, DefaultTexture::Normal},
        };
    }

    // A shader container written by hand: one variant with the fixture's parameters.
    inline ShaderData FixtureShader()
    {
        ShaderContainer container;
        container.name = "ScriptingFixture";
        ShaderVariant variant;
        variant.stages.push_back({ShaderStage::Vertex, {0x07230203u}, {0x07230203u}});
        variant.stages.push_back({ShaderStage::Fragment, {0x07230203u}, {0x07230203u}});
        variant.parameters = FixtureParameters();
        container.variants.push_back(std::move(variant));
        ShaderData data;
        data.bytes = WriteShaderContainer(container);
        return data;
    }

    // Assets with its asset root FenrirAssets's test fixtures.
    struct AssetsPackages
    {
        explicit AssetsPackages(App& app)
            : assets(Assets::Install(app, {std::filesystem::absolute(FENRIR_ASSETS_FIXTURES)}))
        {
        }

        Assets assets;
    };

    // An App with assets installed, the fixture shader registered as a built-in, and scripting running the fixture
    // with assets' extension.
    struct AssetsScriptingApp : ExtensionTestApp<AssetsPackages>
    {
        AssetsScriptingApp()
            : ExtensionTestApp(FixtureFromEnvironment("FENRIR_TEST_ASSETS_FIXTURE"), {Assets::ScriptingExtension()})
        {
            shader = Store().AddBuiltin<Shader>(FixtureShaderName, FixtureShader());
            INFO("The fixture shader was refused");
            REQUIRE(Store().IsValid(shader));
        }

        AssetHandle<Shader> shader;

        AssetStore& Store()
        {
            return assets.Store();
        }

        template <typename... Args>
        std::string Text(const char* typeName, const char* method, Args... arguments)
        {
            return Testing::Text(scripting, typeName, method, arguments...);
        }
    };
} // namespace Fenrir::Testing

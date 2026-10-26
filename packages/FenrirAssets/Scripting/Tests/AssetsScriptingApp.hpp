#pragma once

// The assets scripting tests' harness (spec seam 7): an App with assets installed, its asset root FenrirAssets's test
// fixtures, stepped one frame at a time, with scripting running the assets fixture script assembly, which uses the
// Fenrir.Assets contract. C++ calls the fixture's static methods through Iris, as the system runner calls scripts, and
// compares what C# did with what the store holds and what the same calls do from C++.

#include "HeadlessApp.hpp"
#include "ScriptingProbe.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "FenrirAssets/Assets.hpp"
#include "FenrirAssets/ShaderContainer.hpp"
#include "FenrirAssetsScripting/AssetsScripting.hpp"
#include "FenrirScripting/Scripting.hpp"

namespace AssetsScriptingTests
{
    inline std::filesystem::path FromEnvironment(const char* name)
    {
        const char* value = std::getenv(name);
        if (value == nullptr)
        {
            std::cerr << "Set " << name << " (CTest does): the assets scripting tests need the fixture assembly.\n";
            return {};
        }
        return std::filesystem::absolute(value);
    }

    // Read when scripting is first installed, not at load: the no-op rules (NoOps.cpp) run with no fixture set.
    inline const std::filesystem::path& Fixture()
    {
        static const std::filesystem::path fixture = FromEnvironment("FENRIR_TEST_ASSETS_FIXTURE");
        return fixture;
    }

    inline constexpr double oneTick = Fenrir::Testing::HeadlessApp::FrameDelta;

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

    // A handle as it crosses to the fixture: the generation in the high half, the index in the low.
    template <typename T>
    std::uint64_t BitsOf(Fenrir::AssetHandle<T> handle)
    {
        return (std::uint64_t{handle.generation} << 32u) | handle.index;
    }

    template <typename T>
    Fenrir::AssetHandle<T> HandleFrom(std::uint64_t bits)
    {
        return {static_cast<std::uint32_t>(bits), static_cast<std::uint32_t>(bits >> 32u)};
    }

    // Calls a static method of a type in the loaded fixture through Iris.
    template <typename Result, typename... Args>
    Result Call(Fenrir::Scripting& scripting, const char* typeName, const char* method, Args... arguments)
    {
        auto* component = Fenrir::ScriptingProbe::Component(scripting);
        {
            INFO("There is no loaded script assembly");
            REQUIRE(component != nullptr);
        }
        const auto type = component->FindType(typeName);
        {
            INFO(std::string("The fixture type was not found: ") + type.diagnostic);
            REQUIRE(type.Succeeded());
        }
        const auto handle = type.value->FindMethod<Result(Args...)>(method);
        {
            INFO(std::string("The fixture method ") + method + " was not found: " + handle.diagnostic);
            REQUIRE(handle.Succeeded());
        }
        const auto result = handle.value->Invoke(arguments...);
        {
            INFO(std::string("The fixture method ") + method + " failed: " + result.diagnostic);
            REQUIRE(result.Succeeded());
        }
        if constexpr (!std::is_void_v<Result>)
        {
            return *result.value;
        }
    }

    // Calls a fixture method that returns text; "<null>" if it returned null.
    template <typename... Args>
    std::string Text(Fenrir::Scripting& scripting, const char* typeName, const char* method, Args... arguments)
    {
        return Call<std::optional<std::string>>(scripting, typeName, method, arguments...).value_or("<null>");
    }

    // Scripting running the fixture, with assets' extension.
    inline Fenrir::ScriptingConfig FixtureConfig()
    {
        Fenrir::ScriptingConfig config;
        config.scriptAssembly = Fixture();
        config.hotReload = false;
        config.extensions = {Fenrir::Assets::ScriptingExtension()};
        return config;
    }

    // The fixture shader's parameters: tint (vec4, a colour), roughness (float), offset (vec2), emissive (vec3, no
    // pragma so zero) and baseColourTexture (default normal), as FenrirShaderCompiler would reflect `#pragma fenrir
    // default` lines for them (FenrirAssets's own material tests use the same).
    inline std::vector<Fenrir::ShaderParameter> FixtureParameters()
    {
        using Type = Fenrir::ShaderParameterType;
        return {
            {"tint", Type::Vector4, {1.0f, 0.5f, 0.25f, 1.0f}},
            {"roughness", Type::Float, {0.5f}},
            {"offset", Type::Vector2, {2.0f, 3.0f}},
            {"emissive", Type::Vector3, {}},
            {"baseColourTexture", Type::Texture, {}, Fenrir::DefaultTexture::Normal},
        };
    }

    // A shader container written by hand: one variant with the fixture's parameters.
    inline Fenrir::ShaderData FixtureShader()
    {
        Fenrir::ShaderContainer container;
        container.name = "ScriptingFixture";
        Fenrir::ShaderVariant variant;
        variant.stages.push_back({Fenrir::ShaderStage::Vertex, {0x07230203u}, {0x07230203u}});
        variant.stages.push_back({Fenrir::ShaderStage::Fragment, {0x07230203u}, {0x07230203u}});
        variant.parameters = FixtureParameters();
        container.variants.push_back(std::move(variant));
        Fenrir::ShaderData data;
        data.bytes = Fenrir::WriteShaderContainer(container);
        return data;
    }

    // An App with assets installed, its asset root FenrirAssets's test fixtures and the fixture shader registered as a
    // built-in, and scripting running the fixture with assets' extension. Members end in reverse order: scripting, then
    // the asset store's owner, then the app.
    struct AssetsScriptingApp
    {
        AssetsScriptingApp()
            : assets(Fenrir::Assets::Install(headless.Get(), {std::filesystem::absolute(FENRIR_ASSETS_FIXTURES)})),
              scripting(Fenrir::Scripting::Install(headless.Get(), FixtureConfig()))
        {
            shader = Store().AddBuiltin<Fenrir::Shader>(FixtureShaderName, FixtureShader());
            INFO("The fixture did not load with assets' extension, or the fixture shader was refused");
            REQUIRE(scripting.HasScripts());
            REQUIRE(Store().IsValid(shader));
        }

        Fenrir::Testing::HeadlessApp headless;
        Fenrir::Assets assets;
        Fenrir::Scripting scripting;
        Fenrir::AssetHandle<Fenrir::Shader> shader;

        Fenrir::App& Get()
        {
            return headless.Get();
        }

        Fenrir::AssetStore& Store()
        {
            return assets.Store();
        }

        template <typename Result, typename... Args>
        Result Call(const char* typeName, const char* method, Args... arguments)
        {
            return AssetsScriptingTests::Call<Result>(scripting, typeName, method, arguments...);
        }

        template <typename... Args>
        std::string Text(const char* typeName, const char* method, Args... arguments)
        {
            return AssetsScriptingTests::Text(scripting, typeName, method, arguments...);
        }
    };

    // How many messages of `level` the app logged that contain `text`.
    inline std::size_t LoggedWith(const Fenrir::Testing::HeadlessApp& headless, Fenrir::Testing::LogLevel level,
                                  const std::string& text)
    {
        std::size_t count = 0;
        for (const Fenrir::Testing::LoggedMessage& message : headless.Logger().Messages())
        {
            count += message.level == level && message.text.find(text) != std::string::npos ? 1 : 0;
        }
        return count;
    }

    // A release build warns about nothing; a debug build once per native call site.
#ifdef NDEBUG
    inline constexpr std::size_t warningsPerCallSite = 0;
#else
    inline constexpr std::size_t warningsPerCallSite = 1;
#endif
} // namespace AssetsScriptingTests

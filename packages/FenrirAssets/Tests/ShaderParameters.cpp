// Built only with FENRIR_ENABLE_RENDERER: its fixture container is built from GLSL by FenrirShaderCompiler.
#include <array>
#include <cstddef>
#include <span>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "AssetsTest.hpp"
#include "FenrirShaders/Parameters.hpp"

using AssetsTest::AssetsApp;

TEST_CASE("GetShaderParameters lists a fixture shader's parameters with their types and defaults", "[assets][shaders]")
{
    AssetsApp test;
    Fenrir::ShaderData data;
    const std::span<const std::byte> bytes = std::as_bytes(std::span(Fenrir::Shaders::Parameters));
    data.bytes.assign(bytes.begin(), bytes.end());
    const Fenrir::AssetHandle<Fenrir::Shader> shader = test.store.Add<Fenrir::Shader>(data, Fenrir::AssetOwner::App());
    REQUIRE_FALSE(shader.IsEmpty());

    const std::span<const Fenrir::ShaderParameter> parameters = test.store.GetShaderParameters(shader);
    using Type = Fenrir::ShaderParameterType;
    const std::vector<Fenrir::ShaderParameter> expected = {
        {"colour", Type::Vector4, {1.0f, 0.5f, 0.25f, 1.0f}},
        {"roughness", Type::Float, {0.5f}},
        {"offset", Type::Vector2, {2.0f, 3.0f}},
        {"emissive", Type::Vector3, {}},
        {"colourTexture", Type::Texture, {}, Fenrir::DefaultTexture::White},
        {"normalTexture", Type::Texture, {}, Fenrir::DefaultTexture::Normal},
    };

    INFO("every parameter, in block order then binding order, with its type and its pragma's default, or zero or "
         "white without one");
    REQUIRE(std::vector<Fenrir::ShaderParameter>(parameters.begin(), parameters.end()) == expected);
}

#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "AssetsTest.hpp"
#include "FenrirAssets/ShaderContainer.hpp"

using AssetsTest::AssetsApp;

namespace
{
    // A small container written out by hand: one key, so two variants, each with a stage, a block and a texture.
    Fenrir::ShaderContainer HandMadeContainer()
    {
        Fenrir::ShaderContainer container;
        container.name = "HandMade";
        container.keys = {"ALPHA_MASK"};
        for (int i = 0; i < 2; ++i)
        {
            Fenrir::ShaderVariant variant;
            if (i == 1)
            {
                variant.keys = {"ALPHA_MASK"};
            }
            variant.stages.push_back({Fenrir::ShaderStage::Vertex, {0x07230203u, 1u, 2u}, {0x07230203u, 3u}});
            variant.stages.push_back({Fenrir::ShaderStage::Fragment, {0x07230203u, 4u}, {0x07230203u, 5u, 6u}});
            variant.vertexInputs = {{0, "inPosition", Fenrir::ShaderValueType::Vec3}};
            variant.bindings = {{2, 0, Fenrir::ShaderBindingKind::UniformBuffer, "MaterialParams", 16},
                                {2, 1, Fenrir::ShaderBindingKind::Texture, "baseColourTexture", 0}};
            variant.materialBlockBinding = 0;
            variant.materialMembers = {{"tint", Fenrir::ShaderValueType::Vec3, 0, 12},
                                       {"strength", Fenrir::ShaderValueType::Float, 12, 4}};
            variant.parameters = {
                {"tint", Fenrir::ShaderParameterType::Vector3, {1.0f, 0.5f, 0.25f, 0.0f}},
                {"strength", Fenrir::ShaderParameterType::Float, {i == 1 ? 2.0f : 0.0f}},
                {"baseColourTexture", Fenrir::ShaderParameterType::Texture, {}, Fenrir::DefaultTexture::Normal}};
            variant.defaultBlock.assign(16, std::byte{0});
            variant.defaultBlock[3] = std::byte{0x3f};
            container.variants.push_back(std::move(variant));
        }
        return container;
    }

    void PutU32(std::vector<std::byte>& bytes, std::size_t at, std::uint32_t value)
    {
        for (std::size_t i = 0; i < 4; ++i)
        {
            bytes[at + i] = static_cast<std::byte>((value >> (8 * i)) & 0xffu);
        }
    }
} // namespace

TEST_CASE("A shader container reads back as it was written", "[assets][shaders]")
{
    const Fenrir::ShaderContainer written = HandMadeContainer();
    const std::vector<std::byte> bytes = Fenrir::WriteShaderContainer(written);
    const Fenrir::ShaderContainerResult read = Fenrir::ReadShaderContainer(bytes);

    INFO("the reader takes what the writer wrote: " << read.diagnostic);
    REQUIRE(read.Ok());
    INFO("every field comes back unchanged");
    REQUIRE(*read.container == written);
    INFO("writing it again gives the same bytes");
    REQUIRE(Fenrir::WriteShaderContainer(*read.container) == bytes);
}

TEST_CASE("A shader container starts with its magic and format version", "[assets][shaders]")
{
    const std::vector<std::byte> bytes = Fenrir::WriteShaderContainer(HandMadeContainer());

    INFO("the first four bytes are 'FSHC'");
    REQUIRE(std::memcmp(bytes.data(), "FSHC", 4) == 0);
    INFO("the next four are the format version, little-endian");
    REQUIRE(std::to_integer<std::uint32_t>(bytes[4]) == Fenrir::ShaderContainerVersion);
    REQUIRE(std::to_integer<std::uint32_t>(bytes[5]) == 0);
}

TEST_CASE("Bytes with the wrong magic are not read as a shader container", "[assets][shaders]")
{
    std::vector<std::byte> bytes = Fenrir::WriteShaderContainer(HandMadeContainer());
    bytes[0] = std::byte{'P'};
    const Fenrir::ShaderContainerResult read = Fenrir::ReadShaderContainer(bytes);

    INFO("they are refused");
    REQUIRE_FALSE(read.Ok());
    INFO("the diagnostic says it is not a shader container: " << read.diagnostic);
    REQUIRE(read.diagnostic.find("not a shader container") != std::string::npos);
}

TEST_CASE("A shader container of another format version is refused", "[assets][shaders]")
{
    std::vector<std::byte> bytes = Fenrir::WriteShaderContainer(HandMadeContainer());
    PutU32(bytes, 4, 99);
    const Fenrir::ShaderContainerResult read = Fenrir::ReadShaderContainer(bytes);

    INFO("it is refused");
    REQUIRE_FALSE(read.Ok());
    INFO("the diagnostic names its version and the one this build reads: " << read.diagnostic);
    REQUIRE(read.diagnostic.find("99") != std::string::npos);
    REQUIRE(read.diagnostic.find("version 1") != std::string::npos);
}

TEST_CASE("A shader container cut short is refused", "[assets][shaders]")
{
    std::vector<std::byte> bytes = Fenrir::WriteShaderContainer(HandMadeContainer());
    bytes.resize(bytes.size() - 3);
    const Fenrir::ShaderContainerResult read = Fenrir::ReadShaderContainer(bytes);

    INFO("it is refused, with a diagnostic: " << read.diagnostic);
    REQUIRE_FALSE(read.Ok());
    REQUIRE_FALSE(read.diagnostic.empty());
}

TEST_CASE("A variant is found by the keys it defines, ignoring keys the program lacks", "[assets][shaders]")
{
    const Fenrir::ShaderContainer container = HandMadeContainer();
    const std::string_view none[] = {"SKINNED"};
    const std::string_view masked[] = {"SKINNED", "ALPHA_MASK"};

    INFO("asking for a key the program lacks gives the base variant");
    REQUIRE(container.FindVariant(none) == &container.variants[0]);
    INFO("asking for ALPHA_MASK gives the variant defining it");
    REQUIRE(container.FindVariant(masked) == &container.variants[1]);
}

TEST_CASE("A shader's parameters are each listed once, the base variant's default first", "[assets][shaders]")
{
    const std::vector<Fenrir::ShaderParameter> parameters = Fenrir::CollectShaderParameters(HandMadeContainer());

    INFO("tint, strength and the texture, once each");
    REQUIRE(parameters.size() == 3);
    INFO("strength's default is the base variant's zero, not the ALPHA_MASK variant's 2");
    REQUIRE(parameters[1].name == "strength");
    REQUIRE(parameters[1].value[0] == 0.0f);
}

TEST_CASE("The asset store refuses shader data that is not a shader container", "[assets][shaders]")
{
    AssetsApp test;
    Fenrir::ShaderData data;
    data.bytes = {std::byte{1}, std::byte{2}, std::byte{3}};
    const Fenrir::AssetHandle<Fenrir::Shader> handle = test.store.Add<Fenrir::Shader>(data, Fenrir::AssetOwner::App());

    INFO("the handle is empty");
    REQUIRE(handle.IsEmpty());
    INFO("the refusal is logged");
    REQUIRE(test.errors->size() == 1);
}

TEST_CASE("Shader data exposes its container's parameter table", "[assets][shaders]")
{
    AssetsApp test;
    Fenrir::ShaderData data;
    data.bytes = Fenrir::WriteShaderContainer(HandMadeContainer());
    const Fenrir::AssetHandle<Fenrir::Shader> handle = test.store.Add<Fenrir::Shader>(data, Fenrir::AssetOwner::App());
    REQUIRE_FALSE(handle.IsEmpty());

    const std::span<const Fenrir::ShaderParameter> parameters = test.store.GetShaderParameters(handle);
    INFO("the store lists the container's three parameters");
    REQUIRE(parameters.size() == 3);
    INFO("the texture's default is normal, as the container says");
    REQUIRE(parameters[2].name == "baseColourTexture");
    REQUIRE(parameters[2].texture == Fenrir::DefaultTexture::Normal);
    INFO("the shader data holds the same table");
    REQUIRE(test.store.TryGet(handle)->parameters.size() == 3);
}

TEST_CASE("A stale shader handle has no parameters", "[assets][shaders]")
{
    AssetsApp test;
    Fenrir::ShaderData data;
    data.bytes = Fenrir::WriteShaderContainer(HandMadeContainer());
    const Fenrir::AssetHandle<Fenrir::Shader> handle = test.store.Add<Fenrir::Shader>(data, Fenrir::AssetOwner::App());
    REQUIRE(test.store.Unload(handle));

    INFO("an unloaded shader's parameters are empty");
    REQUIRE(test.store.GetShaderParameters(handle).empty());
}

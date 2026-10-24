#include <array>
#include <cstddef>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "FenrirShaders/Material.hpp"
#include "FenrirShaders/TwoD.hpp"
#include "ShaderCompilerTest.hpp"

using ShaderCompilerTest::FloatAt;
using ShaderCompilerTest::Member;
using ShaderCompilerTest::Parameter;
using ShaderCompilerTest::Read;

namespace
{
    const Fenrir::ShaderVariant& MaterialVariant()
    {
        static const Fenrir::ShaderContainer container = Read(Fenrir::Shaders::Material);
        return container.variants.at(0);
    }

    // Checks one member against the std140 layout worked out by hand in material.frag.
    void RequireMember(const std::string& name, Fenrir::ShaderValueType type, std::uint32_t offset, std::uint32_t size)
    {
        const Fenrir::ShaderBlockMember* member = Member(MaterialVariant(), name);
        INFO(name << " is a member of MaterialParams");
        REQUIRE(member != nullptr);
        INFO(name << "'s type, std140 offset and size are as worked out by hand");
        REQUIRE(member->type == type);
        REQUIRE(member->offset == offset);
        REQUIRE(member->size == size);
    }
} // namespace

TEST_CASE("Set 2's MaterialParams members are reflected with their names, types, offsets and sizes",
          "[shader-compiler][reflection]")
{
    INFO("the block is at set 2 binding 0");
    REQUIRE(MaterialVariant().materialBlockBinding == 0u);
    RequireMember("baseColour", Fenrir::ShaderValueType::Vec4, 0, 16);
    RequireMember("tint", Fenrir::ShaderValueType::Vec3, 16, 12);
    RequireMember("strength", Fenrir::ShaderValueType::Float, 28, 4);
    RequireMember("weights", Fenrir::ShaderValueType::Array, 32, 48);
    RequireMember("after", Fenrir::ShaderValueType::Float, 80, 4);
    RequireMember("transform", Fenrir::ShaderValueType::Matrix, 96, 64);
    RequireMember("scale", Fenrir::ShaderValueType::Vec2, 160, 8);
}

TEST_CASE("A shader's parameters are its float to vec4 members, then its material textures",
          "[shader-compiler][reflection]")
{
    const Fenrir::ShaderVariant& variant = MaterialVariant();
    std::vector<std::string> names;
    for (const Fenrir::ShaderParameter& parameter : variant.parameters)
    {
        names.push_back(parameter.name);
    }

    INFO("the array and the matrix are not parameters; the textures follow the block, by binding");
    REQUIRE(names ==
            std::vector<std::string>{"baseColour", "tint", "strength", "after", "scale", "albedo", "normals", "mask"});
    INFO("each has its parameter type");
    REQUIRE(Parameter(variant, "tint")->type == Fenrir::ShaderParameterType::Vector3);
    REQUIRE(Parameter(variant, "scale")->type == Fenrir::ShaderParameterType::Vector2);
    REQUIRE(Parameter(variant, "albedo")->type == Fenrir::ShaderParameterType::Texture);
}

TEST_CASE("Pragmas give parameters their defaults, and the rest are zero or white", "[shader-compiler][defaults]")
{
    const Fenrir::ShaderVariant& variant = MaterialVariant();

    INFO("baseColour's four values");
    REQUIRE(Parameter(variant, "baseColour")->value == std::array<float, 4>{1.0f, 0.5f, 0.25f, 1.0f});
    INFO("tint's three, the fourth zero");
    REQUIRE(Parameter(variant, "tint")->value == std::array<float, 4>{0.1f, 0.2f, 0.3f, 0.0f});
    INFO("scale's two, written as 3 and 4.0f");
    REQUIRE(Parameter(variant, "scale")->value == std::array<float, 4>{3.0f, 4.0f, 0.0f, 0.0f});
    INFO("albedo has no pragma, so it is white");
    REQUIRE(Parameter(variant, "albedo")->texture == Fenrir::DefaultTexture::White);
    INFO("normals and mask are normal and black, as their pragmas say");
    REQUIRE(Parameter(variant, "normals")->texture == Fenrir::DefaultTexture::Normal);
    REQUIRE(Parameter(variant, "mask")->texture == Fenrir::DefaultTexture::Black);
}

TEST_CASE("The default block image holds the defaults at their std140 offsets", "[shader-compiler][defaults]")
{
    const std::vector<std::byte>& block = MaterialVariant().defaultBlock;

    INFO("it is as long as the block: 168 bytes of members, padded to 176");
    REQUIRE(block.size() == 176);
    INFO("baseColour at 0");
    REQUIRE(FloatAt(block, 0) == 1.0f);
    REQUIRE(FloatAt(block, 12) == 1.0f);
    INFO("the vec3 tint at 16, and the float strength packed into the last four bytes of its 16");
    REQUIRE_THAT(FloatAt(block, 16), Catch::Matchers::WithinULP(0.1f, 0));
    REQUIRE_THAT(FloatAt(block, 24), Catch::Matchers::WithinULP(0.3f, 0));
    REQUIRE(FloatAt(block, 28) == 0.75f);
    INFO("the array's 16-byte stride puts 'after' at 80, not 44, and leaves the array zero");
    REQUIRE(FloatAt(block, 80) == 2.5f);
    REQUIRE(FloatAt(block, 44) == 0.0f);
    REQUIRE(FloatAt(block, 32) == 0.0f);
    INFO("the matrix, with no default, is zero; scale is at 160");
    REQUIRE(FloatAt(block, 96) == 0.0f);
    REQUIRE(FloatAt(block, 160) == 3.0f);
    REQUIRE(FloatAt(block, 164) == 4.0f);
}

TEST_CASE("A 2D shader's material is set 1", "[shader-compiler][reflection]")
{
    const Fenrir::ShaderContainer container = Read(Fenrir::Shaders::TwoD);
    const Fenrir::ShaderVariant& variant = container.variants.at(0);

    INFO("its set 0 is Fenrir/2D.glsl's Target2D, so its material set is 1");
    REQUIRE(container.materialSet == 1);
    INFO("its parameters are set 1's block member and texture");
    REQUIRE(variant.parameters.size() == 2);
    REQUIRE(Parameter(variant, "tint")->value == std::array<float, 4>{1.0f, 1.0f, 1.0f, 0.5f});
    REQUIRE(Parameter(variant, "picture")->type == Fenrir::ShaderParameterType::Texture);
}

TEST_CASE("A mesh shader's material is set 2", "[shader-compiler][reflection]")
{
    const Fenrir::ShaderContainer container = Read(Fenrir::Shaders::Material);

    INFO("with no 2D target in set 0, the material set is 2");
    REQUIRE(container.materialSet == 2);
}

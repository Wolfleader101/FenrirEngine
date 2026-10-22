#include <algorithm>
#include <cstdint>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "FenrirAssets/ShaderGlBindings.hpp"
#include "FenrirShaders/Flatten.hpp"
#include "ShaderCompilerTest.hpp"

using ShaderCompilerTest::Decorate;
using ShaderCompilerTest::Read;
using ShaderCompilerTest::SpirvDecorations;

namespace
{
    constexpr std::uint32_t BuiltInVertexId = 5;
    constexpr std::uint32_t BuiltInVertexIndex = 42;

    // The fixture's stages: [0] vertex, [1] fragment.
    const Fenrir::ShaderVariant& FlattenVariant()
    {
        static const Fenrir::ShaderContainer container = Read(Fenrir::Shaders::Flatten);
        return container.variants.at(0);
    }

    // Every named resource of both stages' SPIR-V, the GL copy's or the original's.
    std::map<std::string, ShaderCompilerTest::Decorations> Resources(bool gl)
    {
        std::map<std::string, ShaderCompilerTest::Decorations> resources;
        for (const Fenrir::ShaderStageCode& stage : FlattenVariant().stages)
        {
            const SpirvDecorations decorations = Decorate(gl ? stage.glSpirv : stage.spirv);
            for (const auto& [name, decoration] : decorations.byName)
            {
                if (decoration.binding.has_value())
                {
                    resources[name] = decoration;
                }
            }
        }
        return resources;
    }
} // namespace

TEST_CASE("The flattening formula is set times four plus binding", "[shader-compiler][flattening]")
{
    INFO("set 0 binding 0 is GL binding 0");
    REQUIRE(Fenrir::GlBindingOf(0, 0) == 0);
    INFO("set 1 binding 2 is GL binding 6");
    REQUIRE(Fenrir::GlBindingOf(1, 2) == 6);
    INFO("set 3 binding 3, the last of a kind, is GL binding 15");
    REQUIRE(Fenrir::GlBindingOf(3, 3) == 15);
}

TEST_CASE("The GL copy flattens every resource kind's bindings and zeroes the sets", "[shader-compiler][flattening]")
{
    const auto gl = Resources(true);

    INFO("the fixture's seven resources are all decorated");
    REQUIRE(gl.size() == 7);
    for (const auto& [name, decoration] : gl)
    {
        INFO(name << "'s descriptor set is zeroed");
        REQUIRE(decoration.set == 0u);
    }
    INFO("uniform blocks: frame (set 0 binding 0), extra (1, 2), material (2, 0)");
    REQUIRE(gl.at("frame").binding == 0u);
    REQUIRE(gl.at("extra").binding == 6u);
    REQUIRE(gl.at("material").binding == 8u);
    INFO("textures: first (set 2 binding 1), second (2, 3)");
    REQUIRE(gl.at("first").binding == 9u);
    REQUIRE(gl.at("second").binding == 11u);
    INFO("storage blocks: draws (set 3 binding 0), lights (3, 2)");
    REQUIRE(gl.at("draws").binding == 12u);
    REQUIRE(gl.at("lights").binding == 14u);
}

TEST_CASE("The SPIR-V kept for Vulkan has the sets and bindings as written", "[shader-compiler][flattening]")
{
    const auto spirv = Resources(false);

    INFO("extra is still set 1 binding 2");
    REQUIRE(spirv.at("extra").set == 1u);
    REQUIRE(spirv.at("extra").binding == 2u);
    INFO("lights is still set 3 binding 2");
    REQUIRE(spirv.at("lights").set == 3u);
    REQUIRE(spirv.at("lights").binding == 2u);
}

TEST_CASE("The GL copy reads VertexId where the shader read gl_VertexIndex", "[shader-compiler][flattening]")
{
    const Fenrir::ShaderStageCode& vertex = FlattenVariant().stages.at(0);
    REQUIRE(vertex.stage == Fenrir::ShaderStage::Vertex);
    const SpirvDecorations gl = Decorate(vertex.glSpirv);
    const SpirvDecorations original = Decorate(vertex.spirv);

    INFO("the original uses the VertexIndex built-in");
    REQUIRE(std::ranges::count(original.builtIns, BuiltInVertexIndex) == 1);
    INFO("the GL copy uses VertexId instead, and VertexIndex nowhere");
    REQUIRE(std::ranges::count(gl.builtIns, BuiltInVertexId) == 1);
    REQUIRE(std::ranges::count(gl.builtIns, BuiltInVertexIndex) == 0);
}

TEST_CASE("Each binding is reflected with its set, kind and name", "[shader-compiler][reflection]")
{
    const Fenrir::ShaderVariant& variant = FlattenVariant();
    const auto find = [&](std::uint32_t set, std::uint32_t binding) {
        return std::ranges::find_if(
            variant.bindings, [&](const Fenrir::ShaderBinding& b) { return b.set == set && b.binding == binding; });
    };

    INFO("seven bindings across both stages");
    REQUIRE(variant.bindings.size() == 7);
    INFO("set 0 binding 0 is the FrameData uniform block from Fenrir/Frame.glsl");
    REQUIRE(find(0, 0)->kind == Fenrir::ShaderBindingKind::UniformBuffer);
    REQUIRE(find(0, 0)->name == "FrameData");
    INFO("set 3 binding 0 is the DrawData storage block from Fenrir/Draw.glsl");
    REQUIRE(find(3, 0)->kind == Fenrir::ShaderBindingKind::StorageBuffer);
    REQUIRE(find(3, 0)->name == "DrawData");
    INFO("set 2 binding 3 is the texture 'second'");
    REQUIRE(find(2, 3)->kind == Fenrir::ShaderBindingKind::Texture);
    REQUIRE(find(2, 3)->name == "second");
}

TEST_CASE("The vertex stage's inputs are reflected by location", "[shader-compiler][reflection]")
{
    const Fenrir::ShaderVariant& variant = FlattenVariant();
    const auto input = [&](std::uint32_t location) {
        return std::ranges::find(variant.vertexInputs, location, &Fenrir::ShaderVertexInput::location);
    };

    INFO("location 0 is inPosition, a vec3");
    REQUIRE(input(0) != variant.vertexInputs.end());
    REQUIRE(input(0)->name == "inPosition");
    REQUIRE(input(0)->type == Fenrir::ShaderValueType::Vec3);
    INFO("location 3 is inUv0, a vec2");
    REQUIRE(input(3) != variant.vertexInputs.end());
    REQUIRE(input(3)->name == "inUv0");
    REQUIRE(input(3)->type == Fenrir::ShaderValueType::Vec2);
    INFO("no built-in (gl_VertexIndex) is listed as an input");
    REQUIRE(std::ranges::none_of(variant.vertexInputs,
                                 [](const Fenrir::ShaderVertexInput& i) { return i.name.starts_with("gl_"); }));
}

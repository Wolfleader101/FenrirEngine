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

TEST_CASE("The flattening formula is set times eight plus the index within the kind", "[shader-compiler][flattening]")
{
    INFO("the first binding of a kind in set 0 is GL binding 0");
    REQUIRE(Fenrir::GlBindingOf(0, 0) == 0);
    INFO("the third binding of a kind in set 1 is GL binding 10");
    REQUIRE(Fenrir::GlBindingOf(1, 2) == 10);
    INFO("the eighth binding of a kind in set 3, the last a set may hold, is GL binding 31");
    REQUIRE(Fenrir::GlBindingOf(3, 7) == 31);
}

TEST_CASE("The GL copy flattens every resource kind's bindings and zeroes the sets", "[shader-compiler][flattening]")
{
    const auto gl = Resources(true);

    INFO("the fixture's twelve resources are all decorated: its own eight, and Frame.glsl's three sky textures and "
         "light buffer");
    REQUIRE(gl.size() == 12);
    for (const auto& [name, decoration] : gl)
    {
        INFO(name << "'s descriptor set is zeroed");
        REQUIRE(decoration.set == 0u);
    }
    INFO("uniform blocks: frame (set 0 binding 0), extra (1, 2) and material (2, 0), each its set's first");
    REQUIRE(gl.at("frame").binding == 0u);
    REQUIRE(gl.at("extra").binding == 8u);
    REQUIRE(gl.at("material").binding == 16u);
    INFO("textures: Frame.glsl's sky textures (set 0 bindings 1, 2 and 3) are set 0's textures 0, 1 and 2");
    REQUIRE(gl.at("fenrirSkyIrradiance").binding == 0u);
    REQUIRE(gl.at("fenrirSkySpecular").binding == 1u);
    REQUIRE(gl.at("fenrirBrdfLut").binding == 2u);
    INFO("textures: first, middle and second (set 2 bindings 1, 2 and 3) are set 2's textures 0, 1 and 2");
    REQUIRE(gl.at("first").binding == 16u);
    REQUIRE(gl.at("middle").binding == 17u);
    REQUIRE(gl.at("second").binding == 18u);
    INFO("storage blocks: draws (set 3 binding 0) and lights (3, 2) are set 3's storage blocks 0 and 1");
    REQUIRE(gl.at("draws").binding == 24u);
    REQUIRE(gl.at("lights").binding == 25u);
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

    INFO("twelve bindings across both stages, four of them Frame.glsl's sky textures and light buffer");
    REQUIRE(variant.bindings.size() == 12);
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

TEST_CASE("A storage block ending in a runtime array is reflected one record long", "[shader-compiler][reflection]")
{
    const Fenrir::ShaderVariant& variant = FlattenVariant();
    const auto draws = std::ranges::find_if(
        variant.bindings, [](const Fenrir::ShaderBinding& b) { return b.set == 3 && b.binding == 0; });
    REQUIRE(draws != variant.bindings.end());

    INFO("DrawData holds DrawItem items[]: two mat4s, a uint and a float, padded to 144 bytes in std430");
    REQUIRE(draws->size == 144);
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

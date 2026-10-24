#include <cstddef>
#include <span>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirShaders/Material.hpp"
#include "FenrirShaders/Variants.hpp"
#include "ShaderCompilerTest.hpp"

TEST_CASE("A container the tool wrote round-trips through the reader", "[shader-compiler][container]")
{
    const std::span<const std::byte> written = std::as_bytes(std::span(Fenrir::Shaders::Variants));
    const Fenrir::ShaderContainerResult read = Fenrir::ReadShaderContainer(written);

    INFO("the reader takes the tool's bytes: " << read.diagnostic);
    REQUIRE(read.Ok());
    INFO("it is the program fenrir_add_shaders named");
    REQUIRE(read.container->name == "Variants");
    INFO("writing what was read gives back the tool's bytes exactly");
    const std::vector<std::byte> rewritten = Fenrir::WriteShaderContainer(*read.container);
    REQUIRE(std::vector<std::byte>(written.begin(), written.end()) == rewritten);
}

TEST_CASE("Each stage of a container holds SPIR-V", "[shader-compiler][container]")
{
    const Fenrir::ShaderContainer container = ShaderCompilerTest::Read(Fenrir::Shaders::Material);
    const Fenrir::ShaderVariant& variant = container.variants.at(0);
    constexpr std::uint32_t SpirvMagic = 0x07230203;

    INFO("vertex first, then fragment");
    REQUIRE(variant.stages.size() == 2);
    REQUIRE(variant.stages[0].stage == Fenrir::ShaderStage::Vertex);
    REQUIRE(variant.stages[1].stage == Fenrir::ShaderStage::Fragment);
    for (const Fenrir::ShaderStageCode& stage : variant.stages)
    {
        INFO("both copies start with SPIR-V's magic number");
        REQUIRE(stage.glSpirv.at(0) == SpirvMagic);
        REQUIRE(stage.spirv.at(0) == SpirvMagic);
        INFO("and are SPIR-V 1.0");
        REQUIRE(stage.spirv.at(1) == 0x00010000u);
    }
}

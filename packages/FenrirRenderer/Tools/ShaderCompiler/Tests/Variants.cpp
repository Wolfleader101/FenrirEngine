#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirShaders/Includes.hpp"
#include "FenrirShaders/Variants.hpp"
#include "ShaderCompilerTest.hpp"

using ShaderCompilerTest::Member;
using ShaderCompilerTest::Parameter;
using ShaderCompilerTest::Read;

namespace
{
    std::string Normalised(const std::filesystem::path& path)
    {
        return std::filesystem::absolute(path).lexically_normal().generic_string();
    }
} // namespace

TEST_CASE("Includes resolve against the engine's directory, a named one, and the including file's own",
          "[shader-compiler][includes]")
{
    const Fenrir::ShaderContainer container = Read(Fenrir::Shaders::Includes);
    const Fenrir::ShaderVariant& variant = container.variants.at(0);

    INFO("Fenrir/View.glsl, from the engine's include directory, declares ViewData at set 1 binding 0");
    REQUIRE(std::ranges::any_of(variant.bindings, [](const Fenrir::ShaderBinding& b) {
        return b.set == 1 && b.binding == 0 && b.name == "ViewData";
    }));
    INFO("Nested.glsl, included by Fixture/Shared.glsl beside it, declares MaterialParams with nestedColour");
    REQUIRE(Member(variant, "nestedColour") != nullptr);
    INFO("its pragma, in the included file, gives nestedColour its default");
    REQUIRE(Parameter(variant, "nestedColour")->value == std::array<float, 4>{0.25f, 0.5f, 0.75f, 1.0f});
}

TEST_CASE("The depfile names every file a program includes, so editing one rebuilds it", "[shader-compiler][includes]")
{
    std::ifstream file(FENRIR_SHADER_TEST_DEPFILE);
    REQUIRE(file);
    std::stringstream text;
    text << file.rdbuf();
    const std::string depfile = text.str();
    const std::filesystem::path fixtures = FENRIR_SHADER_TEST_FIXTURES;

    INFO("the depfile:\n" << depfile);
    INFO("it names the nested include");
    REQUIRE(depfile.find(Normalised(fixtures / "include/Fixture/Nested.glsl")) != std::string::npos);
    INFO("it names the include that includes it");
    REQUIRE(depfile.find(Normalised(fixtures / "include/Fixture/Shared.glsl")) != std::string::npos);
    INFO("it names the engine's include");
    REQUIRE(depfile.find(Normalised(std::filesystem::path(FENRIR_SHADER_TEST_ENGINE_INCLUDE) / "Fenrir/View.glsl")) !=
            std::string::npos);
}

TEST_CASE("Every combination of two variant keys is built", "[shader-compiler][variants]")
{
    const Fenrir::ShaderContainer container = Read(Fenrir::Shaders::Variants);

    INFO("the program lists its keys in the order fenrir_add_shaders named them");
    REQUIRE(container.keys == std::vector<std::string>{"FIRST", "SECOND"});
    INFO("two keys make four variants");
    REQUIRE(container.variants.size() == 4);
    INFO("variant i defines key k when bit k of i is set");
    REQUIRE(container.variants[0].keys.empty());
    REQUIRE(container.variants[1].keys == std::vector<std::string>{"FIRST"});
    REQUIRE(container.variants[2].keys == std::vector<std::string>{"SECOND"});
    REQUIRE(container.variants[3].keys == std::vector<std::string>{"FIRST", "SECOND"});
    INFO("each variant was compiled with its own defines: only FIRST's variants have the member 'first'");
    REQUIRE(Member(container.variants[0], "first") == nullptr);
    REQUIRE(Member(container.variants[1], "first") != nullptr);
    REQUIRE(Member(container.variants[2], "first") == nullptr);
    REQUIRE(Member(container.variants[3], "first") != nullptr);
    INFO("and only SECOND's have 'second'");
    REQUIRE(Member(container.variants[1], "second") == nullptr);
    REQUIRE(Member(container.variants[2], "second") != nullptr);
    REQUIRE(Member(container.variants[3], "second") != nullptr);
    INFO("each variant has both stages");
    for (const Fenrir::ShaderVariant& variant : container.variants)
    {
        REQUIRE(variant.stages.size() == 2);
    }
}

TEST_CASE("A pragma inside an #ifdef applies only to the variants defining it", "[shader-compiler][defaults]")
{
    const Fenrir::ShaderContainer container = Read(Fenrir::Shaders::Variants);

    INFO("without SECOND, strength has no pragma and is zero");
    REQUIRE(Parameter(container.variants[0], "strength")->value[0] == 0.0f);
    REQUIRE(Parameter(container.variants[1], "strength")->value[0] == 0.0f);
    INFO("with SECOND, the pragma inside #ifdef SECOND gives it 7");
    REQUIRE(Parameter(container.variants[2], "strength")->value[0] == 7.0f);
    REQUIRE(Parameter(container.variants[3], "strength")->value[0] == 7.0f);
    INFO("the default block images agree: strength is at offset 0");
    REQUIRE(ShaderCompilerTest::FloatAt(container.variants[0].defaultBlock, 0) == 0.0f);
    REQUIRE(ShaderCompilerTest::FloatAt(container.variants[2].defaultBlock, 0) == 7.0f);
}

TEST_CASE("The renderer finds a variant by the keys it wants", "[shader-compiler][variants]")
{
    const Fenrir::ShaderContainer container = Read(Fenrir::Shaders::Variants);
    const std::string_view second[] = {"SECOND", "ALPHA_MASK"};

    INFO("SECOND, with a key the program lacks, gives variant 2");
    REQUIRE(container.FindVariant(second) == &container.variants[2]);
}

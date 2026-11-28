// C++ data asset types in C# (ADR-0083 §3, the serialisation csharp spec's "C++ data asset types in C#"): the classes
// Iris.Bindings generates from DataAssetsTest.binding.toml read the tree C++'s bridge writes of FenrirAssets' test
// package's data assets and write it back byte for byte, and their codecs are registered as the fixture loads. Each
// type's defaults are the generated defaults test's (Generated/DataAssetsTestDefaultsTests.cpp).

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "AssetsScriptingApp.hpp"
#include "DataAssetTrees.hpp"

using namespace Fenrir;
using namespace Fenrir::Testing;

namespace
{
    constexpr const char* DataAssetCallsType = "AssetsScriptFixture.DataAssetCalls";

    // A managed byte[] holding `bytes`.
    Iris::Array<std::uint8_t> ManagedBytes(const Iris::Component& component, const std::vector<std::byte>& bytes)
    {
        auto array = Iris::Array<std::uint8_t>::Create(component, bytes.size());
        {
            INFO("A managed byte[] could not be made: " << array.diagnostic);
            REQUIRE(array.Succeeded());
        }
        std::vector<std::uint8_t> raw(bytes.size());
        for (std::size_t i = 0; i < bytes.size(); ++i)
            raw[i] = static_cast<std::uint8_t>(bytes[i]);
        INFO("The tree's bytes could not be copied into the managed byte[]");
        REQUIRE(array.value->CopyIn(0, std::span<const std::uint8_t>(raw)).Succeeded());
        return std::move(*array.value);
    }

    // What the fixture's `method(byte[] tree)` returns for `tree`.
    template <typename Result>
    Result CallWithTree(Scripting& scripting, const char* method, const std::vector<std::byte>& tree)
    {
        const Iris::Component* component = ScriptingProbe::Component(scripting);
        {
            INFO("There is no loaded script assembly");
            REQUIRE(component != nullptr);
        }
        const auto type = component->FindType(DataAssetCallsType);
        {
            INFO("The fixture type was not found: " << type.diagnostic);
            REQUIRE(type.Succeeded());
        }
        const auto handle = type.value->FindMethod<Result(const Iris::Array<std::uint8_t>&)>(method);
        {
            INFO("The fixture method " << method << " was not found: " << handle.diagnostic);
            REQUIRE(handle.Succeeded());
        }
        const Iris::Array<std::uint8_t> argument = ManagedBytes(*component, tree);
        auto result = handle.value->Invoke(argument);
        {
            INFO("The fixture method " << method << " failed: " << result.diagnostic);
            REQUIRE(result.Succeeded());
        }
        return std::move(*result.value);
    }

    std::vector<std::byte> RoundTrip(Scripting& scripting, const char* method, const std::vector<std::byte>& tree)
    {
        const auto bytes = BytesOf(CallWithTree<std::optional<Iris::Array<std::uint8_t>>>(scripting, method, tree));
        INFO("The fixture method " << method << " returned null");
        REQUIRE(bytes.has_value());
        return *bytes;
    }
} // namespace

TEST_CASE("A C++ data asset extending another round-trips through its generated C# class, byte for byte",
          "[assets][scripting][data-assets]")
{
    AssetsScriptingApp test;
    AssetsTest::Sword sword;
    sword.displayName = "Glamdring";
    sword.tags = {"elven", "blade"};
    sword.stats.damage = 42.5f;
    sword.edge = 0.875f;
    const std::vector<std::byte> tree = TreeBytes(sword);

    {
        INFO("C# should read the base's and the sword's own fields from C++'s tree");
        REQUIRE(CallWithTree<std::optional<std::string>>(test.scripting, "SwordSummary", tree) ==
                "Glamdring|elven,blade|42.5|0.875");
    }
    INFO("C#'s codecs should write back the tree C++ wrote");
    REQUIRE(RoundTrip(test.scripting, "SwordRoundTrip", tree) == tree);
}

TEST_CASE("A C++ data asset holding a list of structs round-trips through C#, byte for byte",
          "[assets][scripting][data-assets]")
{
    AssetsScriptingApp test;
    MyGame::Sword golden;
    golden.displayName = "Iron Sword";
    golden.damage = 12.5f;
    golden.tags = {"melee"};
    golden.upgrades = {{.level = 1, .cost = 100, .damageBonus = 2.5f}, {.level = 2, .cost = 250, .damageBonus = 0.0f}};
    golden.onHit = "sparks";

    INFO("C#'s codecs should write back the tree C++ wrote, each upgrade's default left out");
    REQUIRE(RoundTrip(test.scripting, "GoldenSwordRoundTrip", TreeBytes(golden)) == TreeBytes(golden));
}

TEST_CASE("A C++ data asset holding a path round-trips through C#, byte for byte", "[assets][scripting][data-assets]")
{
    AssetsScriptingApp test;
    MyGame::ParticleEffect effect;
    effect.texture = "textures/spark.png";
    effect.count = 24;

    INFO("C#'s codecs should write back the path and count C++ wrote");
    REQUIRE(RoundTrip(test.scripting, "ParticleEffectRoundTrip", TreeBytes(effect)) == TreeBytes(effect));
}

TEST_CASE("The fixture's data asset classes register their codecs as it loads", "[assets][scripting][data-assets]")
{
    AssetsScriptingApp test;
    const std::string missing = test.Text(DataAssetCallsType, "UnregisteredCodecs");
    INFO("DataAssetCodecs should find each generated class with its C++ name, and the struct a weapon holds, but "
         "lacks: "
         << missing);
    REQUIRE(missing.empty());
}

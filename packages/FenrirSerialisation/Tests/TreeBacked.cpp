// A tree-backed type (a C# data asset type's): its value is a value tree already, so the bridge never walks its
// description as a layout, and refuses to, with a diagnostic.
#include "FenrirSerialisation/Bridge.hpp"
#include "FenrirSerialisation/Encoding.hpp"
#include "FenrirSerialisation/Value.hpp"

#include "SerialisationTest.hpp"

#include "FenrirTypes/FieldInfo.hpp"
#include "FenrirTypes/Kind.hpp"
#include "FenrirTypes/TypeDescription.hpp"
#include "FenrirTypes/TypeRegistry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

using namespace Fenrir::Serialisation;
using SerialisationTest::RecordingLogger;
using SerialisationTest::TreeOf;

namespace
{
    // A tree-backed data asset type as its registrant would build it, its default an encoded tree.
    struct TreeBackedType
    {
        std::vector<std::byte> defaultTree = EncodeTree(TreeOf("{title: 'Sword'}"), Trivia::Drop);
        Fenrir::FieldInfo fields[1] = {{.name = "title", .type = {.kind = Fenrir::Kind::String}}};
        Fenrir::TypeDescription description{
            .name = "Game::ScriptedSword",
            .package = "GameScripts",
            .role = Fenrir::Role::DataAsset,
            .fields = fields,
            .treeBacked = true,
            .defaultTree = defaultTree,
        };
    };

    constexpr std::string_view Path = "sword.asset.json5";
} // namespace

TEST_CASE("FromTree refuses to read into a tree-backed type, with one error, and reads nothing",
          "[serialisation][fromtree][tree-backed]")
{
    const TreeBackedType type;
    Fenrir::TypeRegistry registry;
    INFO("setup: the tree-backed type registers");
    REQUIRE(registry.Register(type.description));
    RecordingLogger logger;
    // There is no instance of a tree-backed type; any address stands in, and must not be written.
    std::byte untouched[8] = {};

    const BridgeResult result =
        FromTree(TreeOf("{\n  title: 'Axe',\n}"), type.description, registry, Resolvers{}, untouched, Path, logger);

    INFO("the read is refused as tree-backed");
    REQUIRE(result.status == BridgeStatus::TreeBacked);
    INFO("one diagnostic, an error naming the type, at the tree's root");
    REQUIRE(result.diagnostics.size() == 1);
    REQUIRE(result.diagnostics[0].severity == DiagnosticSeverity::Error);
    REQUIRE(result.diagnostics[0].message.find("Game::ScriptedSword") != std::string::npos);
    REQUIRE(result.diagnostics[0].line == 1);
    REQUIRE(result.diagnostics[0].column == 1);
    INFO("it was logged once");
    REQUIRE(logger.errors.size() == 1);
    INFO("nothing was written at the address handed in");
    for (std::byte b : untouched)
        REQUIRE(b == std::byte{0});
}

TEST_CASE("ToTree refuses to write a tree-backed type, giving null with one error",
          "[serialisation][totree][tree-backed]")
{
    const TreeBackedType type;
    Fenrir::TypeRegistry registry;
    INFO("setup: the tree-backed type registers");
    REQUIRE(registry.Register(type.description));
    RecordingLogger logger;
    const std::byte nothing[8] = {};

    const Value tree = ToTree(nothing, type.description, registry, Resolvers{}, nullptr, nullptr, &logger);

    INFO("the result is null, which no walked struct gives");
    REQUIRE(tree.Kind() == ValueKind::Null);
    INFO("one error, naming the type");
    REQUIRE(logger.errors.size() == 1);
    REQUIRE(logger.errors[0].find("Game::ScriptedSword") != std::string::npos);
}

TEST_CASE("A tree-backed type's default is a tree in the tree encoding", "[serialisation][encoding][tree-backed]")
{
    const TreeBackedType type;
    RecordingLogger logger;

    const ReadTreeResult decoded = DecodeTree(type.description.defaultTree, Path, logger);
    INFO("its default bytes decode");
    REQUIRE(decoded.tree.has_value());
    INFO("to the tree they were encoded from");
    REQUIRE(Equals(*decoded.tree, TreeOf("{title: 'Sword'}")));
}

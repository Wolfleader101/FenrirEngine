// FromTree (the spec, "The bridge"): a value tree read into a described value through its description's visitor,
// tolerantly. Each tolerance test reads JSON5 text through ReadText, so its nodes carry the locations the diagnostic is
// asserted at, into an instance its description constructed.
#include "FenrirSerialisation/Bridge.hpp"
#include "FenrirSerialisation/Diagnostic.hpp"
#include "FenrirSerialisation/Text.hpp"
#include "FenrirSerialisation/Value.hpp"

#include "BridgeTypes.hpp"
#include "SerialisationTest.hpp"

#include "FenrirTypes/TypeRegistry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

using namespace Fenrir::Serialisation;
using namespace BridgeTest;
using SerialisationTest::RecordingLogger;
using SerialisationTest::TreeOf;

namespace
{
    constexpr std::string_view Path = "test.json5";

    // Reads `text` into `value`, of type `type`, against a registry holding the bridge's test types.
    BridgeResult Read(std::string_view text, const Fenrir::TypeDescription& type, void* value, RecordingLogger& logger)
    {
        Fenrir::TypeRegistry registry;
        RegisterBridgeTypes(registry);
        return FromTree(TreeOf(text), type, registry, Resolvers{}, value, Path, logger);
    }

    BridgeResult Read(std::string_view text, Fixed& fixed, RecordingLogger& logger)
    {
        return Read(text, FixedDescription, &fixed, logger);
    }

    BridgeResult Read(std::string_view text, Owned& owned, RecordingLogger& logger)
    {
        return Read(text, OwnedDescription, &owned, logger);
    }

    BridgeResult Read(std::string_view text, Renamed& renamed, RecordingLogger& logger)
    {
        return Read(text, RenamedDescription, &renamed, logger);
    }

    // The tree of `value`, which two values of the same type share exactly when every written field is equal.
    Value Written(const void* value, const Fenrir::TypeDescription& type)
    {
        Fenrir::TypeRegistry registry;
        RegisterBridgeTypes(registry);
        return ToTree(value, type, registry, Resolvers{});
    }

    // A diagnostic's location and severity as one string, so a failure shows all three.
    std::string Where(const Diagnostic& diagnostic)
    {
        return std::string(diagnostic.severity == DiagnosticSeverity::Error ? "error" : "warning") + " at " +
               diagnostic.path + ":" + std::to_string(diagnostic.line) + ":" + std::to_string(diagnostic.column);
    }
} // namespace

TEST_CASE("A struct of every fixed-layout kind goes to a tree and back unchanged", "[serialisation][fromtree]")
{
    Fixed original;
    original.lives = -4;
    original.score = std::int64_t{1} << 40;
    original.team = 200;
    original.seed = 0xFFFF'FFFF'FFFF'FFFFull;
    original.speed = 0.7071068f;
    original.drag = 0.25f;
    original.mass = 1.0e300;
    original.charge = -1.25;
    original.visible = false;
    original.selected = true;
    original.key = u'Q';
    original.altKey = u'\x263A';
    original.mobility = Mobility::Movable;
    original.fallback = static_cast<Mobility>(5);
    original.position = {1.0f, 1.8f, -3.0f};
    original.scale = {2.0f, 2.0f, 2.0f};
    original.range = {-1.0f, 4.0f};
    original.limits.max = 9.0f;
    original.layers[1] = 20;
    original.masks[0] = 0xFF;
    std::copy_n("lamp", 5, original.label);
    std::copy_n("hero", 5, original.title);
    original.shape.kind = ShapeKind::Sphere;
    original.shape.sphere = SphereShape{2.5f};
    const Value tree = Written(&original, FixedDescription);

    Fixed read;
    RecordingLogger logger;
    Fenrir::TypeRegistry registry;
    RegisterBridgeTypes(registry);
    const BridgeResult result = FromTree(tree, FixedDescription, registry, Resolvers{}, &read, Path, logger);

    INFO("every field but the reference and entity ones (the resolvers') and the pointer (never written) is off its "
         "default");
    REQUIRE(tree.AsObject().members.size() == 23);
    INFO("the read value writes the same tree as the original, so every field read back equal");
    REQUIRE(Written(&read, FixedDescription) == tree);
    INFO("nothing was defaulted or dropped");
    REQUIRE(result.status == BridgeStatus::Succeeded);
}

TEST_CASE("A struct of every owning kind goes to a tree and back unchanged", "[serialisation][fromtree]")
{
    Owned original;
    original.name = "Grimm";
    original.motto = "";
    original.icon = "ui/icons/grimm.png";
    original.sound = "sfx/grimm.wav";
    original.tags = {4, -8, 15};
    original.ranges = {Range{.min = 2.0f, .max = 1.0f}, Range{.min = 0.0f, .max = 6.0f}};
    original.edge = 0.5f;
    original.reach.reset();
    const Value tree = Written(&original, OwnedDescription);

    Owned read;
    RecordingLogger logger;
    Fenrir::TypeRegistry registry;
    RegisterBridgeTypes(registry);
    const BridgeResult result = FromTree(tree, OwnedDescription, registry, Resolvers{}, &read, Path, logger);

    INFO("every field is off its default, the disengaged reach written null");
    REQUIRE(tree.AsObject().members.size() == 8);
    INFO("the read value writes the same tree as the original, so every field read back equal");
    REQUIRE(Written(&read, OwnedDescription) == tree);
    INFO("nothing was defaulted or dropped");
    REQUIRE(result.status == BridgeStatus::Succeeded);
}

TEST_CASE("A field absent from the tree keeps its default", "[serialisation][fromtree]")
{
    Fixed fixed;
    RecordingLogger logger;
    const BridgeResult result = Read("{\n  score: 9,\n}", fixed, logger);

    INFO("score was read, and lives, absent, kept its default 3");
    REQUIRE(fixed.score == 9);
    REQUIRE(fixed.lives == 3);
    INFO("an absent field is no failure: no count, no diagnostic");
    REQUIRE(result.status == BridgeStatus::Succeeded);
    REQUIRE(result.diagnostics.empty());
}

TEST_CASE("A value of the wrong kind takes its field's default, with one located error", "[serialisation][fromtree]")
{
    Fixed fixed;
    RecordingLogger logger;
    const BridgeResult result = Read("{\n  score: 9,\n  lives: \"three\",\n}", fixed, logger);

    INFO("lives holds a string, so keeps its default 3, while score beside it reads");
    REQUIRE(fixed.lives == 3);
    REQUIRE(fixed.score == 9);
    INFO("it is counted once in defaulted, which makes the read partial");
    REQUIRE(result.defaulted == 1);
    REQUIRE(result.status == BridgeStatus::Partial);
    REQUIRE(result.diagnostics.size() == 1);
    INFO(Where(result.diagnostics[0]) << ": " << result.diagnostics[0].message);
    INFO("the error is at the string, line 3 column 10");
    REQUIRE(result.diagnostics[0].severity == DiagnosticSeverity::Error);
    REQUIRE(result.diagnostics[0].path == Path);
    REQUIRE(result.diagnostics[0].line == 3);
    REQUIRE(result.diagnostics[0].column == 10);
}

TEST_CASE("An integer out of its kind's range takes its field's default, with one located error",
          "[serialisation][fromtree]")
{
    Fixed fixed;
    RecordingLogger logger;
    const BridgeResult result = Read("{\n  team: 300,\n}", fixed, logger);

    INFO("300 does not fit team's u8, so team keeps its default 0");
    REQUIRE(fixed.team == 0);
    INFO("the rejected value is counted once in defaulted");
    REQUIRE(result.defaulted == 1);
    REQUIRE(result.diagnostics.size() == 1);
    INFO(Where(result.diagnostics[0]) << ": " << result.diagnostics[0].message);
    INFO("the error is at the number, line 2 column 9");
    REQUIRE(result.diagnostics[0].line == 2);
    REQUIRE(result.diagnostics[0].column == 9);
}

TEST_CASE("A negative number in an unsigned field takes its field's default, with one located error",
          "[serialisation][fromtree]")
{
    Fixed fixed;
    RecordingLogger logger;
    const BridgeResult result = Read("{\n  seed: -1,\n}", fixed, logger);

    INFO("-1 is below seed's u64 range, so seed keeps its default 7");
    REQUIRE(fixed.seed == 7);
    INFO("the rejected value is counted once in defaulted");
    REQUIRE(result.defaulted == 1);
    REQUIRE(result.diagnostics.size() == 1);
    INFO(Where(result.diagnostics[0]) << ": " << result.diagnostics[0].message);
    REQUIRE(result.diagnostics[0].line == 2);
    REQUIRE(result.diagnostics[0].column == 9);
}

TEST_CASE("A float in an integer field takes its field's default, with one located error", "[serialisation][fromtree]")
{
    Fixed fixed;
    RecordingLogger logger;
    const BridgeResult result = Read("{\n  lives: 2.5,\n}", fixed, logger);

    INFO("2.5 is no i32, so lives keeps its default 3");
    REQUIRE(fixed.lives == 3);
    INFO("the rejected value is counted once in defaulted");
    REQUIRE(result.defaulted == 1);
    REQUIRE(result.diagnostics.size() == 1);
    INFO(Where(result.diagnostics[0]) << ": " << result.diagnostics[0].message);
    REQUIRE(result.diagnostics[0].line == 2);
    REQUIRE(result.diagnostics[0].column == 10);
}

TEST_CASE("An unknown enumerator takes its field's default, with one located error", "[serialisation][fromtree]")
{
    Fixed fixed;
    RecordingLogger logger;
    const BridgeResult result = Read("{\n  mobility: \"Flying\",\n}", fixed, logger);

    INFO("Mobility has no Flying, so mobility keeps its default Static");
    REQUIRE(fixed.mobility == Mobility::Static);
    INFO("the rejected value is counted once in defaulted");
    REQUIRE(result.defaulted == 1);
    REQUIRE(result.diagnostics.size() == 1);
    INFO(Where(result.diagnostics[0]) << ": " << result.diagnostics[0].message);
    REQUIRE(result.diagnostics[0].line == 2);
    REQUIRE(result.diagnostics[0].column == 13);
}

TEST_CASE("Null in a field that does not admit it takes its default, with one located error",
          "[serialisation][fromtree]")
{
    Fixed fixed;
    RecordingLogger logger;
    const BridgeResult result = Read("{\n  speed: null,\n}", fixed, logger);

    INFO("speed is an f32, which null is not, so it keeps its default 1.5");
    REQUIRE(fixed.speed == 1.5f);
    INFO("the rejected value is counted once in defaulted");
    REQUIRE(result.defaulted == 1);
    REQUIRE(result.diagnostics.size() == 1);
    INFO(Where(result.diagnostics[0]) << ": " << result.diagnostics[0].message);
    REQUIRE(result.diagnostics[0].line == 2);
    REQUIRE(result.diagnostics[0].column == 10);
}

TEST_CASE("A finite number beyond a float's range takes its field's default, with one located error",
          "[serialisation][fromtree]")
{
    Fixed fixed;
    RecordingLogger logger;
    const BridgeResult result = Read("{\n  speed: 1e300,\n}", fixed, logger);

    INFO("1e300 has no f32, so speed keeps its default 1.5 rather than becoming Infinity");
    REQUIRE(fixed.speed == 1.5f);
    INFO("the rejected value is counted once in defaulted, its error at line 2 column 10");
    REQUIRE(result.defaulted == 1);
    REQUIRE(result.diagnostics.size() == 1);
    REQUIRE(result.diagnostics[0].line == 2);
    REQUIRE(result.diagnostics[0].column == 10);
}

TEST_CASE("A bound type of the wrong length takes its field's default, with one located error",
          "[serialisation][fromtree]")
{
    Fixed fixed;
    RecordingLogger logger;
    const BridgeResult result = Read("{\n  scale: [4, 5],\n}", fixed, logger);

    INFO("a Vec3 has three components, so two leave scale at its default 1, 1, 1, none of them written");
    REQUIRE(fixed.scale.x == 1.0f);
    REQUIRE(fixed.scale.y == 1.0f);
    INFO("the rejected value is counted once in defaulted");
    REQUIRE(result.defaulted == 1);
    REQUIRE(result.diagnostics.size() == 1);
    INFO(Where(result.diagnostics[0]) << ": " << result.diagnostics[0].message);
    REQUIRE(result.diagnostics[0].line == 2);
    REQUIRE(result.diagnostics[0].column == 10);
}

TEST_CASE("A bad list element takes the element type's default, with one located error", "[serialisation][fromtree]")
{
    Owned owned;
    RecordingLogger logger;
    const BridgeResult result = Read("{\n  tags: [1, true, 3],\n}", owned, logger);

    INFO("the list keeps all three places, the bool's taking an i32's default 0");
    REQUIRE(owned.tags == std::vector<std::int32_t>{1, 0, 3});
    INFO("the rejected value is counted once in defaulted");
    REQUIRE(result.defaulted == 1);
    REQUIRE(result.diagnostics.size() == 1);
    INFO(Where(result.diagnostics[0]) << ": " << result.diagnostics[0].message);
    INFO("the error is at the bool, line 2 column 13");
    REQUIRE(result.diagnostics[0].line == 2);
    REQUIRE(result.diagnostics[0].column == 13);
}

TEST_CASE("An unknown key is warned and dropped, counted in unknownFields", "[serialisation][fromtree]")
{
    Fixed fixed;
    RecordingLogger logger;
    const BridgeResult result = Read("{\n  lives: 5,\n  wings: 2,\n}", fixed, logger);

    INFO("lives beside the unknown key still reads");
    REQUIRE(fixed.lives == 5);
    INFO("wings matches no field, so it is counted in unknownFields, and the read is partial");
    REQUIRE(result.unknownFields == 1);
    REQUIRE(result.defaulted == 0);
    REQUIRE(result.status == BridgeStatus::Partial);
    REQUIRE(result.diagnostics.size() == 1);
    INFO(Where(result.diagnostics[0]) << ": " << result.diagnostics[0].message);
    INFO("the warning is at the key's value, line 3 column 10");
    REQUIRE(result.diagnostics[0].severity == DiagnosticSeverity::Warning);
    REQUIRE(result.diagnostics[0].line == 3);
    REQUIRE(result.diagnostics[0].column == 10);
}

TEST_CASE("A field's former name alone reads into the field", "[serialisation][fromtree]")
{
    Renamed renamed;
    RecordingLogger logger;
    const BridgeResult result = Read("{\n  hp: 40,\n}", renamed, logger);

    INFO("hp is health's @formerly name, so a file written before the rename reads into health");
    REQUIRE(renamed.health == 40);
    INFO("a former name is no failure: no count, no diagnostic");
    REQUIRE(result.status == BridgeStatus::Succeeded);
    REQUIRE(result.diagnostics.empty());
}

TEST_CASE("With both a field's names present the current one wins", "[serialisation][fromtree]")
{
    Renamed renamed;
    RecordingLogger logger;
    Read("{\n  hp: 10,\n  health: 40,\n}", renamed, logger);

    INFO("health is read and hp is not, though hp comes first");
    REQUIRE(renamed.health == 40);
}

TEST_CASE("A former name beside the current one is dropped with a located warning", "[serialisation][fromtree]")
{
    Renamed renamed;
    RecordingLogger logger;
    const BridgeResult result = Read("{\n  hp: 10,\n  health: 40,\n}", renamed, logger);

    INFO("the dropped hp gives the one diagnostic");
    REQUIRE(result.diagnostics.size() == 1);
    INFO(Where(result.diagnostics[0]) << ": " << result.diagnostics[0].message);
    INFO("the warning is at hp's value, line 2 column 7");
    REQUIRE(result.diagnostics[0].severity == DiagnosticSeverity::Warning);
    REQUIRE(result.diagnostics[0].line == 2);
    REQUIRE(result.diagnostics[0].column == 7);
    INFO("the dropped former name is no unknown field");
    REQUIRE(result.unknownFields == 0);
}

TEST_CASE("Of two former names present the first a field lists is read, and the other dropped with a warning",
          "[serialisation][fromtree]")
{
    Renamed renamed;
    RecordingLogger logger;
    const BridgeResult result = Read("{\n  life: 20,\n  hp: 10,\n}", renamed, logger);

    INFO("health lists hp before life, so hp is read though life comes first in the file");
    REQUIRE(renamed.health == 10);
    INFO("life is dropped with one warning, at its value on line 2 column 9");
    REQUIRE(result.diagnostics.size() == 1);
    REQUIRE(result.diagnostics[0].severity == DiagnosticSeverity::Warning);
    REQUIRE(result.diagnostics[0].line == 2);
    REQUIRE(result.diagnostics[0].column == 9);
}

TEST_CASE("A field's current name is its own, though another field was once called it", "[serialisation][fromtree]")
{
    Renamed renamed;
    RecordingLogger logger;
    Read("{\n  shield: 5,\n}", renamed, logger);

    INFO("shield is the shield field's current name and armour's former one: the current name wins");
    REQUIRE(renamed.shield == 5);
    REQUIRE(renamed.armour == 0);
}

TEST_CASE("Null in an optional reads as disengaged", "[serialisation][fromtree]")
{
    Owned owned;
    RecordingLogger logger;
    const BridgeResult result = Read("{\n  reach: null,\n}", owned, logger);

    INFO("reach holds 2 by default, and the null ToTree writes for a disengaged one empties it");
    REQUIRE_FALSE(owned.reach.has_value());
    INFO("null is what an optional admits: no diagnostic");
    REQUIRE(result.status == BridgeStatus::Succeeded);
    REQUIRE(result.diagnostics.empty());
}

TEST_CASE("An optional absent from the tree keeps its default", "[serialisation][fromtree]")
{
    Owned owned;
    RecordingLogger logger;
    const BridgeResult result = Read("{\n  edge: 0.5,\n}", owned, logger);

    INFO("edge was disengaged and reads 0.5; reach, absent, keeps its default 2");
    REQUIRE(owned.edge == std::optional<float>(0.5f));
    REQUIRE(owned.reach == std::optional<float>(2.0f));
    INFO("nothing was defaulted or dropped");
    REQUIRE(result.status == BridgeStatus::Succeeded);
}

TEST_CASE("A bad value in an optional leaves the optional at its default", "[serialisation][fromtree]")
{
    Owned owned;
    RecordingLogger logger;
    const BridgeResult result = Read("{\n  edge: \"wide\",\n}", owned, logger);

    INFO("edge was disengaged by default and stays so, rather than holding a value-initialised float");
    REQUIRE_FALSE(owned.edge.has_value());
    INFO("the rejected value is counted once in defaulted");
    REQUIRE(result.defaulted == 1);
}

TEST_CASE("A fixed string too long for its array is cut, with one located warning", "[serialisation][fromtree]")
{
    Fixed fixed;
    RecordingLogger logger;
    const BridgeResult result = Read("{\n  label: \"much too long\",\n}", fixed, logger);

    INFO("label is a char[8], so it keeps the first 7 bytes");
    REQUIRE(std::string(fixed.label) == "much to");
    REQUIRE(result.diagnostics.size() == 1);
    INFO(Where(result.diagnostics[0]) << ": " << result.diagnostics[0].message);
    INFO("the warning is at the string, line 2 column 10");
    REQUIRE(result.diagnostics[0].severity == DiagnosticSeverity::Warning);
    REQUIRE(result.diagnostics[0].line == 2);
    REQUIRE(result.diagnostics[0].column == 10);
}

TEST_CASE("A fixed string that fits is not reported cut, though a NUL in it ends the text", "[serialisation][fromtree]")
{
    Fixed fixed;
    RecordingLogger logger;
    const BridgeResult result = Read("{\n  label: \"ab\\u0000cd\",\n}", fixed, logger);

    INFO("five bytes fit label's seven, so nothing was cut and nothing is reported");
    REQUIRE(result.diagnostics.empty());
}

TEST_CASE("A tagged union read with kind Sphere holds a sphere with the file's radius", "[serialisation][fromtree]")
{
    Fixed fixed;
    RecordingLogger logger;
    const BridgeResult result = Read("{\n  shape: {kind: \"Sphere\", radius: 4},\n}", fixed, logger);

    INFO("kind selected the sphere before radius filled it");
    REQUIRE(fixed.shape.kind == ShapeKind::Sphere);
    REQUIRE(fixed.shape.sphere.radius == 4.0f);
    INFO("nothing was defaulted or dropped");
    REQUIRE(result.status == BridgeStatus::Succeeded);
}

TEST_CASE("A tagged union with an unknown kind holds its default alternative, with one located error",
          "[serialisation][fromtree]")
{
    Fixed fixed;
    RecordingLogger logger;
    const BridgeResult result = Read("{\n  shape: {kind: \"Cone\", radius: 4},\n}", fixed, logger);

    INFO("ShapeKind has no Cone, so the union keeps its default box, its half extents untouched");
    REQUIRE(fixed.shape.kind == ShapeKind::Box);
    REQUIRE(fixed.shape.box.halfExtents.x == 0.5f);
    INFO("one diagnostic, counted in defaulted, and none for the radius the box lacks");
    REQUIRE(result.defaulted == 1);
    REQUIRE(result.unknownFields == 0);
    REQUIRE(result.diagnostics.size() == 1);
    INFO(Where(result.diagnostics[0]) << ": " << result.diagnostics[0].message);
    INFO("the error is at kind's value, line 2 column 17");
    REQUIRE(result.diagnostics[0].line == 2);
    REQUIRE(result.diagnostics[0].column == 17);
}

TEST_CASE("A tagged union with no kind holds its default alternative, with one located error",
          "[serialisation][fromtree]")
{
    Fixed fixed;
    RecordingLogger logger;
    const BridgeResult result = Read("{\n  shape: {radius: 4},\n}", fixed, logger);

    INFO("without kind the union keeps its default box");
    REQUIRE(fixed.shape.kind == ShapeKind::Box);
    INFO("the rejected value is counted once in defaulted");
    REQUIRE(result.defaulted == 1);
    REQUIRE(result.diagnostics.size() == 1);
    INFO(Where(result.diagnostics[0]) << ": " << result.diagnostics[0].message);
    INFO("the error is at the union's object, line 2 column 10");
    REQUIRE(result.diagnostics[0].line == 2);
    REQUIRE(result.diagnostics[0].column == 10);
}

TEST_CASE("A type the registry lacks returns UnknownType and touches nothing", "[serialisation][fromtree]")
{
    Fixed fixed;
    fixed.lives = 11;
    RecordingLogger logger;
    const Fenrir::TypeRegistry empty;
    const BridgeResult result =
        FromTree(TreeOf("{\n  lives: 5,\n}"), FixedDescription, empty, Resolvers{}, &fixed, Path, logger);

    INFO("the registry has no BridgeTest::Fixed, so nothing is read: lives keeps 11");
    REQUIRE(result.status == BridgeStatus::UnknownType);
    REQUIRE(fixed.lives == 11);
    INFO("the caller reports an unknown type its own way, so the bridge logs nothing");
    REQUIRE(result.diagnostics.empty());
    REQUIRE(logger.errors.empty());
    REQUIRE(logger.warnings.empty());
}

TEST_CASE("An unknown type's subtree, kept by the caller, writes back unchanged", "[serialisation][fromtree]")
{
    const std::string text = "{\n  // kept as it was\n  lives: 5,\n  wings: [1, 2],\n}\n";
    const Value tree = TreeOf(text);
    Fixed fixed;
    RecordingLogger logger;
    const Fenrir::TypeRegistry empty;
    const BridgeResult result = FromTree(tree, FixedDescription, empty, Resolvers{}, &fixed, Path, logger);

    INFO("the read refused the type, leaving the tree the caller holds as it read");
    REQUIRE(result.status == BridgeStatus::UnknownType);
    INFO("writing the kept tree gives the bytes it was read from, its comment and unknown key included");
    REQUIRE(WriteText(tree) == text);
}

TEST_CASE("Each diagnostic is logged once, errors as errors and warnings as warnings", "[serialisation][fromtree]")
{
    Fixed fixed;
    RecordingLogger logger;
    const BridgeResult result = Read("{\n  lives: true,\n  wings: 2,\n}", fixed, logger);

    INFO("the bad lives and the unknown wings give one diagnostic each");
    REQUIRE(result.diagnostics.size() == 2);
    INFO("the bad lives is logged through Error once, as path:line:column: message");
    REQUIRE(logger.errors.size() == 1);
    REQUIRE(logger.errors[0] == FormatDiagnostic(result.diagnostics[0]));
    INFO("the unknown wings is logged through Warn once");
    REQUIRE(logger.warnings.size() == 1);
    REQUIRE(logger.warnings[0] == FormatDiagnostic(result.diagnostics[1]));
}

TEST_CASE("A root that is not an object leaves the whole value at its default", "[serialisation][fromtree]")
{
    Fixed fixed;
    RecordingLogger logger;
    const BridgeResult result = Read("[1, 2]", fixed, logger);

    INFO("a struct reads from an object alone; the array is one value of the wrong kind");
    REQUIRE(fixed.lives == 3);
    INFO("the rejected value is counted once in defaulted");
    REQUIRE(result.defaulted == 1);
    REQUIRE(result.diagnostics.size() == 1);
    INFO(Where(result.diagnostics[0]) << ": " << result.diagnostics[0].message);
    REQUIRE(result.diagnostics[0].line == 1);
    REQUIRE(result.diagnostics[0].column == 1);
}

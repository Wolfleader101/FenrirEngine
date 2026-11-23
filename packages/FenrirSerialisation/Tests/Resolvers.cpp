// Resolvers (the spec, "The bridge"): `reference` and `entity` fields cross the bridge as the strings the caller's
// callbacks give and take, so the bridge never learns what an asset path or an entity id means (ADR-0076 §2, ADR-0078
// §3). Recording lambdas observe each call with the field it was handed; `null` is "none" both ways, and a field no
// callback names is left empty and counted in `unresolved`. A `path` field is a plain string, no callback.
#include "FenrirSerialisation/Bridge.hpp"
#include "FenrirSerialisation/Diagnostic.hpp"
#include "FenrirSerialisation/Text.hpp"
#include "FenrirSerialisation/Value.hpp"

#include "BridgeTypes.hpp"
#include "SerialisationTest.hpp"

#include "FenrirTypes/TypeRegistry.hpp"
#include "FenrirTypes/ValueRef.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace Fenrir::Serialisation;
using namespace BridgeTest;
using Fenrir::EntityValue;
using Fenrir::ReferenceValue;
using SerialisationTest::RecordingLogger;

namespace
{
    constexpr std::string_view Path = "test.json5";

    // One callback call: the field's name, the target type its `type.target` names, and the string handed or given.
    struct Call
    {
        std::string field;
        std::string target;
        std::string text;

        friend bool operator==(const Call&, const Call&) = default;
    };

    // The calls as one string, so a failure shows them all.
    std::string Describe(const std::vector<Call>& calls)
    {
        std::string text;
        for (const Call& call : calls)
            text += "(" + call.field + ", " + call.target + ", " + call.text + ") ";
        return text;
    }

    // The tree of `value`, written through `resolvers` against a registry holding the bridge's test types.
    Value Tree(const void* value, const Fenrir::TypeDescription& type, const Resolvers& resolvers)
    {
        Fenrir::TypeRegistry registry;
        RegisterBridgeTypes(registry);
        return ToTree(value, type, registry, resolvers);
    }

    // The tree `text` reads as; the text must be valid JSON5.
    Value TreeOf(std::string_view text)
    {
        RecordingLogger logger;
        ReadTreeResult read = ReadText(text, Path, logger);
        return read.tree.value_or(Value{});
    }

    // Reads `text` into `value` through `resolvers`.
    BridgeResult Read(std::string_view text, const Fenrir::TypeDescription& type, void* value,
                      const Resolvers& resolvers, RecordingLogger& logger)
    {
        Fenrir::TypeRegistry registry;
        RegisterBridgeTypes(registry);
        return FromTree(TreeOf(text), type, registry, resolvers, value, Path, logger);
    }

    // An object holding the members given, in order.
    Value ObjectOf(std::initializer_list<std::pair<const char*, Value>> members)
    {
        Object object;
        for (const auto& [key, value] : members)
            object.Add(key, value);
        return object;
    }

    Value ArrayOf(std::initializer_list<Value> elements)
    {
        Array array;
        for (const Value& element : elements)
            array.Add(element);
        return array;
    }

    // A diagnostic's location and severity as one string, so a failure shows all three.
    std::string Where(const Diagnostic& diagnostic)
    {
        return std::string(diagnostic.severity == DiagnosticSeverity::Error ? "error" : "warning") + " at " +
               diagnostic.path + ":" + std::to_string(diagnostic.line) + ":" + std::to_string(diagnostic.column);
    }

    // Resolvers that record every call and name a reference `asset<index>` and an entity `e<id>`, both ways.
    Resolvers Recording(std::vector<Call>& calls)
    {
        Resolvers resolvers;
        resolvers.writeReference = [&calls](ReferenceValue reference, const Fenrir::FieldInfo& field) {
            std::string path = "asset" + std::to_string(reference.index);
            calls.push_back({std::string(field.name), std::string(field.type.target), path});
            return path;
        };
        resolvers.readReference = [&calls](std::string_view path,
                                           const Fenrir::FieldInfo& field) -> std::optional<ReferenceValue> {
            calls.push_back({std::string(field.name), std::string(field.type.target), std::string(path)});
            return ReferenceValue{.index = static_cast<std::uint32_t>(std::stoul(std::string(path.substr(5)))),
                                  .generation = 1};
        };
        resolvers.writeEntity = [&calls](EntityValue entity, const Fenrir::FieldInfo& field) {
            std::string id = "e" + std::to_string(entity.id);
            calls.push_back({std::string(field.name), std::string(field.type.target), id});
            return id;
        };
        resolvers.readEntity = [&calls](std::string_view id,
                                        const Fenrir::FieldInfo& field) -> std::optional<EntityValue> {
            calls.push_back({std::string(field.name), std::string(field.type.target), std::string(id)});
            return EntityValue{.id = static_cast<std::uint32_t>(std::stoul(std::string(id.substr(1)))),
                               .list = nullptr};
        };
        return resolvers;
    }
} // namespace

TEST_CASE("Each reference off its default is written as the path writeReference gives it", "[serialisation][resolvers]")
{
    Fixed fixed;
    fixed.mesh = HandleLayout{3, 1};
    fixed.material = HandleLayout{9, 2};
    std::vector<Call> calls;

    const Value tree = Tree(&fixed, FixedDescription, Recording(calls));

    INFO("calls: " << Describe(calls));
    INFO("writeReference is called once for mesh and once for material, each with its field's target type");
    REQUIRE(calls ==
            std::vector<Call>{{"mesh", "BridgeTest::Mesh", "asset3"}, {"material", "BridgeTest::Material", "asset9"}});
    INFO("each field holds the path its call gave");
    REQUIRE(tree == ObjectOf({{"mesh", "asset3"}, {"material", "asset9"}}));
}

TEST_CASE("Each reference in a file is read as the handle readReference gives its path", "[serialisation][resolvers]")
{
    Fixed fixed;
    std::vector<Call> calls;
    RecordingLogger logger;

    const BridgeResult result =
        Read("{ mesh: 'asset3', material: 'asset9' }", FixedDescription, &fixed, Recording(calls), logger);

    INFO("calls: " << Describe(calls));
    INFO("readReference is called once for mesh and once for material, each with its path and target type");
    REQUIRE(calls ==
            std::vector<Call>{{"mesh", "BridgeTest::Mesh", "asset3"}, {"material", "BridgeTest::Material", "asset9"}});
    INFO("each field holds the handle its call gave");
    REQUIRE(fixed.mesh.index == 3);
    REQUIRE(fixed.mesh.generation == 1);
    REQUIRE(fixed.material.index == 9);
    INFO("both resolved, so nothing was counted");
    REQUIRE(result.status == BridgeStatus::Succeeded);
}

TEST_CASE("Each entity off its default is written as the id writeEntity gives it", "[serialisation][resolvers]")
{
    Fixed fixed;
    fixed.target = EntityLayout{4, nullptr};
    fixed.owner = EntityLayout{12, nullptr};
    std::vector<Call> calls;

    const Value tree = Tree(&fixed, FixedDescription, Recording(calls));

    INFO("calls: " << Describe(calls));
    INFO("writeEntity is called once for target and once for owner");
    REQUIRE(calls == std::vector<Call>{{"target", "", "e4"}, {"owner", "", "e12"}});
    INFO("each field holds the id its call gave");
    REQUIRE(tree == ObjectOf({{"target", "e4"}, {"owner", "e12"}}));
}

TEST_CASE("Each entity in a file is read as the entity readEntity gives its id", "[serialisation][resolvers]")
{
    Fixed fixed;
    std::vector<Call> calls;
    RecordingLogger logger;

    const BridgeResult result =
        Read("{ target: 'e4', owner: 'e12' }", FixedDescription, &fixed, Recording(calls), logger);

    INFO("calls: " << Describe(calls));
    INFO("readEntity is called once for target and once for owner, each with its id");
    REQUIRE(calls == std::vector<Call>{{"target", "", "e4"}, {"owner", "", "e12"}});
    INFO("each field holds the entity its call gave");
    REQUIRE(fixed.target.id == 4);
    REQUIRE(fixed.owner.id == 12);
    INFO("both resolved, so nothing was counted");
    REQUIRE(result.status == BridgeStatus::Succeeded);
}

TEST_CASE("A reference or entity in a fixed array is handed to its callback element by element, with the element's "
          "target type",
          "[serialisation][resolvers]")
{
    Linked linked;
    linked.parts[0] = HandleLayout{5, 1};
    linked.crew[1] = EntityLayout{7, nullptr};
    std::vector<Call> calls;

    const Value tree = Tree(&linked, LinkedDescription, Recording(calls));

    INFO("calls: " << Describe(calls));
    INFO("parts[1] is the empty handle and crew[0] the null entity, yet each element is handed over, under its "
         "array's name and its element's target type");
    REQUIRE(calls == std::vector<Call>{{"parts", "BridgeTest::Mesh", "asset5"},
                                       {"parts", "BridgeTest::Mesh", "asset0"},
                                       {"crew", "", "e4294967295"},
                                       {"crew", "", "e7"}});
    INFO("each element holds the string its call gave");
    REQUIRE(tree == ObjectOf({{"parts", ArrayOf({"asset5", "asset0"})}, {"crew", ArrayOf({"e4294967295", "e7"})}}));
}

TEST_CASE("An empty path from writeReference is written as null", "[serialisation][resolvers]")
{
    Fixed fixed;
    fixed.mesh = HandleLayout{3, 1};
    Resolvers resolvers;
    resolvers.writeReference = [](ReferenceValue, const Fenrir::FieldInfo&) { return std::string(); };

    INFO("mesh is a live handle its resolver names no path for");
    REQUIRE(Tree(&fixed, FixedDescription, resolvers) == ObjectOf({{"mesh", nullptr}}));
}

TEST_CASE("An empty id from writeEntity is written as null", "[serialisation][resolvers]")
{
    Fixed fixed;
    fixed.target = EntityLayout{4, nullptr};
    Resolvers resolvers;
    resolvers.writeEntity = [](EntityValue, const Fenrir::FieldInfo&) { return std::string(); };

    INFO("target is an entity its resolver names no id for, such as one outside the file");
    REQUIRE(Tree(&fixed, FixedDescription, resolvers) == ObjectOf({{"target", nullptr}}));
}

TEST_CASE("A null reference reads as the empty handle with no diagnostic and no call", "[serialisation][resolvers]")
{
    Fixed fixed;
    fixed.mesh = HandleLayout{3, 1};
    std::vector<Call> calls;
    RecordingLogger logger;

    const BridgeResult result = Read("{ mesh: null }", FixedDescription, &fixed, Recording(calls), logger);

    INFO("mesh held a live handle before the read");
    REQUIRE(fixed.mesh.index == 0);
    REQUIRE(fixed.mesh.generation == 0);
    INFO("null is none: readReference is not asked");
    REQUIRE(calls.empty());
    INFO("null is what a reference admits: no diagnostic, nothing counted");
    REQUIRE(result.diagnostics.empty());
    REQUIRE(result.status == BridgeStatus::Succeeded);
}

TEST_CASE("A null entity reads as the null entity with no diagnostic and no call", "[serialisation][resolvers]")
{
    Fixed fixed;
    fixed.target = EntityLayout{4, nullptr};
    std::vector<Call> calls;
    RecordingLogger logger;

    const BridgeResult result = Read("{ target: null }", FixedDescription, &fixed, Recording(calls), logger);

    INFO("target held entity 4 before the read");
    REQUIRE(fixed.target.id == 0xFFFFFFFFu);
    INFO("null is none: readEntity is not asked");
    REQUIRE(calls.empty());
    INFO("null is what an entity admits: no diagnostic, nothing counted");
    REQUIRE(result.diagnostics.empty());
    REQUIRE(result.status == BridgeStatus::Succeeded);
}

TEST_CASE("A path readReference names nothing for leaves the reference empty, counted in unresolved",
          "[serialisation][resolvers]")
{
    Fixed fixed;
    fixed.mesh = HandleLayout{3, 1};
    Resolvers resolvers;
    resolvers.readReference = [](std::string_view, const Fenrir::FieldInfo&) -> std::optional<ReferenceValue> {
        return std::nullopt;
    };
    RecordingLogger logger;

    const BridgeResult result = Read("{\n  mesh: 'models/gone.glb',\n}", FixedDescription, &fixed, resolvers, logger);

    INFO("mesh held a live handle before the read, and is left empty");
    REQUIRE(fixed.mesh.index == 0);
    REQUIRE(fixed.mesh.generation == 0);
    INFO("one unresolved field, nothing defaulted");
    REQUIRE(result.unresolved == 1);
    REQUIRE(result.defaulted == 0);
    REQUIRE(result.status == BridgeStatus::Partial);
    INFO("one error, at the path's node");
    REQUIRE(result.diagnostics.size() == 1);
    INFO(Where(result.diagnostics.front()) << ": " << result.diagnostics.front().message);
    REQUIRE(Where(result.diagnostics.front()) == "error at test.json5:2:9");
    REQUIRE(logger.errors.size() == 1);
}

TEST_CASE("An id readEntity names nothing for leaves the entity null, counted in unresolved",
          "[serialisation][resolvers]")
{
    Fixed fixed;
    fixed.target = EntityLayout{4, nullptr};
    Resolvers resolvers;
    resolvers.readEntity = [](std::string_view, const Fenrir::FieldInfo&) -> std::optional<EntityValue> {
        return std::nullopt;
    };
    RecordingLogger logger;

    const BridgeResult result = Read("{\n  target: 'zz9',\n}", FixedDescription, &fixed, resolvers, logger);

    INFO("target held entity 4 before the read, and is left the null entity");
    REQUIRE(fixed.target.id == 0xFFFFFFFFu);
    INFO("one unresolved field, nothing defaulted");
    REQUIRE(result.unresolved == 1);
    REQUIRE(result.defaulted == 0);
    REQUIRE(result.status == BridgeStatus::Partial);
    INFO("one error, at the id's node");
    REQUIRE(result.diagnostics.size() == 1);
    INFO(Where(result.diagnostics.front()) << ": " << result.diagnostics.front().message);
    REQUIRE(Where(result.diagnostics.front()) == "error at test.json5:2:11");
}

TEST_CASE("A reference read with no readReference is left empty, counted in unresolved", "[serialisation][resolvers]")
{
    Fixed fixed;
    fixed.mesh = HandleLayout{3, 1};
    RecordingLogger logger;

    const BridgeResult result =
        Read("{\n  mesh: 'models/sword.glb',\n}", FixedDescription, &fixed, Resolvers{}, logger);

    INFO("mesh held a live handle before the read, and is left empty");
    REQUIRE(fixed.mesh.index == 0);
    REQUIRE(fixed.mesh.generation == 0);
    INFO("one unresolved field with one diagnostic");
    REQUIRE(result.unresolved == 1);
    REQUIRE(result.status == BridgeStatus::Partial);
    REQUIRE(result.diagnostics.size() == 1);
}

TEST_CASE("An entity read with no readEntity is left null, counted in unresolved", "[serialisation][resolvers]")
{
    Fixed fixed;
    fixed.target = EntityLayout{4, nullptr};
    RecordingLogger logger;

    const BridgeResult result = Read("{\n  target: 'a7k2q',\n}", FixedDescription, &fixed, Resolvers{}, logger);

    INFO("target held entity 4 before the read, and is left the null entity");
    REQUIRE(fixed.target.id == 0xFFFFFFFFu);
    INFO("one unresolved field with one diagnostic");
    REQUIRE(result.unresolved == 1);
    REQUIRE(result.status == BridgeStatus::Partial);
    REQUIRE(result.diagnostics.size() == 1);
}

TEST_CASE("A reference that is neither a string nor null takes its default", "[serialisation][resolvers]")
{
    Fixed fixed;
    std::vector<Call> calls;
    RecordingLogger logger;

    const BridgeResult result = Read("{\n  mesh: 42,\n}", FixedDescription, &fixed, Recording(calls), logger);

    INFO("a number names no asset, so readReference is not asked");
    REQUIRE(calls.empty());
    INFO("mesh keeps its default, the empty handle, counted in defaulted and not unresolved");
    REQUIRE(fixed.mesh.index == 0);
    REQUIRE(result.defaulted == 1);
    REQUIRE(result.unresolved == 0);
    INFO("one error, at the number");
    REQUIRE(result.diagnostics.size() == 1);
    INFO(Where(result.diagnostics.front()) << ": " << result.diagnostics.front().message);
    REQUIRE(Where(result.diagnostics.front()) == "error at test.json5:2:9");
}

TEST_CASE("A path field round-trips its text through a file, non-ASCII UTF-8 included", "[serialisation][resolvers]")
{
    Owned original;
    // ui/icônes/épée-ß-剣.png, spelt in escapes so the source's encoding cannot change it: two-, and three-byte
    // sequences.
    const std::u8string icon = u8"ui/icônes/épée-ß-剣.png";
    original.icon = std::filesystem::path(icon);
    std::vector<Call> calls;
    const std::string text = WriteText(Tree(&original, OwnedDescription, Recording(calls)));

    Owned read;
    RecordingLogger logger;
    const BridgeResult result = Read(text, OwnedDescription, &read, Recording(calls), logger);

    INFO("file: " << text);
    INFO("the path's UTF-8 text came back unchanged");
    REQUIRE(read.icon.generic_u8string() == icon);
    INFO("a path is a plain string: no callback is asked either way");
    REQUIRE(calls.empty());
    INFO("nothing was counted");
    REQUIRE(result.status == BridgeStatus::Succeeded);
}

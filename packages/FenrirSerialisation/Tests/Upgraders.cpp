// Upgraders (the spec, "Upgraders"): a tree written at an older version of its type is upgraded to the type's version,
// one step at a time, by the upgraders the type registered, before FromTree reads it. There is no downgrade.
#include "FenrirSerialisation/Bridge.hpp"
#include "FenrirSerialisation/Diagnostic.hpp"
#include "FenrirSerialisation/Text.hpp"
#include "FenrirSerialisation/Upgrade.hpp"
#include "FenrirSerialisation/Value.hpp"

#include "SerialisationTest.hpp"

#include "FenrirTypes/FieldInfo.hpp"
#include "FenrirTypes/Kind.hpp"
#include "FenrirTypes/TypeDescription.hpp"
#include "FenrirTypes/TypeRegistry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace Fenrir::Serialisation;
using SerialisationTest::RecordingLogger;
using SerialisationTest::TreeOf;

namespace
{
    // A sword at version 3. At version 1 its `damage` was `attack`; version 2 renamed it `power`, and version 3 named
    // it `damage`, so only the two steps run in order reach the field the type reads.
    struct Sword
    {
        float damage = 1.0f;
        float weight = 2.0f;
    };

    constexpr Fenrir::ValueType F32{.kind = Fenrir::Kind::F32};

    const Fenrir::FieldInfo SwordFields[] = {
        {.name = "damage", .type = F32, .offset = offsetof(Sword, damage)},
        {.name = "weight", .type = F32, .offset = offsetof(Sword, weight)},
    };

    constexpr std::string_view SwordFormerly[] = {"MyGame::Blade"};

    const Fenrir::TypeDescription SwordDescription{
        .name = "MyGame::Sword",
        .package = "FenrirSerialisationTests",
        .role = Fenrir::Role::Component,
        .version = 3,
        .formerly = SwordFormerly,
        .size = sizeof(Sword),
        .alignment = alignof(Sword),
        .construct = Fenrir::ConstructValue<Sword>,
        .destroy = Fenrir::DestroyValue<Sword>,
        .fields = SwordFields,
    };

    // A registry holding the sword alone, refusing nothing it is given here.
    Fenrir::TypeRegistry SwordRegistry()
    {
        Fenrir::TypeRegistry registry;
        registry.Register(SwordDescription);
        return registry;
    }

    // Renames the member `from` of an object tree to `to`, keeping its value and place.
    void Rename(Value& tree, std::string_view from, std::string_view to)
    {
        for (Member& member : tree.AsObject().members)
            if (member.key == from)
                member.key = std::string(to);
    }

    // The step that renames the member `from` to `to`, recording the version it stepped from in `steps`.
    Upgrader RenameStep(std::vector<std::uint32_t>& steps, std::string from, std::string to)
    {
        return [&steps, from = std::move(from), to = std::move(to)](std::uint32_t fromVersion, Value& tree) {
            steps.push_back(fromVersion);
            Rename(tree, from, to);
        };
    }

    // The version 1 to 2 step: `attack` became `power`.
    Upgrader AttackToPower(std::vector<std::uint32_t>& steps)
    {
        return RenameStep(steps, "attack", "power");
    }

    // The version 2 to 3 step: `power` became `damage`.
    Upgrader PowerToDamage(std::vector<std::uint32_t>& steps)
    {
        return RenameStep(steps, "power", "damage");
    }

    // The path the tests name the sword's file by.
    constexpr std::string_view Path = "sword.json5";

    // A diagnostic's severity, path and location as one string, so a failure shows all of them.
    std::string Where(const Diagnostic& diagnostic)
    {
        return std::string(diagnostic.severity == DiagnosticSeverity::Error ? "error" : "warning") + " at " +
               diagnostic.path + ":" + std::to_string(diagnostic.line) + ":" + std::to_string(diagnostic.column);
    }

    // The sword at version 1, as a file of that time holds its data.
    constexpr std::string_view SwordAtVersion1 = "{attack: 12.5, weight: 3}";
} // namespace

TEST_CASE("A version 1 sword reaches version 3 through its two upgraders and FromTree then reads it",
          "[serialisation][upgrade]")
{
    Fenrir::TypeRegistry registry = SwordRegistry();
    RecordingLogger logger;
    std::vector<std::uint32_t> steps;
    INFO("each upgrader registers, for a registered type and a version not yet taken");
    REQUIRE(RegisterUpgrader(registry, "MyGame::Sword", 1, AttackToPower(steps), logger));
    REQUIRE(RegisterUpgrader(registry, "MyGame::Sword", 2, PowerToDamage(steps), logger));
    Value tree = TreeOf(SwordAtVersion1);

    const UpgradeResult upgraded = Upgrade(registry, "MyGame::Sword", 1, tree, Path, logger);
    INFO("the upgrade reached the type's version");
    REQUIRE(upgraded.status == UpgradeStatus::Succeeded);
    INFO("the version reached is the type's, 3");
    REQUIRE(upgraded.version == 3);
    INFO("each upgrader stepped once, from version 1 and then from version 2");
    REQUIRE(steps == std::vector<std::uint32_t>{1, 2});

    Sword sword;
    const BridgeResult read = FromTree(tree, SwordDescription, registry, Resolvers{}, &sword, Path, logger);
    INFO("the upgraded tree reads with nothing defaulted or dropped");
    REQUIRE(read.status == BridgeStatus::Succeeded);
    INFO("the field written as `attack` at version 1 reads as `damage`");
    REQUIRE(sword.damage == 12.5f);
    INFO("a field no step touched reads as written");
    REQUIRE(sword.weight == 3.0f);
    INFO("nothing was logged");
    REQUIRE(logger.errors.empty());
    REQUIRE(logger.warnings.empty());
}

TEST_CASE("Upgraders run in version order whatever order they were registered in", "[serialisation][upgrade]")
{
    Fenrir::TypeRegistry registry = SwordRegistry();
    RecordingLogger logger;
    std::vector<std::uint32_t> steps;
    INFO("each upgrader registers, for a registered type and a version not yet taken");
    REQUIRE(RegisterUpgrader(registry, "MyGame::Sword", 2, PowerToDamage(steps), logger));
    REQUIRE(RegisterUpgrader(registry, "MyGame::Sword", 1, AttackToPower(steps), logger));
    Value tree = TreeOf(SwordAtVersion1);

    const UpgradeResult upgraded = Upgrade(registry, "MyGame::Sword", 1, tree, Path, logger);
    INFO("the version 1 step ran before the version 2 step, though the version 2 step was registered first");
    REQUIRE(steps == std::vector<std::uint32_t>{1, 2});
    INFO("so `attack` became `power` and then `damage`");
    REQUIRE(Equals(tree, TreeOf("{damage: 12.5, weight: 3}")));
    INFO("the upgrade reached the type's version");
    REQUIRE(upgraded.version == 3);
}

TEST_CASE("A tree at its type's version runs no upgrader", "[serialisation][upgrade]")
{
    Fenrir::TypeRegistry registry = SwordRegistry();
    RecordingLogger logger;
    std::vector<std::uint32_t> steps;
    INFO("each upgrader registers, for a registered type and a version not yet taken");
    REQUIRE(RegisterUpgrader(registry, "MyGame::Sword", 1, AttackToPower(steps), logger));
    REQUIRE(RegisterUpgrader(registry, "MyGame::Sword", 2, PowerToDamage(steps), logger));
    Value tree = TreeOf("{damage: 12.5}");

    const UpgradeResult upgraded = Upgrade(registry, "MyGame::Sword", 3, tree, Path, logger);
    INFO("a tree already at the type's version is current");
    REQUIRE(upgraded.status == UpgradeStatus::Succeeded);
    REQUIRE(upgraded.version == 3);
    INFO("no upgrader ran");
    REQUIRE(steps.empty());
    INFO("the tree is unchanged and nothing was logged");
    REQUIRE(Equals(tree, TreeOf("{damage: 12.5}")));
    REQUIRE(logger.errors.empty());
    REQUIRE(logger.warnings.empty());
}

TEST_CASE("A missing step is one error and leaves the tree at the version it reached", "[serialisation][upgrade]")
{
    Fenrir::TypeRegistry registry = SwordRegistry();
    RecordingLogger logger;
    std::vector<std::uint32_t> steps;
    INFO("each upgrader registers, for a registered type and a version not yet taken");
    REQUIRE(RegisterUpgrader(registry, "MyGame::Sword", 1, AttackToPower(steps), logger));
    // The root begins on line 2, column 3, where the diagnostic is located.
    Value tree = TreeOf("\n  {attack: 12.5, weight: 3}", Path);

    const UpgradeResult upgraded = Upgrade(registry, "MyGame::Sword", 1, tree, Path, logger);
    INFO("the upgrade stopped for want of the version 2 step");
    REQUIRE(upgraded.status == UpgradeStatus::MissingStep);
    INFO("the version reached is 2, the one the registered step gave");
    REQUIRE(upgraded.version == 2);
    INFO("the tree holds the version 2 form, the first step applied");
    REQUIRE(Equals(tree, TreeOf("{power: 12.5, weight: 3}")));
    INFO("one diagnostic, an error");
    REQUIRE(upgraded.diagnostics.size() == 1);
    const Diagnostic& missing = upgraded.diagnostics[0];
    INFO("it is an error in sword.json5 at the tree's root, 2:3; got " + Where(missing));
    REQUIRE(Where(missing) == "error at sword.json5:2:3");
    INFO("it names the type and the missing step's version");
    REQUIRE(missing.message.find("MyGame::Sword") != std::string::npos);
    REQUIRE(missing.message.find("version 2") != std::string::npos);
    INFO("it was logged once as an error, as formatted, and no warning was");
    REQUIRE(logger.errors == std::vector<std::string>{FormatDiagnostic(missing)});
    REQUIRE(logger.warnings.empty());
}

TEST_CASE("A tree newer than its type is left as it is with one warning", "[serialisation][upgrade]")
{
    Fenrir::TypeRegistry registry = SwordRegistry();
    RecordingLogger logger;
    std::vector<std::uint32_t> steps;
    INFO("each upgrader registers, for a registered type and a version not yet taken");
    REQUIRE(RegisterUpgrader(registry, "MyGame::Sword", 1, AttackToPower(steps), logger));
    // The root begins on line 2, column 3, where the diagnostic is located.
    Value tree = TreeOf("\n  {damage: 12.5, edge: 4}", Path);

    const UpgradeResult upgraded = Upgrade(registry, "MyGame::Sword", 5, tree, Path, logger);
    INFO("a newer tree is not downgraded");
    REQUIRE(upgraded.status == UpgradeStatus::Newer);
    INFO("the version reached is the tree's own, 5");
    REQUIRE(upgraded.version == 5);
    INFO("no upgrader ran and the tree is unchanged");
    REQUIRE(steps.empty());
    REQUIRE(Equals(tree, TreeOf("{damage: 12.5, edge: 4}")));
    INFO("one diagnostic, a warning");
    REQUIRE(upgraded.diagnostics.size() == 1);
    const Diagnostic& newer = upgraded.diagnostics[0];
    INFO("it is a warning in sword.json5 at the tree's root, 2:3; got " + Where(newer));
    REQUIRE(Where(newer) == "warning at sword.json5:2:3");
    INFO("it names the type");
    REQUIRE(newer.message.find("MyGame::Sword") != std::string::npos);
    INFO("it was logged once as a warning, as formatted, and no error was");
    REQUIRE(logger.warnings == std::vector<std::string>{FormatDiagnostic(newer)});
    REQUIRE(logger.errors.empty());
}

TEST_CASE("A diagnostic about a tree built in code is at line 1, column 1", "[serialisation][upgrade]")
{
    Fenrir::TypeRegistry registry = SwordRegistry();
    RecordingLogger logger;
    Object object;
    object.Add("attack", 12.5f);
    Value tree(std::move(object));

    const UpgradeResult upgraded = Upgrade(registry, "MyGame::Sword", 1, tree, Path, logger);
    INFO("no upgrader steps from version 1, so there is one diagnostic");
    REQUIRE(upgraded.diagnostics.size() == 1);
    INFO("a root with no location is at 1:1; got " + Where(upgraded.diagnostics[0]));
    REQUIRE(Where(upgraded.diagnostics[0]) == "error at sword.json5:1:1");
}

TEST_CASE("Upgrading a type the registry lacks changes nothing and logs nothing", "[serialisation][upgrade]")
{
    Fenrir::TypeRegistry registry = SwordRegistry();
    RecordingLogger logger;
    Value tree = TreeOf(SwordAtVersion1);

    const UpgradeResult upgraded = Upgrade(registry, "MyGame::Shield", 1, tree, Path, logger);
    INFO("the type is unknown, so the caller keeps the tree to write back as FromTree's UnknownType does");
    REQUIRE(upgraded.status == UpgradeStatus::UnknownType);
    INFO("the version is the tree's own");
    REQUIRE(upgraded.version == 1);
    INFO("the tree is unchanged, and nothing was reported or logged");
    REQUIRE(Equals(tree, TreeOf(SwordAtVersion1)));
    REQUIRE(upgraded.diagnostics.empty());
    REQUIRE(logger.errors.empty());
    REQUIRE(logger.warnings.empty());
}

TEST_CASE("A former type name finds the type to register and run upgraders for", "[serialisation][upgrade]")
{
    Fenrir::TypeRegistry registry = SwordRegistry();
    RecordingLogger logger;
    std::vector<std::uint32_t> steps;
    INFO("the sword was once MyGame::Blade, and registering under that name is accepted");
    REQUIRE(RegisterUpgrader(registry, "MyGame::Blade", 1, AttackToPower(steps), logger));
    REQUIRE(RegisterUpgrader(registry, "MyGame::Sword", 2, PowerToDamage(steps), logger));
    Value tree = TreeOf(SwordAtVersion1);

    const UpgradeResult upgraded = Upgrade(registry, "MyGame::Blade", 1, tree, Path, logger);
    INFO("both steps are the one type's, so a file naming the former type upgrades through both");
    REQUIRE(steps == std::vector<std::uint32_t>{1, 2});
    REQUIRE(upgraded.status == UpgradeStatus::Succeeded);
    REQUIRE(upgraded.version == 3);
}

TEST_CASE("Registering an upgrader for a type the registry lacks is one error and adds nothing",
          "[serialisation][upgrade]")
{
    Fenrir::TypeRegistry registry = SwordRegistry();
    RecordingLogger logger;
    std::vector<std::uint32_t> steps;

    const bool registered = RegisterUpgrader(registry, "MyGame::Shield", 1, AttackToPower(steps), logger);
    INFO("the upgrader is refused");
    REQUIRE_FALSE(registered);
    INFO("one error, naming the type");
    REQUIRE(logger.errors.size() == 1);
    REQUIRE(logger.errors[0].find("MyGame::Shield") != std::string::npos);
    INFO("the registry holds no upgraders for it");
    REQUIRE(registry.Attachment<UpgradersTag>("MyGame::Shield") == nullptr);
}

TEST_CASE("A second upgrader for one version is one error and the first is kept", "[serialisation][upgrade]")
{
    Fenrir::TypeRegistry registry = SwordRegistry();
    RecordingLogger logger;
    std::vector<std::uint32_t> firstSteps;
    std::vector<std::uint32_t> secondSteps;
    INFO("each upgrader registers, for a registered type and a version not yet taken");
    REQUIRE(RegisterUpgrader(registry, "MyGame::Sword", 1, AttackToPower(firstSteps), logger));

    const bool registered = RegisterUpgrader(registry, "MyGame::Sword", 1, AttackToPower(secondSteps), logger);
    INFO("the second upgrader for version 1 is refused");
    REQUIRE_FALSE(registered);
    INFO("one error, naming the type and the version");
    REQUIRE(logger.errors.size() == 1);
    REQUIRE(logger.errors[0].find("MyGame::Sword") != std::string::npos);
    REQUIRE(logger.errors[0].find("version 1") != std::string::npos);

    const UpgradersTag::Value* upgraders = registry.Attachment<UpgradersTag>("MyGame::Sword");
    INFO("the type still holds the one upgrader");
    REQUIRE(upgraders != nullptr);
    REQUIRE(upgraders->size() == 1);

    Value tree = TreeOf(SwordAtVersion1);
    (void)Upgrade(registry, "MyGame::Sword", 1, tree, Path, logger);
    INFO("the first upgrader is the one that runs");
    REQUIRE(firstSteps == std::vector<std::uint32_t>{1});
    REQUIRE(secondSteps.empty());
}

TEST_CASE("Registering an empty upgrader is one error and adds nothing", "[serialisation][upgrade]")
{
    Fenrir::TypeRegistry registry = SwordRegistry();
    RecordingLogger logger;

    const bool registered = RegisterUpgrader(registry, "MyGame::Sword", 1, Upgrader{}, logger);
    INFO("an upgrader with no function to call is refused, so Upgrade never calls it");
    REQUIRE_FALSE(registered);
    INFO("one error, naming the type");
    REQUIRE(logger.errors.size() == 1);
    REQUIRE(logger.errors[0].find("MyGame::Sword") != std::string::npos);
    INFO("the registry holds no upgraders for the type");
    REQUIRE(registry.Attachment<UpgradersTag>("MyGame::Sword") == nullptr);
}

TEST_CASE("Registering an upgrader from version 0 is one error and adds nothing", "[serialisation][upgrade]")
{
    Fenrir::TypeRegistry registry = SwordRegistry();
    RecordingLogger logger;
    std::vector<std::uint32_t> steps;

    const bool registered = RegisterUpgrader(registry, "MyGame::Sword", 0, AttackToPower(steps), logger);
    INFO("versions count from 1, so an upgrader from version 0 is refused");
    REQUIRE_FALSE(registered);
    INFO("one error, naming the type, the version given and the type's version");
    REQUIRE(logger.errors.size() == 1);
    REQUIRE(logger.errors[0].find("MyGame::Sword") != std::string::npos);
    REQUIRE(logger.errors[0].find("version 0") != std::string::npos);
    REQUIRE(logger.errors[0].find("version 3") != std::string::npos);
    INFO("the registry holds no upgraders for the type");
    REQUIRE(registry.Attachment<UpgradersTag>("MyGame::Sword") == nullptr);
}

TEST_CASE("Registering an upgrader from the type's own version is one error and adds nothing",
          "[serialisation][upgrade]")
{
    Fenrir::TypeRegistry registry = SwordRegistry();
    RecordingLogger logger;
    std::vector<std::uint32_t> steps;

    const bool registered = RegisterUpgrader(registry, "MyGame::Sword", 3, AttackToPower(steps), logger);
    INFO("the sword is at version 3, so a step from 3 would never run and is refused");
    REQUIRE_FALSE(registered);
    INFO("one error, naming the type and the version, which is both the one given and the type's");
    REQUIRE(logger.errors.size() == 1);
    REQUIRE(logger.errors[0].find("MyGame::Sword") != std::string::npos);
    REQUIRE(logger.errors[0].find("version 3") != std::string::npos);
    INFO("the registry holds no upgraders for the type");
    REQUIRE(registry.Attachment<UpgradersTag>("MyGame::Sword") == nullptr);
}

TEST_CASE("Registering an upgrader from above the type's version is one error and adds nothing",
          "[serialisation][upgrade]")
{
    Fenrir::TypeRegistry registry = SwordRegistry();
    RecordingLogger logger;
    std::vector<std::uint32_t> steps;

    const bool registered = RegisterUpgrader(registry, "MyGame::Sword", 4, AttackToPower(steps), logger);
    INFO("the sword is at version 3, so a step from version 4 would never run and is refused");
    REQUIRE_FALSE(registered);
    INFO("one error, naming the type, the version given and the type's version");
    REQUIRE(logger.errors.size() == 1);
    REQUIRE(logger.errors[0].find("MyGame::Sword") != std::string::npos);
    REQUIRE(logger.errors[0].find("version 4") != std::string::npos);
    REQUIRE(logger.errors[0].find("version 3") != std::string::npos);
    INFO("the registry holds no upgraders for the type");
    REQUIRE(registry.Attachment<UpgradersTag>("MyGame::Sword") == nullptr);
}

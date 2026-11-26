// Typed references between assets (ADR-0076 §2, §3): a data asset's `AssetHandle<T>` field reads a path, and the
// store loads its target first, with the referencing load's owner, checking its kind and, for a data asset, that its
// type is a `T`. What does not resolve leaves the handle empty, with one located diagnostic saying why.

#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "AssetsProbe.hpp"
#include "DataAssetsTest.hpp"
#include "FenrirTypes/FieldInfo.hpp"
#include "FenrirTypes/Kind.hpp"
#include "FenrirTypes/ValueRef.hpp"

using AssetsTest::CountContaining;
using AssetsTest::DataApp;
using AssetsTest::FindDiagnostic;
using AssetsTest::ShieldData;
using AssetsTest::Sword;
using AssetsTest::WeaponData;

namespace
{
    constexpr std::string_view IconedSword = "data/iconed-sword.asset.json5";
    constexpr std::string_view SelfUpgradingSword = "data/self-upgrading-sword.asset.json5";

    // A `reference` field as a description names it, whose target is `target`.
    Fenrir::FieldInfo ReferenceField(std::string_view name, std::string_view target)
    {
        return {.name = name, .type = {.kind = Fenrir::Kind::Reference, .target = target}};
    }

    template <typename T>
    Fenrir::ReferenceValue ReferenceTo(Fenrir::AssetHandle<T> handle)
    {
        return {handle.index, handle.generation};
    }
} // namespace

TEST_CASE("A data asset's image handle loads its image with the data asset's owner", "[assets][data][references]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);
    Fenrir::Scene& level = test.app.CreateScene("Level");

    const Fenrir::LoadResult<Sword> loaded = test.store.Load<Sword>(IconedSword, level);

    INFO("the sword loads whole");
    REQUIRE(loaded.Status() == Fenrir::LoadStatus::Succeeded);
    const Sword* sword = test.store.TryGet(loaded.handle);
    REQUIRE(sword != nullptr);
    INFO("its icon is the image at the path it names");
    REQUIRE(test.store.TryGet(sword->icon) != nullptr);
    REQUIRE(test.store.PathOf(sword->icon) == "fourbytwo.png");
    INFO("a later load of the image by the scene gives the same handle");
    REQUIRE(test.store.Load<Fenrir::Image>("fourbytwo.png", level).handle == sword->icon);
}

TEST_CASE("An image a data asset names is freed with the data asset's owner", "[assets][data][references]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);
    Fenrir::Scene& level = test.app.CreateScene("Level");
    const Fenrir::LoadResult<Sword> loaded = test.store.Load<Sword>(IconedSword, level);
    const Sword* sword = test.store.TryGet(loaded.handle);
    INFO("the sword and its icon load");
    REQUIRE(sword != nullptr);
    const Fenrir::AssetHandle<Fenrir::Image> icon = sword->icon;
    REQUIRE(test.store.IsValid(icon));

    test.app.DestroyScene("Level");

    INFO("the icon is freed with the scene that loaded the sword");
    REQUIRE_FALSE(test.store.IsValid(icon));
    REQUIRE_FALSE(test.store.IsValid(loaded.handle));
}

TEST_CASE("Two files referencing each other both load, each handle resolving to the other",
          "[assets][data][references]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::LoadResult<WeaponData> first =
        test.store.Load<WeaponData>("data/pair-first.asset.json5", Fenrir::AssetOwner::App());

    INFO("the first loads whole, and through it the second");
    REQUIRE(first.Status() == Fenrir::LoadStatus::Succeeded);
    const WeaponData* dawn = test.store.TryGet(first.handle);
    REQUIRE(dawn != nullptr);
    const WeaponData* dusk = test.store.TryGet(dawn->upgradesTo);
    REQUIRE(dusk != nullptr);
    REQUIRE(dusk->displayName == "Dusk blade");
    INFO("the second's handle is the first's own");
    REQUIRE(dusk->upgradesTo == first.handle);
    INFO("a load of the second gives the handle the first holds");
    REQUIRE(test.store.Load<WeaponData>("data/pair-second.asset.json5", Fenrir::AssetOwner::App()).handle ==
            dawn->upgradesTo);
}

TEST_CASE("A missing target leaves its handle empty, with one located diagnostic, and the load Partial",
          "[assets][data][references]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::LoadResult<WeaponData> loaded =
        test.store.Load<WeaponData>("data/missing-upgrade.asset.json5", Fenrir::AssetOwner::App());

    INFO("the weapon loads in part, its upgrade left empty");
    REQUIRE(loaded.Status() == Fenrir::LoadStatus::Partial);
    const WeaponData* weapon = test.store.TryGet(loaded.handle);
    REQUIRE(weapon != nullptr);
    REQUIRE(weapon->upgradesTo.IsEmpty());
    INFO("one diagnostic, at the path's string, names the file, field, path, expected and found");
    REQUIRE(loaded.diagnostics.size() == 1);
    const Fenrir::Serialisation::Diagnostic& missing = loaded.diagnostics.front();
    INFO(missing.message);
    REQUIRE(missing.severity == Fenrir::Serialisation::DiagnosticSeverity::Error);
    REQUIRE(missing.path.ends_with("data/missing-upgrade.asset.json5"));
    REQUIRE(missing.line == 8);
    REQUIRE(missing.column == 17);
    REQUIRE(missing.message.find("`upgradesTo`") != std::string::npos);
    REQUIRE(missing.message.find("data/no-such-weapon.asset.json5") != std::string::npos);
    REQUIRE(missing.message.find("`AssetsTest::WeaponData`") != std::string::npos);
    REQUIRE(missing.message.find("found no file") != std::string::npos);
}

TEST_CASE("A missing target is logged once, by the file that names it", "[assets][data][references]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::LoadResult<WeaponData> loaded =
        test.store.Load<WeaponData>("data/missing-upgrade.asset.json5", Fenrir::AssetOwner::App());
    INFO("the weapon loads in part");
    REQUIRE(loaded.Ok());

    INFO("the one error is the located diagnostic");
    REQUIRE(test.errors->size() == 1);
    REQUIRE(CountContaining(*test.errors, "data/no-such-weapon.asset.json5") == 1);
}

TEST_CASE("An image in a weapon field leaves its handle empty, with one located diagnostic, and the load Partial",
          "[assets][data][references]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::LoadResult<WeaponData> loaded =
        test.store.Load<WeaponData>("data/image-upgrade.asset.json5", Fenrir::AssetOwner::App());

    INFO("the weapon loads in part, its upgrade left empty");
    REQUIRE(loaded.Status() == Fenrir::LoadStatus::Partial);
    const WeaponData* weapon = test.store.TryGet(loaded.handle);
    REQUIRE(weapon != nullptr);
    REQUIRE(weapon->upgradesTo.IsEmpty());
    INFO("one diagnostic, at the path's string, names the file, field, path, expected and found");
    REQUIRE(loaded.diagnostics.size() == 1);
    const Fenrir::Serialisation::Diagnostic& wrong = loaded.diagnostics.front();
    INFO(wrong.message);
    REQUIRE(wrong.path.ends_with("data/image-upgrade.asset.json5"));
    REQUIRE(wrong.line == 8);
    REQUIRE(wrong.column == 17);
    REQUIRE(wrong.message.find("`upgradesTo`") != std::string::npos);
    REQUIRE(wrong.message.find("\"fourbytwo.png\"") != std::string::npos);
    REQUIRE(wrong.message.find("`AssetsTest::WeaponData`") != std::string::npos);
    REQUIRE(wrong.message.find("found a `.png` file") != std::string::npos);
    INFO("the image is not loaded for it");
    REQUIRE(test.errors->size() == 1);
}

TEST_CASE("A weapon field resolves a sword file, a sword being a weapon", "[assets][data][references]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::LoadResult<WeaponData> loaded =
        test.store.Load<WeaponData>("data/upgrades-to-sword.asset.json5", Fenrir::AssetOwner::App());

    INFO("the weapon loads whole, upgrading to the sword");
    REQUIRE(loaded.Status() == Fenrir::LoadStatus::Succeeded);
    const WeaponData* weapon = test.store.TryGet(loaded.handle);
    REQUIRE(weapon != nullptr);
    const WeaponData* upgrade = test.store.TryGet(weapon->upgradesTo);
    REQUIRE(upgrade != nullptr);
    REQUIRE(upgrade->displayName == "Longsword");
    INFO("its handle reads as the sword it is");
    REQUIRE(test.store.TryGet(Fenrir::AssetHandle<Sword>{weapon->upgradesTo.index, weapon->upgradesTo.generation}) !=
            nullptr);
}

TEST_CASE("A sword field refuses a plain weapon file, naming the type it found", "[assets][data][references]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::LoadResult<ShieldData> loaded =
        test.store.Load<ShieldData>("data/parries-club.asset.json5", Fenrir::AssetOwner::App());

    INFO("the shield loads in part, what it parries left empty");
    REQUIRE(loaded.Status() == Fenrir::LoadStatus::Partial);
    const ShieldData* shield = test.store.TryGet(loaded.handle);
    REQUIRE(shield != nullptr);
    REQUIRE(shield->parries.IsEmpty());
    INFO("one diagnostic, at the path's string, names the type expected and the one found");
    REQUIRE(loaded.diagnostics.size() == 1);
    const Fenrir::Serialisation::Diagnostic& wrong = loaded.diagnostics.front();
    INFO(wrong.message);
    REQUIRE(wrong.line == 8);
    REQUIRE(wrong.column == 14);
    REQUIRE(wrong.message.find("`parries`") != std::string::npos);
    REQUIRE(wrong.message.find("data/weapon.asset.json5") != std::string::npos);
    REQUIRE(wrong.message.find("`AssetsTest::Sword`") != std::string::npos);
    REQUIRE(wrong.message.find("found `AssetsTest::WeaponData`") != std::string::npos);
}

TEST_CASE("A bare #name names a sub-asset of the file that holds it", "[assets][data][references]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::LoadResult<Sword> loaded = test.store.Load<Sword>(SelfUpgradingSword, Fenrir::AssetOwner::App());

    INFO("the sword loads whole");
    REQUIRE(loaded.Status() == Fenrir::LoadStatus::Succeeded);
    const Sword* sword = test.store.TryGet(loaded.handle);
    REQUIRE(sword != nullptr);
    INFO("its upgrade is its own spare blade, which upgrades back to it");
    const Fenrir::LoadResult<Sword> spare =
        test.store.Load<Sword>(std::string(SelfUpgradingSword) + "#spare", Fenrir::AssetOwner::App());
    REQUIRE(ReferenceTo(sword->upgradesTo) == ReferenceTo(spare.handle));
    const Sword* spareSword = test.store.TryGet(spare.handle);
    REQUIRE(spareSword != nullptr);
    REQUIRE(ReferenceTo(spareSword->upgradesTo) == ReferenceTo(loaded.handle));
}

TEST_CASE("The store's writeReference names an asset by its path, and a sub-asset in full",
          "[assets][data][references]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);
    const Fenrir::LoadResult<Sword> iconed = test.store.Load<Sword>(IconedSword, Fenrir::AssetOwner::App());
    const Fenrir::LoadResult<Sword> worn = test.store.Load<Sword>(SelfUpgradingSword, Fenrir::AssetOwner::App());
    const Sword* iconedSword = test.store.TryGet(iconed.handle);
    const Sword* wornSword = test.store.TryGet(worn.handle);
    INFO("both swords load");
    REQUIRE(iconedSword != nullptr);
    REQUIRE(wornSword != nullptr);

    const Fenrir::Serialisation::Resolvers resolvers = Fenrir::AssetsProbe::WritingResolvers(test.store);

    INFO("an image is its path, and a sub-asset its file's path and its name");
    REQUIRE(resolvers.writeReference(ReferenceTo(iconedSword->icon), ReferenceField("icon", "Fenrir::Image")) ==
            "fourbytwo.png");
    REQUIRE(resolvers.writeReference(ReferenceTo(wornSword->upgradesTo),
                                     ReferenceField("upgradesTo", "AssetsTest::WeaponData")) ==
            std::string(SelfUpgradingSword) + "#spare");
}

TEST_CASE("The store's writeReference writes an empty or stale handle as no path", "[assets][data][references]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);
    const Fenrir::LoadResult<Sword> loaded = test.store.Load<Sword>(IconedSword, Fenrir::AssetOwner::App());
    const Sword* sword = test.store.TryGet(loaded.handle);
    INFO("the sword loads");
    REQUIRE(sword != nullptr);
    const Fenrir::ReferenceValue icon = ReferenceTo(sword->icon);
    REQUIRE(test.store.Unload(sword->icon));

    const Fenrir::Serialisation::Resolvers resolvers = Fenrir::AssetsProbe::WritingResolvers(test.store);

    INFO("neither names a path");
    REQUIRE(resolvers.writeReference({}, ReferenceField("icon", "Fenrir::Image")).empty());
    REQUIRE(resolvers.writeReference(icon, ReferenceField("icon", "Fenrir::Image")).empty());
}

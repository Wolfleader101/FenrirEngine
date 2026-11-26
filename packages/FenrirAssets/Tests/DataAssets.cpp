// Data assets (ADR-0075, ADR-0079): the test package's game-defined types, registered into the app's type registry,
// read from the .asset.json5 envelopes in fixtures/data/ through the store's public interface.

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "DataAssetsTest.hpp"
#include "FenrirSerialisation/Upgrade.hpp"
#include "FenrirSerialisation/Value.hpp"

using AssetsTest::AssetsApp;
using AssetsTest::CountContaining;
using AssetsTest::DataApp;
using AssetsTest::FindDiagnostic;
using AssetsTest::Sword;
using AssetsTest::WeaponData;
using Catch::Matchers::WithinAbs;

TEST_CASE("Loading a data asset reads every field of its file", "[assets][data]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::LoadResult<Sword> loaded =
        test.store.Load<Sword>("data/sword.asset.json5", Fenrir::AssetOwner::App());

    INFO("the sword loads wholly: " << (loaded.diagnostic ? loaded.diagnostic->importerMessage : std::string("ok")));
    REQUIRE(loaded.Status() == Fenrir::LoadStatus::Succeeded);
    const Sword* sword = test.store.TryGet(loaded.handle);
    REQUIRE(sword != nullptr);
    INFO("each field holds what the file says, and the handles the file leaves out are empty");
    REQUIRE(sword->displayName == "Longsword");
    REQUIRE(sword->tags == std::vector<std::string>{"blade", "steel"});
    REQUIRE_THAT(sword->stats.damage, WithinAbs(25.0f, 0.0f));
    REQUIRE_THAT(sword->stats.weight, WithinAbs(3.5f, 0.0f));
    REQUIRE_THAT(sword->edge, WithinAbs(0.9f, 0.0f));
    REQUIRE(sword->icon.IsEmpty());
    REQUIRE(sword->upgradesTo.IsEmpty());
    REQUIRE(loaded.diagnostics.empty());
}

TEST_CASE("Loading a data asset already loaded gives the same handle", "[assets][data]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::LoadResult<Sword> first = test.store.Load<Sword>("data/sword.asset.json5", Fenrir::AssetOwner::App());
    const Fenrir::LoadResult<Sword> second =
        test.store.Load<Sword>("data/sword.asset.json5", Fenrir::AssetOwner::App());

    INFO("the second load gives back the first one's handle");
    REQUIRE(first.Ok());
    REQUIRE(second.Ok());
    REQUIRE(second.handle == first.handle);
}

TEST_CASE("A handle of a base type loads a file of a type extending it", "[assets][data]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::LoadResult<WeaponData> loaded =
        test.store.Load<WeaponData>("data/sword.asset.json5", Fenrir::AssetOwner::App());

    INFO("a Sword is a WeaponData, so its file loads as one, and reads as one");
    REQUIRE(loaded.Status() == Fenrir::LoadStatus::Succeeded);
    const WeaponData* weapon = test.store.TryGet(loaded.handle);
    REQUIRE(weapon != nullptr);
    REQUIRE(weapon->displayName == "Longsword");
}

TEST_CASE("Loading a file of a base type as a type extending it fails with WrongType", "[assets][data]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::LoadResult<Sword> loaded =
        test.store.Load<Sword>("data/weapon.asset.json5", Fenrir::AssetOwner::App());

    INFO("a plain WeaponData is no Sword");
    REQUIRE(loaded.Status() == Fenrir::LoadStatus::Failed);
    REQUIRE(loaded.handle.IsEmpty());
    REQUIRE(loaded.diagnostic->reason == Fenrir::LoadFailure::WrongType);
}

TEST_CASE("A failed data asset load is logged once", "[assets][data]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::LoadResult<Sword> loaded =
        test.store.Load<Sword>("data/weapon.asset.json5", Fenrir::AssetOwner::App());

    INFO("the wrong type is one error, naming the type the file holds");
    REQUIRE_FALSE(loaded.Ok());
    REQUIRE(test.errors->size() == 1);
    REQUIRE(CountContaining(*test.errors, "AssetsTest::WeaponData") == 1);
}

TEST_CASE("Loading a data asset already loaded as a type extending T gives the same handle", "[assets][data]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::LoadResult<Sword> asSword =
        test.store.Load<Sword>("data/sword.asset.json5", Fenrir::AssetOwner::App());
    const Fenrir::LoadResult<WeaponData> asWeapon =
        test.store.Load<WeaponData>("data/sword.asset.json5", Fenrir::AssetOwner::App());

    INFO("the loaded Sword is a WeaponData, so the second load shares its slot");
    REQUIRE(asSword.Ok());
    REQUIRE(asWeapon.Ok());
    REQUIRE(asWeapon.handle.index == asSword.handle.index);
    REQUIRE(asWeapon.handle.generation == asSword.handle.generation);
}

TEST_CASE("Loading a data asset already loaded as a type that is not T fails with WrongType", "[assets][data]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::LoadResult<WeaponData> asWeapon =
        test.store.Load<WeaponData>("data/weapon.asset.json5", Fenrir::AssetOwner::App());
    const Fenrir::LoadResult<Sword> asSword =
        test.store.Load<Sword>("data/weapon.asset.json5", Fenrir::AssetOwner::App());

    INFO("the loaded WeaponData is no Sword, so the second load fails though the path is loaded");
    REQUIRE(asWeapon.Ok());
    REQUIRE(asSword.Status() == Fenrir::LoadStatus::Failed);
    REQUIRE(asSword.diagnostic->reason == Fenrir::LoadFailure::WrongType);
}

TEST_CASE("Loading a file whose type is unrelated fails with WrongType", "[assets][data]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::LoadResult<WeaponData> loaded =
        test.store.Load<WeaponData>("data/shield.asset.json5", Fenrir::AssetOwner::App());

    INFO("a shield is no weapon");
    REQUIRE(loaded.Status() == Fenrir::LoadStatus::Failed);
    REQUIRE(loaded.diagnostic->reason == Fenrir::LoadFailure::WrongType);
}

TEST_CASE("Loading a file whose type nothing registers fails with WrongType", "[assets][data]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::LoadResult<WeaponData> loaded =
        test.store.Load<WeaponData>("data/unknown-type.asset.json5", Fenrir::AssetOwner::App());

    INFO("the registry knows no AssetsTest::Spear");
    REQUIRE(loaded.Status() == Fenrir::LoadStatus::Failed);
    REQUIRE(loaded.diagnostic->reason == Fenrir::LoadFailure::WrongType);
    REQUIRE(FindDiagnostic(loaded.diagnostics, "AssetsTest::Spear") != nullptr);
}

TEST_CASE("Loading a type the registry lacks fails with NotADataAsset", "[assets][data]")
{
    AssetsApp test;

    const Fenrir::LoadResult<Sword> loaded =
        test.store.Load<Sword>("data/sword.asset.json5", Fenrir::AssetOwner::App());

    INFO("nothing registered the test package, so the store knows no Sword, and says so in one error");
    REQUIRE(loaded.Status() == Fenrir::LoadStatus::Failed);
    REQUIRE(loaded.diagnostic->reason == Fenrir::LoadFailure::NotADataAsset);
    REQUIRE(test.errors->size() == 1);
}

TEST_CASE("A file with an unknown and a wrong-typed field loads in part, saying where each is", "[assets][data]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::LoadResult<Sword> loaded =
        test.store.Load<Sword>("data/bad-fields.asset.json5", Fenrir::AssetOwner::App());

    INFO("the sword loads, though two of its keys did not read");
    REQUIRE(loaded.Status() == Fenrir::LoadStatus::Partial);
    REQUIRE(loaded.Ok());
    const Sword* sword = test.store.TryGet(loaded.handle);
    REQUIRE(sword != nullptr);
    INFO("what read is kept, and the wrong-typed edge keeps its default");
    REQUIRE(sword->displayName == "Odd sword");
    REQUIRE_THAT(sword->edge, WithinAbs(0.5f, 0.0f));

    const Fenrir::Serialisation::Diagnostic* unknown = FindDiagnostic(loaded.diagnostics, "colour");
    const Fenrir::Serialisation::Diagnostic* wrong = FindDiagnostic(loaded.diagnostics, "edge");
    INFO("one diagnostic each, at its value in the file");
    REQUIRE(loaded.diagnostics.size() == 2);
    REQUIRE(unknown != nullptr);
    REQUIRE(unknown->severity == Fenrir::Serialisation::DiagnosticSeverity::Warning);
    REQUIRE(unknown->line == 8);
    REQUIRE(unknown->column == 13);
    REQUIRE(wrong != nullptr);
    REQUIRE(wrong->severity == Fenrir::Serialisation::DiagnosticSeverity::Error);
    REQUIRE(wrong->line == 9);
    REQUIRE(wrong->column == 11);
}

TEST_CASE("A data asset's diagnostics are each logged once", "[assets][data]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::LoadResult<Sword> loaded =
        test.store.Load<Sword>("data/bad-fields.asset.json5", Fenrir::AssetOwner::App());
    INFO("the sword loads in part");
    REQUIRE(loaded.Ok());

    INFO("the unknown key is warned once, and the wrong-typed value is an error once");
    REQUIRE(CountContaining(*test.warnings, "colour") == 1);
    REQUIRE(CountContaining(*test.errors, "edge") == 1);
    REQUIRE(test.errors->size() == 1);
}

TEST_CASE("A later load of a data asset that loaded in part is in part too, logging nothing again", "[assets][data]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);
    const Fenrir::LoadResult<Sword> first =
        test.store.Load<Sword>("data/bad-fields.asset.json5", Fenrir::AssetOwner::App());
    const std::size_t logged = test.errors->size() + test.warnings->size();

    const Fenrir::LoadResult<Sword> second =
        test.store.Load<Sword>("data/bad-fields.asset.json5", Fenrir::AssetOwner::App());

    INFO("the second load reports the first one's diagnostics, as a model's failed units repeat, and logs none");
    REQUIRE(second.handle == first.handle);
    REQUIRE(second.Status() == Fenrir::LoadStatus::Partial);
    REQUIRE(second.diagnostics == first.diagnostics);
    REQUIRE(test.errors->size() + test.warnings->size() == logged);
}

TEST_CASE("A file at an older version loads through its type's upgrader", "[assets][data]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);
    const bool upgraderAdded = Fenrir::Serialisation::RegisterUpgrader(
        test.app.GetResource<Fenrir::TypeRegistry>(), "AssetsTest::Sword", 1,
        [](std::uint32_t, Fenrir::Serialisation::Value& tree) {
            for (Fenrir::Serialisation::Member& member : tree.AsObject().members)
            {
                if (member.key == "sharpness")
                {
                    member.key = "edge";
                }
            }
        },
        *test.app.Logger());
    INFO("the upgrader from version 1 registers");
    REQUIRE(upgraderAdded);

    const Fenrir::LoadResult<Sword> loaded =
        test.store.Load<Sword>("data/old-sword.asset.json5", Fenrir::AssetOwner::App());

    INFO("version 1's sharpness reads as version 2's edge");
    REQUIRE(loaded.Status() == Fenrir::LoadStatus::Succeeded);
    const Sword* sword = test.store.TryGet(loaded.handle);
    REQUIRE(sword != nullptr);
    REQUIRE_THAT(sword->edge, WithinAbs(0.75f, 0.0f));
}

TEST_CASE("A data asset file with a syntax error fails with one located diagnostic", "[assets][data]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::LoadResult<Sword> loaded =
        test.store.Load<Sword>("data/syntax-error.asset.json5", Fenrir::AssetOwner::App());

    INFO("nothing loads, and the one diagnostic is where the comma is missing");
    REQUIRE(loaded.Status() == Fenrir::LoadStatus::Failed);
    REQUIRE(loaded.diagnostic->reason == Fenrir::LoadFailure::ImportFailed);
    REQUIRE(loaded.diagnostics.size() == 1);
    REQUIRE(loaded.diagnostics.front().line == 5);
    REQUIRE(loaded.diagnostics.front().column == 3);
}

TEST_CASE("A data asset file of another format version fails", "[assets][data]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::LoadResult<Sword> loaded =
        test.store.Load<Sword>("data/format-2.asset.json5", Fenrir::AssetOwner::App());

    INFO("format 2 is not the envelope the store reads, and says where");
    REQUIRE(loaded.Status() == Fenrir::LoadStatus::Failed);
    REQUIRE(loaded.diagnostic->reason == Fenrir::LoadFailure::ImportFailed);
    const Fenrir::Serialisation::Diagnostic* format = FindDiagnostic(loaded.diagnostics, "format");
    REQUIRE(format != nullptr);
    REQUIRE(format->line == 3);
}

TEST_CASE("A data asset owned by a scene is freed with the scene", "[assets][data][owners]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);
    Fenrir::Scene& level = test.app.CreateScene("Level");
    const Fenrir::LoadResult<Sword> loaded = test.store.Load<Sword>("data/sword.asset.json5", level);

    INFO("the sword resolves while its scene lives");
    REQUIRE(test.store.IsValid(loaded.handle));

    test.app.DestroyScene("Level");

    INFO("the sword's handle is stale once its only owner ends");
    REQUIRE_FALSE(test.store.IsValid(loaded.handle));
    REQUIRE(test.store.TryGet(loaded.handle) == nullptr);
}

TEST_CASE("A data asset owned by a scene and the app outlives the scene", "[assets][data][owners]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);
    Fenrir::Scene& level = test.app.CreateScene("Level");
    const Fenrir::LoadResult<Sword> forLevel = test.store.Load<Sword>("data/sword.asset.json5", level);
    const Fenrir::LoadResult<Sword> forApp =
        test.store.Load<Sword>("data/sword.asset.json5", Fenrir::AssetOwner::App());
    INFO("both loads name the one sword");
    REQUIRE(forApp.handle == forLevel.handle);

    test.app.DestroyScene("Level");

    INFO("the app still owns the sword");
    REQUIRE(test.store.IsValid(forLevel.handle));
}

namespace
{
    // A Sword handle made from the numbers of a loaded plain WeaponData's handle: one naming a record of a type that
    // is not a Sword.
    Fenrir::AssetHandle<Sword> SwordHandleToAWeapon(DataApp& test)
    {
        const Fenrir::LoadResult<WeaponData> loaded =
            test.store.Load<WeaponData>("data/weapon.asset.json5", Fenrir::AssetOwner::App());
        INFO("the plain weapon loads");
        REQUIRE(loaded.Ok());
        return {loaded.handle.index, loaded.handle.generation};
    }
} // namespace

TEST_CASE("TryGet of a data asset as a type it is not gives null", "[assets][data]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);
    const Fenrir::AssetHandle<Sword> asSword = SwordHandleToAWeapon(test);

    INFO("a WeaponData record does not read as a Sword");
    REQUIRE(test.store.TryGet(asSword) == nullptr);
}

TEST_CASE("TryGet of a data asset as a type it is not is logged once per handle", "[assets][data]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);
    const Fenrir::AssetHandle<Sword> asSword = SwordHandleToAWeapon(test);

    (void)test.store.TryGet(asSword);
    (void)test.store.TryGet(asSword);

    INFO("the mismatch is logged the first time only");
    REQUIRE(CountContaining(*test.errors, "AssetsTest::Sword") == 1);
}

namespace
{
    // A sword made in code, not read from a file.
    Sword Rapier()
    {
        Sword rapier;
        rapier.displayName = "Rapier";
        rapier.tags = {"blade", "thin"};
        rapier.stats.damage = 18.0f;
        rapier.edge = 0.8f;
        return rapier;
    }
} // namespace

TEST_CASE("A data asset added with no name reads its value and has no path", "[assets][data][add]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::AssetHandle<Sword> handle = test.store.Add<Sword>(Rapier(), Fenrir::AssetOwner::App());

    const Sword* sword = test.store.TryGet(handle);
    INFO("the added sword reads back what it was given, at revision 1");
    REQUIRE(sword != nullptr);
    REQUIRE(sword->displayName == "Rapier");
    REQUIRE(sword->tags == std::vector<std::string>{"blade", "thin"});
    REQUIRE_THAT(sword->edge, WithinAbs(0.8f, 0.0f));
    REQUIRE(test.store.Revision(handle) == 1);
    INFO("with no name it has no asset path");
    REQUIRE_FALSE(test.store.PathOf(handle).has_value());
}

TEST_CASE("A data asset added with a name has that name as its path", "[assets][data][add]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::AssetHandle<Sword> handle =
        test.store.Add<Sword>(Rapier(), Fenrir::AssetOwner::App(), "data/rapier.asset.json5");

    INFO("the named sword resolves, and PathOf gives its name");
    REQUIRE(test.store.TryGet(handle) != nullptr);
    REQUIRE(test.store.PathOf(handle) == std::optional<std::string>("data/rapier.asset.json5"));
}

TEST_CASE("Loading the name of a data asset added with it gives the added asset", "[assets][data][add]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);
    const Fenrir::AssetHandle<Sword> added =
        test.store.Add<Sword>(Rapier(), Fenrir::AssetOwner::App(), "data/rapier.asset.json5");

    const Fenrir::LoadResult<WeaponData> loaded =
        test.store.Load<WeaponData>("data/rapier.asset.json5", Fenrir::AssetOwner::App());

    INFO("the name is deduplicated like a loaded file's path, so no file is read: " << loaded.diagnostics.size());
    REQUIRE(loaded.Status() == Fenrir::LoadStatus::Succeeded);
    REQUIRE(loaded.handle.index == added.index);
    REQUIRE(loaded.handle.generation == added.generation);
}

TEST_CASE("A data asset added under a name in use is refused and logged", "[assets][data][add]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);
    const Fenrir::LoadResult<Sword> loaded =
        test.store.Load<Sword>("data/sword.asset.json5", Fenrir::AssetOwner::App());
    const Fenrir::AssetHandle<Sword> first =
        test.store.Add<Sword>(Rapier(), Fenrir::AssetOwner::App(), "data/rapier.asset.json5");
    INFO("the loaded sword and the first named add both succeed");
    REQUIRE(loaded.Ok());
    REQUIRE_FALSE(first.IsEmpty());

    const Fenrir::AssetHandle<Sword> overAdded =
        test.store.Add<Sword>(Rapier(), Fenrir::AssetOwner::App(), "data/rapier.asset.json5");
    const Fenrir::AssetHandle<Sword> overLoaded =
        test.store.Add<Sword>(Rapier(), Fenrir::AssetOwner::App(), "data/sword.asset.json5");

    INFO("a name an added or a loaded asset has is refused, each with one error naming it");
    REQUIRE(overAdded.IsEmpty());
    REQUIRE(overLoaded.IsEmpty());
    REQUIRE(CountContaining(*test.errors, "data/rapier.asset.json5") == 1);
    REQUIRE(CountContaining(*test.errors, "data/sword.asset.json5") == 1);
    INFO("the assets already under the names are untouched");
    REQUIRE(test.store.TryGet(loaded.handle)->displayName == "Longsword");
    REQUIRE(test.store.TryGet(first)->displayName == "Rapier");
}

TEST_CASE("A data asset added under a name outside the root is refused and logged", "[assets][data][add]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::AssetHandle<Sword> climbing =
        test.store.Add<Sword>(Rapier(), Fenrir::AssetOwner::App(), "../rapier.asset.json5");
    const std::string elsewhere = (test.store.Root().parent_path() / "rapier.asset.json5").generic_string();
    const Fenrir::AssetHandle<Sword> absolute = test.store.Add<Sword>(Rapier(), Fenrir::AssetOwner::App(), elsewhere);

    INFO("a name climbing out through .. and an absolute one elsewhere are each refused with one error");
    REQUIRE(climbing.IsEmpty());
    REQUIRE(absolute.IsEmpty());
    REQUIRE(CountContaining(*test.errors, "outside the asset root") == 2);
}

TEST_CASE("A data asset added under an absolute name under the root has its root-relative path", "[assets][data][add]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);
    const std::string absolute = (test.store.Root() / "data" / "rapier.asset.json5").generic_string();

    const Fenrir::AssetHandle<Sword> handle = test.store.Add<Sword>(Rapier(), Fenrir::AssetOwner::App(), absolute);

    INFO("the name is keyed by its root-relative form, as a load's is");
    REQUIRE(test.store.PathOf(handle) == std::optional<std::string>("data/rapier.asset.json5"));
}

TEST_CASE("A data asset added for a scene is freed with the scene, name and all", "[assets][data][add][owners]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);
    Fenrir::Scene& level = test.app.CreateScene("Level");
    const Fenrir::AssetHandle<Sword> handle = test.store.Add<Sword>(Rapier(), level, "data/rapier.asset.json5");

    test.app.DestroyScene("Level");

    INFO("the sword's handle is stale once its only owner ends, and its name is free again");
    REQUIRE_FALSE(test.store.IsValid(handle));
    REQUIRE_FALSE(test.store.Add<Sword>(Rapier(), Fenrir::AssetOwner::App(), "data/rapier.asset.json5").IsEmpty());
}

TEST_CASE("A data asset of a type the registry lacks is refused when added", "[assets][data][add]")
{
    AssetsApp test;

    const Fenrir::AssetHandle<Sword> handle = test.store.Add<Sword>(Rapier(), Fenrir::AssetOwner::App());

    INFO("with no types registered, the add is refused with one error naming the type");
    REQUIRE(handle.IsEmpty());
    REQUIRE(CountContaining(*test.errors, "AssetsTest::Sword") == 1);
}

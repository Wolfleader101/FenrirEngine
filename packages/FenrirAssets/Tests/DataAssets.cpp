// Data assets (ADR-0075, ADR-0079): the test package's game-defined types, registered into the app's type registry,
// read from the .asset.json5 envelopes in fixtures/data/ through the store's public interface.

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "AssetsTest.hpp"
#include "FenrirSerialisation/Upgrade.hpp"
#include "FenrirSerialisation/Value.hpp"
#include "FenrirTypes/TypeRegistry.hpp"
#include "Generated/DataAssetsTestPackageTypes.hpp"

using AssetsTest::AssetsApp;
using AssetsTest::Sword;
using AssetsTest::WeaponData;
using Catch::Matchers::WithinAbs;

namespace
{
    // An App with assets installed and the test package's data asset types registered, as a game registers its own.
    struct DataApp : AssetsApp
    {
        bool registered =
            Fenrir::Generated::RegisterDataAssetsTestPackageTypes(app.GetResource<Fenrir::TypeRegistry>());
    };

    // How many of `messages` contain `part`.
    std::size_t CountContaining(const std::vector<std::string>& messages, std::string_view part)
    {
        return static_cast<std::size_t>(std::ranges::count_if(
            messages, [&](const std::string& message) { return message.find(part) != std::string::npos; }));
    }

    // The diagnostic whose message contains `part`, or null.
    const Fenrir::Serialisation::Diagnostic* FindDiagnostic(const std::vector<Fenrir::Serialisation::Diagnostic>& all,
                                                            std::string_view part)
    {
        const auto found = std::ranges::find_if(all, [&](const Fenrir::Serialisation::Diagnostic& diagnostic) {
            return diagnostic.message.find(part) != std::string::npos;
        });
        return found != all.end() ? &*found : nullptr;
    }
} // namespace

TEST_CASE("Loading a data asset reads every field of its file", "[assets][data]")
{
    DataApp test;
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
    REQUIRE(test.registered);

    const Fenrir::LoadResult<Sword> loaded =
        test.store.Load<Sword>("data/weapon.asset.json5", Fenrir::AssetOwner::App());

    INFO("a plain WeaponData is no Sword");
    REQUIRE(loaded.Status() == Fenrir::LoadStatus::Failed);
    REQUIRE(loaded.handle.IsEmpty());
    REQUIRE(loaded.diagnostic->reason == Fenrir::LoadFailure::WrongType);
    INFO("the failure is logged once");
    REQUIRE(CountContaining(*test.errors, "AssetsTest::WeaponData") == 1);
}

TEST_CASE("Loading a file whose type is unrelated fails with WrongType", "[assets][data]")
{
    DataApp test;
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

    INFO("nothing registered the test package, so the store knows no Sword");
    REQUIRE(loaded.Status() == Fenrir::LoadStatus::Failed);
    REQUIRE(loaded.diagnostic->reason == Fenrir::LoadFailure::NotADataAsset);
    REQUIRE(test.errors->size() == 1);
}

TEST_CASE("A file with an unknown and a wrong-typed field loads in part, saying where each is", "[assets][data]")
{
    DataApp test;
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
    REQUIRE(test.registered);

    const Fenrir::LoadResult<Sword> loaded =
        test.store.Load<Sword>("data/bad-fields.asset.json5", Fenrir::AssetOwner::App());
    REQUIRE(loaded.Ok());

    INFO("the unknown key is warned once, and the wrong-typed value is an error once");
    REQUIRE(CountContaining(*test.warnings, "colour") == 1);
    REQUIRE(CountContaining(*test.errors, "edge") == 1);
    REQUIRE(test.errors->size() == 1);
}

TEST_CASE("A file at an older version loads through its type's upgrader", "[assets][data]")
{
    DataApp test;
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

TEST_CASE("A data asset file's sub-assets are not read yet, with a warning", "[assets][data]")
{
    DataApp test;
    REQUIRE(test.registered);

    const Fenrir::LoadResult<Sword> loaded =
        test.store.Load<Sword>("data/with-sub.asset.json5", Fenrir::AssetOwner::App());

    INFO("the sword's data loads, and the load is partial with one warning about `sub`");
    REQUIRE(loaded.Status() == Fenrir::LoadStatus::Partial);
    REQUIRE(test.store.TryGet(loaded.handle)->displayName == "Sheathed sword");
    REQUIRE(loaded.diagnostics.size() == 1);
    REQUIRE(loaded.diagnostics.front().severity == Fenrir::Serialisation::DiagnosticSeverity::Warning);
    REQUIRE(CountContaining(*test.warnings, "sub") == 1);
}

TEST_CASE("A data asset owned by a scene is freed with the scene", "[assets][data][owners]")
{
    DataApp test;
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
    REQUIRE(test.registered);
    Fenrir::Scene& level = test.app.CreateScene("Level");
    const Fenrir::LoadResult<Sword> forLevel = test.store.Load<Sword>("data/sword.asset.json5", level);
    const Fenrir::LoadResult<Sword> forApp =
        test.store.Load<Sword>("data/sword.asset.json5", Fenrir::AssetOwner::App());
    REQUIRE(forApp.handle == forLevel.handle);

    test.app.DestroyScene("Level");

    INFO("the app still owns the sword");
    REQUIRE(test.store.IsValid(forLevel.handle));
}

TEST_CASE("TryGet of a data asset as a type it is not gives null, logged once per handle", "[assets][data]")
{
    DataApp test;
    REQUIRE(test.registered);
    const Fenrir::LoadResult<WeaponData> loaded =
        test.store.Load<WeaponData>("data/weapon.asset.json5", Fenrir::AssetOwner::App());
    REQUIRE(loaded.Ok());
    const Fenrir::AssetHandle<Sword> asSword{loaded.handle.index, loaded.handle.generation};

    INFO("a WeaponData record does not read as a Sword");
    REQUIRE(test.store.TryGet(asSword) == nullptr);
    REQUIRE(test.store.TryGet(asSword) == nullptr);
    INFO("the mismatch is logged the first time only");
    REQUIRE(CountContaining(*test.errors, "AssetsTest::Sword") == 1);
}

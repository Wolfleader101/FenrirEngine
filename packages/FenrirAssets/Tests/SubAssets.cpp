// Embedded sub-assets (ADR-0076, ADR-0079): an envelope's `sub` holds named data assets in the same file, each loaded
// by `file#name` from one read of the file, owned with the file's top level, as a model's labelled sub-assets are.

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "AssetSource.hpp"
#include "AssetsProbe.hpp"
#include "DataAssetsTest.hpp"

using AssetsTest::CountContaining;
using AssetsTest::DataApp;
using AssetsTest::FindDiagnostic;
using AssetsTest::Fixtures;
using AssetsTest::ShieldData;
using AssetsTest::Sword;
using Catch::Matchers::WithinAbs;

namespace
{
    constexpr std::string_view SheathedSword = "data/sheathed-sword.asset.json5";

    // The directory source over the fixtures, counting how many times it reads each file.
    class CountingSource : public Fenrir::Detail::AssetSource
    {
      public:
        CountingSource(std::unique_ptr<Fenrir::Detail::AssetSource> inner, std::shared_ptr<std::size_t> reads)
            : m_inner(std::move(inner)), m_reads(std::move(reads))
        {
        }

        bool Exists(std::string_view path) override
        {
            return m_inner->Exists(path);
        }

        std::optional<std::vector<std::byte>> Read(std::string_view path) override
        {
            ++*m_reads;
            return m_inner->Read(path);
        }

      private:
        std::unique_ptr<Fenrir::Detail::AssetSource> m_inner;
        std::shared_ptr<std::size_t> m_reads;
    };

    // Makes `test`'s store read through a counting source, and gives the count.
    std::shared_ptr<std::size_t> CountReads(DataApp& test)
    {
        auto reads = std::make_shared<std::size_t>(0);
        Fenrir::AssetsProbe::UseSource(
            test.store, std::make_unique<CountingSource>(
                            Fenrir::Detail::MakeDirectorySource(Fixtures(), test.store.Logger()), reads));
        return reads;
    }

    std::string Labelled(std::string_view file, std::string_view label)
    {
        return std::string(file) + '#' + std::string(label);
    }

    std::string FailureText(const Fenrir::LoadResult<ShieldData>& loaded)
    {
        return loaded.diagnostic ? loaded.diagnostic->importerMessage : std::string("ok");
    }
} // namespace

TEST_CASE("A sub-asset loads by its file's path and its name", "[assets][data][sub]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::LoadResult<ShieldData> sheath =
        test.store.Load<ShieldData>(Labelled(SheathedSword, "sheath"), Fenrir::AssetOwner::App());

    INFO("the sheath loads wholly, holding what its entry in `sub` says: " << FailureText(sheath));
    REQUIRE(sheath.Status() == Fenrir::LoadStatus::Succeeded);
    const ShieldData* data = test.store.TryGet(sheath.handle);
    REQUIRE(data != nullptr);
    REQUIRE_THAT(data->block, WithinAbs(0.8f, 0.0f));
}

TEST_CASE("A sub-asset of a file's own type loads by name", "[assets][data][sub]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::LoadResult<Sword> spare =
        test.store.Load<Sword>(Labelled(SheathedSword, "spare"), Fenrir::AssetOwner::App());

    INFO("the spare blade is its own asset, not the top-level sword");
    REQUIRE(spare.Status() == Fenrir::LoadStatus::Succeeded);
    const Sword* blade = test.store.TryGet(spare.handle);
    REQUIRE(blade != nullptr);
    REQUIRE(blade->displayName == "Spare blade");
}

TEST_CASE("A sub-asset and its file's top level share one read of the file", "[assets][data][sub]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);
    const std::shared_ptr<std::size_t> reads = CountReads(test);

    const Fenrir::LoadResult<ShieldData> sheath =
        test.store.Load<ShieldData>(Labelled(SheathedSword, "sheath"), Fenrir::AssetOwner::App());
    const Fenrir::LoadResult<Sword> sword = test.store.Load<Sword>(SheathedSword, Fenrir::AssetOwner::App());

    INFO("both load, and the file is read once");
    REQUIRE(sheath.Ok());
    REQUIRE(sword.Ok());
    REQUIRE(*reads == 1);
}

TEST_CASE("Loading a file's top level loads each of its sub-assets", "[assets][data][sub]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);
    const std::shared_ptr<std::size_t> reads = CountReads(test);

    const Fenrir::LoadResult<Sword> sword = test.store.Load<Sword>(SheathedSword, Fenrir::AssetOwner::App());
    const Fenrir::LoadResult<ShieldData> sheath =
        test.store.Load<ShieldData>(Labelled(SheathedSword, "sheath"), Fenrir::AssetOwner::App());
    const Fenrir::LoadResult<Sword> spare =
        test.store.Load<Sword>(Labelled(SheathedSword, "spare"), Fenrir::AssetOwner::App());

    INFO("the sub-assets were loaded with the top level, so loading them reads nothing more");
    REQUIRE(sword.Ok());
    REQUIRE(sheath.Ok());
    REQUIRE(spare.Ok());
    REQUIRE(*reads == 1);
}

TEST_CASE("A later load of a sub-asset gives the same handle", "[assets][data][sub]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::LoadResult<ShieldData> first =
        test.store.Load<ShieldData>(Labelled(SheathedSword, "sheath"), Fenrir::AssetOwner::App());
    const Fenrir::LoadResult<Sword> sword = test.store.Load<Sword>(SheathedSword, Fenrir::AssetOwner::App());
    const Fenrir::LoadResult<ShieldData> second =
        test.store.Load<ShieldData>(Labelled(SheathedSword, "sheath"), Fenrir::AssetOwner::App());

    INFO("the second load of the sheath, after the sword's, gives the first one's handle");
    REQUIRE(first.Ok());
    REQUIRE(sword.Ok());
    REQUIRE(second.handle == first.handle);
}

TEST_CASE("A file's sub-assets are freed with the owner that loaded its top level", "[assets][data][sub][owners]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);
    Fenrir::Scene& level = test.app.CreateScene("Level");
    const Fenrir::LoadResult<Sword> sword = test.store.Load<Sword>(SheathedSword, level);
    const Fenrir::LoadResult<ShieldData> sheath = test.store.Load<ShieldData>(Labelled(SheathedSword, "sheath"), level);
    const Fenrir::LoadResult<Sword> spare = test.store.Load<Sword>(Labelled(SheathedSword, "spare"), level);
    INFO("all three resolve while the scene lives");
    REQUIRE(test.store.IsValid(sword.handle));
    REQUIRE(test.store.IsValid(sheath.handle));
    REQUIRE(test.store.IsValid(spare.handle));

    test.app.DestroyScene("Level");

    INFO("the sword and both its sub-assets end with the scene");
    REQUIRE_FALSE(test.store.IsValid(sword.handle));
    REQUIRE_FALSE(test.store.IsValid(sheath.handle));
    REQUIRE_FALSE(test.store.IsValid(spare.handle));
}

TEST_CASE("A file's top level loaded for a second owner keeps its sub-assets after the first ends",
          "[assets][data][sub][owners]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);
    Fenrir::Scene& level = test.app.CreateScene("Level");
    const Fenrir::LoadResult<ShieldData> sheath = test.store.Load<ShieldData>(Labelled(SheathedSword, "sheath"), level);
    const Fenrir::LoadResult<Sword> forApp = test.store.Load<Sword>(SheathedSword, Fenrir::AssetOwner::App());
    {
        INFO("the sheath loads for the scene, then the sword for the app");
        REQUIRE(sheath.Ok());
        REQUIRE(forApp.Ok());
    }

    test.app.DestroyScene("Level");

    INFO("loading the top level for the app gave the app the sheath too");
    REQUIRE(test.store.IsValid(forApp.handle));
    REQUIRE(test.store.IsValid(sheath.handle));
}

TEST_CASE("A sub-asset loaded for a second owner alone ends with the first owner's other assets",
          "[assets][data][sub][owners]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);
    Fenrir::Scene& level = test.app.CreateScene("Level");
    const Fenrir::LoadResult<Sword> sword = test.store.Load<Sword>(SheathedSword, level);
    const Fenrir::LoadResult<ShieldData> sheath =
        test.store.Load<ShieldData>(Labelled(SheathedSword, "sheath"), Fenrir::AssetOwner::App());
    {
        INFO("the sword loads for the scene, then the sheath for the app");
        REQUIRE(sword.Ok());
        REQUIRE(sheath.Ok());
    }

    test.app.DestroyScene("Level");

    INFO("the app owns the sheath it loaded, and not the sword the scene loaded");
    REQUIRE(test.store.IsValid(sheath.handle));
    REQUIRE_FALSE(test.store.IsValid(sword.handle));
}

TEST_CASE("A label the file's sub lacks fails with LabelNotFound", "[assets][data][sub]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::LoadResult<ShieldData> loaded =
        test.store.Load<ShieldData>(Labelled(SheathedSword, "quiver"), Fenrir::AssetOwner::App());

    INFO("the file has a sheath and a spare, and no quiver, which the error names");
    REQUIRE(loaded.Status() == Fenrir::LoadStatus::Failed);
    REQUIRE(loaded.diagnostic.has_value());
    REQUIRE(loaded.diagnostic->reason == Fenrir::LoadFailure::LabelNotFound);
    REQUIRE(CountContaining(*test.errors, "quiver") == 1);
}

TEST_CASE("A label on a file with no sub fails with LabelNotFound", "[assets][data][sub]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::LoadResult<ShieldData> loaded =
        test.store.Load<ShieldData>("data/sword.asset.json5#sheath", Fenrir::AssetOwner::App());

    INFO("the plain sword embeds nothing");
    REQUIRE(loaded.Status() == Fenrir::LoadStatus::Failed);
    REQUIRE(loaded.diagnostic.has_value());
    REQUIRE(loaded.diagnostic->reason == Fenrir::LoadFailure::LabelNotFound);
}

TEST_CASE("An empty label fails with LabelNotFound though sub has an empty name", "[assets][data][sub]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::LoadResult<ShieldData> loaded =
        test.store.Load<ShieldData>("data/broken-sub.asset.json5#", Fenrir::AssetOwner::App());

    INFO("`file#` names no sub-asset, and the file's empty-named entry is not one");
    REQUIRE(loaded.Status() == Fenrir::LoadStatus::Failed);
    REQUIRE(loaded.diagnostic.has_value());
    REQUIRE(loaded.diagnostic->reason == Fenrir::LoadFailure::LabelNotFound);
}

TEST_CASE("A sub-asset with an empty name is refused with a located diagnostic", "[assets][data][sub]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::LoadResult<Sword> sword =
        test.store.Load<Sword>("data/broken-sub.asset.json5", Fenrir::AssetOwner::App());

    INFO("the sword loads in part, with an error at the empty-named entry");
    REQUIRE(sword.Status() == Fenrir::LoadStatus::Partial);
    const Fenrir::Serialisation::Diagnostic* empty = FindDiagnostic(sword.diagnostics, "empty one");
    REQUIRE(empty != nullptr);
    REQUIRE(empty->line == 21);
    REQUIRE(empty->column == 9);
}

TEST_CASE("A sub-asset loaded as a type it is not fails with WrongType", "[assets][data][sub]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::LoadResult<Sword> loaded =
        test.store.Load<Sword>(Labelled(SheathedSword, "sheath"), Fenrir::AssetOwner::App());

    INFO("the sheath is a ShieldData, not a Sword");
    REQUIRE(loaded.Status() == Fenrir::LoadStatus::Failed);
    REQUIRE(loaded.diagnostic.has_value());
    REQUIRE(loaded.diagnostic->reason == Fenrir::LoadFailure::WrongType);
}

TEST_CASE("A sub-asset holding a sub of its own is refused with a located diagnostic", "[assets][data][sub]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::LoadResult<Sword> sword =
        test.store.Load<Sword>("data/nested-sub.asset.json5", Fenrir::AssetOwner::App());

    INFO("the sword loads in part, with an error at the scabbard's own `sub`");
    REQUIRE(sword.Status() == Fenrir::LoadStatus::Partial);
    const Fenrir::Serialisation::Diagnostic* nested = FindDiagnostic(sword.diagnostics, "scabbard");
    REQUIRE(nested != nullptr);
    REQUIRE(nested->severity == Fenrir::Serialisation::DiagnosticSeverity::Error);
    REQUIRE(nested->line == 19);
    REQUIRE(nested->column == 12);
}

TEST_CASE("A sub-asset refused for its own sub is logged once", "[assets][data][sub]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    (void)test.store.Load<Sword>("data/nested-sub.asset.json5", Fenrir::AssetOwner::App());
    (void)test.store.Load<ShieldData>("data/nested-sub.asset.json5#sheath", Fenrir::AssetOwner::App());

    INFO("the scabbard's error is logged by the one read of the file");
    REQUIRE(CountContaining(*test.errors, "scabbard") == 1);
}

TEST_CASE("A sub-asset refused for its own sub does not load", "[assets][data][sub]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::LoadResult<ShieldData> scabbard =
        test.store.Load<ShieldData>("data/nested-sub.asset.json5#scabbard", Fenrir::AssetOwner::App());

    INFO("the scabbard is refused, for a reason that is not a missing label");
    REQUIRE(scabbard.Status() == Fenrir::LoadStatus::Failed);
    REQUIRE(scabbard.diagnostic.has_value());
    REQUIRE(scabbard.diagnostic->reason == Fenrir::LoadFailure::ImportFailed);
}

TEST_CASE("A broken sub-asset leaves the load of its file Partial, saying where", "[assets][data][sub]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::LoadResult<Sword> sword =
        test.store.Load<Sword>("data/broken-sub.asset.json5", Fenrir::AssetOwner::App());

    INFO("the sword loads in part, with an error at the cracked entry's `type`");
    REQUIRE(sword.Status() == Fenrir::LoadStatus::Partial);
    REQUIRE(test.store.TryGet(sword.handle) != nullptr);
    const Fenrir::Serialisation::Diagnostic* cracked = FindDiagnostic(sword.diagnostics, "NoSuchType");
    REQUIRE(cracked != nullptr);
    REQUIRE(cracked->line == 17);
    REQUIRE(cracked->column == 13);
}

TEST_CASE("A broken sub-asset leaves its siblings loaded", "[assets][data][sub]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::LoadResult<ShieldData> sheath =
        test.store.Load<ShieldData>("data/broken-sub.asset.json5#sheath", Fenrir::AssetOwner::App());

    INFO("the sheath reads beside the cracked entry, and its load repeats the file's diagnostics: "
         << FailureText(sheath));
    REQUIRE(sheath.Status() == Fenrir::LoadStatus::Partial);
    const ShieldData* data = test.store.TryGet(sheath.handle);
    REQUIRE(data != nullptr);
    REQUIRE_THAT(data->block, WithinAbs(0.25f, 0.0f));
}

TEST_CASE("A broken sub-asset's diagnostic is logged once", "[assets][data][sub]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    (void)test.store.Load<Sword>("data/broken-sub.asset.json5", Fenrir::AssetOwner::App());
    (void)test.store.Load<ShieldData>("data/broken-sub.asset.json5#sheath", Fenrir::AssetOwner::App());

    INFO("the cracked entry's error is logged by the one read of the file");
    REQUIRE(CountContaining(*test.errors, "NoSuchType") == 1);
}

TEST_CASE("Loading a broken sub-asset by name fails with its reason", "[assets][data][sub]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::LoadResult<ShieldData> cracked =
        test.store.Load<ShieldData>("data/broken-sub.asset.json5#cracked", Fenrir::AssetOwner::App());

    INFO("the cracked entry's type is registered by nothing");
    REQUIRE(cracked.Status() == Fenrir::LoadStatus::Failed);
    REQUIRE(cracked.diagnostic.has_value());
    REQUIRE(cracked.diagnostic->reason == Fenrir::LoadFailure::WrongType);
}

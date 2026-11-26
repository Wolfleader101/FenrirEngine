// Hot reload of data assets (ADR-0081): the store watches each data asset file it loaded and, between frames, replaces
// a changed one in place under the handles it already gave, bumping revisions and listing each swap in Reloaded(). A
// broken edit keeps the old value and says why once; a deleted file keeps its data with one warning. Every test runs
// on a temporary asset root of its own, with both reload intervals at zero, and rewrites files between RunFrame calls.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "DataAssetsTest.hpp"

using AssetsTest::CountContaining;
using AssetsTest::DataApp;
using AssetsTest::Fixtures;
using AssetsTest::ShieldData;
using AssetsTest::Sword;
using AssetsTest::TempRoot;

namespace
{
    namespace fs = std::filesystem;

    constexpr std::string_view SwordPath = "data/sword.asset.json5";
    constexpr std::string_view SheathedPath = "data/sheathed-sword.asset.json5";

    // A frame's length; the store's watch reads the real clock, so only the reload intervals matter.
    constexpr double Frame = 1.0 / 60.0;

    // A sword envelope whose display name is `name`, its other fields the fixture's.
    std::string SwordText(std::string_view name, std::string_view extra = {})
    {
        return std::string(
                   "{\n  format: 1,\n  type: \"AssetsTest::Sword\",\n  version: 2,\n  data: {\n    displayName: \"") +
               std::string(name) + "\",\n    stats: { damage: 25, weight: 3.5 },\n" + std::string(extra) + "  },\n}\n";
    }

    // The store, over a copy of `fixtures` in `root`, watching with no interval and no debounce, as a test needs.
    Fenrir::AssetsConfig Watching(const TempRoot& root, bool hotReload = true)
    {
        Fenrir::AssetsConfig config{root.Path()};
        config.hotReload = hotReload;
        config.reloadInterval = 0;
        config.reloadDebounce = 0;
        return config;
    }

    // Runs `frames` frames.
    void RunFrames(DataApp& test, int frames)
    {
        for (int frame = 0; frame < frames; ++frame)
        {
            test.app.RunFrame(Frame);
        }
    }

    // How many of this frame's reloads are of `handle`.
    template <typename T>
    std::size_t TimesReloaded(DataApp& test, Fenrir::AssetHandle<T> handle)
    {
        std::size_t times = 0;
        for (const Fenrir::ReloadedAsset& reloaded : test.store.Reloaded())
        {
            times += reloaded.Is(handle) ? 1 : 0;
        }
        return times;
    }

    // Loads the copied sword for the app, and runs a frame so the watch has looked at it once.
    Fenrir::AssetHandle<Sword> LoadSword(DataApp& test)
    {
        const Fenrir::LoadResult<Sword> loaded = test.store.Load<Sword>(SwordPath, Fenrir::AssetOwner::App());
        REQUIRE(loaded.Status() == Fenrir::LoadStatus::Succeeded);
        RunFrames(test, 1);
        return loaded.handle;
    }
} // namespace

TEST_CASE("A rewritten data asset reads its new value through the same handle within two frames",
          "[assets][data][reload]")
{
    TempRoot root;
    root.Copy(Fixtures() / SwordPath, SwordPath);
    DataApp test(Watching(root));
    const Fenrir::AssetHandle<Sword> sword = LoadSword(test);
    const std::uint32_t revision = test.store.Revision(sword);

    root.Write(SwordPath, SwordText("Reforged longsword"));
    RunFrames(test, 2);

    const Sword* value = test.store.TryGet(sword);
    INFO("the handle the load gave still resolves, now to the file's new value");
    REQUIRE(value != nullptr);
    REQUIRE(value->displayName == "Reforged longsword");
    INFO("its revision moved, so a copy of it knows to update");
    REQUIRE(test.store.Revision(sword) == revision + 1);
    INFO("Reloaded() names it once, by its asset path");
    REQUIRE(TimesReloaded(test, sword) == 1);
    REQUIRE(test.store.Reloaded().size() == 1);
    REQUIRE(test.store.Reloaded().front().path == SwordPath);
    INFO("no error was logged");
    REQUIRE(test.errors->empty());

    RunFrames(test, 1);
    INFO("the next poll clears the list, and nothing changed reloads again");
    REQUIRE(test.store.Reloaded().empty());
    REQUIRE(test.store.Revision(sword) == revision + 1);
}

TEST_CASE("A broken rewrite keeps the old value, says why once, and a fix reloads", "[assets][data][reload]")
{
    TempRoot root;
    root.Copy(Fixtures() / SwordPath, SwordPath);
    DataApp test(Watching(root));
    const Fenrir::AssetHandle<Sword> sword = LoadSword(test);
    const std::uint32_t revision = test.store.Revision(sword);

    root.Write(SwordPath, "{\n  format: 1,\n  type: \"AssetsTest::Sword\",\n  data: { displayName: \"Half-wri");
    RunFrames(test, 5);

    INFO("the sword keeps the value it loaded, at the same revision, and nothing is listed as reloaded");
    REQUIRE(test.store.TryGet(sword)->displayName == "Longsword");
    REQUIRE(test.store.Revision(sword) == revision);
    REQUIRE(test.store.Reloaded().empty());
    INFO("one error names the file and keeps what it loaded, with the located diagnostic, once over five frames");
    REQUIRE(CountContaining(*test.errors, "Could not reload data asset 'data/sword.asset.json5'") == 1);
    REQUIRE(CountContaining(*test.errors, "sword.asset.json5:") == 1);

    root.Write(SwordPath, SwordText("Mended longsword"));
    RunFrames(test, 2);

    INFO("the fixed file reloads under the same handle");
    REQUIRE(test.store.TryGet(sword)->displayName == "Mended longsword");
    REQUIRE(TimesReloaded(test, sword) == 1);
}

TEST_CASE("A rewrite with a value that does not read keeps the whole old value", "[assets][data][reload]")
{
    TempRoot root;
    root.Copy(Fixtures() / SwordPath, SwordPath);
    DataApp test(Watching(root));
    const Fenrir::AssetHandle<Sword> sword = LoadSword(test);

    root.Write(SwordPath, SwordText("Bent longsword", "    edge: \"very\",\n"));
    RunFrames(test, 3);

    INFO("all or nothing: the name that did read is not taken either");
    REQUIRE(test.store.TryGet(sword)->displayName == "Longsword");
    REQUIRE(test.store.Reloaded().empty());
    INFO("the field's located diagnostic and the kept reload are logged once each");
    REQUIRE(CountContaining(*test.errors, "Could not reload data asset") == 1);
    REQUIRE(CountContaining(*test.errors, "edge") == 1);
}

TEST_CASE("A deleted data asset file keeps its data with one warning, and reloads when it comes back",
          "[assets][data][reload]")
{
    TempRoot root;
    root.Copy(Fixtures() / SwordPath, SwordPath);
    DataApp test(Watching(root));
    const Fenrir::AssetHandle<Sword> sword = LoadSword(test);

    fs::remove(root.Path() / SwordPath);
    RunFrames(test, 4);

    INFO("the sword keeps its data, with one warning naming the file over four frames, and no error");
    REQUIRE(test.store.TryGet(sword)->displayName == "Longsword");
    REQUIRE(CountContaining(*test.warnings, "'data/sword.asset.json5' is gone") == 1);
    REQUIRE(test.errors->empty());

    root.Write(SwordPath, SwordText("Returned longsword"));
    RunFrames(test, 2);

    INFO("the file that came back reloads under the same handle");
    REQUIRE(test.store.TryGet(sword)->displayName == "Returned longsword");
    REQUIRE(TimesReloaded(test, sword) == 1);
}

TEST_CASE("With hotReload off, a rewritten data asset is not reloaded", "[assets][data][reload]")
{
    TempRoot root;
    root.Copy(Fixtures() / SwordPath, SwordPath);
    DataApp test(Watching(root, false));
    const Fenrir::AssetHandle<Sword> sword = LoadSword(test);
    const std::uint32_t revision = test.store.Revision(sword);

    root.Write(SwordPath, SwordText("Reforged longsword"));
    RunFrames(test, 3);

    INFO("the sword is as it loaded, nothing is listed and nothing is logged");
    REQUIRE(test.store.TryGet(sword)->displayName == "Longsword");
    REQUIRE(test.store.Revision(sword) == revision);
    REQUIRE(test.store.Reloaded().empty());
    REQUIRE(test.errors->empty());
    REQUIRE(test.warnings->empty());
}

TEST_CASE("A rewritten file reloads its sub-assets under their own handles", "[assets][data][reload][sub]")
{
    TempRoot root;
    root.Copy(Fixtures() / SheathedPath, SheathedPath);
    DataApp test(Watching(root));
    const Fenrir::LoadResult<Sword> top = test.store.Load<Sword>(SheathedPath, Fenrir::AssetOwner::App());
    const Fenrir::LoadResult<ShieldData> sheath =
        test.store.Load<ShieldData>(std::string(SheathedPath) + "#sheath", Fenrir::AssetOwner::App());
    const Fenrir::LoadResult<Sword> spare =
        test.store.Load<Sword>(std::string(SheathedPath) + "#spare", Fenrir::AssetOwner::App());
    REQUIRE(top.Ok());
    REQUIRE(sheath.Ok());
    REQUIRE(spare.Ok());
    RunFrames(test, 1);

    root.Write(SheathedPath, R"({
  format: 1,
  type: "AssetsTest::Sword",
  version: 2,
  data: { displayName: "Resheathed sword" },
  sub: {
    sheath: { type: "AssetsTest::ShieldData", version: 1, data: { block: 0.25 } },
    spare: { type: "AssetsTest::Sword", version: 2, data: { displayName: "Second spare" } },
  },
}
)");
    RunFrames(test, 2);

    INFO("each handle reads its entry's new value");
    REQUIRE(test.store.TryGet(top.handle)->displayName == "Resheathed sword");
    REQUIRE(test.store.TryGet(sheath.handle)->block == 0.25f);
    REQUIRE(test.store.TryGet(spare.handle)->displayName == "Second spare");
    INFO("Reloaded() lists the top level and each sub-asset once, by its own asset path");
    REQUIRE(test.store.Reloaded().size() == 3);
    REQUIRE(TimesReloaded(test, top.handle) == 1);
    REQUIRE(TimesReloaded(test, sheath.handle) == 1);
    REQUIRE(TimesReloaded(test, spare.handle) == 1);
    REQUIRE(test.store.PathOf(sheath.handle) == std::string(SheathedPath) + "#sheath");
}

TEST_CASE("A reference a reload adds loads with every owner of the reloaded asset", "[assets][data][reload][owners]")
{
    TempRoot root;
    root.Copy(Fixtures() / SwordPath, SwordPath);
    root.Copy(Fixtures() / "fourbytwo.png", "fourbytwo.png");
    DataApp test(Watching(root));
    Fenrir::Scene& level = test.app.CreateScene("Level");
    const Fenrir::LoadResult<Sword> loaded = test.store.Load<Sword>(SwordPath, level);
    REQUIRE(loaded.Ok());
    RunFrames(test, 1);

    root.Write(SwordPath, SwordText("Gilded longsword", "    icon: \"fourbytwo.png\",\n"));
    RunFrames(test, 2);

    const Sword* sword = test.store.TryGet(loaded.handle);
    REQUIRE(sword != nullptr);
    REQUIRE(sword->displayName == "Gilded longsword");
    const Fenrir::AssetHandle<Fenrir::Image> icon = sword->icon;
    INFO("the icon the new file names loaded with the sword");
    REQUIRE(test.store.TryGet(icon) != nullptr);
    REQUIRE(test.store.PathOf(icon) == "fourbytwo.png");

    test.app.DestroyScene("Level");
    INFO("the scene was the icon's only owner: the reload held it for no one else, so it goes with the scene");
    REQUIRE_FALSE(test.store.IsValid(icon));
    REQUIRE_FALSE(test.store.IsValid(loaded.handle));
}

TEST_CASE("A failed reload frees what its read loaded", "[assets][data][reload][owners]")
{
    TempRoot root;
    root.Copy(Fixtures() / SwordPath, SwordPath);
    root.Copy(Fixtures() / "fourbytwo.png", "fourbytwo.png");
    DataApp test(Watching(root));
    const Fenrir::AssetHandle<Sword> sword = LoadSword(test);

    // The icon reads and loads; the edge does not, so the reload keeps the old sword.
    root.Write(SwordPath, SwordText("Gilded longsword", "    icon: \"fourbytwo.png\",\n    edge: \"very\",\n"));
    RunFrames(test, 2);

    REQUIRE(test.store.TryGet(sword)->displayName == "Longsword");
    INFO("the image the failed read loaded is held for no one: loaded now for a scene, it goes with the scene");
    Fenrir::Scene& level = test.app.CreateScene("Level");
    const Fenrir::LoadResult<Fenrir::Image> image = test.store.Load<Fenrir::Image>("fourbytwo.png", level);
    REQUIRE(image.Ok());
    test.app.DestroyScene("Level");
    REQUIRE_FALSE(test.store.IsValid(image.handle));
    REQUIRE(test.store.Reloaded().empty());
}

TEST_CASE("Saving a data asset to its own file is no change to reload", "[assets][data][reload][saving]")
{
    TempRoot root;
    root.Copy(Fixtures() / SwordPath, SwordPath);
    DataApp test(Watching(root));
    const Fenrir::AssetHandle<Sword> sword = LoadSword(test);

    test.store.Edit(sword, "rename")->displayName = "Saved longsword";
    const std::uint32_t revision = test.store.Revision(sword);
    REQUIRE(test.store.Save(sword).status == Fenrir::SaveStatus::Succeeded);
    RunFrames(test, 3);

    INFO("the store's own write reloads nothing");
    REQUIRE(test.store.Reloaded().empty());
    REQUIRE(test.store.Revision(sword) == revision);
    REQUIRE(test.store.TryGet(sword)->displayName == "Saved longsword");
}

TEST_CASE("An unloaded data asset's file is no longer watched", "[assets][data][reload]")
{
    TempRoot root;
    root.Copy(Fixtures() / SwordPath, SwordPath);
    DataApp test(Watching(root));
    const Fenrir::AssetHandle<Sword> sword = LoadSword(test);

    REQUIRE(test.store.Unload(sword));
    fs::remove(root.Path() / SwordPath);
    RunFrames(test, 2);

    INFO("a file the store holds nothing from is not warned about when it goes");
    REQUIRE(test.warnings->empty());
    REQUIRE(test.store.Reloaded().empty());
}

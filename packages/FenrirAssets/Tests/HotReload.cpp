// Hot reload (ADR-0081): the store watches each file it loaded, data asset or imported, and, between frames, replaces
// a changed one in place under the handles it already gave, bumping revisions and listing each swap in Reloaded(). A
// broken edit keeps the old value and says why once; a deleted file keeps its data with one warning. Every test runs
// on a temporary asset root of its own, with both reload intervals at zero, and rewrites files between RunFrame calls.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
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

    // A config whose asset root is `root`, watching with no interval and no debounce, as a test needs.
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
        INFO("the copied sword loads whole");
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

TEST_CASE("A reload may move a data asset to a type extending the one it first held, and back",
          "[assets][data][reload][types]")
{
    TempRoot root;
    root.Copy(Fixtures() / "data/weapon.asset.json5", "data/weapon.asset.json5");
    DataApp test(Watching(root));
    const Fenrir::LoadResult<AssetsTest::WeaponData> loaded =
        test.store.Load<AssetsTest::WeaponData>("data/weapon.asset.json5", Fenrir::AssetOwner::App());
    INFO("the weapon loads whole");
    REQUIRE(loaded.Status() == Fenrir::LoadStatus::Succeeded);
    RunFrames(test, 1);

    root.Write("data/weapon.asset.json5", SwordText("Weapon made sword"));
    RunFrames(test, 2);
    INFO("a sword is a weapon, so the weapon's handle reads it");
    REQUIRE(test.store.TryGet(loaded.handle)->displayName == "Weapon made sword");

    root.Write("data/weapon.asset.json5",
               R"({ format: 1, type: "AssetsTest::WeaponData", version: 1, data: { displayName: "A weapon again" } }
)");
    RunFrames(test, 2);
    INFO("back to the type it first held reloads too, with nothing logged");
    REQUIRE(test.store.TryGet(loaded.handle)->displayName == "A weapon again");
    REQUIRE(TimesReloaded(test, loaded.handle) == 1);
    REQUIRE(test.errors->empty());

    root.Write("data/weapon.asset.json5",
               R"({ format: 1, type: "AssetsTest::ShieldData", version: 1, data: { block: 0.5 } }
)");
    RunFrames(test, 2);
    INFO("a shield is no weapon: the reload is refused and the weapon kept");
    REQUIRE(test.store.TryGet(loaded.handle)->displayName == "A weapon again");
    REQUIRE(CountContaining(*test.errors, "Could not reload data asset") == 1);
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
    INFO("the sheathed sword and both its sub-assets load");
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
    INFO("the sword loads for the scene");
    REQUIRE(loaded.Ok());
    RunFrames(test, 1);

    root.Write(SwordPath, SwordText("Gilded longsword", "    icon: \"fourbytwo.png\",\n"));
    RunFrames(test, 2);

    const Sword* sword = test.store.TryGet(loaded.handle);
    INFO("the sword reads the new file");
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

    INFO("the reload is refused, so the sword keeps the name it loaded");
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
    INFO("the edited sword saves to its own file");
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

    INFO("the sword unloads");
    REQUIRE(test.store.Unload(sword));
    fs::remove(root.Path() / SwordPath);
    RunFrames(test, 2);

    INFO("a file the store holds nothing from is not warned about when it goes");
    REQUIRE(test.warnings->empty());
    REQUIRE(test.store.Reloaded().empty());
}

// Imported files (ADR-0081 §2): an image, cubemap, font or model file reloads through its importer, replacing its
// assets under the handles they had.

namespace
{
    constexpr std::string_view ImagePath = "images/icon.png";
    constexpr std::string_view CubemapPath = "cubemap/sky.cubemap";
    constexpr std::string_view FontPath = "fonts/tagged.ttf";
    constexpr std::string_view ModelPath = "models/prop.gltf";

    // A cubemap file naming the fixture faces, `first` as its +X face and `second` as its -X face.
    std::string CubemapText(std::string_view first, std::string_view second)
    {
        return std::format("+X {}\n-X {}\n+Y py.png\n-Y ny.png\n+Z pz.png\n-Z nz.png\n", first, second);
    }

    // The red, green, blue and alpha of the first pixel of the cubemap's +X face, its top left.
    std::array<unsigned, 4> FirstPixel(const Fenrir::ImageData& image)
    {
        return {std::to_integer<unsigned>(image.pixels.at(0)), std::to_integer<unsigned>(image.pixels.at(1)),
                std::to_integer<unsigned>(image.pixels.at(2)), std::to_integer<unsigned>(image.pixels.at(3))};
    }

    // A root holding the six cubemap fixture faces and a cubemap file naming them in order.
    void CopyCubemap(const TempRoot& root)
    {
        for (const std::string_view face : {"px.png", "nx.png", "py.png", "ny.png", "pz.png", "nz.png"})
        {
            root.Copy(Fixtures() / "cubemap" / face, std::format("cubemap/{}", face));
        }
        root.Write(CubemapPath, CubemapText("px.png", "nx.png"));
    }

    // A root holding the three-mesh tangents glTF as the prop, and the buffers of it and of the two-mesh owners glTF
    // it is rewritten as.
    void CopyModel(const TempRoot& root)
    {
        root.Copy(Fixtures() / "models/tangents.gltf", ModelPath);
        root.Copy(Fixtures() / "models/tangents.bin", "models/tangents.bin");
        root.Copy(Fixtures() / "models/owners.bin", "models/owners.bin");
    }

    // Loads the copied prop for the app, and runs a frame so the watch has looked at it once.
    Fenrir::AssetHandle<Fenrir::Model> LoadModel(DataApp& test)
    {
        const Fenrir::LoadResult<Fenrir::Model> loaded =
            test.store.Load<Fenrir::Model>(ModelPath, Fenrir::AssetOwner::App());
        INFO("the copied prop loads whole");
        REQUIRE(loaded.Status() == Fenrir::LoadStatus::Succeeded);
        RunFrames(test, 1);
        return loaded.handle;
    }
} // namespace

TEST_CASE("A rewritten image reloads under its handle within two frames", "[assets][images][reload]")
{
    TempRoot root;
    root.Copy(Fixtures() / "fourbytwo.png", ImagePath);
    DataApp test(Watching(root));
    const Fenrir::LoadResult<Fenrir::Image> loaded =
        test.store.Load<Fenrir::Image>(ImagePath, Fenrir::AssetOwner::App());
    INFO("the copied 4 by 2 image loads");
    REQUIRE(loaded.Ok());
    RunFrames(test, 1);
    const std::uint32_t revision = test.store.Revision(loaded.handle);

    root.Copy(Fixtures() / "threebyfive.jpg", ImagePath);
    RunFrames(test, 2);

    const Fenrir::ImageData* image = test.store.TryGet(loaded.handle);
    INFO("the handle the load gave reads the new file's 3 by 5 pixels");
    REQUIRE(image != nullptr);
    REQUIRE(image->width == 3);
    REQUIRE(image->height == 5);
    INFO("its revision moved, and Reloaded() names it once, by its asset path");
    REQUIRE(test.store.Revision(loaded.handle) == revision + 1);
    REQUIRE(test.store.Reloaded().size() == 1);
    REQUIRE(TimesReloaded(test, loaded.handle) == 1);
    REQUIRE(test.store.Reloaded().front().path == ImagePath);
    REQUIRE(test.errors->empty());
}

TEST_CASE("A broken rewrite of an image keeps its pixels and says why once", "[assets][images][reload]")
{
    TempRoot root;
    root.Copy(Fixtures() / "fourbytwo.png", ImagePath);
    DataApp test(Watching(root));
    const Fenrir::LoadResult<Fenrir::Image> loaded =
        test.store.Load<Fenrir::Image>(ImagePath, Fenrir::AssetOwner::App());
    INFO("the copied image loads");
    REQUIRE(loaded.Ok());
    RunFrames(test, 1);

    root.Copy(Fixtures() / "corrupt.png", ImagePath);
    RunFrames(test, 5);

    INFO("the image keeps the 4 by 2 pixels it loaded, and nothing is listed as reloaded");
    REQUIRE(test.store.TryGet(loaded.handle)->width == 4);
    REQUIRE(test.store.Reloaded().empty());
    INFO("one error over five frames names the file and keeps what it loaded");
    REQUIRE(CountContaining(*test.errors, "Could not reload image 'images/icon.png'") == 1);
    REQUIRE(test.errors->size() == 1);
}

TEST_CASE("A rewritten .cubemap file reloads its faces under its handle", "[assets][images][cubemaps][reload]")
{
    TempRoot root;
    CopyCubemap(root);
    DataApp test(Watching(root));
    const Fenrir::LoadResult<Fenrir::Image> loaded =
        test.store.Load<Fenrir::Image>(CubemapPath, Fenrir::AssetOwner::App());
    INFO("the copied cubemap loads, its +X face red");
    REQUIRE(loaded.Ok());
    REQUIRE(FirstPixel(*test.store.TryGet(loaded.handle)) == std::array<unsigned, 4>{255, 0, 0, 255});
    RunFrames(test, 1);

    root.Write(CubemapPath, CubemapText("nx.png", "px.png"));
    RunFrames(test, 2);

    INFO("the handle reads the green face the file names for +X now, and Reloaded() names it once");
    REQUIRE(FirstPixel(*test.store.TryGet(loaded.handle)) == std::array<unsigned, 4>{0, 255, 0, 255});
    REQUIRE(TimesReloaded(test, loaded.handle) == 1);
    REQUIRE(test.errors->empty());
}

TEST_CASE("A broken rewrite of a .cubemap file keeps its faces and says why once", "[assets][images][cubemaps][reload]")
{
    TempRoot root;
    CopyCubemap(root);
    DataApp test(Watching(root));
    const Fenrir::LoadResult<Fenrir::Image> loaded =
        test.store.Load<Fenrir::Image>(CubemapPath, Fenrir::AssetOwner::App());
    INFO("the copied cubemap loads");
    REQUIRE(loaded.Ok());
    RunFrames(test, 1);

    root.Write(CubemapPath, CubemapText("nx.png", "gone.png"));
    RunFrames(test, 5);

    INFO("the cubemap keeps its red +X face, and nothing is listed as reloaded");
    REQUIRE(FirstPixel(*test.store.TryGet(loaded.handle)) == std::array<unsigned, 4>{255, 0, 0, 255});
    REQUIRE(test.store.Reloaded().empty());
    INFO("one error over five frames names the file and the face it could not find");
    REQUIRE(CountContaining(*test.errors, "Could not reload image 'cubemap/sky.cubemap'") == 1);
    REQUIRE(CountContaining(*test.errors, "gone.png") == 1);
}

TEST_CASE("A font file broken and then restored keeps its font, then reloads it", "[assets][fonts][reload]")
{
    TempRoot root;
    root.Copy(Fixtures() / FontPath, FontPath);
    DataApp test(Watching(root));
    const Fenrir::LoadResult<Fenrir::Font> loaded = test.store.Load<Fenrir::Font>(FontPath, Fenrir::AssetOwner::App());
    INFO("the copied font loads");
    REQUIRE(loaded.Ok());
    RunFrames(test, 1);
    const std::size_t size = test.store.TryGet(loaded.handle)->bytes.size();

    root.Write(FontPath, "not a font");
    RunFrames(test, 3);
    INFO("the broken rewrite keeps the font's bytes, with one error");
    REQUIRE(test.store.TryGet(loaded.handle)->bytes.size() == size);
    REQUIRE(CountContaining(*test.errors, "Could not reload font 'fonts/tagged.ttf'") == 1);

    root.Copy(Fixtures() / FontPath, FontPath);
    RunFrames(test, 2);
    INFO("the restored file reloads under the same handle");
    REQUIRE(TimesReloaded(test, loaded.handle) == 1);
    REQUIRE(test.store.TryGet(loaded.handle)->bytes.size() == size);
}

TEST_CASE("A rewritten glTF replaces its model and its meshes under their labels", "[assets][models][reload]")
{
    TempRoot root;
    CopyModel(root);
    DataApp test(Watching(root));
    const Fenrir::AssetHandle<Fenrir::Model> model = LoadModel(test);
    const Fenrir::LoadResult<Fenrir::Mesh> second =
        test.store.Load<Fenrir::Mesh>(std::string(ModelPath) + "#Mesh1", Fenrir::AssetOwner::App());
    INFO("the prop has three meshes, its second of four vertices");
    REQUIRE(test.store.TryGet(model)->meshes.size() == 3);
    REQUIRE(test.store.TryGet(second.handle)->positions.size() == 4);
    const std::uint32_t revision = test.store.Revision(second.handle);

    root.Copy(Fixtures() / "models/owners.gltf", ModelPath);
    RunFrames(test, 2);

    INFO("the model's handle reads the new file's two meshes, the first still under the label it had");
    REQUIRE(test.store.TryGet(model)->meshes.size() == 2);
    REQUIRE(test.store.TryGet(model)->meshes.at(1) == second.handle);
    INFO("the second mesh's handle reads the new file's three vertices, a revision on");
    REQUIRE(test.store.TryGet(second.handle)->positions.size() == 3);
    REQUIRE(test.store.Revision(second.handle) == revision + 1);
    INFO("Reloaded() names the model and each mesh it replaced once, by asset path");
    REQUIRE(TimesReloaded(test, model) == 1);
    REQUIRE(TimesReloaded(test, second.handle) == 1);
    REQUIRE(std::ranges::count(test.store.Reloaded(), std::string(ModelPath) + "#Mesh0",
                               &Fenrir::ReloadedAsset::path) == 1);
    REQUIRE(test.errors->empty());
}

TEST_CASE("A mesh the rewritten model file no longer has keeps its data, with one warning", "[assets][models][reload]")
{
    TempRoot root;
    CopyModel(root);
    DataApp test(Watching(root));
    LoadModel(test);
    const Fenrir::LoadResult<Fenrir::Mesh> third =
        test.store.Load<Fenrir::Mesh>(std::string(ModelPath) + "#Mesh2", Fenrir::AssetOwner::App());
    INFO("the prop's third mesh loads");
    REQUIRE(third.Ok());
    const std::uint32_t revision = test.store.Revision(third.handle);

    root.Copy(Fixtures() / "models/owners.gltf", ModelPath);
    RunFrames(test, 3);

    INFO("the third mesh keeps its four vertices at the same revision, and is not listed as reloaded");
    REQUIRE(test.store.TryGet(third.handle)->positions.size() == 4);
    REQUIRE(test.store.Revision(third.handle) == revision);
    REQUIRE(TimesReloaded(test, third.handle) == 0);
    INFO("one warning names the file and the sub-asset it lost");
    REQUIRE(CountContaining(*test.warnings, "'models/prop.gltf' no longer has its sub-asset 'Mesh2'") == 1);
}

TEST_CASE("A broken rewrite of a model file keeps the model and its meshes and says why once",
          "[assets][models][reload]")
{
    TempRoot root;
    CopyModel(root);
    DataApp test(Watching(root));
    const Fenrir::AssetHandle<Fenrir::Model> model = LoadModel(test);
    const std::uint32_t revision = test.store.Revision(model);

    root.Write(ModelPath, "{ \"asset\": { \"version\": ");
    RunFrames(test, 5);

    INFO("the model keeps its three meshes at the same revision, and nothing is listed as reloaded");
    REQUIRE(test.store.TryGet(model)->meshes.size() == 3);
    REQUIRE(test.store.Revision(model) == revision);
    REQUIRE(test.store.Reloaded().empty());
    INFO("one error over five frames names the file and keeps what it loaded");
    REQUIRE(CountContaining(*test.errors, "Could not reload model 'models/prop.gltf'") == 1);
    REQUIRE(test.errors->size() == 1);
}

TEST_CASE("An unloaded image's file is no longer watched", "[assets][images][reload]")
{
    TempRoot root;
    root.Copy(Fixtures() / "fourbytwo.png", ImagePath);
    DataApp test(Watching(root));
    const Fenrir::LoadResult<Fenrir::Image> loaded =
        test.store.Load<Fenrir::Image>(ImagePath, Fenrir::AssetOwner::App());
    INFO("the copied image loads, then unloads");
    REQUIRE(loaded.Ok());
    RunFrames(test, 1);
    REQUIRE(test.store.Unload(loaded.handle));

    fs::remove(root.Path() / ImagePath);
    RunFrames(test, 2);

    INFO("a file the store holds nothing from is not warned about when it goes");
    REQUIRE(test.warnings->empty());
}

TEST_CASE("A model file stays watched while a mesh outlives its model", "[assets][models][reload]")
{
    TempRoot root;
    CopyModel(root);
    DataApp test(Watching(root));
    const Fenrir::AssetHandle<Fenrir::Model> model = LoadModel(test);
    const Fenrir::LoadResult<Fenrir::Mesh> second =
        test.store.Load<Fenrir::Mesh>(std::string(ModelPath) + "#Mesh1", Fenrir::AssetOwner::App());
    INFO("the model unloads, its second mesh held on its own");
    REQUIRE(test.store.Unload(model));
    REQUIRE(test.store.IsValid(second.handle));

    root.Copy(Fixtures() / "models/owners.gltf", ModelPath);
    RunFrames(test, 2);

    INFO("the mesh reloads under its handle; no model is stored again by it");
    REQUIRE(test.store.TryGet(second.handle)->positions.size() == 3);
    REQUIRE(TimesReloaded(test, second.handle) == 1);
    REQUIRE_FALSE(test.store.IsValid(model));
}

TEST_CASE("A mesh a rewritten model file adds loads under its label for the model's owners",
          "[assets][models][reload][owners]")
{
    TempRoot root;
    root.Copy(Fixtures() / "models/owners.gltf", ModelPath);
    root.Copy(Fixtures() / "models/owners.bin", "models/owners.bin");
    root.Copy(Fixtures() / "models/tangents.bin", "models/tangents.bin");
    DataApp test(Watching(root));
    Fenrir::Scene& level = test.app.CreateScene("Level");
    const Fenrir::LoadResult<Fenrir::Model> loaded = test.store.Load<Fenrir::Model>(ModelPath, level);
    INFO("the two-mesh prop loads for the scene");
    REQUIRE(loaded.Ok());
    RunFrames(test, 1);

    root.Copy(Fixtures() / "models/tangents.gltf", ModelPath);
    RunFrames(test, 2);

    INFO("the model reads three meshes, the third loadable by its label");
    REQUIRE(test.store.TryGet(loaded.handle)->meshes.size() == 3);
    const Fenrir::AssetHandle<Fenrir::Mesh> third = test.store.TryGet(loaded.handle)->meshes.at(2);
    REQUIRE(test.store.PathOf(third) == std::string(ModelPath) + "#Mesh2");
    INFO("an added mesh is no replacement, so Reloaded() does not list it");
    REQUIRE(TimesReloaded(test, third) == 0);

    test.app.DestroyScene("Level");
    INFO("the scene was the model's only owner, so the added mesh goes with it");
    REQUIRE_FALSE(test.store.IsValid(third));
}

TEST_CASE("A rewritten model file that imports only in part replaces nothing and says why once",
          "[assets][models][reload]")
{
    TempRoot root;
    CopyModel(root);
    root.Copy(Fixtures() / "models/missing-texture.bin", "models/missing-texture.bin");
    DataApp test(Watching(root));
    const Fenrir::AssetHandle<Fenrir::Model> model = LoadModel(test);
    const std::uint32_t revision = test.store.Revision(model);

    // Its one mesh would import, but its texture names a file that is not there.
    root.Copy(Fixtures() / "models/missing-texture.gltf", ModelPath);
    RunFrames(test, 5);

    INFO("the model keeps its three meshes at the same revision, and nothing is listed as reloaded");
    REQUIRE(test.store.TryGet(model)->meshes.size() == 3);
    REQUIRE(test.store.Revision(model) == revision);
    REQUIRE(test.store.Reloaded().empty());
    INFO("one error over five frames names the file and the texture it left out");
    REQUIRE(CountContaining(*test.errors, "Could not reload model 'models/prop.gltf': it imported only in part") == 1);
    REQUIRE(test.errors->size() == 1);
}

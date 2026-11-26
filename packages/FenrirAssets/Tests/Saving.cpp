// Saving data assets (ADR-0077, ADR-0079, ADR-0085): Save writes an asset and its sub-assets back through the envelope
// writer, the same data as the same bytes with the file's comments kept, atomically, and only under the asset root.
// Every test saves into a temporary asset root of its own, holding copies of the fixtures it needs.

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include <catch2/catch_test_macros.hpp>

#include "DataAssetsTest.hpp"

using AssetsTest::DataApp;
using AssetsTest::Fixtures;
using AssetsTest::ReadFile;
using AssetsTest::ShieldData;
using AssetsTest::Sword;
using AssetsTest::TempRoot;

namespace
{
    namespace fs = std::filesystem;

    // `text` with its one `from` changed to `to`, as an edit of one field changes a file's text.
    std::string Replaced(std::string text, std::string_view from, std::string_view to)
    {
        const std::size_t at = text.find(from);
        if (at != std::string::npos)
        {
            text.replace(at, from.size(), to);
        }
        return text;
    }

    // The format research's sword, FenrirSerialisation's golden file, as it is committed there.
    fs::path Golden()
    {
        return fs::absolute(FENRIR_SERIALISATION_GOLDEN_SWORD);
    }

    constexpr std::string_view GoldenPath = "golden/sword.asset.json5";
    constexpr std::string_view CommentedPath = "saving/commented-sword.asset.json5";
    constexpr std::string_view SheathedPath = "saving/sheathed-sword.asset.json5";

    // A shield, added with no name, that parries a sword added with no name: a reference no file can hold.
    Fenrir::AssetHandle<ShieldData> ShieldParryingUnnamed(DataApp& test)
    {
        Sword sword;
        sword.displayName = "Unsaved sword";
        ShieldData shield;
        shield.parries = test.store.Add<Sword>(sword, Fenrir::AssetOwner::App());
        return test.store.Add<ShieldData>(shield, Fenrir::AssetOwner::App());
    }
} // namespace

TEST_CASE("Loading the golden sword then saving it reproduces its bytes", "[assets][saving][golden]")
{
    TempRoot root;
    root.Copy(Golden(), GoldenPath);
    DataApp test({root.Path()});
    const Fenrir::LoadResult<MyGame::Sword> loaded =
        test.store.Load<MyGame::Sword>(GoldenPath, Fenrir::AssetOwner::App());
    {
        INFO("the golden sword and its sparks load through the test package's MyGame types, with nothing to report");
        REQUIRE(loaded.Status() == Fenrir::LoadStatus::Succeeded);
    }

    const Fenrir::SaveResult saved = test.store.Save(loaded.handle);

    INFO("the save succeeds, and the file it wrote over is the golden file byte for byte, its comment and sub-asset "
         "included");
    REQUIRE(saved.status == Fenrir::SaveStatus::Succeeded);
    REQUIRE(ReadFile(root.Path() / GoldenPath) == ReadFile(Golden()));
}

TEST_CASE("Saving the golden sword a second time reproduces its bytes again", "[assets][saving][golden]")
{
    TempRoot root;
    root.Copy(Golden(), GoldenPath);
    DataApp test({root.Path()});
    const Fenrir::AssetHandle<MyGame::Sword> sword =
        test.store.Load<MyGame::Sword>(GoldenPath, Fenrir::AssetOwner::App()).handle;
    {
        INFO("the first save succeeds");
        REQUIRE(test.store.Save(sword).status == Fenrir::SaveStatus::Succeeded);
    }

    const Fenrir::SaveResult saved = test.store.Save(sword);

    INFO("the second save, from what the first wrote, still writes the golden file byte for byte");
    REQUIRE(saved.status == Fenrir::SaveStatus::Succeeded);
    REQUIRE(ReadFile(root.Path() / GoldenPath) == ReadFile(Golden()));
}

TEST_CASE("A file's comments survive a load, an Edit of another field and a save", "[assets][saving][comments]")
{
    TempRoot root;
    root.Copy(Fixtures() / CommentedPath, CommentedPath);
    DataApp test({root.Path()});
    const Fenrir::AssetHandle<Sword> sword = test.store.Load<Sword>(CommentedPath, Fenrir::AssetOwner::App()).handle;
    Sword* edited = test.store.Edit(sword, "sharpen a sword");
    {
        INFO("the sword loads and edits");
        REQUIRE(edited != nullptr);
    }
    edited->edge = 0.75f;

    const Fenrir::SaveResult saved = test.store.Save(sword);

    INFO(
        "the file is the fixture with only the edited edge changed: the comments on the envelope, on a field and after "
        "a field are each where they were");
    REQUIRE(saved.status == Fenrir::SaveStatus::Succeeded);
    REQUIRE(ReadFile(root.Path() / CommentedPath) ==
            Replaced(ReadFile(Fixtures() / CommentedPath), "edge: 0.25", "edge: 0.75"));
}

TEST_CASE("A file read at an older version is saved at its type's version", "[assets][saving][version]")
{
    TempRoot root;
    root.Copy(Fixtures() / "data/old-sword.asset.json5", "old-sword.asset.json5");
    DataApp test({root.Path()});
    const Fenrir::AssetHandle<Sword> sword =
        test.store.Load<Sword>("old-sword.asset.json5", Fenrir::AssetOwner::App()).handle;

    const Fenrir::SaveResult saved = test.store.Save(sword);

    INFO("the file, written at version 1, now says the sword's version, 2");
    REQUIRE(saved.status != Fenrir::SaveStatus::Failed);
    const std::string text = ReadFile(root.Path() / "old-sword.asset.json5");
    REQUIRE(text.find("version: 2,") != std::string::npos);
    REQUIRE(text.find("version: 1,") == std::string::npos);
}

TEST_CASE("Saving through a sub-asset's handle writes its whole file", "[assets][saving][sub]")
{
    TempRoot root;
    root.Copy(Fixtures() / SheathedPath, SheathedPath);
    DataApp test({root.Path()});
    const Fenrir::AssetHandle<Sword> spare =
        test.store.Load<Sword>(std::format("{}#spare", SheathedPath), Fenrir::AssetOwner::App()).handle;
    Sword* edited = test.store.Edit(spare, "rename a spare blade");
    {
        INFO("the spare blade loads and edits");
        REQUIRE(edited != nullptr);
    }
    edited->displayName = "Notched spare";

    const Fenrir::SaveResult saved = test.store.Save(spare);

    INFO("the file keeps its top-level sword and writes the edited spare under `sub`, all else unchanged");
    REQUIRE(saved.status == Fenrir::SaveStatus::Succeeded);
    REQUIRE(ReadFile(root.Path() / SheathedPath) ==
            Replaced(ReadFile(Fixtures() / SheathedPath), "Spare blade", "Notched spare"));
}

TEST_CASE("Saving a sub-asset's handle to a path is refused", "[assets][saving][sub]")
{
    TempRoot root;
    root.Copy(Fixtures() / SheathedPath, SheathedPath);
    DataApp test({root.Path()});
    const Fenrir::AssetHandle<Sword> spare =
        test.store.Load<Sword>(std::format("{}#spare", SheathedPath), Fenrir::AssetOwner::App()).handle;

    const Fenrir::SaveResult saved = test.store.Save(spare, "spare.asset.json5");

    INFO("the save fails with one logged error, and no file is written");
    REQUIRE(saved.status == Fenrir::SaveStatus::Failed);
    REQUIRE(saved.diagnostics.size() == 1);
    REQUIRE(test.errors->size() == 1);
    REQUIRE_FALSE(fs::exists(root.Path() / "spare.asset.json5"));
}

TEST_CASE("Saving an unnamed asset to a path gives it that path", "[assets][saving][paths]")
{
    TempRoot root;
    DataApp test({root.Path()});
    Sword value;
    value.displayName = "Forged sword";
    const Fenrir::AssetHandle<Sword> sword = test.store.Add<Sword>(value, Fenrir::AssetOwner::App());

    const Fenrir::SaveResult saved = test.store.Save(sword, "forge/forged.asset.json5");

    INFO("the save writes the file, and the asset now answers that path, so a load of it gives the same handle");
    REQUIRE(saved.status == Fenrir::SaveStatus::Succeeded);
    REQUIRE(fs::exists(root.Path() / "forge/forged.asset.json5"));
    REQUIRE(test.store.PathOf(sword) == std::optional<std::string>("forge/forged.asset.json5"));
    REQUIRE(test.store.Load<Sword>("forge/forged.asset.json5", Fenrir::AssetOwner::App()).handle == sword);
}

TEST_CASE("Saving a named asset to a new path leaves its own path unchanged", "[assets][saving][paths]")
{
    TempRoot root;
    DataApp test({root.Path()});
    const Fenrir::AssetHandle<Sword> sword =
        test.store.Add<Sword>(Sword{}, Fenrir::AssetOwner::App(), "named.asset.json5");

    const Fenrir::SaveResult saved = test.store.Save(sword, "copies/copy.asset.json5");

    INFO("the copy is written, and the asset still answers the name it was added under");
    REQUIRE(saved.status == Fenrir::SaveStatus::Succeeded);
    REQUIRE(fs::exists(root.Path() / "copies/copy.asset.json5"));
    REQUIRE(test.store.PathOf(sword) == std::optional<std::string>("named.asset.json5"));
}

TEST_CASE("Saving to the file another asset is from is refused", "[assets][saving][paths]")
{
    TempRoot root;
    root.Copy(Fixtures() / CommentedPath, CommentedPath);
    DataApp test({root.Path()});
    {
        INFO("the commented sword loads, so the store holds an asset from its file");
        REQUIRE(test.store.Load<Sword>(CommentedPath, Fenrir::AssetOwner::App()).Ok());
    }
    const Fenrir::AssetHandle<Sword> sword = test.store.Add<Sword>(Sword{}, Fenrir::AssetOwner::App());

    const Fenrir::SaveResult saved = test.store.Save(sword, CommentedPath);

    INFO("the loaded sword's file is not written over by another asset: the save fails, and the file is as it was");
    REQUIRE(saved.status == Fenrir::SaveStatus::Failed);
    REQUIRE(test.errors->size() == 1);
    REQUIRE(ReadFile(root.Path() / CommentedPath) == ReadFile(Fixtures() / CommentedPath));
}

TEST_CASE("Saving an unnamed asset to no path is refused", "[assets][saving][paths]")
{
    TempRoot root;
    DataApp test({root.Path()});
    const Fenrir::AssetHandle<Sword> sword = test.store.Add<Sword>(Sword{}, Fenrir::AssetOwner::App());

    const Fenrir::SaveResult saved = test.store.Save(sword);

    INFO("an asset with no path has no file to save to: the save fails with one logged error");
    REQUIRE(saved.status == Fenrir::SaveStatus::Failed);
    REQUIRE(saved.diagnostics.size() == 1);
    REQUIRE(test.errors->size() == 1);
}

TEST_CASE("A reference to an unnamed asset is written null", "[assets][saving][references]")
{
    TempRoot root;
    DataApp test({root.Path()});
    const Fenrir::AssetHandle<ShieldData> shield = ShieldParryingUnnamed(test);

    const Fenrir::SaveResult saved = test.store.Save(shield, "shield.asset.json5");

    INFO("the file is written, its `parries` naming nothing, since the sword it holds has no asset path");
    REQUIRE(saved.status != Fenrir::SaveStatus::Failed);
    REQUIRE(ReadFile(root.Path() / "shield.asset.json5").find("parries: null") != std::string::npos);
}

TEST_CASE("A reference written empty is counted in SaveResult with one diagnostic", "[assets][saving][references]")
{
    TempRoot root;
    DataApp test({root.Path()});
    const Fenrir::AssetHandle<ShieldData> shield = ShieldParryingUnnamed(test);

    const Fenrir::SaveResult saved = test.store.Save(shield, "shield.asset.json5");

    INFO("the save is Partial: one reference written empty, one diagnostic naming its field, logged once as a warning");
    REQUIRE(saved.status == Fenrir::SaveStatus::Partial);
    REQUIRE(saved.emptyReferences == 1);
    REQUIRE(saved.diagnostics.size() == 1);
    REQUIRE(saved.diagnostics.front().message.find("`parries`") != std::string::npos);
    REQUIRE(AssetsTest::CountContaining(*test.warnings, "`parries`") == 1);
}

TEST_CASE("Saving to an absolute path is refused", "[assets][saving][paths]")
{
    TempRoot root;
    DataApp test({root.Path()});
    const Fenrir::AssetHandle<Sword> sword = test.store.Add<Sword>(Sword{}, Fenrir::AssetOwner::App());
    const fs::path absolute = root.Path() / "absolute.asset.json5";

    const Fenrir::SaveResult saved = test.store.Save(sword, absolute.generic_string());

    INFO("an asset path is relative to the asset root, even for a file under it: nothing is written, and the asset "
         "takes no path");
    REQUIRE(saved.status == Fenrir::SaveStatus::Failed);
    REQUIRE(test.errors->size() == 1);
    REQUIRE_FALSE(fs::exists(absolute));
    REQUIRE_FALSE(test.store.PathOf(sword).has_value());
}

TEST_CASE("Saving to a path that escapes the asset root is refused", "[assets][saving][paths]")
{
    TempRoot root;
    const fs::path inner = root.Path() / "root";
    fs::create_directories(inner);
    DataApp test({inner});
    const Fenrir::AssetHandle<Sword> sword = test.store.Add<Sword>(Sword{}, Fenrir::AssetOwner::App());

    const Fenrir::SaveResult saved = test.store.Save(sword, "../escaped.asset.json5");

    INFO("a path climbing out of the asset root is refused, and nothing is written beside the root");
    REQUIRE(saved.status == Fenrir::SaveStatus::Failed);
    REQUIRE(test.errors->size() == 1);
    REQUIRE_FALSE(fs::exists(root.Path() / "escaped.asset.json5"));
}

TEST_CASE("A save interrupted before its rename leaves the old file intact", "[assets][saving][atomic]")
{
    TempRoot root;
    root.Copy(Fixtures() / CommentedPath, CommentedPath);
    DataApp test({root.Path()});
    const Fenrir::AssetHandle<Sword> sword = test.store.Load<Sword>(CommentedPath, Fenrir::AssetOwner::App()).handle;
    Sword* edited = test.store.Edit(sword, "blunt a sword");
    {
        INFO("the sword loads and edits");
        REQUIRE(edited != nullptr);
    }
    edited->edge = 0.0f;
    // A directory where the save writes its text first, so the write beside the file fails before any rename.
    fs::create_directories(root.Path() / std::format("{}.saving", CommentedPath));

    const Fenrir::SaveResult saved = test.store.Save(sword);

    INFO("the save fails with one logged error, and the file is the fixture as it was");
    REQUIRE(saved.status == Fenrir::SaveStatus::Failed);
    REQUIRE(test.errors->size() == 1);
    REQUIRE(ReadFile(root.Path() / CommentedPath) == ReadFile(Fixtures() / CommentedPath));
}

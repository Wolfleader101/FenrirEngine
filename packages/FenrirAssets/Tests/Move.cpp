// Moving a file under the asset root (ADR-0076 §5, ADR-0079 §6): Assets::Move needs no running store, rewrites every
// reference to the file and to its sub-assets across the root's envelopes, keeps every other byte of them, names the
// files it could not scan, and writes nothing on a dry run. Every test moves within a temporary asset root of its own.

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "DataAssetsTest.hpp"
#include "FenrirAssets/Assets.hpp"

using AssetsTest::DataApp;
using AssetsTest::ReadFile;
using AssetsTest::ShieldData;
using AssetsTest::Sword;
using AssetsTest::TempRoot;
using AssetsTest::WeaponData;

namespace
{
    constexpr std::string_view SwordPath = "weapons/sword.asset.json5";
    constexpr std::string_view BladePath = "arms/blade.asset.json5";

    // The sword the tests move, with a comment on its own reference: its spare blade upgrades back to it. Each file
    // is in the form WriteText writes, so a rewrite changes its reference strings alone.
    constexpr std::string_view SwordText = R"(// The sword the tests move.
{
  format: 1,
  type: "AssetsTest::Sword",
  version: 2,
  data: {
    displayName: "Longsword",
    // Sharp enough.
    edge: 0.9,
  },
  sub: {
    spare: {
      type: "AssetsTest::Sword",
      version: 2,
      data: {
        displayName: "Spare blade",
        upgradesTo: "weapons/sword.asset.json5", // back to the sword
      },
    },
  },
}
)";

    constexpr std::string_view AxePath = "weapons/axe.asset.json5";
    constexpr std::string_view AxeText = R"(// An axe that upgrades to the sword.
{
  format: 1,
  type: "AssetsTest::WeaponData",
  version: 1,
  data: {
    displayName: "Axe",
    tags: ["heavy", "iron"],
    upgradesTo: "weapons/sword.asset.json5",
  },
}
)";

    constexpr std::string_view ShieldPath = "shields/shield.asset.json5";
    constexpr std::string_view ShieldText = R"({
  format: 1,
  type: "AssetsTest::ShieldData",
  version: 1,
  data: {
    block: 0.8,
    // Made for the spare blade.
    parries: "weapons/sword.asset.json5#spare",
  },
}
)";

    // A club whose name spells the sword's path in a field that is no reference, and which upgrades to the axe.
    constexpr std::string_view ClubPath = "club.asset.json5";
    constexpr std::string_view ClubText = R"({
  format: 1,
  type: "AssetsTest::WeaponData",
  version: 1,
  data: {
    displayName: "weapons/sword.asset.json5",
    upgradesTo: "weapons/axe.asset.json5",
  },
}
)";

    // A file of a type the registry lacks, naming the sword: it cannot be scanned, so it keeps the old path.
    constexpr std::string_view SpearPath = "spear.asset.json5";
    constexpr std::string_view SpearText = R"({
  format: 1,
  type: "AssetsTest::Spear",
  version: 1,
  data: {
    upgradesTo: "weapons/sword.asset.json5",
  },
}
)";

    constexpr std::string_view BrokenPath = "notes/broken.json5";
    constexpr std::string_view BrokenText = "{ format: 1, type: \"AssetsTest::Sword\",\n";

    // An asset root holding every file above.
    struct MoveRoot
    {
        TempRoot root;
        Fenrir::TypeRegistry registry;
        bool registered = Fenrir::Generated::RegisterDataAssetsTestPackageTypes(registry);

        MoveRoot()
        {
            root.Write(SwordPath, SwordText);
            root.Write(AxePath, AxeText);
            root.Write(ShieldPath, ShieldText);
            root.Write(ClubPath, ClubText);
            root.Write(SpearPath, SpearText);
            root.Write(BrokenPath, BrokenText);
        }

        [[nodiscard]] std::string Read(std::string_view path) const
        {
            return ReadFile(root.Path() / path);
        }

        [[nodiscard]] bool Exists(std::string_view path) const
        {
            return std::filesystem::exists(root.Path() / path);
        }
    };

    // `text` with every `from` changed to `to`.
    std::string ReplacedAll(std::string text, std::string_view from, std::string_view to)
    {
        for (std::size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size()))
        {
            text.replace(at, from.size(), to);
        }
        return text;
    }

    const std::vector<std::string> ChangedByTheMove = {std::string(BladePath), std::string(ShieldPath),
                                                       std::string(AxePath)};

    std::vector<std::string> Sorted(std::vector<std::string> paths)
    {
        std::ranges::sort(paths);
        return paths;
    }

    std::vector<std::string> UnscannedPaths(const Fenrir::MoveResult& result)
    {
        std::vector<std::string> paths;
        for (const Fenrir::UnscannedFile& file : result.unscanned)
        {
            paths.push_back(file.path);
        }
        return paths;
    }
} // namespace

TEST_CASE("Moving a sword rewrites the references of two files and of its own sub-asset", "[assets][move]")
{
    MoveRoot test;
    INFO("the test package's types register into a registry of the test's own");
    REQUIRE(test.registered);

    const Fenrir::MoveResult result = Fenrir::Assets::Move(test.root.Path(), SwordPath, BladePath, test.registry);

    INFO(result.problem);
    INFO("the move succeeds");
    REQUIRE(result.status == Fenrir::MoveStatus::Succeeded);
    INFO("the moved file, by its new path, and the two files naming it are the files changed");
    REQUIRE(result.changed == Sorted(ChangedByTheMove));
    INFO("the axe's, the shield's and the spare blade's references are the three rewritten");
    REQUIRE(result.references == 3);
    INFO("the file is at its new path");
    REQUIRE(test.Exists(BladePath));
    INFO("the file is no longer at its old path");
    REQUIRE_FALSE(test.Exists(SwordPath));
}

TEST_CASE("A move changes the reference strings alone, keeping every other byte", "[assets][move]")
{
    MoveRoot test;

    const Fenrir::MoveResult result = Fenrir::Assets::Move(test.root.Path(), SwordPath, BladePath, test.registry);

    INFO(result.problem);
    INFO("the move succeeds");
    REQUIRE(result.status == Fenrir::MoveStatus::Succeeded);
    INFO("the moved file's own reference is rewritten, its comments kept");
    REQUIRE(test.Read(BladePath) == ReplacedAll(std::string(SwordText), SwordPath, BladePath));
    INFO("the axe's reference to the sword is rewritten, and nothing else in it");
    REQUIRE(test.Read(AxePath) == ReplacedAll(std::string(AxeText), SwordPath, BladePath));
    INFO("the shield's reference to the sword's sub-asset keeps its label, and its comment stays");
    REQUIRE(test.Read(ShieldPath) == ReplacedAll(std::string(ShieldText), SwordPath, BladePath));
    INFO("a string field spelling the sword's path is no reference, so the club is untouched");
    REQUIRE(test.Read(ClubPath) == ClubText);
}

TEST_CASE("A reference spelling the moved file's path as Load also reads it is rewritten too", "[assets][move]")
{
    MoveRoot test;
    constexpr std::string_view DaggerPath = "dagger.asset.json5";
    constexpr std::string_view KnifePath = "knife.asset.json5";
    test.root.Write(DaggerPath, ReplacedAll(std::string(AxeText), SwordPath, "./weapons/sword.asset.json5"));
    test.root.Write(KnifePath, ReplacedAll(std::string(AxeText), SwordPath, "weapons\\\\sword.asset.json5"));

    const Fenrir::MoveResult result = Fenrir::Assets::Move(test.root.Path(), SwordPath, BladePath, test.registry);

    INFO(result.problem);
    INFO("the dagger names the sword through `./`, which Load resolves to the sword");
    REQUIRE(test.Read(DaggerPath) == ReplacedAll(std::string(AxeText), SwordPath, BladePath));
    INFO("the knife names the sword with a backslash, which the path rules read as a slash");
    REQUIRE(test.Read(KnifePath) == ReplacedAll(std::string(AxeText), SwordPath, BladePath));
}

TEST_CASE("A dry run reports the same files and writes nothing", "[assets][move][dry-run]")
{
    MoveRoot test;

    const Fenrir::MoveResult dry = Fenrir::Assets::Move(test.root.Path(), SwordPath, BladePath, test.registry, true);

    INFO(dry.problem);
    INFO("the dry run succeeds");
    REQUIRE(dry.status == Fenrir::MoveStatus::Succeeded);
    INFO("the dry run lists the files the move would change");
    REQUIRE(dry.changed == Sorted(ChangedByTheMove));
    INFO("the dry run counts the three references the move would rewrite");
    REQUIRE(dry.references == 3);
    INFO("nothing moved: the sword is at its old path");
    REQUIRE(test.Exists(SwordPath));
    INFO("nothing moved: nothing is at the new path");
    REQUIRE_FALSE(test.Exists(BladePath));
    INFO("no file was rewritten");
    REQUIRE(test.Read(SwordPath) == SwordText);
    REQUIRE(test.Read(AxePath) == AxeText);
    REQUIRE(test.Read(ShieldPath) == ShieldText);
}

TEST_CASE("A move changes exactly the files a dry run before it listed", "[assets][move][dry-run]")
{
    MoveRoot test;

    const Fenrir::MoveResult dry = Fenrir::Assets::Move(test.root.Path(), SwordPath, BladePath, test.registry, true);
    const Fenrir::MoveResult moved = Fenrir::Assets::Move(test.root.Path(), SwordPath, BladePath, test.registry);

    INFO("the move lists the files the dry run listed as changed");
    REQUIRE(moved.changed == dry.changed);
    INFO("the move lists the files the dry run listed as unscanned");
    REQUIRE(UnscannedPaths(moved) == UnscannedPaths(dry));
}

TEST_CASE("A move to a destination that exists is refused, writing nothing", "[assets][move][refused]")
{
    MoveRoot test;

    const Fenrir::MoveResult result = Fenrir::Assets::Move(test.root.Path(), SwordPath, AxePath, test.registry);

    INFO("a move never writes over a file");
    REQUIRE(result.status == Fenrir::MoveStatus::Refused);
    INFO(result.problem);
    INFO("the refusal says the destination exists");
    REQUIRE(result.problem.find("exists") != std::string::npos);
    INFO("a refused move changes no file");
    REQUIRE(result.changed.empty());
    INFO("both files are as they were");
    REQUIRE(test.Read(SwordPath) == SwordText);
    REQUIRE(test.Read(AxePath) == AxeText);
}

TEST_CASE("A move to a path climbing out of the root is refused", "[assets][move][refused]")
{
    MoveRoot test;

    const Fenrir::MoveResult result =
        Fenrir::Assets::Move(test.root.Path(), SwordPath, "../outside.asset.json5", test.registry);

    INFO("a destination through `..` is no asset path");
    REQUIRE(result.status == Fenrir::MoveStatus::Refused);
    INFO("the sword is where it was");
    REQUIRE(test.Read(SwordPath) == SwordText);
}

TEST_CASE("A move to an absolute path is refused", "[assets][move][refused]")
{
    MoveRoot test;

    const Fenrir::MoveResult result =
        Fenrir::Assets::Move(test.root.Path(), SwordPath, "/arms/blade.asset.json5", test.registry);

    INFO("an absolute destination is no asset path");
    REQUIRE(result.status == Fenrir::MoveStatus::Refused);
    INFO("the sword is where it was");
    REQUIRE(test.Read(SwordPath) == SwordText);
}

TEST_CASE("A move of a sub-asset alone is refused", "[assets][move][refused]")
{
    MoveRoot test;

    const Fenrir::MoveResult result =
        Fenrir::Assets::Move(test.root.Path(), "weapons/sword.asset.json5#spare", BladePath, test.registry);

    INFO("a sub-asset moves with its file, never alone");
    REQUIRE(result.status == Fenrir::MoveStatus::Refused);
    INFO("the sword is where it was");
    REQUIRE(test.Read(SwordPath) == SwordText);
}

TEST_CASE("A move of a file that is not there is refused", "[assets][move][refused]")
{
    MoveRoot test;

    const Fenrir::MoveResult result =
        Fenrir::Assets::Move(test.root.Path(), "weapons/missing.asset.json5", BladePath, test.registry);

    INFO("a file that is not there cannot move");
    REQUIRE(result.status == Fenrir::MoveStatus::Refused);
    INFO("nothing is written at the destination");
    REQUIRE_FALSE(test.Exists(BladePath));
}

TEST_CASE("A rename that changes only the case moves the file and rewrites its references", "[assets][move]")
{
    MoveRoot test;
    constexpr std::string_view Capitalised = "weapons/Sword.asset.json5";

    const Fenrir::MoveResult result = Fenrir::Assets::Move(test.root.Path(), SwordPath, Capitalised, test.registry);

    INFO(result.problem);
    INFO("where the disk ignores case it finds the file itself at the destination, which is no other file");
    REQUIRE(result.status == Fenrir::MoveStatus::Succeeded);
    INFO("the renamed file's own reference names its new spelling");
    REQUIRE(test.Read(Capitalised) == ReplacedAll(std::string(SwordText), SwordPath, Capitalised));
    INFO("the axe names the new spelling");
    REQUIRE(test.Read(AxePath) == ReplacedAll(std::string(AxeText), SwordPath, Capitalised));
}

TEST_CASE("A file of an unregistered type, or one that does not parse, is named as unscanned and left as it was",
          "[assets][move][unscanned]")
{
    MoveRoot test;

    const Fenrir::MoveResult result = Fenrir::Assets::Move(test.root.Path(), SwordPath, BladePath, test.registry);

    INFO(result.problem);
    INFO("the move succeeds");
    REQUIRE(result.status == Fenrir::MoveStatus::Succeeded);
    INFO("the spear, of a type the registry lacks, and the file with a syntax error are unscanned, in path order");
    REQUIRE(UnscannedPaths(result) == std::vector<std::string>{std::string(BrokenPath), std::string(SpearPath)});
    INFO(result.unscanned[1].reason);
    INFO("the spear's reason names the type the registry lacks");
    REQUIRE(result.unscanned[1].reason.find("AssetsTest::Spear") != std::string::npos);
    INFO(result.unscanned[0].reason);
    INFO("the broken file's reason is its located syntax error");
    REQUIRE(result.unscanned[0].reason.find("notes/broken.json5:") != std::string::npos);
    INFO("the spear still names the old path, as it was written");
    REQUIRE(test.Read(SpearPath) == SpearText);
    INFO("the broken file is as it was");
    REQUIRE(test.Read(BrokenPath) == BrokenText);
}

TEST_CASE("After a move, each file loads with every reference resolved", "[assets][move][load]")
{
    MoveRoot test;
    INFO("the move succeeds");
    REQUIRE(Fenrir::Assets::Move(test.root.Path(), SwordPath, BladePath, test.registry).status ==
            Fenrir::MoveStatus::Succeeded);

    DataApp app(Fenrir::AssetsConfig{test.root.Path()});
    INFO("the test package's types register into the app's registry");
    REQUIRE(app.registered);
    const Fenrir::AssetOwner owner = Fenrir::AssetOwner::App();

    const Fenrir::LoadResult<ShieldData> shield = app.store.Load<ShieldData>(ShieldPath, owner);
    const Fenrir::LoadResult<WeaponData> axe = app.store.Load<WeaponData>(AxePath, owner);
    const Fenrir::LoadResult<Sword> blade = app.store.Load<Sword>(BladePath, owner);
    const Fenrir::LoadResult<Sword> spare = app.store.Load<Sword>(std::string(BladePath) + "#spare", owner);

    INFO("every file loads whole, with no reference left empty");
    REQUIRE(shield.Status() == Fenrir::LoadStatus::Succeeded);
    REQUIRE(axe.Status() == Fenrir::LoadStatus::Succeeded);
    REQUIRE(blade.Status() == Fenrir::LoadStatus::Succeeded);
    REQUIRE(spare.Status() == Fenrir::LoadStatus::Succeeded);
    REQUIRE(app.errors->empty());
    INFO("the shield parries the moved sword's spare blade");
    REQUIRE(app.store.TryGet<ShieldData>(shield.handle)->parries == spare.handle);
    INFO("the axe upgrades to the moved sword");
    REQUIRE(app.store.PathOf(app.store.TryGet<WeaponData>(axe.handle)->upgradesTo) ==
            std::optional<std::string>(BladePath));
    INFO("the spare blade upgrades back to the sword it is part of");
    const Fenrir::AssetHandle<WeaponData> back = app.store.TryGet<Sword>(spare.handle)->upgradesTo;
    REQUIRE(app.store.PathOf(back) == std::optional<std::string>(BladePath));
}

// Moving a file under the asset root (ADR-0076 §5, ADR-0079 §6): Assets::Move needs no running store, rewrites every
// reference to the file and to its sub-assets across the root's envelopes, keeps every other byte of them, names the
// files it could not scan, and writes nothing on a dry run. Scene files are scanned too: each block's `instance` and
// the asset handle fields of their entries (ADR-0078, ADR-0080). Every test moves within a temporary asset root of its
// own.

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
#include "FenrirECS/SceneComponents.hpp"

using AssetsTest::DataApp;
using AssetsTest::Fixtures;
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

namespace
{
    constexpr std::string_view TowerPath = "prefabs/tower.scene.json5";
    constexpr std::string_view FortPath = "prefabs/forts/tower.scene.json5";
    constexpr std::string_view KeepPath = "levels/keep.scene.json5";
    constexpr std::string_view WatchtowerPath = "prefabs/watchtower.scene.json5";

    // A board whose marker shows the tower as its icon: the tower's path in an asset handle field, not an instance.
    // A move matches the path alone, never the kind of asset the field holds.
    constexpr std::string_view BoardPath = "levels/board.scene.json5";
    constexpr std::string_view BoardText = R"(// The board the keep's map hangs on.
{
  format: 1,
  type: "Fenrir::Scene",
  version: 1,
  data: {
    resources: {},
    entities: [
      {
        id: "b0ard",
        components: {
          "Fenrir::Name": {name: "Board"},
          "AssetsTest::Follower": {
            icon: "prefabs/tower.scene.json5", // the fort it marks
            speed: 3,
          },
        },
      },
    ],
  },
}
)";

    // An asset root holding the tower and what it nests, its two hosts (the keep, and the watchtower variant of it),
    // and the board.
    struct SceneRoot
    {
        TempRoot root;

        SceneRoot()
        {
            root.Copy(Fixtures() / "fourbytwo.png", "icons/sword.png");
            root.Copy(Fixtures() / "twobytwo.hdr", "sky/day.hdr");
            root.Copy(Fixtures() / "scenes" / "player.scene.json5", "prefabs/player.scene.json5");
            root.Copy(Fixtures() / "scenes" / "tower.scene.json5", TowerPath);
            root.Copy(Fixtures() / "scenes" / "keep.scene.json5", KeepPath);
            root.Copy(Fixtures() / "scenes" / "watchtower.scene.json5", WatchtowerPath);
            root.Write(BoardPath, BoardText);
        }

        [[nodiscard]] std::string Read(std::string_view path) const
        {
            return ReadFile(root.Path() / path);
        }

        [[nodiscard]] static std::string Fixture(std::string_view name)
        {
            return ReadFile(Fixtures() / "scenes" / name);
        }
    };

    const std::vector<std::string> ChangedByTheFortMove = {std::string(BoardPath), std::string(KeepPath),
                                                           std::string(WatchtowerPath)};

    // A scene naming the sword's icon in a resource, a component, and an instance's override, the last through a
    // label; `{icon}` marks each, which a move of the icon rewrites. Its relic is of a type the registry lacks, so the
    // relic's own path to the icon is left as it is.
    constexpr std::string_view ArmouryPath = "levels/armoury.scene.json5";
    constexpr std::string_view ArmouryTemplate = R"({
  format: 1,
  type: "Fenrir::Scene",
  version: 1,
  data: {
    resources: {
      "AssetsTest::Sky": {
        cubemap: "{icon}",
        exposure: 2,
      },
    },
    entities: [
      {
        id: "rack0",
        components: {
          "Fenrir::Name": {name: "Rack"},
          "Fenrir::Transform": {},
          "AssetsTest::Follower": {
            icon: "{icon}",
            speed: 2,
          },
          "Game::Relic": {
            icon: "icons/sword.png",
            weight: 4,
          },
        },
      },
      {
        id: "t1nst",
        parent: "rack0",
        instance: "prefabs/tower.scene.json5",
        overrides: {
          g4te1: {
            components: {
              "AssetsTest::Follower": {
                icon: "{icon}#small",
                speed: 4,
              },
            },
          },
        },
      },
    ],
  },
}
)";

    std::string Armoury(std::string_view icon)
    {
        return ReplacedAll(std::string(ArmouryTemplate), "{icon}", icon);
    }

    // The instance root's source, of the one root a host file loads to.
    std::string RootSource(const Fenrir::SceneLoadResult& loaded)
    {
        INFO("the file loads to one root");
        REQUIRE(loaded.roots.size() == 1);
        INFO("the root is an instance");
        REQUIRE(loaded.roots[0].HasComponent<Fenrir::Instance>());
        return loaded.roots[0].GetComponent<Fenrir::Instance>().source;
    }
} // namespace

TEST_CASE("Moving a prefab rewrites its two hosts' instances and the asset handle field naming it, and nothing else",
          "[assets][move][scenes]")
{
    SceneRoot test;
    DataApp app({test.root.Path()});
    INFO("the test package's types register into the app's registry, beside the engine's");
    REQUIRE(app.registered);

    const Fenrir::MoveResult result =
        Fenrir::Assets::Move(test.root.Path(), TowerPath, FortPath, app.app.GetResource<Fenrir::TypeRegistry>());

    INFO(result.problem);
    INFO("the move succeeds, and every scene file is scanned");
    REQUIRE(result.status == Fenrir::MoveStatus::Succeeded);
    REQUIRE(result.unscanned.empty());
    INFO("the keep's and the watchtower's instances, and the board's icon, are the three references rewritten");
    REQUIRE(result.changed == Sorted(ChangedByTheFortMove));
    REQUIRE(result.references == 3);
    INFO("the keep's instance string alone changed");
    REQUIRE(test.Read(KeepPath) == ReplacedAll(SceneRoot::Fixture("keep.scene.json5"), TowerPath, FortPath));
    INFO("the watchtower's instance string alone changed");
    REQUIRE(test.Read(WatchtowerPath) ==
            ReplacedAll(SceneRoot::Fixture("watchtower.scene.json5"), TowerPath, FortPath));
    INFO("the board's icon alone changed, its comments kept");
    REQUIRE(test.Read(BoardPath) == ReplacedAll(std::string(BoardText), TowerPath, FortPath));
    INFO("the tower moved, and names nothing that moved");
    REQUIRE(test.Read(FortPath) == SceneRoot::Fixture("tower.scene.json5"));
    REQUIRE_FALSE(std::filesystem::exists(test.root.Path() / TowerPath));
}

TEST_CASE("A dry run of a prefab's move reports the same scene files and writes nothing",
          "[assets][move][scenes][dry-run]")
{
    SceneRoot test;
    DataApp app({test.root.Path()});

    const Fenrir::MoveResult dry =
        Fenrir::Assets::Move(test.root.Path(), TowerPath, FortPath, app.app.GetResource<Fenrir::TypeRegistry>(), true);

    INFO(dry.problem);
    INFO("the dry run lists the three files and three references the move would rewrite");
    REQUIRE(dry.status == Fenrir::MoveStatus::Succeeded);
    REQUIRE(dry.changed == Sorted(ChangedByTheFortMove));
    REQUIRE(dry.references == 3);
    INFO("nothing moved, and no file was rewritten");
    REQUIRE(test.Read(TowerPath) == SceneRoot::Fixture("tower.scene.json5"));
    REQUIRE_FALSE(std::filesystem::exists(test.root.Path() / FortPath));
    REQUIRE(test.Read(KeepPath) == SceneRoot::Fixture("keep.scene.json5"));
    REQUIRE(test.Read(WatchtowerPath) == SceneRoot::Fixture("watchtower.scene.json5"));
    REQUIRE(test.Read(BoardPath) == BoardText);
}

TEST_CASE("After a prefab's move, each host loads with its instance expanded from the moved file",
          "[assets][move][scenes][load]")
{
    SceneRoot test;
    std::size_t keepBefore = 0;
    std::size_t watchtowerBefore = 0;
    {
        DataApp before({test.root.Path()});
        keepBefore = before.store.LoadScene(before.app, KeepPath).entities;
        watchtowerBefore = before.store.LoadScene(before.app, WatchtowerPath).entities;
    }
    DataApp app({test.root.Path()});
    INFO("the move succeeds");
    REQUIRE(Fenrir::Assets::Move(test.root.Path(), TowerPath, FortPath, app.app.GetResource<Fenrir::TypeRegistry>())
                .status == Fenrir::MoveStatus::Succeeded);

    const Fenrir::SceneLoadResult keep = app.store.LoadScene(app.app, KeepPath);
    const Fenrir::SceneLoadResult watchtower = app.store.LoadScene(app.app, WatchtowerPath);

    INFO("both hosts load whole, with nothing to report");
    REQUIRE(keep.status == Fenrir::LoadStatus::Succeeded);
    REQUIRE(keep.diagnostics.empty());
    REQUIRE(watchtower.status == Fenrir::LoadStatus::Succeeded);
    REQUIRE(watchtower.diagnostics.empty());
    INFO("each expands to as many entities as before the move, its tower's included");
    REQUIRE(keepBefore > 2);
    REQUIRE(keep.entities == keepBefore);
    REQUIRE(watchtower.entities == watchtowerBefore);
    INFO("the watchtower's root is an instance of the tower at its new path");
    REQUIRE(RootSource(watchtower) == FortPath);
}

TEST_CASE("A scene's resource, component and override entries naming the moved file are rewritten, and an entry of a "
          "type the registry lacks leaves the file listed as unscanned",
          "[assets][move][scenes][unscanned]")
{
    TempRoot root;
    root.Copy(Fixtures() / "fourbytwo.png", "icons/sword.png");
    root.Write(ArmouryPath, Armoury("icons/sword.png"));
    DataApp app({root.Path()});

    const Fenrir::MoveResult result = Fenrir::Assets::Move(root.Path(), "icons/sword.png", "icons/blade.png",
                                                           app.app.GetResource<Fenrir::TypeRegistry>());

    INFO(result.problem);
    INFO("the move succeeds");
    REQUIRE(result.status == Fenrir::MoveStatus::Succeeded);
    INFO("the sky's cubemap, the rack's icon and the gate's labelled icon are the three references rewritten");
    REQUIRE(result.changed == std::vector<std::string>{std::string(ArmouryPath)});
    REQUIRE(result.references == 3);
    INFO("the relic's path is left as it was, and every other byte too");
    REQUIRE(ReadFile(root.Path() / ArmouryPath) == Armoury("icons/blade.png"));
    INFO("the armoury is listed as unscanned, naming the type the registry lacks");
    REQUIRE(UnscannedPaths(result) == std::vector<std::string>{std::string(ArmouryPath)});
    INFO(result.unscanned[0].reason);
    REQUIRE(result.unscanned[0].reason.find("`Game::Relic`") != std::string::npos);
}

TEST_CASE("A scene file that does not parse, or is no scene envelope, is named as unscanned and left as it was",
          "[assets][move][scenes][unscanned]")
{
    SceneRoot test;
    constexpr std::string_view BrokenScene = "levels/broken.scene.json5";
    constexpr std::string_view WrongScene = "levels/wrong.scene.json5";
    const std::string broken =
        "{ format: 1, type: \"Fenrir::Scene\", data: { entities: [\n  \"prefabs/tower.scene.json5\"";
    const std::string wrong =
        ReplacedAll(SceneRoot::Fixture("keep.scene.json5"), "\"Fenrir::Scene\"", "\"Fenrir::Level\"");
    // A scene at a layout version the reader does not know, which a load refuses.
    constexpr std::string_view LaterScene = "levels/later.scene.json5";
    const std::string later =
        ReplacedAll(SceneRoot::Fixture("keep.scene.json5"), "version: 1,\n  data", "version: 9,\n  data");
    test.root.Write(BrokenScene, broken);
    test.root.Write(WrongScene, wrong);
    test.root.Write(LaterScene, later);
    DataApp app({test.root.Path()});

    const Fenrir::MoveResult result =
        Fenrir::Assets::Move(test.root.Path(), TowerPath, FortPath, app.app.GetResource<Fenrir::TypeRegistry>());

    INFO(result.problem);
    REQUIRE(result.status == Fenrir::MoveStatus::Succeeded);
    INFO("the file with a syntax error, the later layout and the one of another type are unscanned, in path order");
    REQUIRE(UnscannedPaths(result) ==
            std::vector<std::string>{std::string(BrokenScene), std::string(LaterScene), std::string(WrongScene)});
    INFO(result.unscanned[0].reason);
    INFO("the broken file's reason is its located syntax error");
    REQUIRE(result.unscanned[0].reason.find("levels/broken.scene.json5:") != std::string::npos);
    INFO(result.unscanned[1].reason);
    INFO("the later layout's reason is its version");
    REQUIRE(result.unscanned[1].reason.find("`version`") != std::string::npos);
    INFO(result.unscanned[2].reason);
    INFO("the other's reason is that it is no scene envelope");
    REQUIRE(result.unscanned[2].reason.find("no scene envelope") != std::string::npos);
    INFO("each is as it was, so each still names the old path");
    REQUIRE(test.Read(BrokenScene) == broken);
    REQUIRE(test.Read(WrongScene) == wrong);
    REQUIRE(test.Read(LaterScene) == later);
    INFO("the later layout's file differs from the keep's in its version");
    REQUIRE(later != SceneRoot::Fixture("keep.scene.json5"));
    INFO("the hosts are rewritten as before");
    REQUIRE(result.changed == Sorted(ChangedByTheFortMove));
}

namespace
{
    // A banner and a scene each naming the sword's icon from the painted look of a tagged union, whose tag field is
    // `m_type` and which a file spells `kind`; `{icon}` marks each reference, which a move of the icon rewrites.
    constexpr std::string_view BannerPath = "banners/painted.asset.json5";
    constexpr std::string_view BannerTemplate = R"(// A banner painted with the sword's icon.
{
  format: 1,
  type: "AssetsTest::BannerData",
  version: 1,
  data: {
    look: {
      kind: "Painted",
      // The icon, twice its size.
      image: "{icon}",
      scale: 2,
    },
  },
}
)";

    constexpr std::string_view HallPath = "levels/hall.scene.json5";
    constexpr std::string_view HallTemplate = R"(// A hall with the sword's icon painted on its wall.
{
  format: 1,
  type: "Fenrir::Scene",
  version: 1,
  data: {
    resources: {},
    entities: [
      {
        id: "wa11d",
        components: {
          "Fenrir::Name": {name: "Wall"},
          "AssetsTest::Decal": {
            look: {
              kind: "Painted",
              image: "{icon}", // the sword's icon
              scale: 3,
            },
          },
        },
      },
    ],
  },
}
)";

    std::string WithIcon(std::string_view text, std::string_view icon)
    {
        return ReplacedAll(std::string(text), "{icon}", icon);
    }
} // namespace

TEST_CASE("A reference inside a tagged union's live alternative, in an envelope and in a scene, is rewritten, keeping "
          "every other byte",
          "[assets][move][union]")
{
    TempRoot root;
    root.Copy(Fixtures() / "fourbytwo.png", "icons/sword.png");
    root.Write(BannerPath, WithIcon(BannerTemplate, "icons/sword.png"));
    root.Write(HallPath, WithIcon(HallTemplate, "icons/sword.png"));
    DataApp app({root.Path()});
    INFO("the test package's types register into the app's registry");
    REQUIRE(app.registered);

    const Fenrir::MoveResult result = Fenrir::Assets::Move(root.Path(), "icons/sword.png", "icons/blade.png",
                                                           app.app.GetResource<Fenrir::TypeRegistry>());

    INFO(result.problem);
    INFO("the move succeeds, and both files are scanned");
    REQUIRE(result.status == Fenrir::MoveStatus::Succeeded);
    REQUIRE(result.unscanned.empty());
    INFO("the union's tag is found under `kind`, so the banner's and the decal's images are the references rewritten");
    REQUIRE(result.changed == Sorted({std::string(BannerPath), std::string(HallPath)}));
    REQUIRE(result.references == 2);
    INFO("the banner's image names the new path, and every other byte, its comments included, stays");
    REQUIRE(ReadFile(root.Path() / BannerPath) == WithIcon(BannerTemplate, "icons/blade.png"));
    INFO("the decal's image names the new path, and every other byte stays");
    REQUIRE(ReadFile(root.Path() / HallPath) == WithIcon(HallTemplate, "icons/blade.png"));
}

TEST_CASE("After a move, a banner loads with its painted look naming the moved image", "[assets][move][union][load]")
{
    TempRoot root;
    root.Copy(Fixtures() / "fourbytwo.png", "icons/sword.png");
    root.Write(BannerPath, WithIcon(BannerTemplate, "icons/sword.png"));
    {
        DataApp before({root.Path()});
        INFO("the move succeeds");
        REQUIRE(Fenrir::Assets::Move(root.Path(), "icons/sword.png", "icons/blade.png",
                                     before.app.GetResource<Fenrir::TypeRegistry>())
                    .status == Fenrir::MoveStatus::Succeeded);
    }

    DataApp app({root.Path()});
    const Fenrir::LoadResult<AssetsTest::BannerData> banner =
        app.store.Load<AssetsTest::BannerData>(BannerPath, Fenrir::AssetOwner::App());

    INFO("the banner loads whole");
    REQUIRE(banner.Status() == Fenrir::LoadStatus::Succeeded);
    REQUIRE(app.errors->empty());
    const AssetsTest::Look& look = app.store.TryGet<AssetsTest::BannerData>(banner.handle)->look;
    INFO("its look is painted");
    REQUIRE(look.Type() == AssetsTest::LookType::Painted);
    INFO("with the image at its new path");
    REQUIRE(app.store.PathOf(look.Painted().image) == std::optional<std::string>("icons/blade.png"));
}

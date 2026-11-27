// Saving and loading scenes (ADR-0078, ADR-0079, ADR-0080): SaveScene writes a scene built in code to a scene file in
// ADR-0078's layout, the same scene as the same bytes, minting the entity ids it lacks, leaving out what a scene file
// never holds and counting it; LoadScene reads one back into a new scene in two passes, keeping what it cannot read as
// a component and reporting the rest. LoadSceneInto merges a file into a scene that exists, under a parent, leaving the
// scene's resources standing; the subtree SaveScene writes a selection as its own file. CopyScene runs the writer and
// the reader through a tree in memory, so a copy is what a save and a load would give, with no file. Every test saves
// into, or loads from, a temporary asset root of its own.

#include <cstddef>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "AssetsProbe.hpp"
#include "DataAssetsTest.hpp"
#include "FenrirECS/ComponentType.hpp"
#include "FenrirECS/ComponentWatch.hpp"
#include "FenrirECS/EntityId.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/SceneComponents.hpp"
#include "FenrirScene/Scene.hpp"

using AssetsTest::DataApp;
using AssetsTest::Fixtures;
using AssetsTest::ReadFile;
using AssetsTest::TempRoot;
using Fenrir::Entity;
using Fenrir::LoadStatus;
using Fenrir::SaveStatus;
using Fenrir::SceneLoadResult;
using Fenrir::SceneSaveResult;
using Fenrir::Serialisation::DiagnosticSeverity;

namespace
{
    namespace fs = std::filesystem;

    constexpr std::string_view LevelPath = "levels/level1.scene.json5";

    // A component no package describes: a scene file cannot hold it.
    struct Wobble
    {
        float phase = 0.0f;
    };

    constexpr std::string_view PlayerPath = "prefabs/player.scene.json5";

    // The committed golden scene file.
    std::string Golden()
    {
        return ReadFile(Fixtures() / "scenes" / "level1.scene.json5");
    }

    // The committed golden subtree file: the golden scene's player, with the shield AddShield gives them.
    std::string GoldenPlayer()
    {
        return ReadFile(Fixtures() / "scenes" / "player.scene.json5");
    }

    // An asset root holding the images the golden scene names: the sky's cubemap and the sword's icon.
    void CopyImages(const TempRoot& root)
    {
        root.Copy(Fixtures() / "twobytwo.hdr", "sky/day.hdr");
        root.Copy(Fixtures() / "fourbytwo.png", "icons/sword.png");
    }

    Entity Spawn(Fenrir::Scene& scene, const char* name, Entity parent = {})
    {
        Entity entity = scene.GetEntityList().CreateEntity();
        entity.GetComponent<Fenrir::Name>().Set(name);
        if (parent.IsValid())
        {
            parent.AddChild(entity);
        }
        return entity;
    }

    void GiveId(Entity entity, std::string_view spelled)
    {
        const std::optional<Fenrir::EntityId> id = Fenrir::ParseEntityId(spelled);
        REQUIRE(id.has_value());
        entity.AddComponent<Fenrir::EntityId>(*id);
    }

    // The entities of the golden scene, by name.
    struct Level
    {
        Entity level;
        Entity player;
        Entity sword;
        Entity sun;
    };

    // The golden scene, built in code: a level holding a player, who holds a sword following them, and a sun, under a
    // sky. With `ids`, each entity has the golden file's id; without, it has none.
    Level BuildLevel(DataApp& test, Fenrir::Scene& scene, bool ids = true)
    {
        auto& sky = scene.AddResource<AssetsTest::Sky>();
        sky.cubemap = test.store.Load<Fenrir::Image>("sky/day.hdr", scene).handle;
        sky.exposure = 1.5f;

        Level built;
        built.level = Spawn(scene, "Level");
        built.player = Spawn(scene, "Player", built.level);
        built.player.GetComponent<Fenrir::Transform>().pos = {0.0f, 2.0f, 0.0f};
        built.player.AddComponent<AssetsTest::Health>().current = 75.0f;
        built.sword = Spawn(scene, "Sword", built.player);
        auto& follower = built.sword.AddComponent<AssetsTest::Follower>();
        follower.target = built.player;
        follower.icon = test.store.Load<Fenrir::Image>("icons/sword.png", scene).handle;
        built.sun = Spawn(scene, "Sun", built.level);
        built.sun.GetComponent<Fenrir::Transform>().rot = Fenrir::Math::Quat(0.0f, 0.0f, 1.0f, 0.0f);
        if (ids)
        {
            GiveId(built.level, "a7k2q");
            GiveId(built.player, "d4e5f");
            GiveId(built.sword, "m9x1p");
            GiveId(built.sun, "s3w8c");
        }
        return built;
    }

    // Gives the golden scene's player a second child, a shield following the sun: a reference out of the player's
    // subtree.
    Entity AddShield(Fenrir::Scene& scene, const Level& level)
    {
        Entity shield = Spawn(scene, "Shield", level.player);
        shield.AddComponent<AssetsTest::Follower>().target = level.sun;
        GiveId(shield, "h2q7r");
        return shield;
    }

    // The scene fixture `name` under `fixtures/scenes/`, copied to LevelPath in `root` beside the images it names.
    void CopyScene(const TempRoot& root, std::string_view name)
    {
        CopyImages(root);
        root.Copy(Fixtures() / "scenes" / name, LevelPath);
    }

    // The first entity of `scene` named `name`, or the null entity.
    Entity Named(Fenrir::Scene& scene, std::string_view name)
    {
        Fenrir::EntityList& entities = scene.GetEntityList();
        Entity found;
        entities.ForEachEntity([&](auto handle) {
            const Entity entity = entities.GetEntity(static_cast<std::uint32_t>(handle));
            if (!found.IsValid() && entity.HasComponent<Fenrir::Name>() &&
                std::string_view(entity.GetComponent<Fenrir::Name>().Get()) == name)
            {
                found = entity;
            }
        });
        return found;
    }

    // The names of `parent`'s children, in their sibling order.
    std::vector<std::string> ChildNames(const Entity& parent)
    {
        std::vector<std::string> names;
        parent.ForEachChild([&](const Entity& child) { names.emplace_back(child.GetComponent<Fenrir::Name>().Get()); });
        return names;
    }

    // How many scenes `app` has.
    std::size_t SceneCount(Fenrir::App& app)
    {
        std::size_t count = 0;
        app.ForEachScene([&](Fenrir::Scene&) { ++count; });
        return count;
    }

    // Whether every change in `changes` is an add.
    bool AddsOnly(const std::vector<Fenrir::ComponentChange>& changes)
    {
        for (const Fenrir::ComponentChange& change : changes)
        {
            if (change.kind != Fenrir::ComponentChange::Kind::Added)
            {
                return false;
            }
        }
        return true;
    }

    std::string IdOf(const Entity& entity)
    {
        return entity.HasComponent<Fenrir::EntityId>() ? Fenrir::SpellEntityId(entity.GetComponent<Fenrir::EntityId>())
                                                       : std::string();
    }

    // The store's source over the asset root, counting each read, so a test can tell nothing was read.
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

    // Makes `test`'s store read through a counting source over `root`, and gives the count.
    std::shared_ptr<std::size_t> CountReads(DataApp& test, const TempRoot& root)
    {
        auto reads = std::make_shared<std::size_t>(0);
        Fenrir::AssetsProbe::UseSource(
            test.store, std::make_unique<CountingSource>(
                            Fenrir::Detail::MakeDirectorySource(root.Path(), test.store.Logger()), reads));
        return reads;
    }

    // Every file under `root`, relative to it.
    std::set<std::string> FilesUnder(const TempRoot& root)
    {
        std::set<std::string> files;
        for (const fs::directory_entry& entry : fs::recursive_directory_iterator(root.Path()))
        {
            if (entry.is_regular_file())
            {
                files.insert(fs::relative(entry.path(), root.Path()).generic_string());
            }
        }
        return files;
    }
} // namespace

TEST_CASE("A scene built in code saves byte for byte as the golden scene file", "[assets][scenes][golden]")
{
    TempRoot root;
    CopyImages(root);
    DataApp test({root.Path()});
    Fenrir::Scene& scene = test.app.CreateScene("Level");
    BuildLevel(test, scene);

    const SceneSaveResult saved = test.store.SaveScene(scene, LevelPath);

    INFO("the save succeeds with nothing to report");
    REQUIRE(saved.status == SaveStatus::Succeeded);
    REQUIRE(saved.diagnostics.empty());
    INFO(
        "the file is the golden file: the sky under resources, then each entity depth-first with its id, parent and "
        "components, Name and Transform first, each entry's defaults left out (a Transform at its defaults as {}), and "
        "Health's version 2 before its fields");
    REQUIRE(ReadFile(root.Path() / LevelPath) == Golden());
    INFO("four entities are written, with Name, Transform and their own components: ten components");
    REQUIRE(saved.entitiesWritten == 4);
    REQUIRE(saved.componentsWritten == 10);
    INFO("each entity's Relationship and EntityId are spelled as its parent and id, and counted as left out");
    REQUIRE(saved.componentsSkipped == 8);
    REQUIRE(saved.entitiesSkipped == 0);
    REQUIRE(saved.nullReferences == 0);
}

TEST_CASE("Saving a scene twice writes the same bytes, keeping the ids the first save minted", "[assets][scenes]")
{
    TempRoot root;
    CopyImages(root);
    DataApp test({root.Path()});
    Fenrir::Scene& scene = test.app.CreateScene("Level");
    const Level level = BuildLevel(test, scene, false);

    const SceneSaveResult first = test.store.SaveScene(scene, LevelPath);
    REQUIRE(first.status == SaveStatus::Succeeded);
    const std::string firstBytes = ReadFile(root.Path() / LevelPath);
    const std::vector<std::string> minted = {IdOf(level.level), IdOf(level.player), IdOf(level.sword), IdOf(level.sun)};

    INFO("the first save gives every entity an id of five characters, each different");
    for (const std::string& id : minted)
    {
        REQUIRE(id.size() == 5);
    }
    REQUIRE(std::set<std::string>(minted.begin(), minted.end()).size() == 4);
    INFO("the file names each entity by the id it was given");
    for (const std::string& id : minted)
    {
        REQUIRE(firstBytes.find(std::format("id: \"{}\"", id)) != std::string::npos);
    }

    const SceneSaveResult second = test.store.SaveScene(scene, LevelPath);
    REQUIRE(second.status == SaveStatus::Succeeded);
    INFO("the second save keeps every id and writes the same bytes");
    REQUIRE(std::vector<std::string>{IdOf(level.level), IdOf(level.player), IdOf(level.sword), IdOf(level.sun)} ==
            minted);
    REQUIRE(ReadFile(root.Path() / LevelPath) == firstBytes);
}

TEST_CASE("Transient entities, Relationship, transient types and undescribed components are left out and counted",
          "[assets][scenes]")
{
    TempRoot root;
    CopyImages(root);
    DataApp test({root.Path()});
    Fenrir::Scene& scene = test.app.CreateScene("Level");
    const Level level = BuildLevel(test, scene);
    // A free-fly camera with a child of its own, both left out.
    Entity camera = Spawn(scene, "Camera");
    camera.AddComponent<Fenrir::Transient>();
    Spawn(scene, "Camera rig", camera);
    // A Transient entity below a written one, left out with its child.
    Entity debug = Spawn(scene, "Debug marker", level.sun);
    debug.AddComponent<Fenrir::Transient>();
    Spawn(scene, "Debug label", debug);
    Entity(level.player).AddComponent<AssetsTest::FrameCounter>().frames = 12;
    Entity(level.player).AddComponent<Wobble>().phase = 0.5f;

    const SceneSaveResult saved = test.store.SaveScene(scene, LevelPath);

    INFO("the save succeeds with nothing to report: what a scene file never holds is not warned of");
    REQUIRE(saved.status == SaveStatus::Succeeded);
    REQUIRE(saved.diagnostics.empty());
    INFO("the file is the golden file still: nothing left out reaches it");
    REQUIRE(ReadFile(root.Path() / LevelPath) == Golden());
    INFO("the two Transient entities and their children are counted as four entities left out");
    REQUIRE(saved.entitiesWritten == 4);
    REQUIRE(saved.entitiesSkipped == 4);
    INFO("beside each entity's Relationship and EntityId, the player's transient FrameCounter and undescribed Wobble "
         "are counted as left out");
    REQUIRE(saved.componentsWritten == 10);
    REQUIRE(saved.componentsSkipped == 10);
    INFO("a left-out entity gets no id");
    REQUIRE(IdOf(camera).empty());
    REQUIRE(IdOf(debug).empty());
}

TEST_CASE("An entity field naming an entity outside the file is written null, warned of once and counted",
          "[assets][scenes][references]")
{
    TempRoot root;
    CopyImages(root);
    DataApp test({root.Path()});
    Fenrir::Scene& scene = test.app.CreateScene("Level");
    const Level level = BuildLevel(test, scene);
    Fenrir::Scene& other = test.app.CreateScene("Other");
    Entity elsewhere = other.GetEntityList().CreateEntity();
    Entity(level.sword).GetComponent<AssetsTest::Follower>().target = elsewhere;

    const SceneSaveResult saved = test.store.SaveScene(scene, LevelPath);

    INFO("the save is written, Partial for the reference written null");
    REQUIRE(saved.status == SaveStatus::Partial);
    REQUIRE(saved.nullReferences == 1);
    INFO("one warning names the entity, the component and the field, and the file");
    REQUIRE(saved.diagnostics.size() == 1);
    REQUIRE(saved.diagnostics[0].severity == Fenrir::Serialisation::DiagnosticSeverity::Warning);
    REQUIRE(saved.diagnostics[0].message.find("entity `m9x1p`, `AssetsTest::Follower`: `target`") != std::string::npos);
    REQUIRE(saved.diagnostics[0].path.find("level1.scene.json5") != std::string::npos);
    REQUIRE(AssetsTest::CountContaining(*test.warnings, "`target` names an entity the file does not hold") == 1);
    INFO("the field is written null in place of the player's id");
    const std::string written = ReadFile(root.Path() / LevelPath);
    REQUIRE(written.find("target: null,") != std::string::npos);
    REQUIRE(written.find("target: \"d4e5f\"") == std::string::npos);
}

TEST_CASE("An empty entity field is its default: left out, with no warning and no count",
          "[assets][scenes][references]")
{
    TempRoot root;
    CopyImages(root);
    DataApp test({root.Path()});
    Fenrir::Scene& scene = test.app.CreateScene("Level");
    const Level level = BuildLevel(test, scene);
    Entity(level.sword).GetComponent<AssetsTest::Follower>().target = Entity();

    const SceneSaveResult saved = test.store.SaveScene(scene, LevelPath);

    INFO("an unset entity field is no broken reference: the save succeeds with nothing to report");
    REQUIRE(saved.status == SaveStatus::Succeeded);
    REQUIRE(saved.nullReferences == 0);
    REQUIRE(saved.diagnostics.empty());
    INFO("the follower is written without a `target`, its default");
    const std::string written = ReadFile(root.Path() / LevelPath);
    REQUIRE(written.find("target:") == std::string::npos);
    REQUIRE(written.find("icon: \"icons/sword.png\"") != std::string::npos);
}

TEST_CASE("An entity field naming a Transient entity of the same scene is written null", "[assets][scenes][references]")
{
    TempRoot root;
    CopyImages(root);
    DataApp test({root.Path()});
    Fenrir::Scene& scene = test.app.CreateScene("Level");
    const Level level = BuildLevel(test, scene);
    Entity camera = Spawn(scene, "Camera");
    camera.AddComponent<Fenrir::Transient>();
    Entity(level.sword).GetComponent<AssetsTest::Follower>().target = camera;

    const SceneSaveResult saved = test.store.SaveScene(scene, LevelPath);

    INFO("the camera is not in the file, so the follower's target is written null and counted");
    REQUIRE(saved.status == SaveStatus::Partial);
    REQUIRE(saved.nullReferences == 1);
    REQUIRE(saved.diagnostics.size() == 1);
    REQUIRE(ReadFile(root.Path() / LevelPath).find("target: null,") != std::string::npos);
}

TEST_CASE("An asset handle field naming an asset with no path is written null and counted",
          "[assets][scenes][references]")
{
    TempRoot root;
    CopyImages(root);
    DataApp test({root.Path()});
    Fenrir::Scene& scene = test.app.CreateScene("Level");
    const Level level = BuildLevel(test, scene);
    const Fenrir::LoadResult<Fenrir::Image> loaded =
        test.store.Load<Fenrir::Image>("icons/sword.png", Fenrir::AssetOwner::App());
    const Fenrir::ImageData* image = test.store.TryGet(loaded.handle);
    REQUIRE(image != nullptr);
    Entity(level.sword).GetComponent<AssetsTest::Follower>().icon =
        test.store.Add<Fenrir::Image>(*image, Fenrir::AssetOwner::App());

    const SceneSaveResult saved = test.store.SaveScene(scene, LevelPath);

    INFO("the icon, added with no name, has no path a file can hold: it is written null, warned of and counted");
    REQUIRE(saved.status == SaveStatus::Partial);
    REQUIRE(saved.nullReferences == 1);
    REQUIRE(saved.diagnostics.size() == 1);
    REQUIRE(saved.diagnostics[0].message.find("`icon` names an asset with no asset path") != std::string::npos);
    REQUIRE(ReadFile(root.Path() / LevelPath).find("icon: null,") != std::string::npos);
}

TEST_CASE("An id two entities share is kept by the first in the file and re-minted for the second", "[assets][scenes]")
{
    TempRoot root;
    CopyImages(root);
    DataApp test({root.Path()});
    Fenrir::Scene& scene = test.app.CreateScene("Level");
    const Level level = BuildLevel(test, scene);
    GiveId(level.sun, "d4e5f");

    const SceneSaveResult saved = test.store.SaveScene(scene, LevelPath);

    REQUIRE(saved.status == SaveStatus::Succeeded);
    INFO("the player, written first, keeps the id; the sun is given a new one, five characters long");
    REQUIRE(IdOf(level.player) == "d4e5f");
    REQUIRE(IdOf(level.sun) != "d4e5f");
    REQUIRE(IdOf(level.sun).size() == 5);
    INFO("the file names the sun by its new id, so no id repeats in it");
    const std::string written = ReadFile(root.Path() / LevelPath);
    REQUIRE(written.find(std::format("id: \"{}\"", IdOf(level.sun))) != std::string::npos);
    REQUIRE(written.find("id: \"d4e5f\"") == written.rfind("id: \"d4e5f\""));
}

TEST_CASE("A scene save to a path outside the asset root, absolute or naming a sub-asset is refused",
          "[assets][scenes]")
{
    TempRoot root;
    CopyImages(root);
    DataApp test({root.Path()});
    Fenrir::Scene& scene = test.app.CreateScene("Level");
    BuildLevel(test, scene);
    const std::string absolute = (root.Path() / "absolute.scene.json5").generic_string();

    for (const std::string_view path : {std::string_view("../outside.scene.json5"), std::string_view(absolute),
                                        std::string_view("levels/level1.scene.json5#part")})
    {
        test.errors->clear();
        const SceneSaveResult saved = test.store.SaveScene(scene, path);
        INFO(std::format("saving to '{}' fails with one error, and writes no file", path));
        REQUIRE(saved.status == SaveStatus::Failed);
        REQUIRE(saved.diagnostics.size() == 1);
        REQUIRE(test.errors->size() == 1);
        REQUIRE(saved.entitiesWritten == 0);
    }
    REQUIRE_FALSE(fs::exists(root.Path().parent_path() / "outside.scene.json5"));
    REQUIRE_FALSE(fs::exists(root.Path() / "absolute.scene.json5"));
    REQUIRE_FALSE(fs::exists(root.Path() / "levels"));
}

TEST_CASE("The golden scene file loads into a new scene, and saving it gives the same bytes",
          "[assets][scenes][golden]")
{
    TempRoot root;
    CopyScene(root, "level1.scene.json5");
    DataApp test({root.Path()});

    const SceneLoadResult loaded = test.store.LoadScene(test.app, LevelPath, "Level");

    INFO("the load succeeds with nothing to report, into a new scene of the name given");
    REQUIRE(loaded.status == LoadStatus::Succeeded);
    REQUIRE(loaded.diagnostics.empty());
    REQUIRE(loaded.scene != nullptr);
    REQUIRE(loaded.scene->GetName() == "Level");
    INFO("four entities and their ten components are read, and nothing is kept, dropped, emptied or skipped");
    REQUIRE(loaded.entities == 4);
    REQUIRE(loaded.components == 10);
    REQUIRE(loaded.unknownComponentsKept == 0);
    REQUIRE(loaded.fieldsDropped == 0);
    REQUIRE(loaded.referencesEmptied == 0);
    REQUIRE(loaded.resourcesSkipped == 0);

    Fenrir::Scene& scene = *loaded.scene;
    const Entity level = Named(scene, "Level");
    const Entity player = Named(scene, "Player");
    const Entity sword = Named(scene, "Sword");
    INFO("the level is the one root, holding the player then the sun, in the file's order; the player holds the sword");
    REQUIRE(loaded.roots.size() == 1);
    REQUIRE(loaded.roots[0] == level);
    REQUIRE(ChildNames(level) == std::vector<std::string>{"Player", "Sun"});
    REQUIRE(ChildNames(player) == std::vector<std::string>{"Sword"});
    INFO("each entity has its block's id");
    REQUIRE(IdOf(level) == "a7k2q");
    REQUIRE(IdOf(sword) == "m9x1p");
    INFO("the fields are the file's: the player's position, and its health read at version 2, the type's own");
    REQUIRE(player.GetComponent<Fenrir::Transform>().pos.y == 2.0f);
    REQUIRE(player.GetComponent<AssetsTest::Health>().current == 75.0f);
    INFO("the sword follows the player, named by its id, and its icon loaded with the scene as owner");
    const auto& follower = sword.GetComponent<AssetsTest::Follower>();
    REQUIRE(follower.target == player);
    REQUIRE(test.store.IsValid(follower.icon));
    INFO("the sky resource is added, applied after the entities");
    const auto* sky = scene.GetResource<AssetsTest::Sky>();
    REQUIRE(sky != nullptr);
    REQUIRE(sky->exposure == 1.5f);
    REQUIRE(test.store.IsValid(sky->cubemap));

    const SceneSaveResult saved = test.store.SaveScene(scene, "levels/again.scene.json5");
    INFO("saving the loaded scene writes the golden file again, byte for byte");
    REQUIRE(saved.status == SaveStatus::Succeeded);
    REQUIRE(ReadFile(root.Path() / "levels/again.scene.json5") == Golden());
}

TEST_CASE("A loaded scene is named by its path when no name is given", "[assets][scenes]")
{
    TempRoot root;
    CopyScene(root, "level1.scene.json5");
    DataApp test({root.Path()});

    const SceneLoadResult loaded = test.store.LoadScene(test.app, LevelPath);

    INFO("the load succeeds, and the scene is named by the path as given");
    REQUIRE(loaded.status == LoadStatus::Succeeded);
    REQUIRE(loaded.scene->GetName() == LevelPath);
}

TEST_CASE("A watch drained after a load sees one add per component the file holds", "[assets][scenes]")
{
    TempRoot root;
    CopyScene(root, "level1.scene.json5");
    DataApp test({root.Path()});
    Fenrir::Scene& scene = test.app.CreateScene("Level");
    Fenrir::EntityList& entities = scene.GetEntityList();
    Fenrir::ComponentWatch names = entities.Watch(Fenrir::ComponentType::Native<Fenrir::Name>());
    Fenrir::ComponentWatch ids = entities.Watch(Fenrir::ComponentType::Native<Fenrir::EntityId>());
    Fenrir::ComponentWatch healths = entities.Watch(Fenrir::ComponentType::Native<AssetsTest::Health>());
    Fenrir::ComponentWatch followers = entities.Watch(Fenrir::ComponentType::Native<AssetsTest::Follower>());

    const SceneLoadResult loaded = Fenrir::AssetsProbe::ReadSceneInto(test.store, scene, LevelPath);
    INFO("the golden file loads into the watched scene");
    REQUIRE(loaded.status == LoadStatus::Succeeded);

    INFO("each of the four entities gains one Name, filled in place, and one EntityId, and nothing is removed");
    const std::vector<Fenrir::ComponentChange> named = names.Drain();
    const std::vector<Fenrir::ComponentChange> identified = ids.Drain();
    REQUIRE(named.size() == 4);
    REQUIRE(AddsOnly(named));
    REQUIRE(identified.size() == 4);
    REQUIRE(AddsOnly(identified));
    INFO("the player's Health and the sword's Follower are each added once");
    using Change = Fenrir::ComponentChange;
    REQUIRE(healths.Drain() == std::vector<Change>{{Named(scene, "Player").GetId(), Change::Kind::Added}});
    REQUIRE(followers.Drain() == std::vector<Change>{{Named(scene, "Sword").GetId(), Change::Kind::Added}});
    INFO("the level's children are in the file's order");
    REQUIRE(ChildNames(Named(scene, "Level")) == std::vector<std::string>{"Player", "Sun"});
}

TEST_CASE("A component type the registry lacks is kept with one warning, and written back unchanged on save",
          "[assets][scenes][unknown]")
{
    TempRoot root;
    CopyScene(root, "unknown-components.scene.json5");
    DataApp test({root.Path()});

    const SceneLoadResult loaded = test.store.LoadScene(test.app, LevelPath, "Level");

    INFO("the load is Partial, keeping the level's Tint and the two Glow entries");
    REQUIRE(loaded.status == LoadStatus::Partial);
    REQUIRE(loaded.unknownComponentsKept == 3);
    REQUIRE(loaded.entities == 4);
    REQUIRE(loaded.components == 10);
    INFO("one warning per type in this load, each located at its first entry, though two entities hold a Glow");
    REQUIRE(loaded.diagnostics.size() == 2);
    REQUIRE(loaded.diagnostics[0].severity == DiagnosticSeverity::Warning);
    REQUIRE(loaded.diagnostics[0].message.find("`Mods::Tint` is no component type") != std::string::npos);
    REQUIRE(loaded.diagnostics[0].line == 18);
    REQUIRE(loaded.diagnostics[1].message.find("`Mods::Glow` is no component type") != std::string::npos);
    REQUIRE(loaded.diagnostics[1].line == 48);
    REQUIRE(AssetsTest::CountContaining(*test.warnings, "`Mods::Glow` is no component type") == 1);
    INFO("the sun's Glow is held opaque with the version its entry recorded");
    const Entity sun = Named(*loaded.scene, "Sun");
    REQUIRE(sun.HasComponent<Fenrir::UnknownComponents>());
    const auto& kept = sun.GetComponent<Fenrir::UnknownComponents>().components;
    REQUIRE(kept.size() == 1);
    REQUIRE(kept[0].typeName == "Mods::Glow");
    REQUIRE(kept[0].version == 3);

    const SceneSaveResult saved = test.store.SaveScene(*loaded.scene, "levels/again.scene.json5");
    INFO("the save writes each kept entry back among its entity's entries as it was read, the Glow's leading version "
         "and the Tint's trailing `version: 1` alike: the file comes back unchanged");
    REQUIRE(saved.status == SaveStatus::Succeeded);
    REQUIRE(saved.componentsWritten == 13);
    REQUIRE(ReadFile(root.Path() / "levels/again.scene.json5") ==
            ReadFile(Fixtures() / "scenes" / "unknown-components.scene.json5"));
}

TEST_CASE("An entity field naming an id no block has is left empty, with one located error, and counted",
          "[assets][scenes][references]")
{
    TempRoot root;
    CopyScene(root, "unknown-id.scene.json5");
    DataApp test({root.Path()});

    const SceneLoadResult loaded = test.store.LoadScene(test.app, LevelPath, "Level");

    INFO("the load is Partial, with one reference emptied and the rest of the scene loaded");
    REQUIRE(loaded.status == LoadStatus::Partial);
    REQUIRE(loaded.referencesEmptied == 1);
    REQUIRE(loaded.entities == 4);
    REQUIRE(loaded.components == 10);
    INFO("one error, located at the id's string");
    REQUIRE(loaded.diagnostics.size() == 1);
    REQUIRE(loaded.diagnostics[0].severity == DiagnosticSeverity::Error);
    REQUIRE(loaded.diagnostics[0].line == 41);
    REQUIRE(loaded.diagnostics[0].message.find("zzzzz") != std::string::npos);
    INFO("the follower has no target, and keeps its icon");
    const auto& follower = Named(*loaded.scene, "Sword").GetComponent<AssetsTest::Follower>();
    REQUIRE_FALSE(follower.target.IsValid());
    REQUIRE(test.store.IsValid(follower.icon));
}

TEST_CASE("A block repeating an earlier block's id loads, with one located error, and references name the first",
          "[assets][scenes]")
{
    TempRoot root;
    CopyScene(root, "duplicate-id.scene.json5");
    DataApp test({root.Path()});

    const SceneLoadResult loaded = test.store.LoadScene(test.app, LevelPath, "Level");

    INFO("the load is Partial, and every entity still loads");
    REQUIRE(loaded.status == LoadStatus::Partial);
    REQUIRE(loaded.entities == 4);
    REQUIRE(loaded.components == 10);
    REQUIRE(loaded.referencesEmptied == 0);
    INFO("one error, located at the sun's id");
    REQUIRE(loaded.diagnostics.size() == 1);
    REQUIRE(loaded.diagnostics[0].severity == DiagnosticSeverity::Error);
    REQUIRE(loaded.diagnostics[0].line == 47);
    REQUIRE(loaded.diagnostics[0].message.find("`d4e5f` is an earlier block's too") != std::string::npos);
    INFO("the sword follows the player, the first block with the id");
    Fenrir::Scene& scene = *loaded.scene;
    REQUIRE(Named(scene, "Sword").GetComponent<AssetsTest::Follower>().target == Named(scene, "Player"));
    INFO("the sun is still a child of the level, and loads with no id, so no two entities share one");
    REQUIRE(ChildNames(Named(scene, "Level")) == std::vector<std::string>{"Player", "Sun"});
    REQUIRE(IdOf(Named(scene, "Player")) == "d4e5f");
    REQUIRE(IdOf(Named(scene, "Sun")).empty());
    INFO("a save mints the sun an id of its own");
    REQUIRE(test.store.SaveScene(scene, "levels/again.scene.json5").status == SaveStatus::Succeeded);
    REQUIRE(IdOf(Named(scene, "Sun")).size() == 5);
    REQUIRE(IdOf(Named(scene, "Sun")) != "d4e5f");
}

TEST_CASE("A scene file that does not parse fails, with its located diagnostics and no scene made", "[assets][scenes]")
{
    TempRoot root;
    CopyScene(root, "unparsable.scene.json5");
    DataApp test({root.Path()});
    const std::size_t scenes = SceneCount(test.app);

    const SceneLoadResult loaded = test.store.LoadScene(test.app, LevelPath, "Level");

    INFO("the load fails, makes no scene and reads nothing");
    REQUIRE(loaded.status == LoadStatus::Failed);
    REQUIRE(loaded.scene == nullptr);
    REQUIRE(SceneCount(test.app) == scenes);
    REQUIRE(loaded.roots.empty());
    REQUIRE(loaded.entities == 0);
    REQUIRE(loaded.components == 0);
    INFO("the text's own errors say where the file stopped, each located past the first line");
    REQUIRE_FALSE(loaded.diagnostics.empty());
    for (const Fenrir::Serialisation::Diagnostic& diagnostic : loaded.diagnostics)
    {
        REQUIRE(diagnostic.severity == DiagnosticSeverity::Error);
        REQUIRE(diagnostic.line > 1);
    }
    INFO("the store's one error says the load failed");
    REQUIRE(AssetsTest::CountContaining(*test.errors, "Could not load scene") == 1);
}

TEST_CASE("A file naming another type fails at its `type`, with no scene made", "[assets][scenes]")
{
    TempRoot root;
    CopyScene(root, "wrong-type.scene.json5");
    DataApp test({root.Path()});
    const std::size_t scenes = SceneCount(test.app);

    const SceneLoadResult loaded = test.store.LoadScene(test.app, LevelPath, "Level");

    INFO("the load fails and makes no scene");
    REQUIRE(loaded.status == LoadStatus::Failed);
    REQUIRE(loaded.scene == nullptr);
    REQUIRE(SceneCount(test.app) == scenes);
    INFO("one error, at the `type`, says what was expected and found, logged once");
    REQUIRE(loaded.diagnostics.size() == 1);
    REQUIRE(loaded.diagnostics[0].line == 3);
    REQUIRE(loaded.diagnostics[0].message == "expected `type` \"Fenrir::Scene\", found \"AssetsTest::WeaponData\"");
    REQUIRE(AssetsTest::CountContaining(*test.errors, "AssetsTest::WeaponData") == 1);
}

TEST_CASE("A scene load from outside the asset root, of a sub-asset or of a missing file fails", "[assets][scenes]")
{
    TempRoot root;
    CopyScene(root, "level1.scene.json5");
    DataApp test({root.Path()});
    const std::size_t scenes = SceneCount(test.app);

    for (const std::string_view path :
         {std::string_view("../outside.scene.json5"), std::string_view("levels/level1.scene.json5#part"),
          std::string_view("levels/missing.scene.json5")})
    {
        test.errors->clear();
        const SceneLoadResult loaded = test.store.LoadScene(test.app, path);
        INFO(std::format("loading '{}' fails with one error and makes no scene", path));
        REQUIRE(loaded.status == LoadStatus::Failed);
        REQUIRE(loaded.scene == nullptr);
        REQUIRE(loaded.diagnostics.size() == 1);
        REQUIRE(test.errors->size() == 1);
    }
    REQUIRE(SceneCount(test.app) == scenes);
}

TEST_CASE("A kept component that cannot be written back is left out with a warning, and the save is Partial",
          "[assets][scenes][unknown]")
{
    TempRoot root;
    CopyImages(root);
    DataApp test({root.Path()});
    Fenrir::Scene& scene = test.app.CreateScene("Level");
    const Level level = BuildLevel(test, scene);
    Fenrir::UnknownComponents kept;
    kept.components.push_back({"Mods::Broken", 1, {std::byte{0x00}, std::byte{0xFF}}});
    Entity(level.sun).AddComponent<Fenrir::UnknownComponents>(std::move(kept));

    const SceneSaveResult saved = test.store.SaveScene(scene, LevelPath);

    INFO("the file is written, Partial, with one warning naming the entity and the type");
    REQUIRE(saved.status == SaveStatus::Partial);
    REQUIRE(saved.keptComponentsLost == 1);
    REQUIRE(saved.diagnostics.size() == 1);
    REQUIRE(saved.diagnostics[0].severity == DiagnosticSeverity::Warning);
    REQUIRE(saved.diagnostics[0].message.find("entity `s3w8c`: the kept `Mods::Broken`") != std::string::npos);
    INFO("the rest of the scene is the golden file, and the lost entry is counted as left out");
    REQUIRE(ReadFile(root.Path() / LevelPath) == Golden());
    REQUIRE(saved.componentsWritten == 10);
    REQUIRE(saved.componentsSkipped == 10);
}

TEST_CASE("Merging a scene file under a parent appends its roots after the parent's children, leaving the resources",
          "[assets][scenes][merge]")
{
    TempRoot root;
    CopyScene(root, "level1.scene.json5");
    DataApp test({root.Path()});
    Fenrir::Scene& host = test.app.CreateScene("Host");
    host.AddResource<AssetsTest::Sky>().exposure = 3.0f;
    const Entity mount = Spawn(host, "Mount");
    Spawn(host, "Existing", mount);
    const std::size_t scenes = SceneCount(test.app);

    const SceneLoadResult first = test.store.LoadSceneInto(host, LevelPath, mount);
    const SceneLoadResult second = test.store.LoadSceneInto(host, LevelPath, mount);

    INFO("each merge succeeds with nothing to report, into the scene given; no scene is made");
    REQUIRE(first.status == LoadStatus::Succeeded);
    REQUIRE(first.diagnostics.empty());
    REQUIRE(first.scene == &host);
    REQUIRE(second.status == LoadStatus::Succeeded);
    REQUIRE(SceneCount(test.app) == scenes);
    INFO("each reads the file's four entities and ten components");
    REQUIRE(first.entities == 4);
    REQUIRE(first.components == 10);
    INFO("the file's root goes under the mount after its existing child, the second merge's after the first's");
    REQUIRE(first.roots.size() == 1);
    REQUIRE(second.roots.size() == 1);
    REQUIRE(ChildNames(mount) == std::vector<std::string>{"Existing", "Level", "Level"});
    REQUIRE(first.roots[0].GetComponent<Fenrir::Relationship>().parent == mount);
    REQUIRE(ChildNames(first.roots[0]) == std::vector<std::string>{"Player", "Sun"});
    INFO("the file's sky is counted as skipped, with no diagnostic, and the host's own sky stands");
    REQUIRE(first.resourcesSkipped == 1);
    REQUIRE(host.GetResource<AssetsTest::Sky>()->exposure == 3.0f);
    INFO("each merge's references name its own entities: the second sword follows the second player");
    Entity secondPlayer;
    second.roots[0].ForEachChild([&](const Entity& child) {
        if (std::string_view(child.GetComponent<Fenrir::Name>().Get()) == "Player")
        {
            secondPlayer = child;
        }
    });
    REQUIRE(secondPlayer.IsValid());
    std::size_t swords = 0;
    secondPlayer.ForEachChild([&](const Entity& sword) {
        ++swords;
        REQUIRE(sword.GetComponent<AssetsTest::Follower>().target == secondPlayer);
    });
    REQUIRE(swords == 1);
}

TEST_CASE("Merging a scene file with no parent puts its roots at the top of the scene", "[assets][scenes][merge]")
{
    TempRoot root;
    CopyScene(root, "level1.scene.json5");
    DataApp test({root.Path()});
    Fenrir::Scene& host = test.app.CreateScene("Host");

    const SceneLoadResult merged = test.store.LoadSceneInto(host, LevelPath);

    INFO("the merge succeeds, its root at the top, and the sky the host lacks is still not applied");
    REQUIRE(merged.status == LoadStatus::Succeeded);
    REQUIRE(merged.roots.size() == 1);
    REQUIRE_FALSE(merged.roots[0].GetComponent<Fenrir::Relationship>().parent.IsValid());
    REQUIRE(merged.resourcesSkipped == 1);
    REQUIRE(host.GetResource<AssetsTest::Sky>() == nullptr);
}

TEST_CASE("Merging under a parent of another scene reports an error and puts the roots at the top",
          "[assets][scenes][merge]")
{
    TempRoot root;
    CopyScene(root, "level1.scene.json5");
    DataApp test({root.Path()});
    Fenrir::Scene& host = test.app.CreateScene("Host");
    Fenrir::Scene& other = test.app.CreateScene("Other");
    const Entity elsewhere = Spawn(other, "Elsewhere");

    const SceneLoadResult merged = test.store.LoadSceneInto(host, LevelPath, elsewhere);

    INFO("the merge is Partial, with one error, its root at the top of the host and nothing added to the other scene");
    REQUIRE(merged.status == LoadStatus::Partial);
    REQUIRE(merged.diagnostics.size() == 1);
    REQUIRE(merged.diagnostics[0].severity == DiagnosticSeverity::Error);
    REQUIRE(merged.roots.size() == 1);
    REQUIRE_FALSE(merged.roots[0].GetComponent<Fenrir::Relationship>().parent.IsValid());
    REQUIRE(ChildNames(elsewhere).empty());
}

TEST_CASE("A merge of a file that is no scene fails, adding nothing", "[assets][scenes][merge]")
{
    TempRoot root;
    CopyScene(root, "wrong-type.scene.json5");
    DataApp test({root.Path()});
    Fenrir::Scene& host = test.app.CreateScene("Host");
    const Entity mount = Spawn(host, "Mount");

    const SceneLoadResult merged = test.store.LoadSceneInto(host, LevelPath, mount);

    INFO("the merge fails as LoadScene would, naming no scene, and the mount gains no child");
    REQUIRE(merged.status == LoadStatus::Failed);
    REQUIRE(merged.scene == nullptr);
    REQUIRE(merged.roots.empty());
    REQUIRE(ChildNames(mount).empty());
}

TEST_CASE("A subtree saves byte for byte as the golden subtree file, its references out written null and counted",
          "[assets][scenes][subtree][golden]")
{
    TempRoot root;
    CopyImages(root);
    DataApp test({root.Path()});
    Fenrir::Scene& scene = test.app.CreateScene("Level");
    const Level level = BuildLevel(test, scene);
    AddShield(scene, level);

    const SceneSaveResult saved = test.store.SaveScene(scene, level.player, PlayerPath);

    INFO("the file is the golden subtree file: no resources, the player as its root with no parent, then the sword "
         "following the player and the shield whose target, the sun, is outside the subtree, written null");
    REQUIRE(ReadFile(root.Path() / PlayerPath) == GoldenPlayer());
    INFO("the save is Partial for the one reference written null, with one warning naming it");
    REQUIRE(saved.status == SaveStatus::Partial);
    REQUIRE(saved.nullReferences == 1);
    REQUIRE(saved.diagnostics.size() == 1);
    REQUIRE(saved.diagnostics[0].severity == DiagnosticSeverity::Warning);
    REQUIRE(saved.diagnostics[0].message.find("entity `h2q7r`, `AssetsTest::Follower`: `target`") != std::string::npos);
    INFO("three entities and their nine components are written, and nothing outside the subtree is counted skipped");
    REQUIRE(saved.entitiesWritten == 3);
    REQUIRE(saved.componentsWritten == 9);
    REQUIRE(saved.entitiesSkipped == 0);
    INFO("the scene is unchanged: the player keeps its parent");
    REQUIRE(level.player.GetComponent<Fenrir::Relationship>().parent == level.level);
}

TEST_CASE("A subtree saved then merged back reproduces the subtree's components", "[assets][scenes][subtree][merge]")
{
    TempRoot root;
    CopyImages(root);
    DataApp test({root.Path()});
    Fenrir::Scene& scene = test.app.CreateScene("Level");
    const Level level = BuildLevel(test, scene, false);
    const Entity shield = AddShield(scene, level);
    shield.RemoveComponent<Fenrir::EntityId>();
    const SceneSaveResult saved = test.store.SaveScene(scene, level.player, PlayerPath);
    REQUIRE(saved.status == SaveStatus::Partial);
    INFO("the subtree's entities are minted ids, and the rest of the scene is left without");
    REQUIRE_FALSE(IdOf(level.player).empty());
    REQUIRE_FALSE(IdOf(shield).empty());
    REQUIRE(IdOf(level.level).empty());
    REQUIRE(IdOf(level.sun).empty());

    Fenrir::Scene& copy = test.app.CreateScene("Copy");
    const SceneLoadResult merged = test.store.LoadSceneInto(copy, PlayerPath);

    INFO("the merge reads the three entities and nine components with nothing to report");
    REQUIRE(merged.status == LoadStatus::Succeeded);
    REQUIRE(merged.entities == 3);
    REQUIRE(merged.components == 9);
    REQUIRE(merged.resourcesSkipped == 0);
    REQUIRE(merged.roots.size() == 1);
    const Entity player = merged.roots[0];
    INFO("the player is the root, with the sword then the shield, each with the id the save gave its original");
    REQUIRE(ChildNames(player) == std::vector<std::string>{"Sword", "Shield"});
    REQUIRE(IdOf(player) == IdOf(level.player));
    REQUIRE(IdOf(Named(copy, "Sword")) == IdOf(level.sword));
    REQUIRE(IdOf(Named(copy, "Shield")) == IdOf(shield));
    INFO("the components are the originals': the player's position and health, the sword's icon and its target");
    REQUIRE(player.GetComponent<Fenrir::Transform>().pos.y == 2.0f);
    REQUIRE(player.GetComponent<AssetsTest::Health>().current == 75.0f);
    const auto& follower = Named(copy, "Sword").GetComponent<AssetsTest::Follower>();
    REQUIRE(follower.target == player);
    REQUIRE(follower.icon == level.sword.GetComponent<AssetsTest::Follower>().icon);
    INFO("the shield's target, outside the subtree, comes back empty");
    REQUIRE_FALSE(Named(copy, "Shield").GetComponent<AssetsTest::Follower>().target.IsValid());

    INFO("saving the copy's subtree writes the same file again, save that the emptied target is now a default, left "
         "out");
    const SceneSaveResult again = test.store.SaveScene(copy, player, "prefabs/again.scene.json5");
    REQUIRE(again.status == SaveStatus::Succeeded);
    std::string expected = ReadFile(root.Path() / PlayerPath);
    const std::string nullTarget = "\"AssetsTest::Follower\": {target: null},";
    const std::size_t at = expected.find(nullTarget);
    REQUIRE(at != std::string::npos);
    expected.replace(at, nullTarget.size(), "\"AssetsTest::Follower\": {},");
    REQUIRE(ReadFile(root.Path() / "prefabs/again.scene.json5") == expected);
}

TEST_CASE("A subtree save of an entity not live in the scene is refused, writing nothing", "[assets][scenes][subtree]")
{
    TempRoot root;
    CopyImages(root);
    DataApp test({root.Path()});
    Fenrir::Scene& scene = test.app.CreateScene("Level");
    BuildLevel(test, scene);
    Fenrir::Scene& other = test.app.CreateScene("Other");
    const Entity elsewhere = Spawn(other, "Elsewhere");

    for (const Entity& subtree : {Entity(), elsewhere})
    {
        test.errors->clear();
        const SceneSaveResult saved = test.store.SaveScene(scene, subtree, PlayerPath);
        INFO("an empty root, and one of another scene, each fail with one logged error");
        REQUIRE(saved.status == SaveStatus::Failed);
        REQUIRE(saved.diagnostics.size() == 1);
        REQUIRE(test.errors->size() == 1);
    }
    INFO("no file is written");
    REQUIRE_FALSE(fs::exists(root.Path() / PlayerPath));
}

TEST_CASE("A subtree save of a Transient root is refused, writing nothing", "[assets][scenes][subtree]")
{
    TempRoot root;
    CopyImages(root);
    DataApp test({root.Path()});
    Fenrir::Scene& scene = test.app.CreateScene("Level");
    const Level level = BuildLevel(test, scene);
    Entity(level.player).AddComponent<Fenrir::Transient>();

    const SceneSaveResult saved = test.store.SaveScene(scene, level.player, PlayerPath);

    INFO("a scene file never holds a Transient entity: the save fails with one error and writes no file");
    REQUIRE(saved.status == SaveStatus::Failed);
    REQUIRE(saved.diagnostics.size() == 1);
    REQUIRE(saved.entitiesWritten == 0);
    REQUIRE_FALSE(fs::exists(root.Path() / PlayerPath));
}

TEST_CASE("A subtree merged back into its own scene is given fresh ids, its references naming the copies",
          "[assets][scenes][subtree][merge]")
{
    TempRoot root;
    CopyImages(root);
    DataApp test({root.Path()});
    Fenrir::Scene& scene = test.app.CreateScene("Level");
    const Level level = BuildLevel(test, scene);
    REQUIRE(test.store.SaveScene(scene, level.player, PlayerPath).status == SaveStatus::Succeeded);

    const SceneLoadResult merged = test.store.LoadSceneInto(scene, PlayerPath, level.level);

    INFO("the merge succeeds with nothing to report, the copy going under the level after the sun");
    REQUIRE(merged.status == LoadStatus::Succeeded);
    REQUIRE(merged.diagnostics.empty());
    REQUIRE(ChildNames(level.level) == std::vector<std::string>{"Player", "Sun", "Player"});
    const Entity copy = merged.roots.at(0);
    INFO("the originals keep their ids, and the copies, whose ids the scene had, are each minted another");
    REQUIRE(IdOf(level.player) == "d4e5f");
    REQUIRE(IdOf(level.sword) == "m9x1p");
    REQUIRE_FALSE(IdOf(copy).empty());
    REQUIRE(IdOf(copy) != "d4e5f");
    Entity copiedSword;
    copy.ForEachChild([&](const Entity& child) { copiedSword = child; });
    REQUIRE(copiedSword.IsValid());
    REQUIRE_FALSE(IdOf(copiedSword).empty());
    REQUIRE(IdOf(copiedSword) != "m9x1p");
    INFO("the copied sword follows the copied player, as the file's ids named it");
    REQUIRE(copiedSword.GetComponent<AssetsTest::Follower>().target == copy);
    REQUIRE(level.sword.GetComponent<AssetsTest::Follower>().target == level.player);
}

TEST_CASE("A copy of a scene built in code saves to the same bytes as the original", "[assets][scenes][copy]")
{
    TempRoot root;
    CopyImages(root);
    DataApp test({root.Path()});
    Fenrir::Scene& scene = test.app.CreateScene("Level");
    const Level level = BuildLevel(test, scene);
    const std::size_t scenes = SceneCount(test.app);

    Fenrir::Scene& copy = test.store.CopyScene(test.app, scene, "Play");

    INFO("the copy is a new scene of the name given, apart from the original");
    REQUIRE(&copy != &scene);
    REQUIRE(copy.GetName() == "Play");
    REQUIRE(SceneCount(test.app) == scenes + 1);
    INFO("it holds the level's entities, with their ids, parents and fields, the sword following the copied player");
    const Entity player = Named(copy, "Player");
    const Entity sword = Named(copy, "Sword");
    REQUIRE(player.IsValid());
    REQUIRE(player != level.player);
    REQUIRE(ChildNames(Named(copy, "Level")) == std::vector<std::string>{"Player", "Sun"});
    REQUIRE(IdOf(sword) == "m9x1p");
    REQUIRE(player.GetComponent<AssetsTest::Health>().current == 75.0f);
    REQUIRE(sword.GetComponent<AssetsTest::Follower>().target == player);
    INFO("and the sky resource");
    REQUIRE(copy.GetResource<AssetsTest::Sky>() != nullptr);
    REQUIRE(copy.GetResource<AssetsTest::Sky>()->exposure == 1.5f);

    INFO("saving the copy writes the golden file, the bytes the original saves to");
    REQUIRE(test.store.SaveScene(copy, "levels/copy.scene.json5").status == SaveStatus::Succeeded);
    REQUIRE(test.store.SaveScene(scene, LevelPath).status == SaveStatus::Succeeded);
    REQUIRE(ReadFile(root.Path() / "levels/copy.scene.json5") == Golden());
    REQUIRE(ReadFile(root.Path() / LevelPath) == Golden());
}

TEST_CASE("A copy gives the original's entities that lack one the ids it mints, as a save does",
          "[assets][scenes][copy]")
{
    TempRoot root;
    CopyImages(root);
    DataApp test({root.Path()});
    Fenrir::Scene& scene = test.app.CreateScene("Level");
    const Level level = BuildLevel(test, scene, false);

    Fenrir::Scene& copy = test.store.CopyScene(test.app, scene, "Play");

    INFO("each original entity now has an id, and its copy has the same one");
    REQUIRE(IdOf(level.player).size() == 5);
    REQUIRE(IdOf(Named(copy, "Player")) == IdOf(level.player));
    REQUIRE(IdOf(Named(copy, "Sword")) == IdOf(level.sword));
}

TEST_CASE("A copy leaves out Transient entities, transient and undescribed components", "[assets][scenes][copy]")
{
    TempRoot root;
    CopyImages(root);
    DataApp test({root.Path()});
    Fenrir::Scene& scene = test.app.CreateScene("Level");
    const Level level = BuildLevel(test, scene);
    Entity camera = Spawn(scene, "Camera");
    camera.AddComponent<Fenrir::Transient>();
    Spawn(scene, "Camera rig", camera);
    Entity(level.player).AddComponent<AssetsTest::FrameCounter>().frames = 12;
    Entity(level.player).AddComponent<Wobble>().phase = 0.5f;
    // A reference to the Transient camera, which the copy does not hold.
    Entity(level.sword).GetComponent<AssetsTest::Follower>().target = camera;

    Fenrir::Scene& copy = test.store.CopyScene(test.app, scene, "Play");

    INFO("the Transient camera and the rig below it are not copied");
    REQUIRE_FALSE(Named(copy, "Camera").IsValid());
    REQUIRE_FALSE(Named(copy, "Camera rig").IsValid());
    INFO("the copied player has neither the transient FrameCounter nor the undescribed Wobble");
    const Entity player = Named(copy, "Player");
    REQUIRE(player.IsValid());
    REQUIRE_FALSE(player.HasComponent<AssetsTest::FrameCounter>());
    REQUIRE_FALSE(player.HasComponent<Wobble>());
    INFO("the sword's field naming the camera is left empty");
    REQUIRE_FALSE(Named(copy, "Sword").GetComponent<AssetsTest::Follower>().target.IsValid());
    INFO("the original is untouched");
    REQUIRE(Named(scene, "Camera").IsValid());
    REQUIRE(level.player.HasComponent<Wobble>());
    REQUIRE(level.sword.GetComponent<AssetsTest::Follower>().target == camera);
}

TEST_CASE("A copy writes no file and reads none for the scene", "[assets][scenes][copy]")
{
    TempRoot root;
    CopyImages(root);
    DataApp test({root.Path()});
    Fenrir::Scene& scene = test.app.CreateScene("Level");
    BuildLevel(test, scene);
    const std::set<std::string> before = FilesUnder(root);
    const std::shared_ptr<std::size_t> reads = CountReads(test, root);

    Fenrir::Scene& copy = test.store.CopyScene(test.app, scene, "Play");

    INFO("the asset root holds the same files, and the store's source was not read");
    REQUIRE(FilesUnder(root) == before);
    REQUIRE(*reads == 0);
    INFO("the copy's assets are its own: they outlive the original");
    test.app.DestroyScene("Level");
    const Entity sword = Named(copy, "Sword");
    REQUIRE(test.store.IsValid(sword.GetComponent<AssetsTest::Follower>().icon));
    REQUIRE(test.store.IsValid(copy.GetResource<AssetsTest::Sky>()->cubemap));
}

TEST_CASE("An instance copies as an instance, with what it changes of its source", "[assets][scenes][copy][instance]")
{
    TempRoot root;
    CopyImages(root);
    root.Copy(Fixtures() / "scenes" / "player.scene.json5", PlayerPath);
    DataApp test({root.Path()});
    Fenrir::Scene& scene = test.app.CreateScene("Level");
    const Entity level = Spawn(scene, "Level");
    GiveId(level, "a7k2q");
    const Fenrir::InstanceResult placed = test.store.Instantiate(scene, PlayerPath, level);
    REQUIRE(placed.root.IsValid());
    GiveId(placed.root, "p4tr1");
    // An override inside the instance, and an entity tagged Transient below it, which the copy leaves out.
    Entity sword = placed.entities.at(Fenrir::ParseEntityId("m9x1p")->value);
    sword.GetComponent<AssetsTest::Follower>().speed = 3.0f;
    Entity marker = Spawn(scene, "Marker", sword);
    marker.AddComponent<Fenrir::Transient>();

    Fenrir::Scene& copy = test.store.CopyScene(test.app, scene, "Play");

    INFO("the copied player is an instance of the same source, below the copied level");
    const Entity player = Named(copy, "Player");
    REQUIRE(player.IsValid());
    REQUIRE(player.HasComponent<Fenrir::Instance>());
    REQUIRE(std::string_view(player.GetComponent<Fenrir::Instance>().source) == PlayerPath);
    REQUIRE(player.GetComponent<Fenrir::Relationship>().parent == Named(copy, "Level"));
    INFO("the source's entities are there, the override applied, and the Transient marker is not");
    REQUIRE(Named(copy, "Sword").GetComponent<AssetsTest::Follower>().speed == 3.0f);
    REQUIRE(Named(copy, "Shield").IsValid());
    REQUIRE_FALSE(Named(copy, "Marker").IsValid());

    INFO("the copy and the original save to the same bytes");
    REQUIRE(test.store.SaveScene(scene, LevelPath).status == SaveStatus::Succeeded);
    REQUIRE(test.store.SaveScene(copy, "levels/copy.scene.json5").status == SaveStatus::Succeeded);
    REQUIRE(ReadFile(root.Path() / "levels/copy.scene.json5") == ReadFile(root.Path() / LevelPath));
}

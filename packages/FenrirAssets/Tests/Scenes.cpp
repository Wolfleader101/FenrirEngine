// Saving and loading scenes (ADR-0078, ADR-0079): SaveScene writes a scene built in code to a scene file in ADR-0078's
// layout, the same scene as the same bytes, minting the entity ids it lacks, leaving out what a scene file never holds
// and counting it; LoadScene reads one back into a new scene in two passes, keeping what it cannot read as a component
// and reporting the rest. Every test saves into, or loads from, a temporary asset root of its own.

#include <cstddef>
#include <filesystem>
#include <format>
#include <optional>
#include <set>
#include <string>
#include <string_view>
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

    // The committed golden scene file.
    std::string Golden()
    {
        return ReadFile(Fixtures() / "scenes" / "level1.scene.json5");
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

    const SceneLoadResult loaded = Fenrir::AssetsProbe::LoadSceneInto(test.store, scene, LevelPath);
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

    INFO("the load is Partial, keeping the two Glow entries");
    REQUIRE(loaded.status == LoadStatus::Partial);
    REQUIRE(loaded.unknownComponentsKept == 2);
    REQUIRE(loaded.entities == 4);
    REQUIRE(loaded.components == 10);
    INFO("one warning for the type in this load, located at the first entry, though two entities hold one");
    REQUIRE(loaded.diagnostics.size() == 1);
    REQUIRE(loaded.diagnostics[0].severity == DiagnosticSeverity::Warning);
    REQUIRE(loaded.diagnostics[0].message.find("`Mods::Glow` is no component type") != std::string::npos);
    REQUIRE(loaded.diagnostics[0].line == 44);
    REQUIRE(AssetsTest::CountContaining(*test.warnings, "`Mods::Glow` is no component type") == 1);
    INFO("the sun's Glow is held opaque with the version its entry recorded");
    const Entity sun = Named(*loaded.scene, "Sun");
    REQUIRE(sun.HasComponent<Fenrir::UnknownComponents>());
    const auto& kept = sun.GetComponent<Fenrir::UnknownComponents>().components;
    REQUIRE(kept.size() == 1);
    REQUIRE(kept[0].typeName == "Mods::Glow");
    REQUIRE(kept[0].version == 3);

    const SceneSaveResult saved = test.store.SaveScene(*loaded.scene, "levels/again.scene.json5");
    INFO("the save writes each Glow back among its entity's entries, version and all: the file comes back unchanged");
    REQUIRE(saved.status == SaveStatus::Succeeded);
    REQUIRE(saved.componentsWritten == 12);
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
    INFO("the sun is still a child of the level, and a save gives it an id of its own");
    REQUIRE(ChildNames(Named(scene, "Level")) == std::vector<std::string>{"Player", "Sun"});
    REQUIRE(test.store.SaveScene(scene, "levels/again.scene.json5").status == SaveStatus::Succeeded);
    REQUIRE(IdOf(Named(scene, "Player")) == "d4e5f");
    REQUIRE(IdOf(Named(scene, "Sun")) != "d4e5f");
}

TEST_CASE("A scene file that does not parse fails, with its located diagnostics and no scene made", "[assets][scenes]")
{
    TempRoot root;
    CopyScene(root, "unparsable.scene.json5");
    DataApp test({root.Path()});
    const std::size_t scenes = SceneCount(test.app);

    const SceneLoadResult loaded = test.store.LoadScene(test.app, LevelPath, "Level");

    REQUIRE(loaded.status == LoadStatus::Failed);
    REQUIRE(loaded.scene == nullptr);
    REQUIRE(loaded.roots.empty());
    REQUIRE(loaded.entities == 0);
    REQUIRE(SceneCount(test.app) == scenes);
    INFO("the text's own error says where the file stopped");
    REQUIRE_FALSE(loaded.diagnostics.empty());
    REQUIRE(loaded.diagnostics[0].severity == DiagnosticSeverity::Error);
    REQUIRE(loaded.diagnostics[0].line > 1);
}

TEST_CASE("A file naming another type fails at its `type`, with no scene made", "[assets][scenes]")
{
    TempRoot root;
    CopyScene(root, "wrong-type.scene.json5");
    DataApp test({root.Path()});
    const std::size_t scenes = SceneCount(test.app);

    const SceneLoadResult loaded = test.store.LoadScene(test.app, LevelPath, "Level");

    REQUIRE(loaded.status == LoadStatus::Failed);
    REQUIRE(loaded.scene == nullptr);
    REQUIRE(SceneCount(test.app) == scenes);
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

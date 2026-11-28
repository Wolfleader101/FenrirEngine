// Saving scenes (ADR-0078, ADR-0079): SaveScene writes a scene built in code to a scene file in ADR-0078's layout, the
// same scene as the same bytes, minting the entity ids it lacks, leaving out what a scene file never holds and
// counting it. Every test saves into a temporary asset root of its own.

#include <cstddef>
#include <filesystem>
#include <format>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "DataAssetsTest.hpp"
#include "FenrirECS/EntityId.hpp"
#include "FenrirECS/SceneComponents.hpp"
#include "FenrirScene/Scene.hpp"

using AssetsTest::DataApp;
using AssetsTest::Fixtures;
using AssetsTest::ReadFile;
using AssetsTest::TempRoot;
using Fenrir::Entity;
using Fenrir::SaveStatus;
using Fenrir::SceneSaveResult;

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

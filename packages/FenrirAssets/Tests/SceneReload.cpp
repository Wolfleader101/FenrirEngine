// The scene patch (ADR-0081 §3): a scene LoadScene filled follows its file when the file changes, matched by entity
// id. A matched entity has the components the file writes rewritten and its parent and order restored, an entity only
// in the file is added, and one whose id the file no longer has is destroyed with its children; an entity with no id,
// and a component the file does not write, is left alone, and the file's resources are rewritten. A file that does not
// read changes nothing and says why once, and a destroyed scene is never patched. Every test runs on a temporary asset
// root of its own, with both reload intervals at zero, and rewrites the scene file between RunFrame calls. The instance
// re-patch's tests (ADR-0081 §4) follow the patch's, rewriting a scene or model source instead. No fixture holds a
// tagged union.

#include <cstddef>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "DataAssetsTest.hpp"
#include "FenrirECS/EntityId.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirScene/Scene.hpp"

using AssetsTest::CountContaining;
using AssetsTest::DataApp;
using AssetsTest::TempRoot;
using Fenrir::Entity;
using Fenrir::LoadStatus;
using Fenrir::SceneLoadResult;

namespace
{
    namespace fs = std::filesystem;

    constexpr std::string_view LevelPath = "levels/level.scene.json5";
    constexpr std::string_view RoomPath = "levels/room.scene.json5";

    // A frame's length; the store's watch reads the real clock, so only the reload intervals matter.
    constexpr double Frame = 1.0 / 60.0;

    // A component no package describes, which no scene file writes.
    struct Wobble
    {
        float phase = 0.0f;
    };

    // A scene envelope holding `entities`, the text of its blocks, and a sky at `exposure`.
    std::string SceneText(std::string_view entities, std::string_view exposure = "1.5")
    {
        return std::string("{\n  format: 1,\n  type: \"Fenrir::Scene\",\n  version: 1,\n  data: {\n    resources: {\n"
                           "      \"AssetsTest::Sky\": { exposure: ") +
               std::string(exposure) + " },\n    },\n    entities: [\n" + std::string(entities) + "    ],\n  },\n}\n";
    }

    // The level's blocks: a level holding a player at 75 health, who holds a sword following them, and a crate with a
    // lid at the top.
    constexpr std::string_view LevelBlock = R"(      { id: "a7k2q", components: { "Fenrir::Name": { name: "Level" } } },
)";
    constexpr std::string_view PlayerBlock = R"(      {
        id: "d4e5f",
        parent: "a7k2q",
        components: {
          "Fenrir::Name": { name: "Player" },
          "Fenrir::Transform": { pos: [0, 2, 0] },
          "AssetsTest::Health": { version: 2, current: 75 },
        },
      },
)";
    constexpr std::string_view SwordBlock = R"(      {
        id: "m9x1p",
        parent: "d4e5f",
        components: {
          "Fenrir::Name": { name: "Sword" },
          "AssetsTest::Follower": { target: "d4e5f", speed: 2 },
        },
      },
)";
    constexpr std::string_view CrateBlocks =
        R"(      { id: "c8r4t", components: { "Fenrir::Name": { name: "Crate" } } },
      { id: "k1d0a", parent: "c8r4t", components: { "Fenrir::Name": { name: "Lid" } } },
)";

    // The level as it is first written.
    std::string Level()
    {
        return SceneText(std::string(LevelBlock) + std::string(PlayerBlock) + std::string(SwordBlock) +
                         std::string(CrateBlocks));
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

    // Loads the scene file `path` as the scene `name`, whole, and runs a frame so the watch has looked at it once.
    Fenrir::Scene& Load(DataApp& test, std::string_view path, std::string_view name)
    {
        const SceneLoadResult loaded = test.store.LoadScene(test.app, path, name);
        INFO("the scene file loads whole");
        REQUIRE(loaded.status == LoadStatus::Succeeded);
        REQUIRE(loaded.scene != nullptr);
        RunFrames(test, 1);
        return *loaded.scene;
    }

    // The entity of `scene` holding the id `spelled`, or the null entity.
    Entity WithId(Fenrir::Scene& scene, std::string_view spelled)
    {
        const std::optional<Fenrir::EntityId> id = Fenrir::ParseEntityId(spelled);
        REQUIRE(id.has_value());
        Fenrir::EntityList& entities = scene.GetEntityList();
        Entity found;
        entities.ForEachEntity([&](auto handle) {
            const Entity entity = entities.GetEntity(static_cast<std::uint32_t>(handle));
            if (entity.HasComponent<Fenrir::EntityId>() && entity.GetComponent<Fenrir::EntityId>().value == id->value)
            {
                found = entity;
            }
        });
        return found;
    }

    // Whether `entity` is a live entity of `scene`.
    bool Alive(Fenrir::Scene& scene, const Entity& entity)
    {
        return entity.IsValid() && entity == scene.GetEntityList().GetEntity(entity.GetId());
    }

    // The parent of `entity`, the null entity at the top.
    Entity ParentOf(const Entity& entity)
    {
        return entity.GetComponent<Fenrir::Relationship>().parent;
    }

    // The names of `parent`'s children, in order.
    std::vector<std::string> ChildNames(const Entity& parent)
    {
        std::vector<std::string> names;
        parent.ForEachChild([&](const Entity& child) { names.emplace_back(child.GetComponent<Fenrir::Name>().Get()); });
        return names;
    }

    // How many entities `scene` has.
    std::size_t EntityCount(Fenrir::Scene& scene)
    {
        std::size_t count = 0;
        scene.GetEntityList().ForEachEntity([&](auto) { ++count; });
        return count;
    }
} // namespace

TEST_CASE("A rewritten scene file patches its running scene by entity id within two frames", "[assets][scenes][reload]")
{
    TempRoot root;
    root.Write(LevelPath, Level());
    DataApp test(Watching(root));
    Fenrir::Scene& scene = Load(test, LevelPath, "Level");
    const Entity player = WithId(scene, "d4e5f");
    const Entity sword = WithId(scene, "m9x1p");
    const Entity crate = WithId(scene, "c8r4t");
    const Entity lid = WithId(scene, "k1d0a");
    REQUIRE(player.GetComponent<AssetsTest::Health>().current == 75.0f);

    // The player's health changes and their transform goes back to its defaults; the crate and its lid are gone; a
    // lantern is new; the sky is dimmer.
    constexpr std::string_view Edited = R"(      {
        id: "d4e5f",
        parent: "a7k2q",
        components: {
          "Fenrir::Name": { name: "Player" },
          "Fenrir::Transform": {},
          "AssetsTest::Health": { version: 2, current: 40 },
        },
      },
)";
    constexpr std::string_view Lantern =
        R"(      { id: "n3w1t", parent: "a7k2q", components: { "Fenrir::Name": { name: "Lantern" } } },
)";
    root.Write(LevelPath,
               SceneText(std::string(LevelBlock) + std::string(Edited) + std::string(SwordBlock) + std::string(Lantern),
                         "0.25"));
    RunFrames(test, 2);

    INFO("the player is the same entity, its written components rewritten: a field the file leaves out is its default");
    REQUIRE(Alive(scene, player));
    REQUIRE(WithId(scene, "d4e5f") == player);
    REQUIRE(player.GetComponent<AssetsTest::Health>().current == 40.0f);
    REQUIRE(player.GetComponent<AssetsTest::Health>().maximum == 100.0f);
    REQUIRE(player.GetComponent<Fenrir::Transform>().pos.y == 0.0f);
    INFO("the sword still follows the player");
    REQUIRE(Alive(scene, sword));
    REQUIRE(sword.GetComponent<AssetsTest::Follower>().target == player);
    INFO("the crate is destroyed with its lid, and the lantern is added under the level, after the player");
    REQUIRE_FALSE(Alive(scene, crate));
    REQUIRE_FALSE(Alive(scene, lid));
    const Entity lantern = WithId(scene, "n3w1t");
    REQUIRE(Alive(scene, lantern));
    REQUIRE(ChildNames(WithId(scene, "a7k2q")) == std::vector<std::string>{"Player", "Lantern"});
    REQUIRE(EntityCount(scene) == 4);
    INFO("the sky is rewritten");
    REQUIRE(scene.GetResource<AssetsTest::Sky>()->exposure == 0.25f);
    INFO("nothing was logged");
    REQUIRE(test.errors->empty());
    REQUIRE(test.warnings->empty());

    RunFrames(test, 2);
    INFO("nothing changed since, so nothing more is patched");
    REQUIRE(EntityCount(scene) == 4);
}

TEST_CASE("A scene patch restores each matched entity's parent and order, in file order", "[assets][scenes][reload]")
{
    TempRoot root;
    root.Write(LevelPath, Level());
    DataApp test(Watching(root));
    Fenrir::Scene& scene = Load(test, LevelPath, "Level");
    const Entity level = WithId(scene, "a7k2q");
    const Entity player = WithId(scene, "d4e5f");
    const Entity sword = WithId(scene, "m9x1p");
    const Entity crate = WithId(scene, "c8r4t");

    // At run time the sword is dropped onto the level, and the crate put on the player.
    player.RemoveChild(sword);
    level.AddChild(sword);
    player.AddChild(crate);

    // The file now puts the crate under the level, before the player.
    constexpr std::string_view Crate =
        R"(      { id: "c8r4t", parent: "a7k2q", components: { "Fenrir::Name": { name: "Crate" } } },
      { id: "k1d0a", parent: "c8r4t", components: { "Fenrir::Name": { name: "Lid" } } },
)";
    root.Write(LevelPath, SceneText(std::string(LevelBlock) + std::string(Crate) + std::string(PlayerBlock) +
                                    std::string(SwordBlock)));
    RunFrames(test, 2);

    INFO("the sword is back under the player, and the crate is under the level before them, as the file says");
    REQUIRE(ParentOf(sword) == player);
    REQUIRE(ParentOf(crate) == level);
    REQUIRE(ChildNames(level) == std::vector<std::string>{"Crate", "Player"});
    REQUIRE(ChildNames(player) == std::vector<std::string>{"Sword"});
    REQUIRE(ChildNames(crate) == std::vector<std::string>{"Lid"});
    REQUIRE(test.errors->empty());
}

TEST_CASE("A scene patch leaves run-time entities and components the file does not write alone",
          "[assets][scenes][reload]")
{
    TempRoot root;
    root.Write(LevelPath, Level());
    DataApp test(Watching(root));
    Fenrir::Scene& scene = Load(test, LevelPath, "Level");
    Entity player = WithId(scene, "d4e5f");
    const Entity crate = WithId(scene, "c8r4t");

    // At run time: a torch with no id under the player, a camera with no id at the top, a wobble on the player, and the
    // player hurt.
    Entity torch = scene.GetEntityList().CreateEntity();
    torch.GetComponent<Fenrir::Name>().Set("Torch");
    player.AddChild(torch);
    Entity camera = scene.GetEntityList().CreateEntity();
    camera.GetComponent<Fenrir::Name>().Set("Camera");
    player.AddComponent<Wobble>().phase = 0.5f;
    player.GetComponent<AssetsTest::Health>().current = 10.0f;
    // A run-time child of the crate, which the file drops, goes with it.
    Entity splinter = scene.GetEntityList().CreateEntity();
    crate.AddChild(splinter);

    root.Write(LevelPath, SceneText(std::string(LevelBlock) + std::string(PlayerBlock) + std::string(SwordBlock)));
    RunFrames(test, 2);

    INFO("the entities with no id survive where they were, and the wobble is untouched");
    REQUIRE(Alive(scene, torch));
    REQUIRE(ParentOf(torch) == player);
    REQUIRE(Alive(scene, camera));
    REQUIRE_FALSE(ParentOf(camera).IsValid());
    REQUIRE(player.GetComponent<Wobble>().phase == 0.5f);
    INFO("the health the file writes is the file's again");
    REQUIRE(player.GetComponent<AssetsTest::Health>().current == 75.0f);
    INFO("the crate went, its run-time child with it");
    REQUIRE_FALSE(Alive(scene, crate));
    REQUIRE_FALSE(Alive(scene, splinter));
    REQUIRE(test.errors->empty());
}

TEST_CASE("A scene patch leaves an entity whose id the file never held alone", "[assets][scenes][reload]")
{
    TempRoot root;
    root.Write(LevelPath, Level());
    root.Write(RoomPath, SceneText(R"(      { id: "r00m1", components: { "Fenrir::Name": { name: "Room" } } },
)"));
    DataApp test(Watching(root));
    Fenrir::Scene& scene = Load(test, LevelPath, "Level");
    INFO("a room merges into the level, bringing an id the level's file does not hold");
    REQUIRE(test.store.LoadSceneInto(scene, RoomPath).status == LoadStatus::Succeeded);
    const Entity room = WithId(scene, "r00m1");
    REQUIRE(Alive(scene, room));

    root.Write(LevelPath, SceneText(std::string(LevelBlock) + std::string(PlayerBlock)));
    RunFrames(test, 2);

    INFO("the merged room stays; the sword and crate the level's file dropped are gone");
    REQUIRE(Alive(scene, room));
    REQUIRE_FALSE(WithId(scene, "m9x1p").IsValid());
    REQUIRE_FALSE(WithId(scene, "c8r4t").IsValid());

    root.Write(RoomPath, SceneText(""));
    RunFrames(test, 2);
    INFO("a merge records nothing, so the room's file changing patches nothing");
    REQUIRE(Alive(scene, room));
    REQUIRE(test.errors->empty());
}

TEST_CASE("A broken scene file rewrite changes nothing and says why once", "[assets][scenes][reload]")
{
    TempRoot root;
    root.Write(LevelPath, Level());
    DataApp test(Watching(root));
    Fenrir::Scene& scene = Load(test, LevelPath, "Level");
    const Entity player = WithId(scene, "d4e5f");
    const std::size_t entities = EntityCount(scene);

    root.Write(LevelPath, "{\n  format: 1,\n  type: \"Fenrir::Scene\",\n  data: { entities: [ { id: \"d4e");
    RunFrames(test, 5);

    INFO("the scene is as it was");
    REQUIRE(EntityCount(scene) == entities);
    REQUIRE(player.GetComponent<AssetsTest::Health>().current == 75.0f);
    INFO("one error names the file, once over five frames");
    REQUIRE(CountContaining(*test.errors, "Could not patch the scenes filled from 'levels/level.scene.json5'") == 1);
}

TEST_CASE("A scene file mended after a broken rewrite patches its scene", "[assets][scenes][reload]")
{
    TempRoot root;
    root.Write(LevelPath, Level());
    DataApp test(Watching(root));
    Fenrir::Scene& scene = Load(test, LevelPath, "Level");
    root.Write(LevelPath, "{ format: 1, type: \"Fenrir::Scene\", data: { entities: [");
    RunFrames(test, 2);

    root.Write(LevelPath, SceneText(std::string(LevelBlock) + std::string(PlayerBlock)));
    RunFrames(test, 2);

    INFO("the broken file was taken as read, so the mended one is a change, and patches");
    REQUIRE(EntityCount(scene) == 2);
    REQUIRE(CountContaining(*test.errors, "Could not patch") == 1);
}

TEST_CASE("A scene file's block with no id is not added again by each patch", "[assets][scenes][reload]")
{
    constexpr std::string_view Unnamed = R"(      { components: { "Fenrir::Name": { name: "Rock" } } },
)";
    TempRoot root;
    root.Write(LevelPath, SceneText(std::string(LevelBlock) + std::string(Unnamed)));
    DataApp test(Watching(root));
    const SceneLoadResult loaded = test.store.LoadScene(test.app, LevelPath, "Level");
    REQUIRE(loaded.scene != nullptr);
    Fenrir::Scene& scene = *loaded.scene;
    RunFrames(test, 1);
    REQUIRE(EntityCount(scene) == 2);

    root.Write(LevelPath, SceneText(std::string(LevelBlock) + std::string(Unnamed) + std::string(PlayerBlock)));
    RunFrames(test, 2);
    root.Write(LevelPath, SceneText(std::string(LevelBlock) + std::string(Unnamed)));
    RunFrames(test, 2);

    INFO("the rock the load made is the only one: a block with no id matches nothing, so a patch skips it");
    REQUIRE(EntityCount(scene) == 2);
}

TEST_CASE("A scene patch never gives a merged entity's id to a second entity", "[assets][scenes][reload]")
{
    TempRoot root;
    root.Write(LevelPath, Level());
    root.Write(RoomPath, SceneText(R"(      { id: "r00m1", components: { "Fenrir::Name": { name: "Room" } } },
)"));
    DataApp test(Watching(root));
    Fenrir::Scene& scene = Load(test, LevelPath, "Level");
    REQUIRE(test.store.LoadSceneInto(scene, RoomPath).status == LoadStatus::Succeeded);
    const Entity room = WithId(scene, "r00m1");

    // The level's file gains an instance block of the room's id: it matches no entity, the room being no instance.
    root.Write(LevelPath, SceneText(std::string(LevelBlock) +
                                    R"(      { id: "r00m1", instance: "levels/room.scene.json5" },
)"));
    RunFrames(test, 2);

    INFO("the merged room keeps its id, and no other entity of the scene has it");
    REQUIRE(Alive(scene, room));
    std::size_t holding = 0;
    const std::optional<Fenrir::EntityId> id = Fenrir::ParseEntityId("r00m1");
    scene.GetEntityList().ForEachEntity([&](auto handle) {
        const Entity entity = scene.GetEntityList().GetEntity(static_cast<std::uint32_t>(handle));
        holding += entity.HasComponent<Fenrir::EntityId>() && entity.GetComponent<Fenrir::EntityId>().value == id->value
                       ? 1
                       : 0;
    });
    REQUIRE(holding == 1);
}

TEST_CASE("A scene file that is no scene envelope changes nothing and says why once", "[assets][scenes][reload]")
{
    TempRoot root;
    root.Write(LevelPath, Level());
    DataApp test(Watching(root));
    Fenrir::Scene& scene = Load(test, LevelPath, "Level");
    const std::size_t entities = EntityCount(scene);

    root.Write(LevelPath, "{ format: 1, type: \"AssetsTest::Sword\", version: 1, data: {} }\n");
    RunFrames(test, 5);

    REQUIRE(EntityCount(scene) == entities);
    REQUIRE(CountContaining(*test.errors, "Could not patch the scenes filled from") == 1);
    REQUIRE(CountContaining(*test.errors, "Fenrir::Scene") == 1);
}

TEST_CASE("Every scene loaded from a changed file is patched, and a destroyed one never is", "[assets][scenes][reload]")
{
    TempRoot root;
    root.Write(LevelPath, Level());
    DataApp test(Watching(root));
    Fenrir::Scene& first = Load(test, LevelPath, "First");
    Fenrir::Scene& second = Load(test, LevelPath, "Second");
    (void)Load(test, LevelPath, "Gone");
    test.app.DestroyScene("Gone");

    root.Write(LevelPath, SceneText(std::string(LevelBlock) + std::string(PlayerBlock)));
    RunFrames(test, 2);

    INFO("both live scenes follow the file; the destroyed one was dropped from the record, so nothing touched it");
    REQUIRE(EntityCount(first) == 2);
    REQUIRE(EntityCount(second) == 2);
    REQUIRE(test.errors->empty());
}

TEST_CASE("A destroyed scene's file is no longer watched", "[assets][scenes][reload]")
{
    TempRoot root;
    root.Write(LevelPath, Level());
    DataApp test(Watching(root));
    (void)Load(test, LevelPath, "Level");
    test.app.DestroyScene("Level");

    root.Write(LevelPath, SceneText(std::string(LevelBlock)));
    RunFrames(test, 2);
    fs::remove(root.Path() / LevelPath);
    RunFrames(test, 2);

    INFO("no scene is filled from the file, so its change patches nothing and its going is not warned about");
    REQUIRE(test.errors->empty());
    REQUIRE(test.warnings->empty());
}

TEST_CASE("A deleted scene file keeps its scene with one warning", "[assets][scenes][reload]")
{
    TempRoot root;
    root.Write(LevelPath, Level());
    DataApp test(Watching(root));
    Fenrir::Scene& scene = Load(test, LevelPath, "Level");
    const std::size_t entities = EntityCount(scene);

    fs::remove(root.Path() / LevelPath);
    RunFrames(test, 3);

    REQUIRE(EntityCount(scene) == entities);
    REQUIRE(CountContaining(*test.warnings, "'levels/level.scene.json5' is gone") == 1);
    REQUIRE(test.errors->empty());
}

TEST_CASE("A copied scene records no file, so its source's file changing leaves the copy alone",
          "[assets][scenes][reload][copy]")
{
    TempRoot root;
    root.Write(LevelPath, Level());
    DataApp test(Watching(root));
    Fenrir::Scene& scene = Load(test, LevelPath, "Level");
    Fenrir::Scene& copy = test.store.CopyScene(test.app, scene, "Copy");
    const std::size_t entities = EntityCount(copy);

    root.Write(LevelPath, SceneText(std::string(LevelBlock)));
    RunFrames(test, 2);

    INFO("the loaded scene follows its file; its copy does not");
    REQUIRE(EntityCount(scene) == 1);
    REQUIRE(EntityCount(copy) == entities);
}

TEST_CASE("With hotReload off, a rewritten scene file patches nothing", "[assets][scenes][reload]")
{
    TempRoot root;
    root.Write(LevelPath, Level());
    DataApp test(Watching(root, false));
    Fenrir::Scene& scene = Load(test, LevelPath, "Level");
    const std::size_t entities = EntityCount(scene);

    root.Write(LevelPath, SceneText(std::string(LevelBlock)));
    RunFrames(test, 2);

    REQUIRE(EntityCount(scene) == entities);
}

// The instance re-patch (ADR-0081 §4): a scene LoadScene filled follows the sources its instances expanded. A changed
// scene source is read again into each of its instances by source id, with the host block's diff reapplied on top, a
// run-time child left where it is and the host file never written; a changed model re-spawns its instances under
// their roots; and a source that does not read changes nothing and says why once.
namespace
{
    constexpr std::string_view CratePath = "prefabs/crate.scene.json5";
    constexpr std::string_view YardPath = "levels/yard.scene.json5";
    constexpr std::string_view PropPath = "models/prop.obj";

    // The crate prefab: a crate at `health` with a lid, and `extra`, more blocks.
    std::string Crate(std::string_view health, std::string_view extra = {})
    {
        return SceneText(std::string(R"(      {
        id: "c8r4t",
        components: { "Fenrir::Name": { name: "Crate" }, "AssetsTest::Health": { version: 2, current: )") +
                         std::string(health) + R"( } },
      },
      { id: "k1d0a", parent: "c8r4t", components: { "Fenrir::Name": { name: "Lid" } } },
)" + std::string(extra));
    }

    // The yard: two crates, `first` at x 1 and `second` at x 2, the second at 10 health; `diffs` replaces the second's
    // components when given.
    std::string Yard(
        std::string_view secondComponents =
            R"("Fenrir::Transform": { pos: [2, 0, 0] }, "AssetsTest::Health": { version: 2, current: 10 })",
        std::string_view firstComponents = R"("Fenrir::Transform": { pos: [1, 0, 0] })")
    {
        return SceneText(std::string(R"(      { id: "a1a1a", instance: "prefabs/crate.scene.json5", components: { )") +
                         std::string(firstComponents) + R"( } },
      { id: "b2b2b", instance: "prefabs/crate.scene.json5", components: { )" +
                         std::string(secondComponents) + R"( } },
)");
    }

    // The child of `parent` named `name`, or the null entity.
    Entity ChildNamed(const Entity& parent, std::string_view name)
    {
        Entity found;
        parent.ForEachChild([&](const Entity& child) {
            if (!found.IsValid() && std::string_view(child.GetComponent<Fenrir::Name>().Get()) == name)
            {
                found = child;
            }
        });
        return found;
    }

    // An OBJ model of the objects `names`, each one triangle.
    std::string Prop(const std::vector<std::string>& names)
    {
        std::string text;
        for (std::size_t index = 0; index < names.size(); ++index)
        {
            const std::size_t first = index * 3 + 1;
            text +=
                std::format("o {}\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf {} {} {}\n", names[index], first, first + 1, first + 2);
        }
        return text;
    }
} // namespace

TEST_CASE("An edited scene source re-patches each instance of it, each host's overrides kept",
          "[assets][scenes][reload][instances]")
{
    TempRoot root;
    root.Write(CratePath, Crate("50"));
    root.Write(YardPath, Yard());
    DataApp test(Watching(root));
    Fenrir::Scene& scene = Load(test, YardPath, "Yard");
    const Entity first = WithId(scene, "a1a1a");
    const Entity second = WithId(scene, "b2b2b");
    const Entity firstLid = ChildNamed(first, "Lid");
    REQUIRE(first.GetComponent<AssetsTest::Health>().current == 50.0f);
    REQUIRE(second.GetComponent<AssetsTest::Health>().current == 10.0f);
    const std::string yard = AssetsTest::ReadFile(root.Path() / YardPath);

    // The crate is sturdier now, and gains a hinge under its lid.
    root.Write(CratePath,
               Crate("80", R"(      { id: "h1ng3", parent: "k1d0a", components: { "Fenrir::Name": { name: "Hinge" } } },
)"));
    RunFrames(test, 2);

    INFO("the instances are the same entities, where their hosts put them");
    REQUIRE(Alive(scene, first));
    REQUIRE(Alive(scene, second));
    REQUIRE(first.GetComponent<Fenrir::Transform>().pos.x == 1.0f);
    REQUIRE(second.GetComponent<Fenrir::Transform>().pos.x == 2.0f);
    INFO("the crate the host left alone takes the edit, and the one it overrode keeps its override");
    REQUIRE(first.GetComponent<AssetsTest::Health>().current == 80.0f);
    REQUIRE(second.GetComponent<AssetsTest::Health>().current == 10.0f);
    INFO("each lid is the same entity, with the new hinge under it");
    REQUIRE(Alive(scene, firstLid));
    REQUIRE(ChildNamed(first, "Lid") == firstLid);
    REQUIRE(ChildNames(firstLid) == std::vector<std::string>{"Hinge"});
    REQUIRE(ChildNames(ChildNamed(second, "Lid")) == std::vector<std::string>{"Hinge"});
    REQUIRE(EntityCount(scene) == 6);
    INFO("the host file is untouched, and nothing was logged");
    REQUIRE(AssetsTest::ReadFile(root.Path() / YardPath) == yard);
    REQUIRE(test.errors->empty());
    REQUIRE(test.warnings->empty());

    // The hinge goes again.
    root.Write(CratePath, Crate("80"));
    RunFrames(test, 2);
    INFO("an entity the source no longer has goes from every instance");
    REQUIRE(ChildNames(firstLid).empty());
    REQUIRE(EntityCount(scene) == 4);
}

TEST_CASE("An instance re-patch leaves a run-time child of the instance where it is",
          "[assets][scenes][reload][instances]")
{
    TempRoot root;
    root.Write(CratePath, Crate("50"));
    root.Write(YardPath, Yard());
    DataApp test(Watching(root));
    Fenrir::Scene& scene = Load(test, YardPath, "Yard");
    const Entity first = WithId(scene, "a1a1a");
    const Entity lid = ChildNamed(first, "Lid");

    // At run time a coin goes in the crate, and a fly sits on its lid.
    Entity coin = scene.GetEntityList().CreateEntity();
    coin.GetComponent<Fenrir::Name>().Set("Coin");
    first.AddChild(coin);
    Entity fly = scene.GetEntityList().CreateEntity();
    fly.GetComponent<Fenrir::Name>().Set("Fly");
    lid.AddChild(fly);

    root.Write(CratePath, Crate("80"));
    RunFrames(test, 2);

    REQUIRE(first.GetComponent<AssetsTest::Health>().current == 80.0f);
    INFO("the coin and the fly survive, under the crate and its lid");
    REQUIRE(Alive(scene, coin));
    REQUIRE(ParentOf(coin) == first);
    REQUIRE(Alive(scene, fly));
    REQUIRE(ParentOf(fly) == lid);
    REQUIRE(test.errors->empty());
}

TEST_CASE("An edit to a host's instance diff is reapplied over its source", "[assets][scenes][reload][instances]")
{
    TempRoot root;
    root.Write(CratePath, Crate("50"));
    root.Write(YardPath, Yard());
    DataApp test(Watching(root));
    Fenrir::Scene& scene = Load(test, YardPath, "Yard");
    const Entity first = WithId(scene, "a1a1a");
    const Entity second = WithId(scene, "b2b2b");

    // The second crate is mended to 20, and the first goes back to where its source puts it.
    root.Write(
        YardPath,
        Yard(R"("Fenrir::Transform": { pos: [2, 0, 0] }, "AssetsTest::Health": { version: 2, current: 20 })", ""));
    RunFrames(test, 2);

    REQUIRE(Alive(scene, first));
    REQUIRE(Alive(scene, second));
    REQUIRE(second.GetComponent<AssetsTest::Health>().current == 20.0f);
    REQUIRE(first.GetComponent<Fenrir::Transform>().pos.x == 0.0f);
    REQUIRE(first.GetComponent<AssetsTest::Health>().current == 50.0f);
    REQUIRE(test.errors->empty());
}

TEST_CASE("A rewritten model source re-spawns its instances under their roots", "[assets][scenes][reload][instances]")
{
    TempRoot root;
    root.Write(PropPath, Prop({"Panel", "Marker"}));
    root.Write(
        YardPath,
        SceneText(
            R"(      { id: "a1a1a", instance: "models/prop.obj", components: { "Fenrir::Transform": { pos: [1, 0, 0] } } },
      { id: "b2b2b", instance: "models/prop.obj" },
)"));
    DataApp test(Watching(root));
    Fenrir::Scene& scene = Load(test, YardPath, "Yard");
    const Entity first = WithId(scene, "a1a1a");
    const Entity second = WithId(scene, "b2b2b");
    REQUIRE(ChildNames(first) == std::vector<std::string>{"Panel", "Marker"});
    // A run-time child of the panel, which the model keeps.
    Entity sticker = scene.GetEntityList().CreateEntity();
    sticker.GetComponent<Fenrir::Name>().Set("Sticker");
    ChildNamed(first, "Panel").AddChild(sticker);

    root.Write(PropPath, Prop({"Panel", "Marker", "Beacon"}));
    RunFrames(test, 2);

    INFO("each root is the same entity, where its host put it, with the new model's nodes under it");
    REQUIRE(Alive(scene, first));
    REQUIRE(Alive(scene, second));
    REQUIRE(first.GetComponent<Fenrir::Transform>().pos.x == 1.0f);
    REQUIRE(ChildNames(first) == std::vector<std::string>{"Panel", "Marker", "Beacon"});
    REQUIRE(ChildNames(second) == std::vector<std::string>{"Panel", "Marker", "Beacon"});
    INFO("the run-time child moved to the new panel");
    REQUIRE(Alive(scene, sticker));
    REQUIRE(ParentOf(sticker) == ChildNamed(first, "Panel"));
    REQUIRE(EntityCount(scene) == 9);
    REQUIRE(test.errors->empty());
}

TEST_CASE("A broken scene source changes none of its instances and says why once",
          "[assets][scenes][reload][instances]")
{
    TempRoot root;
    root.Write(CratePath, Crate("50"));
    root.Write(YardPath, Yard());
    DataApp test(Watching(root));
    Fenrir::Scene& scene = Load(test, YardPath, "Yard");
    const Entity first = WithId(scene, "a1a1a");
    const Entity lid = ChildNamed(first, "Lid");
    const std::size_t entities = EntityCount(scene);

    root.Write(CratePath, "{\n  format: 1,\n  type: \"Fenrir::Scene\",\n  data: { entities: [ { id: \"c8r");
    RunFrames(test, 5);

    INFO("the instances are as they were");
    REQUIRE(EntityCount(scene) == entities);
    REQUIRE(Alive(scene, lid));
    REQUIRE(first.GetComponent<AssetsTest::Health>().current == 50.0f);
    INFO("one error names the source, once over five frames");
    REQUIRE(CountContaining(*test.errors, "Could not re-patch the instances of 'prefabs/crate.scene.json5'") == 1);

    // Mended, it re-patches them.
    root.Write(CratePath, Crate("80"));
    RunFrames(test, 2);
    REQUIRE(first.GetComponent<AssetsTest::Health>().current == 80.0f);
}

TEST_CASE("A destroyed scene's instance sources are no longer watched", "[assets][scenes][reload][instances]")
{
    TempRoot root;
    root.Write(CratePath, Crate("50"));
    root.Write(YardPath, Yard());
    DataApp test(Watching(root));
    (void)Load(test, YardPath, "Yard");
    test.app.DestroyScene("Yard");

    fs::remove(root.Path() / CratePath);
    RunFrames(test, 2);

    INFO("no scene instances the crate, so its going is not warned about");
    REQUIRE(test.errors->empty());
    REQUIRE(test.warnings->empty());
}

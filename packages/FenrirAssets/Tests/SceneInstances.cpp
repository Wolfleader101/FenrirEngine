// A scene instance's diff (ADR-0080 §4, §6, §8): SaveScene writes an instance as its source and what it changes of a
// fresh expansion of the source, never a value equal to the source's; LoadScene expands the source and applies the
// diff, so an edit to the source reaches every field the host did not override; a variant is a file whose one root
// block is an instance; and Unpack unlinks an instance so its subtree saves verbatim. The source is the tower fixture:
// a gate following the tower, a flag with health, a guard, and a nested instance of the player. Every test works in a
// temporary asset root of its own.

#include <cstddef>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "DataAssetsTest.hpp"
#include "FenrirAssets/ModelSpawner.hpp"
#include "FenrirECS/EntityId.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/SceneComponents.hpp"
#include "FenrirScene/Scene.hpp"

using AssetsTest::DataApp;
using AssetsTest::FindDiagnostic;
using AssetsTest::Fixtures;
using AssetsTest::ReadFile;
using AssetsTest::TempRoot;
using Fenrir::Entity;
using Fenrir::InstanceResult;
using Fenrir::LoadStatus;
using Fenrir::SaveStatus;
using Fenrir::SceneLoadResult;
using Fenrir::SceneSaveResult;
using Fenrir::Math::Vec3;

namespace
{
    constexpr std::string_view TowerPath = "prefabs/tower.scene.json5";
    constexpr std::string_view PlayerPath = "prefabs/player.scene.json5";
    constexpr std::string_view VariantPath = "prefabs/watchtower.scene.json5";
    constexpr std::string_view ModelPath = "models/scene.gltf";
    constexpr std::string_view HostPath = "levels/keep.scene.json5";

    // An asset root holding the tower, the player it nests (with the icon its sword names), the sky's cubemap, and
    // the model.
    void CopySources(const TempRoot& root)
    {
        root.Copy(Fixtures() / "fourbytwo.png", "icons/sword.png");
        root.Copy(Fixtures() / "twobytwo.hdr", "sky/day.hdr");
        root.Copy(Fixtures() / "scenes" / "player.scene.json5", PlayerPath);
        root.Copy(Fixtures() / "scenes" / "tower.scene.json5", TowerPath);
        for (const char* file : {"scene.gltf", "scene.bin", "checker.png", "normal.png"})
        {
            root.Copy(Fixtures() / "models" / file, std::string("models/") + file);
        }
    }

    std::string Golden(std::string_view name)
    {
        return ReadFile(Fixtures() / "scenes" / std::string(name));
    }

    Fenrir::EntityId Id(std::string_view spelled)
    {
        const std::optional<Fenrir::EntityId> id = Fenrir::ParseEntityId(spelled);
        REQUIRE(id.has_value());
        return *id;
    }

    void GiveId(Entity entity, std::string_view spelled)
    {
        if (entity.HasComponent<Fenrir::EntityId>())
        {
            entity.GetComponent<Fenrir::EntityId>() = Id(spelled);
            return;
        }
        entity.AddComponent<Fenrir::EntityId>(Id(spelled));
    }

    std::string IdOf(const Entity& entity)
    {
        return entity.HasComponent<Fenrir::EntityId>() ? Fenrir::SpellEntityId(entity.GetComponent<Fenrir::EntityId>())
                                                       : std::string();
    }

    std::string NameOf(const Entity& entity)
    {
        return entity.GetComponent<Fenrir::Name>().Get();
    }

    std::string SourceOf(const Entity& entity)
    {
        return entity.HasComponent<Fenrir::Instance>() ? std::string(entity.GetComponent<Fenrir::Instance>().source)
                                                       : std::string();
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

    // The child of `parent` named `name`, or the null entity.
    Entity ChildNamed(const Entity& parent, std::string_view name)
    {
        Entity found;
        parent.ForEachChild([&](const Entity& child) {
            if (!found.IsValid() && NameOf(child) == name)
            {
                found = child;
            }
        });
        return found;
    }

    std::vector<std::string> ChildNames(const Entity& parent)
    {
        std::vector<std::string> names;
        parent.ForEachChild([&](const Entity& child) { names.push_back(NameOf(child)); });
        return names;
    }

    // Every block id the file `bytes` holds, in order.
    std::vector<std::string> BlockIds(const std::string& bytes)
    {
        static const std::regex pattern("\\bid: \"([^\"]+)\"");
        std::vector<std::string> ids;
        for (auto match = std::sregex_iterator(bytes.begin(), bytes.end(), pattern); match != std::sregex_iterator();
             ++match)
        {
            ids.push_back((*match)[1]);
        }
        return ids;
    }

    // The keep the golden keep file holds, built in code: a keep holding an instance of the tower that is moved, whose
    // gate is faster and follows the keep, whose flag has lost its health, whose guard is gone, whose nested player's
    // sword is faster, and whose flag holds a banner the host added, following the gate. Two values are set to what
    // the source holds already, so the file must not write them.
    struct Keep
    {
        Entity keep;
        InstanceResult tower;
        Entity banner;
    };

    Keep BuildKeep(DataApp& test, Fenrir::Scene& scene)
    {
        Keep built;
        built.keep = Spawn(scene, "Keep");
        GiveId(built.keep, "k33p0");
        built.tower = test.store.Instantiate(scene, TowerPath, built.keep);
        REQUIRE(built.tower.load.status == LoadStatus::Succeeded);
        const Entity root = built.tower.root;
        GiveId(root, "t1nst");
        root.GetComponent<Fenrir::Transform>().pos = Vec3(10.0f, 0.0f, 0.0f);
        root.GetComponent<Fenrir::Name>().Set("Tower");

        const Entity gate = built.tower.Find(Id("g4te1"));
        auto& follower = gate.GetComponent<AssetsTest::Follower>();
        follower.speed = 4.0f;
        follower.target = built.keep;

        Entity flag = built.tower.Find(Id("f1ag0"));
        flag.RemoveComponent<AssetsTest::Health>();
        flag.GetComponent<Fenrir::Transform>().pos = Vec3(0.0f, 5.0f, 0.0f);

        const Entity guard = built.tower.Find(Id("g0ard"));
        root.RemoveChild(guard);
        scene.GetEntityList().DestroyEntity(guard);

        const Entity sword = ChildNamed(built.tower.Find(Id("p4tr1")), "Sword");
        sword.GetComponent<AssetsTest::Follower>().speed = 6.0f;

        built.banner = Spawn(scene, "Banner", flag);
        GiveId(built.banner, "b4nnr");
        built.banner.AddComponent<AssetsTest::Follower>().target = gate;
        return built;
    }

    // Checks the tower below `keep` is the golden keep's: moved, its gate faster and following the keep, its flag
    // without health and holding the banner, its guard gone, its nested sword faster.
    void CheckKeep(const Entity& keep)
    {
        const Entity tower = ChildNamed(keep, "Tower");
        REQUIRE(tower.IsValid());
        REQUIRE(IdOf(tower) == "t1nst");
        REQUIRE(SourceOf(tower) == TowerPath);
        REQUIRE(tower.GetComponent<Fenrir::Transform>().pos == Vec3(10.0f, 0.0f, 0.0f));
        REQUIRE(ChildNames(tower) == std::vector<std::string>{"Gate", "Flag", "Player"});
        const Entity gate = ChildNamed(tower, "Gate");
        REQUIRE(gate.GetComponent<AssetsTest::Follower>().speed == 4.0f);
        REQUIRE(gate.GetComponent<AssetsTest::Follower>().target == keep);
        const Entity flag = ChildNamed(tower, "Flag");
        REQUIRE_FALSE(flag.HasComponent<AssetsTest::Health>());
        REQUIRE(flag.GetComponent<Fenrir::Transform>().pos == Vec3(0.0f, 5.0f, 0.0f));
        const Entity banner = ChildNamed(flag, "Banner");
        REQUIRE(IdOf(banner) == "b4nnr");
        REQUIRE(banner.GetComponent<AssetsTest::Follower>().target == gate);
        const Entity player = ChildNamed(tower, "Player");
        const Entity sword = ChildNamed(player, "Sword");
        REQUIRE(sword.GetComponent<AssetsTest::Follower>().speed == 6.0f);
        REQUIRE(sword.GetComponent<AssetsTest::Follower>().target == player);
    }

    // The keep's one root, from a load of the keep file.
    Entity LoadedKeep(const SceneLoadResult& loaded)
    {
        REQUIRE(loaded.roots.size() == 1);
        REQUIRE(NameOf(loaded.roots[0]) == "Keep");
        return loaded.roots[0];
    }
} // namespace

TEST_CASE("An instance saves as its source and what it changes, never a value equal to the source's",
          "[assets][scenes][instances][overrides][golden]")
{
    TempRoot root;
    CopySources(root);
    DataApp test({root.Path()});
    Fenrir::Scene& scene = test.app.CreateScene("Keep");
    BuildKeep(test, scene);

    const SceneSaveResult saved = test.store.SaveScene(scene, HostPath);

    INFO("the save succeeds with nothing to report");
    REQUIRE(saved.status == SaveStatus::Succeeded);
    REQUIRE(saved.diagnostics.empty());
    INFO(
        "the file is the golden keep: the root's Transform, the gate's two changed fields, the flag's lost health, the "
        "sword's speed, the guard removed, and the banner a block under the flag's slash path");
    const std::string bytes = ReadFile(root.Path() / HostPath);
    REQUIRE(bytes == Golden("keep.scene.json5"));
    INFO("three blocks: the keep, the tower and the banner");
    REQUIRE(saved.entitiesWritten == 3);
    REQUIRE(saved.nullReferences == 0);
    INFO("the root's name and the flag's place equal the source's, so neither is written");
    REQUIRE(bytes.find("\"Tower\"") == std::string::npos);
    REQUIRE(bytes.find("[0, 5, 0]") == std::string::npos);
}

TEST_CASE("A host file applies each instance's diff to a fresh expansion of its source, and saves the same bytes",
          "[assets][scenes][instances][overrides][golden]")
{
    TempRoot root;
    CopySources(root);
    root.Copy(Fixtures() / "scenes" / "keep.scene.json5", HostPath);
    DataApp test({root.Path()});

    const SceneLoadResult loaded = test.store.LoadScene(test.app, HostPath);

    INFO("the load succeeds with nothing to report");
    REQUIRE(loaded.status == LoadStatus::Succeeded);
    REQUIRE(loaded.diagnostics.empty());
    INFO("the tower is moved, its gate follows the keep out of the instance, the guard is gone, the banner is added");
    CheckKeep(LoadedKeep(loaded));

    INFO("saving it again writes the golden bytes");
    REQUIRE(test.store.SaveScene(*loaded.scene, HostPath).status == SaveStatus::Succeeded);
    REQUIRE(ReadFile(root.Path() / HostPath) == Golden("keep.scene.json5"));
}

TEST_CASE("An edit to the source reaches every field the host did not override when the host next loads",
          "[assets][scenes][instances][overrides]")
{
    TempRoot root;
    CopySources(root);
    root.Copy(Fixtures() / "scenes" / "keep.scene.json5", HostPath);
    std::string tower = Golden("tower.scene.json5");
    // The tower is renamed, its gate placed and made slower, its flag's health raised.
    const auto replace = [&](std::string_view from, std::string_view to) {
        const std::size_t at = tower.find(from);
        REQUIRE(at != std::string::npos);
        tower.replace(at, from.size(), to);
    };
    replace("{name: \"Tower\"}", "{name: \"Big Tower\"}");
    replace("\"Fenrir::Name\": {name: \"Gate\"},\n          \"Fenrir::Transform\": {},",
            "\"Fenrir::Name\": {name: \"Gate\"},\n          \"Fenrir::Transform\": {pos: [0, 0, 3]},");
    replace("speed: 2,", "speed: 9,");
    replace("current: 50,", "current: 80,");
    root.Write(TowerPath, tower);
    DataApp test({root.Path()});

    const SceneLoadResult loaded = test.store.LoadScene(test.app, HostPath);

    REQUIRE(loaded.status == LoadStatus::Succeeded);
    const Entity keep = LoadedKeep(loaded);
    INFO("the name and the gate's place were not overridden, so they are the source's new ones");
    const Entity big = ChildNamed(keep, "Big Tower");
    REQUIRE(big.IsValid());
    const Entity gate = ChildNamed(big, "Gate");
    REQUIRE(gate.GetComponent<Fenrir::Transform>().pos == Vec3(0.0f, 0.0f, 3.0f));
    INFO("what the host overrode is still the host's: the tower's place and the gate's speed");
    REQUIRE(big.GetComponent<Fenrir::Transform>().pos == Vec3(10.0f, 0.0f, 0.0f));
    REQUIRE(gate.GetComponent<AssetsTest::Follower>().speed == 4.0f);
    INFO("and the flag's health is still removed, whatever the source holds now");
    REQUIRE_FALSE(ChildNamed(big, "Flag").HasComponent<AssetsTest::Health>());
}

TEST_CASE("An entity a host adds inside an instance saves as a block of its own, and a field names it",
          "[assets][scenes][instances][overrides]")
{
    TempRoot root;
    CopySources(root);
    DataApp test({root.Path()});
    Fenrir::Scene& scene = test.app.CreateScene("Host");
    const InstanceResult player = test.store.Instantiate(scene, PlayerPath);
    GiveId(player.root, "p1ace");
    // Added at run time with no id: the save mints one.
    const Entity added = Spawn(scene, "Added", player.Find(Id("m9x1p")));
    Entity watcher = Spawn(scene, "Watcher");
    GiveId(watcher, "w4tch");
    watcher.AddComponent<AssetsTest::Follower>().target = added;

    const SceneSaveResult saved = test.store.SaveScene(scene, HostPath);

    INFO("the added entity is a block under the sword's slash path, and the field names it by its minted id");
    REQUIRE(saved.status == SaveStatus::Succeeded);
    REQUIRE(saved.entitiesWritten == 3);
    const std::string bytes = ReadFile(root.Path() / HostPath);
    const std::string id = IdOf(added);
    REQUIRE(id.size() == 5);
    REQUIRE(bytes.find("parent: \"p1ace/m9x1p\"") != std::string::npos);
    REQUIRE(bytes.find("{target: \"" + id + "\"}") != std::string::npos);

    const SceneLoadResult loaded = test.store.LoadScene(test.app, HostPath, "Loaded");
    REQUIRE(loaded.status == LoadStatus::Succeeded);
    Entity loadedPlayer;
    Entity loadedWatcher;
    for (const Entity& top : loaded.roots)
    {
        (NameOf(top) == "Player" ? loadedPlayer : loadedWatcher) = top;
    }
    INFO("it loads back under the sword, once, the field naming it, and saves the same bytes");
    const Entity sword = ChildNamed(loadedPlayer, "Sword");
    REQUIRE(ChildNames(sword) == std::vector<std::string>{"Added"});
    REQUIRE(loadedWatcher.GetComponent<AssetsTest::Follower>().target == ChildNamed(sword, "Added"));
    REQUIRE(test.store.SaveScene(*loaded.scene, HostPath).status == SaveStatus::Succeeded);
    REQUIRE(ReadFile(root.Path() / HostPath) == bytes);
}

TEST_CASE("A component the instance's root lost is written in the block's `removed`, and is taken off on load",
          "[assets][scenes][instances][overrides]")
{
    TempRoot root;
    CopySources(root);
    DataApp test({root.Path()});
    Fenrir::Scene& scene = test.app.CreateScene("Host");
    const InstanceResult player = test.store.Instantiate(scene, PlayerPath);
    GiveId(player.root, "p1ace");
    Entity(player.root).RemoveComponent<AssetsTest::Health>();

    REQUIRE(test.store.SaveScene(scene, HostPath).status == SaveStatus::Succeeded);

    INFO("the block names the lost type, and nothing else of the player");
    const std::string bytes = ReadFile(root.Path() / HostPath);
    REQUIRE(bytes.find("removed: [\"AssetsTest::Health\"]") != std::string::npos);
    REQUIRE(bytes.find("overrides") == std::string::npos);
    const SceneLoadResult loaded = test.store.LoadScene(test.app, HostPath, "Loaded");
    REQUIRE(loaded.status == LoadStatus::Succeeded);
    REQUIRE(NameOf(loaded.roots.at(0)) == "Player");
    REQUIRE_FALSE(loaded.roots.at(0).HasComponent<AssetsTest::Health>());
}

TEST_CASE("A model instance diffs against a fresh spawn of the model, its overrides keyed by node number",
          "[assets][scenes][instances][overrides][models]")
{
    TempRoot root;
    CopySources(root);
    DataApp test({root.Path()});
    Fenrir::Scene& scene = test.app.CreateScene("Host");
    const InstanceResult model = test.store.Instantiate(scene, ModelPath);
    GiveId(model.root, "m0de1");
    Entity marker = model.Find(Fenrir::ModelEntityId(3));
    marker.GetComponent<Fenrir::Transform>().pos = Vec3(0.0f, 7.0f, 0.0f);

    const SceneSaveResult saved = test.store.SaveScene(scene, HostPath);

    INFO("one override, the Marker's place, by its number; nothing else of the model is written");
    REQUIRE(saved.status == SaveStatus::Succeeded);
    const std::string bytes = ReadFile(root.Path() / HostPath);
    REQUIRE(bytes.find("\"3\": {") != std::string::npos);
    REQUIRE(bytes.find("[0, 7, 0]") != std::string::npos);
    REQUIRE(bytes.find("\"2\"") == std::string::npos);

    const SceneLoadResult loaded = test.store.LoadScene(test.app, HostPath, "Loaded");
    REQUIRE(loaded.status == LoadStatus::Succeeded);
    INFO("the load spawns the model afresh and moves the Marker again");
    const Entity loadedMarker = ChildNamed(ChildNamed(loaded.roots.at(0), "Root"), "Marker");
    REQUIRE(loadedMarker.GetComponent<Fenrir::Transform>().pos == Vec3(0.0f, 7.0f, 0.0f));
    REQUIRE(test.store.SaveScene(*loaded.scene, HostPath).status == SaveStatus::Succeeded);
    REQUIRE(ReadFile(root.Path() / HostPath) == bytes);
}

TEST_CASE("A variant is a scene file whose one root block is an instance of another, with its own resources",
          "[assets][scenes][instances][variants][golden]")
{
    TempRoot root;
    CopySources(root);
    DataApp test({root.Path()});
    Fenrir::Scene& scene = test.app.CreateScene("Watchtower");
    const InstanceResult tower = test.store.Instantiate(scene, TowerPath);
    GiveId(tower.root, "v4r1a");
    tower.root.GetComponent<Fenrir::Name>().Set("Watchtower");
    tower.Find(Id("g4te1")).GetComponent<AssetsTest::Follower>().speed = 7.0f;
    GiveId(Spawn(scene, "Bell", tower.Find(Id("f1ag0"))), "b3110");
    auto& sky = scene.AddResource<AssetsTest::Sky>();
    sky.cubemap = test.store.Load<Fenrir::Image>("sky/day.hdr", scene).handle;
    sky.exposure = 2.0f;

    const SceneSaveResult saved = test.store.SaveScene(scene, VariantPath);

    INFO("the file is the golden variant: its sky, the instance of the tower with its changes, and the bell");
    REQUIRE(saved.status == SaveStatus::Succeeded);
    REQUIRE(ReadFile(root.Path() / VariantPath) == Golden("watchtower.scene.json5"));

    const SceneLoadResult loaded = test.store.LoadScene(test.app, VariantPath, "Loaded");

    INFO("LoadScene loads it like any scene: the base's entities, the variant's changes and its sky");
    REQUIRE(loaded.status == LoadStatus::Succeeded);
    REQUIRE(loaded.diagnostics.empty());
    REQUIRE(loaded.roots.size() == 1);
    const Entity watchtower = loaded.roots[0];
    REQUIRE(NameOf(watchtower) == "Watchtower");
    REQUIRE(SourceOf(watchtower) == TowerPath);
    REQUIRE(ChildNames(watchtower) == std::vector<std::string>{"Gate", "Flag", "Guard", "Player"});
    REQUIRE(ChildNamed(watchtower, "Gate").GetComponent<AssetsTest::Follower>().speed == 7.0f);
    REQUIRE(ChildNames(ChildNamed(watchtower, "Flag")) == std::vector<std::string>{"Bell"});
    const auto* loadedSky = loaded.scene->GetResource<AssetsTest::Sky>();
    REQUIRE(loadedSky != nullptr);
    REQUIRE(loadedSky->exposure == 2.0f);
    REQUIRE(test.store.SaveScene(*loaded.scene, VariantPath).status == SaveStatus::Succeeded);
    REQUIRE(ReadFile(root.Path() / VariantPath) == Golden("watchtower.scene.json5"));
}

TEST_CASE("A variant instanced into a host holds its base's entities and its own changes, and saves as one block",
          "[assets][scenes][instances][variants]")
{
    TempRoot root;
    CopySources(root);
    root.Copy(Fixtures() / "scenes" / "watchtower.scene.json5", VariantPath);
    DataApp test({root.Path()});
    Fenrir::Scene& scene = test.app.CreateScene("Host");

    const InstanceResult watchtower = test.store.Instantiate(scene, VariantPath);

    INFO("the root names the variant; the base's entities and the variant's bell are found by their ids");
    REQUIRE(watchtower.load.status == LoadStatus::Succeeded);
    REQUIRE(SourceOf(watchtower.root) == VariantPath);
    REQUIRE(NameOf(watchtower.root) == "Watchtower");
    REQUIRE(watchtower.Find(Id("g4te1")).GetComponent<AssetsTest::Follower>().speed == 7.0f);
    REQUIRE(NameOf(watchtower.Find(Id("b3110"))) == "Bell");
    INFO("the variant's sky is not the host's");
    REQUIRE(scene.GetResource<AssetsTest::Sky>() == nullptr);

    GiveId(watchtower.root, "h0st1");
    REQUIRE(test.store.SaveScene(scene, HostPath).status == SaveStatus::Succeeded);
    INFO("unchanged, it is one block naming the variant, with no diff");
    const std::string bytes = ReadFile(root.Path() / HostPath);
    REQUIRE(bytes.find("{id: \"h0st1\", instance: \"prefabs/watchtower.scene.json5\"}") != std::string::npos);
    REQUIRE(BlockIds(bytes) == std::vector<std::string>{"h0st1"});
}

TEST_CASE("Unpack unlinks an instance one level, re-minting its entities' ids, so it saves verbatim",
          "[assets][scenes][instances][unpack][golden]")
{
    TempRoot root;
    CopySources(root);
    root.Copy(Fixtures() / "scenes" / "keep.scene.json5", HostPath);
    DataApp test({root.Path()});
    const SceneLoadResult loaded = test.store.LoadScene(test.app, HostPath);
    REQUIRE(loaded.status == LoadStatus::Succeeded);
    Fenrir::Scene& scene = *loaded.scene;
    const Entity keep = LoadedKeep(loaded);
    const Entity tower = ChildNamed(keep, "Tower");
    const Entity gate = ChildNamed(tower, "Gate");
    const Entity flag = ChildNamed(tower, "Flag");
    const Entity banner = ChildNamed(flag, "Banner");
    const Entity player = ChildNamed(tower, "Player");
    const Entity sword = ChildNamed(player, "Sword");

    REQUIRE(test.store.Unpack(scene, tower));

    INFO("the tower is no instance now, and keeps its own id");
    REQUIRE_FALSE(tower.HasComponent<Fenrir::Instance>());
    REQUIRE(IdOf(tower) == "t1nst");
    INFO("its entities, the nested player's root among them, have fresh ids, none of them repeated");
    const std::set<std::string> fresh{IdOf(gate), IdOf(flag), IdOf(banner), IdOf(player)};
    REQUIRE(fresh.size() == 4);
    for (const std::string& id : {"g4te1", "f1ag0", "b4nnr", "p4tr1", "k33p0", "t1nst"})
    {
        REQUIRE_FALSE(fresh.contains(id));
    }
    INFO("the nested player is still an instance, and its sword keeps its id there");
    REQUIRE(SourceOf(player) == PlayerPath);
    REQUIRE(IdOf(sword) == "m9x1p");
    INFO("unpacking what is no instance's root is refused");
    REQUIRE_FALSE(test.store.Unpack(scene, gate));

    // The minted ids are random: the golden spells them so, which fixes the bytes and changes nothing else.
    GiveId(gate, "g8t30");
    GiveId(flag, "f8a90");
    GiveId(banner, "b8nn0");
    GiveId(player, "p8tr0");
    const SceneSaveResult saved = test.store.SaveScene(scene, HostPath);

    INFO("the file is the expanded golden: a block per entity, the nested player one instance block with its diff");
    REQUIRE(saved.status == SaveStatus::Succeeded);
    const std::string bytes = ReadFile(root.Path() / HostPath);
    REQUIRE(bytes == Golden("keep-unpacked.scene.json5"));
    const std::vector<std::string> ids = BlockIds(bytes);
    REQUIRE(std::set<std::string>(ids.begin(), ids.end()).size() == ids.size());
}

TEST_CASE("A diff naming what its source lacks is warned of and ignored, and the rest of it applies",
          "[assets][scenes][instances][overrides]")
{
    TempRoot root;
    CopySources(root);
    root.Write(HostPath, R"({
  format: 1,
  type: "Fenrir::Scene",
  version: 1,
  data: {
    resources: {},
    entities: [
      {
        id: "t1nst",
        instance: "prefabs/tower.scene.json5",
        removed: ["AssetsTest::Missing"],
        overrides: {
          zzzzz: {components: {"Fenrir::Transform": {pos: [1, 1, 1]}}},
          g4te1: {components: {"AssetsTest::Follower": {speed: 3}}},
        },
        removedEntities: ["qqqqq", "g0ard"],
      },
      {id: "n0b0d", parent: "t1nst/zzzzz", components: {"Fenrir::Name": {name: "Orphan"}}},
    ],
  },
})");
    DataApp test({root.Path()});

    const SceneLoadResult loaded = test.store.LoadScene(test.app, HostPath);

    INFO("the load is Partial, a warning apiece for the missing component, override and entity");
    REQUIRE(loaded.status == LoadStatus::Partial);
    REQUIRE(FindDiagnostic(loaded.diagnostics, "the override `zzzzz` names no entity") != nullptr);
    REQUIRE(FindDiagnostic(loaded.diagnostics, "qqqqq") != nullptr);
    REQUIRE(FindDiagnostic(loaded.diagnostics, "AssetsTest::Missing") != nullptr);
    INFO("an added block under a path naming nothing loads at the top, with an error");
    REQUIRE(FindDiagnostic(loaded.diagnostics, "slash path") != nullptr);
    REQUIRE(loaded.roots.size() == 2);
    INFO("the rest applies: the gate is faster and the guard gone");
    const Entity tower = loaded.roots[0];
    REQUIRE(ChildNamed(tower, "Gate").GetComponent<AssetsTest::Follower>().speed == 3.0f);
    REQUIRE_FALSE(ChildNamed(tower, "Guard").IsValid());
}

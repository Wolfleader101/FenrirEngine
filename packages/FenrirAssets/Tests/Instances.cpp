// Scene instances (ADR-0080): Instantiate places a scene or model file into a scene as one instance, its root marked
// with Instance; SaveScene writes the instance as one block naming its source, and an entity field pointing inside it
// as a slash path; LoadScene expands each instance block again, and refuses one that would instance its own file.
// Every test works in a temporary asset root of its own.

#include <cstddef>
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

namespace
{
    constexpr std::string_view PlayerPath = "prefabs/player.scene.json5";
    constexpr std::string_view ModelPath = "models/scene.gltf";
    constexpr std::string_view HostPath = "levels/host.scene.json5";

    // An asset root holding the player prefab (with the icon its sword names), the model, and the other scene
    // fixtures under `prefabs/`.
    void CopySources(const TempRoot& root)
    {
        root.Copy(Fixtures() / "fourbytwo.png", "icons/sword.png");
        root.Copy(Fixtures() / "scenes" / "player.scene.json5", PlayerPath);
        for (const char* name : {"pair", "self", "chain-a", "chain-b", "squad"})
        {
            const std::string file = std::string(name) + ".scene.json5";
            root.Copy(Fixtures() / "scenes" / file, "prefabs/" + file);
        }
        for (const char* file : {"scene.gltf", "scene.bin", "checker.png", "normal.png"})
        {
            root.Copy(Fixtures() / "models" / file, std::string("models/") + file);
        }
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

    Entity ParentOf(const Entity& entity)
    {
        return entity.GetComponent<Fenrir::Relationship>().parent;
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

    std::size_t EntityCount(Fenrir::Scene& scene)
    {
        std::size_t count = 0;
        scene.GetEntityList().ForEachEntity([&](auto) { ++count; });
        return count;
    }

    // The host the golden host file holds, built in code: a stage holding the player prefab and the model as
    // instances, a watcher following the sword inside the player, and a spotter following the model's Marker.
    struct Host
    {
        Entity stage;
        InstanceResult player;
        InstanceResult model;
        Entity watcher;
        Entity spotter;
    };

    Host BuildHost(DataApp& test, Fenrir::Scene& scene)
    {
        Host host;
        host.stage = Spawn(scene, "Stage");
        GiveId(host.stage, "st4ge");
        host.player = test.store.Instantiate(scene, PlayerPath, host.stage);
        REQUIRE(host.player.root.IsValid());
        GiveId(host.player.root, "p1ace");
        host.model = test.store.Instantiate(scene, ModelPath, host.stage);
        REQUIRE(host.model.root.IsValid());
        GiveId(host.model.root, "m0de1");
        host.watcher = Spawn(scene, "Watcher", host.stage);
        GiveId(host.watcher, "w4tch");
        host.watcher.AddComponent<AssetsTest::Follower>().target = host.player.Find(Id("m9x1p"));
        host.spotter = Spawn(scene, "Spotter", host.stage);
        GiveId(host.spotter, "sp0tr");
        host.spotter.AddComponent<AssetsTest::Follower>().target = host.model.Find(Fenrir::ModelEntityId(3));
        return host;
    }
} // namespace

TEST_CASE("Instancing a scene file puts its single root under the parent, marked with Instance, its entities found by "
          "their ids in the file",
          "[assets][scenes][instances]")
{
    TempRoot root;
    CopySources(root);
    DataApp test({root.Path()});
    Fenrir::Scene& scene = test.app.CreateScene("Host");
    const Entity mount = Spawn(scene, "Mount");

    const InstanceResult player = test.store.Instantiate(scene, PlayerPath, mount);

    INFO("the source reads whole, its three entities and its empty resources");
    REQUIRE(player.load.status == LoadStatus::Succeeded);
    REQUIRE(player.load.diagnostics.empty());
    REQUIRE(player.load.entities == 3);
    REQUIRE(player.load.scene == &scene);
    INFO("the root is the file's root, the player, under the mount, marked with the source and holding no id");
    REQUIRE(NameOf(player.root) == "Player");
    REQUIRE(ParentOf(player.root) == mount);
    REQUIRE(SourceOf(player.root) == PlayerPath);
    REQUIRE_FALSE(player.root.HasComponent<Fenrir::EntityId>());
    INFO("each entity is found by its id in the file, the root by its own too");
    REQUIRE(player.entities.size() == 3);
    REQUIRE(player.Find(Id("d4e5f")) == player.root);
    const Entity sword = player.Find(Id("m9x1p"));
    REQUIRE(NameOf(sword) == "Sword");
    REQUIRE(ParentOf(sword) == player.root);
    REQUIRE(IdOf(sword) == "m9x1p");
    REQUIRE(NameOf(player.Find(Id("h2q7r"))) == "Shield");
    REQUIRE_FALSE(player.Find(Id("zzzzz")).IsValid());
    INFO("the sword's field names the root, as the file's names the player");
    REQUIRE(sword.GetComponent<AssetsTest::Follower>().target == player.root);
}

TEST_CASE("Instancing a scene file twice keeps its ids in each instance", "[assets][scenes][instances]")
{
    TempRoot root;
    CopySources(root);
    DataApp test({root.Path()});
    Fenrir::Scene& scene = test.app.CreateScene("Host");

    const InstanceResult first = test.store.Instantiate(scene, PlayerPath);
    const InstanceResult second = test.store.Instantiate(scene, PlayerPath);

    INFO("both read whole, and each sword keeps the file's id, scoped by its own root");
    REQUIRE(first.load.status == LoadStatus::Succeeded);
    REQUIRE(second.load.status == LoadStatus::Succeeded);
    REQUIRE(IdOf(first.Find(Id("m9x1p"))) == "m9x1p");
    REQUIRE(IdOf(second.Find(Id("m9x1p"))) == "m9x1p");
    REQUIRE(first.Find(Id("m9x1p")) != second.Find(Id("m9x1p")));
    INFO("each instance's references stay inside it");
    REQUIRE(second.Find(Id("m9x1p")).GetComponent<AssetsTest::Follower>().target == second.root);
}

TEST_CASE("A scene file with several roots is instanced under a root named after the file, at the identity",
          "[assets][scenes][instances]")
{
    TempRoot root;
    CopySources(root);
    DataApp test({root.Path()});
    Fenrir::Scene& scene = test.app.CreateScene("Host");

    const InstanceResult pair = test.store.Instantiate(scene, "prefabs/pair.scene.json5");

    INFO("a new root named after the file holds both of the file's roots in file order, and is counted");
    REQUIRE(pair.load.status == LoadStatus::Succeeded);
    REQUIRE(NameOf(pair.root) == "pair");
    REQUIRE(SourceOf(pair.root) == "prefabs/pair.scene.json5");
    REQUIRE(ChildNames(pair.root) == std::vector<std::string>{"Left", "Right"});
    REQUIRE(pair.load.roots.size() == 2);
    REQUIRE(pair.load.entities == 3);
    REQUIRE(pair.root.GetComponent<Fenrir::Transform>().pos == Fenrir::Math::Vec3(0.0f));
    INFO("the file's roots are found by their ids; the made root has none");
    REQUIRE(NameOf(pair.Find(Id("aaaa1"))) == "Left");
    REQUIRE(NameOf(pair.Find(Id("bbbb2"))) == "Right");
    REQUIRE_FALSE(pair.root.HasComponent<Fenrir::EntityId>());
}

TEST_CASE("Instancing a model spawns its nodes under the parent, the root marked with Instance and each node numbered "
          "from one",
          "[assets][scenes][instances][models]")
{
    TempRoot root;
    CopySources(root);
    DataApp test({root.Path()});
    Fenrir::Scene& scene = test.app.CreateScene("Host");
    const Entity mount = Spawn(scene, "Mount");

    const InstanceResult model = test.store.Instantiate(scene, ModelPath, mount);

    INFO("SpawnNodes' root, named after the file, under the mount and marked with the model's path");
    REQUIRE(model.load.status == LoadStatus::Succeeded);
    REQUIRE(NameOf(model.root) == "scene");
    REQUIRE(ParentOf(model.root) == mount);
    REQUIRE(SourceOf(model.root) == ModelPath);
    REQUIRE(model.load.roots == std::vector<Entity>{model.root});
    INFO("the root and its four nodes: Root, Panel, Marker, Empty, found by their node index plus one");
    REQUIRE(model.load.entities == 5);
    REQUIRE(model.entities.size() == 4);
    REQUIRE(NameOf(model.Find(Fenrir::ModelEntityId(1))) == "Root");
    REQUIRE(NameOf(model.Find(Fenrir::ModelEntityId(2))) == "Panel");
    REQUIRE(NameOf(model.Find(Fenrir::ModelEntityId(3))) == "Marker");
    REQUIRE(NameOf(model.Find(Fenrir::ModelEntityId(4))) == "Empty");
}

TEST_CASE("A model id is its number in base32", "[assets][scenes][instances][models]")
{
    INFO("one to thirty-one are one character, thirty-two is the first of two");
    REQUIRE(Fenrir::SpellEntityId(Fenrir::ModelEntityId(1)) == "1");
    REQUIRE(Fenrir::SpellEntityId(Fenrir::ModelEntityId(10)) == "a");
    REQUIRE(Fenrir::SpellEntityId(Fenrir::ModelEntityId(31)) == "z");
    REQUIRE(Fenrir::SpellEntityId(Fenrir::ModelEntityId(32)) == "10");
    INFO("a model id never collides with a minted one, which has five characters");
    REQUIRE(Fenrir::SpellEntityId(Fenrir::ModelEntityId(32 * 32 * 32 * 32 - 1)) == "zzzz");
}

TEST_CASE("A host holding a scene instance and a model instance saves each as one block, a field into each as a "
          "slash path",
          "[assets][scenes][instances][golden]")
{
    TempRoot root;
    CopySources(root);
    DataApp test({root.Path()});
    Fenrir::Scene& scene = test.app.CreateScene("Host");
    BuildHost(test, scene);

    const SceneSaveResult saved = test.store.SaveScene(scene, HostPath);

    INFO("the save succeeds with nothing to report");
    REQUIRE(saved.status == SaveStatus::Succeeded);
    REQUIRE(saved.diagnostics.empty());
    INFO("the file is the golden host: each instance an id, a parent and its source, each field into one a slash path");
    REQUIRE(ReadFile(root.Path() / HostPath) == ReadFile(Fixtures() / "scenes" / "host.scene.json5"));
    INFO("five blocks; the entities inside the instances are their sources', neither written nor skipped");
    REQUIRE(saved.entitiesWritten == 5);
    REQUIRE(saved.entitiesSkipped == 0);
    REQUIRE(saved.nullReferences == 0);
}

TEST_CASE("The host file loads each instance back under its parent, its slash paths naming the entities inside, and "
          "saves the same bytes",
          "[assets][scenes][instances][golden]")
{
    TempRoot root;
    CopySources(root);
    root.Copy(Fixtures() / "scenes" / "host.scene.json5", HostPath);
    DataApp test({root.Path()});

    const SceneLoadResult loaded = test.store.LoadScene(test.app, HostPath);

    INFO("the load succeeds, with the entities of both instances among those made");
    REQUIRE(loaded.status == LoadStatus::Succeeded);
    REQUIRE(loaded.diagnostics.empty());
    REQUIRE(loaded.entitiesSkipped == 0);
    REQUIRE(loaded.entities == 3 + 3 + 5);
    REQUIRE(loaded.roots.size() == 1);
    const Entity stage = loaded.roots[0];
    REQUIRE(ChildNames(stage) == std::vector<std::string>{"Player", "scene", "Watcher", "Spotter"});
    INFO("each instance's root takes its block's id and is marked with its source");
    const Entity player = ChildNamed(stage, "Player");
    const Entity model = ChildNamed(stage, "scene");
    REQUIRE(IdOf(player) == "p1ace");
    REQUIRE(SourceOf(player) == PlayerPath);
    REQUIRE(IdOf(model) == "m0de1");
    REQUIRE(SourceOf(model) == ModelPath);
    INFO("the watcher follows the sword inside the player, the spotter the Marker inside the model");
    REQUIRE(ChildNamed(stage, "Watcher").GetComponent<AssetsTest::Follower>().target == ChildNamed(player, "Sword"));
    const Entity marker = ChildNamed(ChildNamed(model, "Root"), "Marker");
    REQUIRE(marker.IsValid());
    REQUIRE(ChildNamed(stage, "Spotter").GetComponent<AssetsTest::Follower>().target == marker);

    const SceneSaveResult saved = test.store.SaveScene(*loaded.scene, HostPath);
    INFO("saving it again writes the golden bytes");
    REQUIRE(saved.status == SaveStatus::Succeeded);
    REQUIRE(ReadFile(root.Path() / HostPath) == ReadFile(Fixtures() / "scenes" / "host.scene.json5"));
}

TEST_CASE("A slash path chains through a nested instance, and reads back", "[assets][scenes][instances]")
{
    TempRoot root;
    CopySources(root);
    DataApp test({root.Path()});
    Fenrir::Scene& scene = test.app.CreateScene("Host");
    const InstanceResult squad = test.store.Instantiate(scene, "prefabs/squad.scene.json5");
    REQUIRE(squad.load.status == LoadStatus::Succeeded);
    GiveId(squad.root, "t3am0");
    INFO("the squad's player is found by its own id in the squad, its sword not: it is in the player's scope");
    const Entity player = squad.Find(Id("a1pha"));
    REQUIRE(SourceOf(player) == PlayerPath);
    REQUIRE_FALSE(squad.Find(Id("m9x1p")).IsValid());
    Entity watcher = Spawn(scene, "Watcher");
    GiveId(watcher, "w4tch");
    watcher.AddComponent<AssetsTest::Follower>().target = ChildNamed(player, "Sword");

    const SceneSaveResult saved = test.store.SaveScene(scene, HostPath);

    INFO("the field is written through both instances' ids");
    REQUIRE(saved.status == SaveStatus::Succeeded);
    const std::string bytes = ReadFile(root.Path() / HostPath);
    REQUIRE(bytes.find("{target: \"t3am0/a1pha/m9x1p\"}") != std::string::npos);

    const SceneLoadResult loaded = test.store.LoadScene(test.app, HostPath, "Loaded");
    REQUIRE(loaded.status == LoadStatus::Succeeded);
    Entity loadedSquad;
    Entity loadedWatcher;
    for (const Entity& top : loaded.roots)
    {
        (NameOf(top) == "Squad" ? loadedSquad : loadedWatcher) = top;
    }
    INFO("the load follows the path to the sword inside the nested player, and saves the same bytes");
    const Entity loadedSword = ChildNamed(ChildNamed(loadedSquad, "Player"), "Sword");
    REQUIRE(loadedSword.IsValid());
    REQUIRE(loadedWatcher.GetComponent<AssetsTest::Follower>().target == loadedSword);
    REQUIRE(test.store.SaveScene(*loaded.scene, HostPath).status == SaveStatus::Succeeded);
    REQUIRE(ReadFile(root.Path() / HostPath) == bytes);
}

TEST_CASE("A field naming an entity inside an instance with no id in its source is written null",
          "[assets][scenes][instances][references]")
{
    TempRoot root;
    CopySources(root);
    DataApp test({root.Path()});
    Fenrir::Scene& scene = test.app.CreateScene("Host");
    const InstanceResult player = test.store.Instantiate(scene, PlayerPath);
    // Added at run time: it is no entity of the source, and has no id there.
    const Entity added = Spawn(scene, "Added", player.root);
    Spawn(scene, "Watcher").AddComponent<AssetsTest::Follower>().target = added;

    const SceneSaveResult saved = test.store.SaveScene(scene, HostPath);

    INFO("the field is written null, warned of and counted");
    REQUIRE(saved.status == SaveStatus::Partial);
    REQUIRE(saved.nullReferences == 1);
    REQUIRE(FindDiagnostic(saved.diagnostics, "inside an instance") != nullptr);
}

TEST_CASE("A file instancing itself is refused at the block, with an error, and counted as a skipped entity",
          "[assets][scenes][instances][self]")
{
    TempRoot root;
    CopySources(root);
    DataApp test({root.Path()});
    Fenrir::Scene& scene = test.app.CreateScene("Host");

    const InstanceResult mirror = test.store.Instantiate(scene, "prefabs/self.scene.json5");

    INFO("the rest of the file loads, Partial: the mirror, with nothing below it");
    REQUIRE(mirror.load.status == LoadStatus::Partial);
    REQUIRE(NameOf(mirror.root) == "Mirror");
    REQUIRE(ChildNames(mirror.root).empty());
    REQUIRE(mirror.load.entities == 1);
    INFO("the block is counted as skipped, with one located error at its `instance`");
    REQUIRE(mirror.load.entitiesSkipped == 1);
    REQUIRE(mirror.load.diagnostics.size() == 1);
    const Fenrir::Serialisation::Diagnostic* refused = FindDiagnostic(mirror.load.diagnostics, "instance itself");
    REQUIRE(refused != nullptr);
    REQUIRE(refused->message.find("this file") != std::string::npos);
    REQUIRE(refused->line > 1);
    REQUIRE(AssetsTest::CountContaining(*test.errors, "instance itself") == 1);
}

TEST_CASE("A file instancing itself through another file is refused at the block naming it", "[assets][scenes][self]")
{
    TempRoot root;
    CopySources(root);
    DataApp test({root.Path()});

    const SceneLoadResult loaded = test.store.LoadScene(test.app, "prefabs/chain-a.scene.json5");

    INFO("A holds its instance of B, and B's instance of A is refused and counted");
    REQUIRE(loaded.status == LoadStatus::Partial);
    REQUIRE(loaded.roots.size() == 1);
    const Entity b = ChildNamed(loaded.roots[0], "B");
    REQUIRE(SourceOf(b) == "prefabs/chain-b.scene.json5");
    REQUIRE(ChildNames(b).empty());
    REQUIRE(loaded.entitiesSkipped == 1);
    REQUIRE(FindDiagnostic(loaded.diagnostics, "a file instancing this one") != nullptr);
}

TEST_CASE("An instance of a source that does not load adds nothing; a block naming one keeps its root and saves back",
          "[assets][scenes][instances]")
{
    TempRoot root;
    CopySources(root);
    root.Copy(Fixtures() / "scenes" / "missing-source.scene.json5", HostPath);
    DataApp test({root.Path()});
    Fenrir::Scene& scene = test.app.CreateScene("Host");

    const InstanceResult missing = test.store.Instantiate(scene, "prefabs/missing.scene.json5");
    const InstanceResult outside = test.store.Instantiate(scene, "../outside.scene.json5");
    const InstanceResult noModel = test.store.Instantiate(scene, "models/missing.gltf");

    INFO("each fails, with no root and nothing added to the scene");
    for (const InstanceResult* failed : {&missing, &outside, &noModel})
    {
        REQUIRE(failed->load.status == LoadStatus::Failed);
        REQUIRE_FALSE(failed->root.IsValid());
        REQUIRE_FALSE(failed->load.diagnostics.empty());
    }
    REQUIRE(EntityCount(scene) == 0);

    const SceneLoadResult loaded = test.store.LoadScene(test.app, HostPath, "Loaded");
    INFO("the host loads Partial, the block's root alone, marked with its source, with an error at the block");
    REQUIRE(loaded.status == LoadStatus::Partial);
    Entity ghost;
    loaded.roots.at(0).ForEachChild([&](const Entity& child) {
        if (SourceOf(child) == "prefabs/missing.scene.json5")
        {
            ghost = child;
        }
    });
    REQUIRE(ghost.IsValid());
    REQUIRE(SourceOf(ghost) == "prefabs/missing.scene.json5");
    REQUIRE(IdOf(ghost) == "gh0st");
    REQUIRE(FindDiagnostic(loaded.diagnostics, "did not load") != nullptr);
    INFO("saving it writes the block back as it was");
    REQUIRE(test.store.SaveScene(*loaded.scene, HostPath).status == SaveStatus::Succeeded);
    REQUIRE(ReadFile(root.Path() / HostPath) == ReadFile(Fixtures() / "scenes" / "missing-source.scene.json5"));
}

TEST_CASE("Instancing under a parent of another scene reports an error and puts the root at the top",
          "[assets][scenes][instances]")
{
    TempRoot root;
    CopySources(root);
    DataApp test({root.Path()});
    Fenrir::Scene& scene = test.app.CreateScene("Host");
    Fenrir::Scene& other = test.app.CreateScene("Other");
    const Entity elsewhere = Spawn(other, "Elsewhere");

    const InstanceResult player = test.store.Instantiate(scene, PlayerPath, elsewhere);

    INFO("the instance loads Partial, at the top, with one error");
    REQUIRE(player.load.status == LoadStatus::Partial);
    REQUIRE(player.root.IsValid());
    REQUIRE_FALSE(ParentOf(player.root).IsValid());
    REQUIRE(FindDiagnostic(player.load.diagnostics, "no live entity") != nullptr);
}

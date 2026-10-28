// Components defined in C#: what a script assembly reports, the storage shared with C# and removed with its
// entity, an aligned component and an empty one, and the change buffer from C#: entities created at once, Refs
// surviving changes, reads of live state, playback in order, and the changes that are refused.

#include "FixtureCalls.hpp"
#include "HeadlessApp.hpp"
#include "ScriptingProbe.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirScripting/Scripting.hpp"

namespace
{
    using namespace Fenrir;
    using namespace Fenrir::Testing;
    using namespace Fenrir::Testing::ScriptFixture;

    void AScriptAssemblyReportsTheComponentsItDefines()
    {
        HeadlessApp headless;
        auto& scene = headless.Get().GetActiveScene().GetEntityList();
        const std::size_t poolsBefore = PoolCount(scene);
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        {
            INFO("The fixture did not load");
            REQUIRE(scripting.HasScripts());
        }

        // Registering components creates no storage, and neither does asking about them: a pool appears when a
        // component is first added in a scene. (So "a failed load leaves no pools" is a real property, not an accident
        // of timing.)
        {
            INFO("Loading a script assembly created storage");
            REQUIRE(PoolCount(scene) == poolsBefore);
        }
        const auto* velocity = scripting.FindComponent(VelocityName);
        const auto probe = scene.CreateEntity().GetId();
        const std::size_t poolsWithEntity = PoolCount(scene);
        {
            INFO("Reading or removing an absent script component misbehaved");
            REQUIRE((!scene.Has(velocity->type, probe) && scene.Get(velocity->type, probe) == nullptr &&
                     scene.Remove(velocity->type, probe)));
        }
        {
            INFO("Asking about a script component created storage");
            REQUIRE(PoolCount(scene) == poolsWithEntity);
        }
        {
            INFO("Adding a script component did not create its pool");
            REQUIRE((scene.Add(velocity->type, probe) && PoolCount(scene) == poolsWithEntity + 1));
        }

        const auto expect = [&](const char* name, std::size_t size, std::size_t alignment) {
            const auto* registration = scripting.FindComponent(name);
            {
                INFO(std::string("The script component ") + name + " was not registered");
                REQUIRE(registration != nullptr);
            }
            {
                INFO(std::string(name) + " was not described as a script component");
                REQUIRE((registration->scriptDefined && !registration->readOnly && registration->typeName == name));
            }
            {
                INFO(std::string(name) +
                     " has the wrong size or alignment: " + std::to_string(registration->type.Size()) + " / " +
                     std::to_string(registration->type.Alignment()));
                REQUIRE((registration->type.Size() == size && registration->type.Alignment() == alignment));
            }
        };
        expect(VelocityName, 12, 4);
        expect(CounterName, 16, 8);
        expect(AlignedName, 32, 16);
        expect(TaggedName, 1, 1);

        // Native components are not script-defined, and a [NativeComponent] struct in the script is not reported as
        // one.
        {
            INFO("A native component was described as script-defined");
            REQUIRE(!scripting.FindComponent("Fenrir.Transform")->scriptDefined);
        }
        {
            INFO("A [NativeComponent] struct was reported as a script-defined component");
            REQUIRE(scripting.FindComponent("Fenrir.ScriptFixture.Health") == nullptr);
        }
    }

    void AScriptComponentIsSharedWithCSharpAndRemovedWithItsEntity()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        const auto* velocity = scripting.FindComponent(VelocityName);
        {
            INFO("Velocity was not registered");
            REQUIRE(velocity != nullptr);
        }

        auto first = entities.CreateEntity();
        auto second = entities.CreateEntity();
        auto third = entities.CreateEntity();
        {
            INFO("A component was there before it was added");
            REQUIRE(!entities.Has(velocity->type, first.GetId()));
        }
        for (auto* entity : {&first, &second, &third})
        {
            {
                INFO("A component was not added");
                REQUIRE(entities.Add(velocity->type, entity->GetId()));
            }
        }
        {
            INFO("Adding twice failed");
            REQUIRE(entities.Add(velocity->type, first.GetId()));
        } // Harmless, and keeps the one it has.

        // A new component is all zero bytes, which is a C# default.
        {
            INFO("A new script component was not zeroed");
            REQUIRE(Scripted<float>(scripting, "SpeedX", first.GetId()) == 0.0F);
        }

        // C# writes to it; native code sees the same bytes.
        Scripted<void>(scripting, "Accelerate", first.GetId());
        Scripted<void>(scripting, "Accelerate", first.GetId());
        Scripted<void>(scripting, "Accelerate", third.GetId());
        const auto* values = static_cast<const float*>(entities.Get(velocity->type, first.GetId()));
        {
            INFO("C# writes were not in the engine's storage");
            REQUIRE((values != nullptr && values[0] == 2 && values[1] == 4 && values[2] == 6));
        }
        {
            INFO("A second entity's component was wrong");
            REQUIRE(Scripted<float>(scripting, "SpeedX", third.GetId()) == 1.0F);
        }
        {
            INFO("A write reached the wrong entity's component");
            REQUIRE(Scripted<float>(scripting, "SpeedX", second.GetId()) == 0.0F);
        }

        // Native code writes it too, and C# reads that.
        static_cast<float*>(entities.Get(velocity->type, second.GetId()))[0] = 42;
        {
            INFO("C# did not read a native write");
            REQUIRE(Scripted<float>(scripting, "SpeedX", second.GetId()) == 42.0F);
        }

        // The pool holds one element per entity that has the component.
        {
            INFO("The pool did not hold three elements");
            REQUIRE(entities.Count(velocity->type) == 3);
        }

        // Destroying an entity removes its element, and the others keep their values.
        entities.DestroyEntity(second.GetId());
        {
            INFO("Destroying an entity left its component in the pool");
            REQUIRE(entities.Count(velocity->type) == 2);
        }
        {
            INFO("A destroyed entity still had the component");
            REQUIRE(!entities.Has(velocity->type, second.GetId()));
        }
        {
            INFO("Removing an element disturbed the others");
            REQUIRE((Scripted<float>(scripting, "SpeedX", first.GetId()) == 2.0F &&
                     Scripted<float>(scripting, "SpeedX", third.GetId()) == 1.0F));
        }

        // The slot is reused by a new entity, which does not inherit the old component.
        const auto reused = entities.CreateEntity().GetId();
        {
            INFO("A new entity inherited a destroyed entity's component");
            REQUIRE(!entities.Has(velocity->type, reused));
        }
        {
            INFO("C# saw a component on an entity that has none");
            REQUIRE(!Scripted<bool>(scripting, "HasVelocity", reused));
        }
        {
            INFO("C# did not see a component that is there");
            REQUIRE(Scripted<bool>(scripting, "HasVelocity", first.GetId()));
        }

        // Removing one explicitly, and again (harmless).
        {
            INFO("A component was not removed");
            REQUIRE((entities.Remove(velocity->type, first.GetId()) && !entities.Has(velocity->type, first.GetId())));
        }
        {
            INFO("Removing twice failed");
            REQUIRE(entities.Remove(velocity->type, first.GetId()));
        }
        {
            INFO("The pool did not shrink");
            REQUIRE(entities.Count(velocity->type) == 1);
        }
    }

    // What C# sees of a script component the ECS aligns for it (the storage itself is FenrirECS's, tested there).
    void CSharpUsesAnAlignedComponentAndAnEmptyOneAsATag()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        const auto* aligned = scripting.FindComponent(AlignedName);
        const auto* tagged = scripting.FindComponent(TaggedName);
        {
            INFO("Aligned or Tagged was not registered");
            REQUIRE((aligned != nullptr && tagged != nullptr));
        }
        const auto holder = entities.CreateEntity().GetId();
        const auto other = entities.CreateEntity().GetId();
        {
            INFO("Aligned was not added");
            REQUIRE(entities.Add(aligned->type, holder));
        }

        // C# reads and writes a Vector128 in it.
        Scripted<void>(scripting, "FillAligned", holder);
        const auto* lanes = static_cast<const float*>(entities.Get(aligned->type, holder));
        {
            INFO("C# writes to an aligned component were not in the storage");
            REQUIRE((lanes[0] == 1 && lanes[3] == 4 &&
                     static_cast<const std::uint8_t*>(entities.Get(aligned->type, holder))[16] == 7));
        }
        {
            INFO("C# did not read a lane back");
            REQUIRE(Scripted<float>(scripting, "AlignedLane", holder, 2) == 3.0F);
        }

        // A tag with no data is still a component.
        {
            INFO("An empty component did not work as a tag");
            REQUIRE((entities.Add(tagged->type, holder) && Scripted<bool>(scripting, "HasTagged", holder) &&
                     !Scripted<bool>(scripting, "HasTagged", other)));
        }
    }

    float VelocityX(Scripting& scripting, EntityList& entities, std::uint32_t id)
    {
        const auto* values = static_cast<const float*>(entities.Get(scripting.FindComponent(VelocityName)->type, id));
        {
            INFO("The entity had no Velocity");
            REQUIRE(values != nullptr);
        }
        return values[0];
    }

    void ACreatedEntityIsUsableAtOnceAndExistsAfterwards()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        {
            INFO("The fixture did not load");
            REQUIRE(scripting.HasScripts());
        }
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        const auto* velocity = scripting.FindComponent(VelocityName);

        const auto id = Changes<std::uint32_t>(scripting, "CreateWithVelocity");
        // Alive and usable (the script added to it), but nothing is in the storage until playback.
        {
            INFO("The new entity was not alive at once");
            REQUIRE((id != NullEntity && entities.HasEntity(id)));
        }
        {
            INFO("The script's changes were not recorded");
            REQUIRE(ScriptingProbe::PendingChanges(scripting) == 3);
        }
        {
            INFO("A change reached the storage before playback");
            REQUIRE((!entities.Has(velocity->type, id) && !entities.GetEntity(id).HasComponent<Transform>()));
        }

        const auto summary = ScriptingProbe::ApplyChanges(scripting);
        {
            INFO("Playback did not apply all");
            REQUIRE((summary.applied == 3 && summary.skipped == 0 && ScriptingProbe::PendingChanges(scripting) == 0));
        }
        auto entity = entities.GetEntity(id);
        {
            INFO("A created entity did not get the engine's components");
            REQUIRE((entity.HasComponent<Transform>() && entity.HasComponent<Name>() &&
                     entity.HasComponent<Relationship>()));
        }
        {
            INFO("A created entity did not get a name");
            REQUIRE(std::string(entity.GetComponent<Name>().Get()).starts_with("Entity "));
        }
        const auto* values = static_cast<const float*>(entities.Get(velocity->type, id));
        {
            INFO("The added value was not applied");
            REQUIRE((values != nullptr && values[0] == 1 && values[1] == 2 && values[2] == 3));
        }
        {
            INFO("A default component was not added");
            REQUIRE(entities.Has(scripting.FindComponent("Fenrir.ScriptFixture.Counter")->type, id));
        }

        // Nothing left over to play back.
        {
            INFO("Playing back twice applied something twice");
            REQUIRE(ScriptingProbe::ApplyChanges(scripting).applied == 0);
        }
    }

    void ARefStaysValidWhileTheSameComponentIsAddedAndRemovedElsewhere()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        const auto victim =
            EntityWithVelocity(scripting, entities); // First in the pool: its removal moves the last one.
        const auto middle = EntityWithVelocity(scripting, entities);
        const auto kept = EntityWithVelocity(scripting, entities);
        (void)middle;

        // The script holds a Ref to `kept` while the victim loses its component and thousands of entities gain one.
        const auto sum = Changes<float>(scripting, "RefSurvivesChanges", kept, victim);
        {
            INFO("The Ref did not keep reading and writing the same memory");
            REQUIRE(sum == 22.0F);
        }
        {
            INFO("A removal reached the storage before playback");
            REQUIRE(entities.Has(scripting.FindComponent(VelocityName)->type, victim));
        }

        const auto summary = ScriptingProbe::ApplyChanges(scripting);
        {
            INFO("Playback did not apply every change");
            REQUIRE((summary.skipped == 0 && summary.applied > 5000));
        }
        {
            INFO("The removal was not applied");
            REQUIRE(!entities.Has(scripting.FindComponent(VelocityName)->type, victim));
        }
        {
            INFO("The write through the Ref did not stay with the kept entity when the pool changed");
            REQUIRE(VelocityX(scripting, entities, kept) == 9.0F);
        }
        // 3 to begin with, minus the victim, plus 5000 new entities and the one made before them.
        {
            INFO("The pool did not end up with the right entities");
            REQUIRE(entities.Count(scripting.FindComponent(VelocityName)->type) == 5003);
        }
    }

    void ReadsSeeLiveStateAndChangesWaitForPlayback()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        const auto bare = entities.CreateEntity().GetId();
        const auto holder = EntityWithVelocity(scripting, entities);
        const auto doomed = entities.CreateEntity().GetId();

        {
            INFO("Has saw a component that was only recorded");
            REQUIRE(!Changes<bool>(scripting, "AddThenHas", bare));
        }
        {
            INFO("Has missed a component that was only recorded as removed");
            REQUIRE(Changes<bool>(scripting, "RemoveThenHas", holder));
        }
        {
            INFO("A destroyed entity was gone before playback");
            REQUIRE(Changes<bool>(scripting, "DestroyThenIsValid", doomed));
        }
        {
            INFO("A destroy reached the ECS before playback");
            REQUIRE(entities.HasEntity(doomed));
        }

        (void)ScriptingProbe::ApplyChanges(scripting);
        {
            INFO("The add was not applied");
            REQUIRE(entities.Has(scripting.FindComponent(VelocityName)->type, bare));
        }
        {
            INFO("The removal was not applied");
            REQUIRE(!entities.Has(scripting.FindComponent(VelocityName)->type, holder));
        }
        {
            INFO("The destroy was not applied");
            REQUIRE(!entities.HasEntity(doomed));
        }
    }

    void ChangesFromCSharpApplyInOrderAndCarryTheirValues()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        const auto entity = entities.CreateEntity().GetId();

        Changes<void>(scripting, "ChangesApplyInOrder", entity);
        {
            INFO("Not every change was applied");
            REQUIRE(ScriptingProbe::ApplyChanges(scripting).applied == 4);
        }
        {
            INFO("The last add did not win: C# calls did not reach the buffer in the order they were made");
            REQUIRE(VelocityX(scripting, entities, entity) == 3.0F);
        }

        // A native component takes a value too.
        Changes<void>(scripting, "SetTransform", entity);
        (void)ScriptingProbe::ApplyChanges(scripting);
        const auto& transform = entities.GetEntity(entity).GetComponent<Transform>();
        {
            INFO("A native component did not take the value");
            REQUIRE((transform.pos == glm::vec3(9, 8, 7) && transform.scale == glm::vec3(2, 2, 2) &&
                     transform.rot.w == 1.0F));
        }
    }

    void GetAndStructuralChangesFailClearly()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        const auto bare = entities.CreateEntity().GetId();
        const auto stale = entities.CreateEntity().GetId();
        entities.DestroyEntity(stale);
        const auto message = [&](const char* method, std::uint32_t entity) {
            return Changes<std::optional<std::string>>(scripting, method, entity).value_or("");
        };

        const auto missing = message("TryGetMissing", bare);
        {
            INFO("Get on a missing component said: " + missing);
            REQUIRE(missing.find("has no Velocity") != std::string::npos);
        }
        const auto staleGet = message("TryGetMissing", stale);
        {
            INFO("Get on a stale entity said: " + staleGet);
            REQUIRE(staleGet.find("not alive") != std::string::npos);
        }
        {
            INFO("Add on a stale entity did not say so");
            REQUIRE(message("TryAddOnStale", stale).find("not alive") != std::string::npos);
        }
        {
            INFO("Destroy of a stale entity did not say so");
            REQUIRE(message("TryDestroyStale", stale).find("not alive") != std::string::npos);
        }
        {
            INFO("Removing Transform was not refused clearly");
            REQUIRE(message("TryRemoveRequired", bare).find("needs every entity") != std::string::npos);
        }
        {
            INFO("Removing Name was not refused");
            REQUIRE(message("TryRemoveName", bare).find("needs every entity") != std::string::npos);
        }
        {
            INFO("A refused change was recorded");
            REQUIRE(ScriptingProbe::PendingChanges(scripting) == 0);
        }
        {
            INFO("A refused removal changed the entity");
            REQUIRE(entities.GetEntity(bare).HasComponent<Transform>());
        }
    }

} // namespace

TEST_CASE("a script assembly reports the components it defines, with size and alignment", "[scripting]")
{
    AScriptAssemblyReportsTheComponentsItDefines();
}

TEST_CASE("a script component is shared with C# and removed with its entity", "[scripting]")
{
    AScriptComponentIsSharedWithCSharpAndRemovedWithItsEntity();
}

TEST_CASE("C# uses an aligned component, and an empty one as a tag", "[scripting]")
{
    CSharpUsesAnAlignedComponentAndAnEmptyOneAsATag();
}

TEST_CASE("a created entity is usable at once and exists afterwards", "[scripting]")
{
    ACreatedEntityIsUsableAtOnceAndExistsAfterwards();
}

TEST_CASE("a Ref stays valid while the same component is added and removed elsewhere", "[scripting]")
{
    ARefStaysValidWhileTheSameComponentIsAddedAndRemovedElsewhere();
}

TEST_CASE("reads see live state and changes wait for playback", "[scripting]")
{
    ReadsSeeLiveStateAndChangesWaitForPlayback();
}

TEST_CASE("changes recorded from C# are applied in order and carry their values", "[scripting]")
{
    ChangesFromCSharpApplyInOrderAndCarryTheirValues();
}

TEST_CASE("get and structural changes fail clearly", "[scripting]")
{
    GetAndStructuralChangesFailClearly();
}

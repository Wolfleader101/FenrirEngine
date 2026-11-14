// The ragdoll natives' rules, with no .NET and no fixture assembly: through FenrirScripting's NativeCallHarness, which
// runs the ragdoll extension as shipped (Ragdolls::ScriptingExtension()), each native is called from C++ by its
// registered name through the registration's own thunk. The rules: on an entity that is not alive or not a Ready
// skeleton entity (ADR-0039) the natives describe and build nothing; a refused bone map describes nothing; a stale or
// absent handle builds nothing, and a buffer too small or a skeleton that already has a ragdoll keeps the handle; each
// warns once per native and message, under the native's name, in the words RagdollScripting names. A described handle
// is the active scene's, held by that scene, so it goes when the scene does; a built one is dropped. What a script sees
// of ragdolls stays in RagdollScriptingTests.cpp, on .NET.
#include <catch2/catch_test_macros.hpp>

#include "NativeCallHarness.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "FenrirAssets/Assets.hpp"
#include "FenrirAssets/SkeletonData.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirPhysics/Components.hpp"
#include "FenrirPhysics/Joints.hpp"
#include "FenrirRagdoll/Components.hpp"
#include "FenrirRagdoll/Description.hpp"
#include "FenrirRagdoll/Ragdolls.hpp"
#include "FenrirRagdollScripting/RagdollDescriptionHandle.hpp"
#include "FenrirRagdollScripting/RagdollScripting.hpp"
#include "FenrirScripting/NativeGuard.hpp"
#include "FenrirSkeleton/SkeletonPose.hpp"
#include "FenrirSkeleton/Skeletons.hpp"
#include "RagdollScriptingState.hpp"
#include "RagdollTest.hpp"

namespace
{
    using namespace Fenrir;

    constexpr std::int32_t Parts = 16;

    EntityList& Entities(Testing::NativeCallHarness& harness)
    {
        return harness.GetApp().GetActiveScene().GetEntityList();
    }

    // The harness with assets and skeletons installed and a Ready, tick-posed test humanoid in the active scene.
    struct Rig
    {
        Testing::NativeCallHarness harness{{Ragdolls::ScriptingExtension()}};
        Assets assets = Assets::Install(harness.GetApp());
        Skeletons skeletons = Skeletons::Install(harness.GetApp());
        Entity skeleton = Spawn();
        HumanoidBones bones = RagdollTest::HumanoidMap();

        Entity Spawn()
        {
            Scene& scene = harness.GetApp().GetActiveScene();
            const AssetHandle<Skeleton> handle = assets.Store().Add<Skeleton>(RagdollTest::Humanoid(), scene);
            Entity entity = Entities(harness).CreateEntity();
            entity.AddComponent<SkeletonPose>(SkeletonPose{handle, PoseTiming::Tick});
            REQUIRE(skeletons.Ensure(scene, entity) == PoseState::Ready);
            return entity;
        }

        RagdollDescriptionHandle Describe(const Entity& entity)
        {
            return harness.Call<RagdollDescriptionHandle>("RagdollNative.DescribeHumanoid", &entity,
                                                          static_cast<const HumanoidBones*>(&bones), 70.0f);
        }

        std::int32_t PartCount(RagdollDescriptionHandle handle)
        {
            return harness.Call<std::int32_t>("RagdollNative.PartCount", handle);
        }

        // Build into buffers of `bodies` and `joints` entities.
        bool Build(const Entity& entity, RagdollDescriptionHandle handle, std::int32_t bodies = Parts,
                   std::int32_t joints = Parts - 1)
        {
            parent = Entity{};
            return harness.Call<bool>("RagdollNative.Build", &entity, handle, bodyOut.data(), bodies, jointOut.data(),
                                      joints, &parent);
        }

        std::array<Entity, Parts> bodyOut{};
        std::array<Entity, Parts> jointOut{};
        Entity parent;
    };
} // namespace

TEST_CASE("DescribeHumanoid and Build give a ragdoll onto the entities Build hands out, when the changes play back",
          "[ragdoll][scripting][rules]")
{
    Rig rig;
    const RagdollDescriptionHandle handle = rig.Describe(rig.skeleton);
    INFO("the humanoid should be described, with sixteen parts");
    REQUIRE(handle.id != 0);
    REQUIRE(rig.PartCount(handle) == Parts);

    INFO("Build should be recorded");
    REQUIRE(rig.Build(rig.skeleton, handle));
    INFO("the entities should be alive at once, with their components still to come");
    REQUIRE(rig.parent.IsValid());
    for (const Entity& body : rig.bodyOut)
    {
        REQUIRE(body.IsValid());
        REQUIRE_FALSE(body.HasComponent<RagdollBody>());
    }
    REQUIRE_FALSE(rig.skeleton.HasComponent<Ragdoll>());
    INFO("the handle should be dropped at once");
    REQUIRE(rig.PartCount(handle) == 0);

    rig.harness.ApplyChanges();
    INFO("after playback the ragdoll should be built onto exactly those entities");
    REQUIRE(rig.skeleton.HasComponent<Ragdoll>());
    REQUIRE(rig.skeleton.GetComponent<Ragdoll>().rootBody == rig.bodyOut[0]);
    REQUIRE(RagdollTest::Text(rig.bodyOut[0].GetComponent<RagdollBody>().bone) == "pelvis");
    for (std::size_t i = 0; i + 1 < rig.bodyOut.size(); ++i)
    {
        INFO("joint " << i << " should join body " << i + 1);
        REQUIRE(rig.jointOut[i].GetComponent<Joint>().bodyB == rig.bodyOut[i + 1]);
    }
    REQUIRE(rig.bodyOut[5].GetComponent<Relationship>().parent == rig.parent);
    INFO("nothing should be logged");
    REQUIRE(rig.harness.Logger().Messages().empty());
}

TEST_CASE("On an entity that is not a skeleton entity the natives describe and build nothing, warning once",
          "[ragdoll][scripting][rules]")
{
    Rig rig;
    const Entity loose = Entities(rig.harness).CreateEntity();
    for (int call = 0; call < 2; ++call)
    {
        INFO("call " << call << " should describe nothing");
        REQUIRE(rig.Describe(loose).id == 0);
    }
    INFO("DescribeHumanoid should warn once that it is not a Ready skeleton entity");
    REQUIRE(rig.harness.WarnedOnce("RagdollNative.DescribeHumanoid", RagdollScripting::NotReady));

    const RagdollDescriptionHandle handle = rig.Describe(rig.skeleton);
    for (int call = 0; call < 2; ++call)
    {
        INFO("call " << call << " should build nothing");
        REQUIRE_FALSE(rig.Build(loose, handle));
    }
    INFO("Build should warn once, and keep the handle, which builds on the Ready skeleton");
    REQUIRE(rig.harness.WarnedOnce("RagdollNative.Build", RagdollScripting::NotReady));
    REQUIRE(rig.PartCount(handle) == Parts);
    REQUIRE(rig.Build(rig.skeleton, handle));
}

TEST_CASE("On a skeleton entity whose pose is not Ready the natives describe and build nothing, warning once",
          "[ragdoll][scripting][rules]")
{
    Rig rig;
    // A SkeletonPose naming no skeleton asset never becomes Ready (FenrirSkeleton warns about it itself).
    Entity unready = Entities(rig.harness).CreateEntity();
    unready.AddComponent<SkeletonPose>(SkeletonPose{AssetHandle<Skeleton>{}, PoseTiming::Tick});
    (void)rig.skeletons.Ensure(rig.harness.GetApp().GetActiveScene(), unready);
    INFO("the skeleton entity should not be Ready");
    REQUIRE(Skeletons::GetPoseState(rig.harness.GetApp().GetActiveScene(), unready) != PoseState::Ready);

    const RagdollDescriptionHandle handle = rig.Describe(rig.skeleton);
    for (int call = 0; call < 2; ++call)
    {
        INFO("call " << call << " should describe and build nothing");
        REQUIRE(rig.Describe(unready).id == 0);
        REQUIRE_FALSE(rig.Build(unready, handle));
    }
    INFO("each native should warn once, and nothing be built");
    REQUIRE(rig.harness.WarnedOnce("RagdollNative.DescribeHumanoid", RagdollScripting::NotReady));
    REQUIRE(rig.harness.WarnedOnce("RagdollNative.Build", RagdollScripting::NotReady));
    REQUIRE_FALSE(rig.parent.IsValid());
    rig.harness.ApplyChanges();
    REQUIRE_FALSE(unready.HasComponent<Ragdoll>());
}

TEST_CASE("On an entity that is not alive the natives do nothing, warning once in the guard's words",
          "[ragdoll][scripting][rules]")
{
    Rig rig;
    Entity gone = Entities(rig.harness).CreateEntity();
    const Entity goneHandle = gone;
    Entities(rig.harness).DestroyEntity(gone);
    INFO("DescribeHumanoid and Destroy should do nothing and warn that the entity is not alive");
    REQUIRE(rig.Describe(goneHandle).id == 0);
    REQUIRE(rig.harness.WarnedOnce("RagdollNative.DescribeHumanoid", NativeGuard::NotAlive));
    REQUIRE_FALSE(rig.harness.Call<bool>("RagdollNative.Destroy", &goneHandle));
    REQUIRE(rig.harness.WarnedOnce("RagdollNative.Destroy", NativeGuard::NotAlive));
}

TEST_CASE("A refused bone map describes nothing, warning once with FenrirRagdoll's reason",
          "[ragdoll][scripting][rules]")
{
    Rig rig;
    rig.bones.hips = RagdollTest::NameOf("no_such_bone");
    for (int call = 0; call < 2; ++call)
    {
        INFO("call " << call << " should describe nothing");
        REQUIRE(rig.Describe(rig.skeleton).id == 0);
    }
    const SkeletonData& data = *rig.assets.Store().TryGet(rig.skeleton.GetComponent<SkeletonPose>().skeleton);
    const std::string error = DescribeHumanoid(data, rig.bones).error;
    INFO("it should warn once, giving FenrirRagdoll's reason");
    REQUIRE_FALSE(error.empty());
    REQUIRE(rig.harness.WarnedOnce("RagdollNative.DescribeHumanoid", RagdollScripting::Refused(error)));
}

TEST_CASE("Build of no description does nothing silently", "[ragdoll][scripting][rules]")
{
    Rig rig;
    INFO("no description builds nothing, counts no parts and logs nothing");
    REQUIRE_FALSE(rig.Build(rig.skeleton, RagdollDescriptionHandle{}));
    REQUIRE(rig.PartCount(RagdollDescriptionHandle{}) == 0);
    REQUIRE(rig.harness.Logger().Messages().empty());
}

TEST_CASE("Build of a handle never given, or one already built, builds nothing, warning once",
          "[ragdoll][scripting][rules]")
{
    Rig rig;
    for (int call = 0; call < 2; ++call)
    {
        INFO("call " << call << " with a made-up handle should build nothing");
        REQUIRE_FALSE(rig.Build(rig.skeleton, RagdollDescriptionHandle{42}));
    }
    INFO("it should warn once that the handle is stale, handing out no entities");
    REQUIRE(rig.harness.WarnedOnce("RagdollNative.Build", RagdollScripting::StaleDescription));
    REQUIRE_FALSE(rig.parent.IsValid());

    const RagdollDescriptionHandle handle = rig.Describe(rig.skeleton);
    REQUIRE(rig.Build(rig.skeleton, handle));
    rig.harness.ApplyChanges();
    const Entity second = rig.Spawn();
    INFO("a built handle should be stale too, with no second warning from the same place");
    REQUIRE_FALSE(rig.Build(second, handle));
    REQUIRE(rig.harness.WarnedOnce("RagdollNative.Build", RagdollScripting::StaleDescription));
}

TEST_CASE("Build into buffers too small builds nothing, warning once, and keeps the handle",
          "[ragdoll][scripting][rules]")
{
    Rig rig;
    const RagdollDescriptionHandle handle = rig.Describe(rig.skeleton);
    INFO("a body short or two joints short, Build should refuse and hand out no entities");
    REQUIRE_FALSE(rig.Build(rig.skeleton, handle, Parts - 1, Parts - 1));
    REQUIRE_FALSE(rig.Build(rig.skeleton, handle, Parts, Parts - 2));
    REQUIRE(rig.harness.WarnedOnce("RagdollNative.Build", RagdollScripting::BuffersTooSmall));
    REQUIRE_FALSE(rig.parent.IsValid());
    INFO("the handle should be kept");
    REQUIRE(rig.PartCount(handle) == Parts);
}

TEST_CASE("Build onto a skeleton that already has a ragdoll builds nothing, warning once, and keeps the handle",
          "[ragdoll][scripting][rules]")
{
    Rig rig;
    REQUIRE(rig.Build(rig.skeleton, rig.Describe(rig.skeleton)));
    rig.harness.ApplyChanges();
    const RagdollDescriptionHandle again = rig.Describe(rig.skeleton);
    INFO("one ragdoll per skeleton: the second Build should refuse and warn once");
    REQUIRE_FALSE(rig.Build(rig.skeleton, again));
    REQUIRE(rig.harness.WarnedOnce("RagdollNative.Build", RagdollScripting::AlreadyBuilt));
    INFO("the handle should be kept");
    REQUIRE(rig.PartCount(again) == Parts);
}

// The one case that reads the extension's state (its header is this executable's to see, as the module gives the rules
// tests): "dropped without a leak" is about memory, which nothing public observes.
TEST_CASE("A described handle is held by its scene and goes with it; another scene builds nothing from it",
          "[ragdoll][scripting][rules]")
{
    Rig rig;
    App& app = rig.harness.GetApp();
    Scene& first = app.GetActiveScene();
    const RagdollDescriptionHandle handle = rig.Describe(rig.skeleton);
    rig.harness.ApplyChanges();
    const Detail::RagdollDescriptions* held = first.GetResource<Detail::RagdollDescriptions>();
    INFO("once the changes play back, the scene should hold the description");
    REQUIRE(held != nullptr);
    REQUIRE(held->shelf->descriptions.size() == 1);
    const std::weak_ptr<Detail::RagdollDescriptionShelf> shelf = held->shelf;

    (void)app.CreateScene("Second");
    app.ChangeActiveScene("Second");
    const Entity elsewhere = rig.Spawn();
    INFO("in another scene the handle should be stale");
    REQUIRE(rig.PartCount(handle) == 0);
    REQUIRE_FALSE(rig.Build(elsewhere, handle));
    REQUIRE(rig.harness.WarnedOnce("RagdollNative.Build", RagdollScripting::StaleDescription));

    const std::string name = first.GetName();
    app.DestroyScene(name);
    INFO("destroying the first scene should drop the never-built description: nothing else holds it");
    REQUIRE(shelf.expired());
}

TEST_CASE("Destroy and ClearOverrides of an entity with no ragdoll do nothing silently", "[ragdoll][scripting][rules]")
{
    Rig rig;
    const Entity loose = Entities(rig.harness).CreateEntity();
    INFO("both should return false and log nothing, as C++'s own calls do nothing silently");
    REQUIRE_FALSE(rig.harness.Call<bool>("RagdollNative.Destroy", &loose));
    REQUIRE_FALSE(rig.harness.Call<bool>("RagdollNative.ClearOverrides", &loose));
    REQUIRE(rig.harness.Logger().Messages().empty());
}

TEST_CASE("ClearOverrides clears a ragdoll's entries at once", "[ragdoll][scripting][rules]")
{
    Rig rig;
    REQUIRE(rig.Build(rig.skeleton, rig.Describe(rig.skeleton)));
    rig.harness.ApplyChanges();
    Scene& scene = rig.harness.GetApp().GetActiveScene();
    const std::int32_t hips = Skeletons::FindBone(scene, rig.skeleton, "pelvis");
    REQUIRE(Skeletons::SetOverride(scene, rig.skeleton, hips, Target{}));
    INFO("ClearOverrides should say there was a ragdoll and clear the hips' entry");
    REQUIRE(rig.harness.Call<bool>("RagdollNative.ClearOverrides", static_cast<const Entity*>(&rig.skeleton)));
    REQUIRE_FALSE(Skeletons::GetOverride(scene, rig.skeleton, hips).has_value());
}

TEST_CASE("Destroy removes a ragdoll when the changes play back", "[ragdoll][scripting][rules]")
{
    Rig rig;
    REQUIRE(rig.Build(rig.skeleton, rig.Describe(rig.skeleton)));
    rig.harness.ApplyChanges();
    INFO("Destroy should be recorded, and the ragdoll stay until playback");
    REQUIRE(rig.harness.Call<bool>("RagdollNative.Destroy", static_cast<const Entity*>(&rig.skeleton)));
    REQUIRE(rig.skeleton.HasComponent<Ragdoll>());
    rig.harness.ApplyChanges();
    INFO("after playback the Ragdoll, its bodies and their parent should be gone");
    REQUIRE_FALSE(rig.skeleton.HasComponent<Ragdoll>());
    REQUIRE_FALSE(rig.bodyOut[0].IsValid());
    REQUIRE_FALSE(rig.parent.IsValid());
}

// Ragdolls from C# (the ragdolls spec, seam 3), through the fixture's Ragdolls class: a script fills HumanoidBones,
// describes and builds a ragdoll on the code-built humanoid, gets its entities, switches it to simulating and reads the
// glue's states through ref readonly; writes a body's own mode; ClearOverrides and Destroy; and the natives' no-op
// warnings as a script meets them. The mirrors' layouts are checked when the fixture loads, and each `new` against
// C++'s `{}` by the generated defaults test (Generated/FenrirRagdollDefaultsTests.cpp). That a description never built
// goes with its scene is the rules tests' (Rules.cpp), which see the extension's state.

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirPhysics/Components.hpp"
#include "FenrirPhysics/Joints.hpp"
#include "FenrirRagdoll/Components.hpp"
#include "FenrirSkeleton/Skeletons.hpp"
#include "NativeCallHarness.hpp"
#include "RagdollScriptingApp.hpp"
#include "RagdollScriptingState.hpp"

using namespace Fenrir;
using namespace Fenrir::Testing;

namespace
{
    constexpr const char* RagdollsType = "Fenrir.RagdollScriptFixture.Ragdolls";
    constexpr int Parts = 16;
    // How many times a place warns: once in a debug build, never in a release one (WarnOnceRecord).
    constexpr std::size_t once = NativeCallHarness::once;

    Entity EntityOf(RagdollScriptingApp& test, std::uint32_t id)
    {
        INFO("entity " << id << " should be alive");
        REQUIRE(test.Entities().HasEntity(id));
        return test.Entities().GetEntity(id);
    }

    // The humanoid, described and built from C# in one call, with the changes played back.
    struct Built
    {
        RagdollScriptingApp test;
        Entity skeleton = test.SpawnHumanoid();
        int bodies = test.Call<int>(RagdollsType, "DescribeAndBuild", skeleton.GetId());

        Built()
        {
            INFO("C# should build all sixteen bodies");
            REQUIRE(bodies == Parts);
            test.ApplyChanges();
        }

        std::uint32_t Body(int index)
        {
            return test.Call<std::uint32_t>(RagdollsType, "Body", index);
        }
    };
} // namespace

TEST_CASE("The entities a C# Build gives are the ragdoll C++ builds when the changes play back", "[ragdoll][scripting]")
{
    Built built;
    RagdollScriptingApp& test = built.test;

    const Entity root = EntityOf(test, built.Body(0));
    INFO("the skeleton entity should have a Ragdoll whose root body is C#'s first body");
    REQUIRE(built.skeleton.HasComponent<Ragdoll>());
    REQUIRE(built.skeleton.GetComponent<Ragdoll>().rootBody == root);
    INFO("the first body's bone, read through the skeleton contract's BoneName, should be the hips'");
    REQUIRE(test.Call<bool>(RagdollsType, "BoneIsPelvis", root.GetId()));
    INFO("C# should get one joint per part but the root, each joining the next body");
    REQUIRE(test.Call<int>(RagdollsType, "JointCount") == Parts - 1);
    for (int i = 0; i + 1 < Parts; ++i)
    {
        const Entity joint = EntityOf(test, test.Call<std::uint32_t>(RagdollsType, "Joint", i));
        INFO("joint " << i);
        REQUIRE(joint.GetComponent<Joint>().bodyB.GetId() == built.Body(i + 1));
    }
    INFO("the bodies should sit under the parent C# got");
    REQUIRE(root.GetComponent<Relationship>().parent.GetId() == test.Call<std::uint32_t>(RagdollsType, "Parent"));
    INFO("nothing should warn");
    REQUIRE(LoggedWith(test.headless, LogLevel::Warn, "ragdoll") == 0);
}

TEST_CASE("A ragdoll C# sets simulating falls, and C# reads its state and its bodies' through ref readonly",
          "[ragdoll][scripting]")
{
    Built built;
    RagdollScriptingApp& test = built.test;
    const std::uint32_t root = built.Body(0);

    test.Run(2);
    INFO("following, the glue should drive every body and say so");
    REQUIRE(test.Call<int>(RagdollsType, "BodiesDriven", built.skeleton.GetId()) == Parts);
    REQUIRE(test.Call<int>(RagdollsType, "BodyMode", root) == static_cast<int>(RagdollMode::Following));

    test.Call<void>(RagdollsType, "Simulate", built.skeleton.GetId());
    INFO("C#'s write of Mode through Ref<Ragdoll> should reach the component");
    REQUIRE(built.skeleton.GetComponent<Ragdoll>().mode == RagdollMode::Simulating);
    test.Run(3);
    INFO("a few ticks on, the bodies should be awake, simulating and dynamic");
    REQUIRE(test.Call<bool>(RagdollsType, "AnyAwake", built.skeleton.GetId()));
    REQUIRE(test.Call<int>(RagdollsType, "BodyMode", root) == static_cast<int>(RagdollMode::Simulating));
    REQUIRE(EntityOf(test, root).GetComponent<RigidBody>().motionType == MotionType::Dynamic);
}

TEST_CASE("C# writes a body's own mode and strength scale, as a partial ragdoll does", "[ragdoll][scripting]")
{
    Built built;
    const std::uint32_t hand = built.Body(6);
    built.test.Call<void>(RagdollsType, "LimpBody", hand);
    const RagdollBody& body = EntityOf(built.test, hand).GetComponent<RagdollBody>();
    INFO("the body's own mode and strength scale should be C#'s");
    REQUIRE(body.mode == RagdollBodyMode::Simulating);
    REQUIRE(body.strengthScale == 0.5f);
}

TEST_CASE("C#'s ClearOverrides clears the glue's override entries at once", "[ragdoll][scripting]")
{
    Built built;
    RagdollScriptingApp& test = built.test;
    test.Call<void>(RagdollsType, "Simulate", built.skeleton.GetId());
    test.Run(2);
    Scene& scene = test.Get().GetActiveScene();
    const std::int32_t hips = Skeletons::FindBone(scene, built.skeleton, "pelvis");
    INFO("while simulating, the glue should have written the hips' entry");
    REQUIRE(Skeletons::GetOverride(scene, built.skeleton, hips).has_value());

    INFO("ClearOverrides should say there was a ragdoll and clear the entry");
    REQUIRE(test.Call<bool>(RagdollsType, "ClearOverrides", built.skeleton.GetId()));
    REQUIRE_FALSE(Skeletons::GetOverride(scene, built.skeleton, hips).has_value());
}

TEST_CASE("C#'s Destroy removes the ragdoll when the changes play back", "[ragdoll][scripting]")
{
    Built built;
    RagdollScriptingApp& test = built.test;
    const std::uint32_t root = built.Body(0);
    const std::uint32_t parent = test.Call<std::uint32_t>(RagdollsType, "Parent");

    INFO("Destroy should be recorded");
    REQUIRE(test.Call<bool>(RagdollsType, "Destroy", built.skeleton.GetId()));
    test.ApplyChanges();
    INFO("the Ragdoll, its bodies and their parent should be gone");
    REQUIRE_FALSE(built.skeleton.HasComponent<Ragdoll>());
    REQUIRE_FALSE(test.Entities().HasEntity(root));
    REQUIRE_FALSE(test.Entities().HasEntity(parent));
}

TEST_CASE("From C#, an entity that is not a skeleton gives no description, warning once", "[ragdoll][scripting]")
{
    RagdollScriptingApp test;
    const Entity loose = test.Entities().CreateEntity();
    for (int call = 0; call < 2; ++call)
    {
        INFO("call " << call << " should give no description");
        REQUIRE(test.Call<int>(RagdollsType, "Describe", loose.GetId()) == -1);
    }
    INFO("it should warn that the entity is not a Ready skeleton entity, once");
    REQUIRE(LoggedWith(test.headless, LogLevel::Warn, std::string(RagdollScripting::NotReady)) == once);
}

TEST_CASE("From C#, a bone map naming a bone the rig lacks gives no description, warning once, and builds nothing",
          "[ragdoll][scripting]")
{
    RagdollScriptingApp test;
    const Entity skeleton = test.SpawnHumanoid();
    for (int call = 0; call < 2; ++call)
    {
        INFO("call " << call << ": no description, and Build of none builds nothing");
        REQUIRE(test.Call<int>(RagdollsType, "DescribeUnknownBone", skeleton.GetId()) == -1);
        REQUIRE_FALSE(test.Call<bool>(RagdollsType, "BuildNone", skeleton.GetId()));
    }
    INFO("it should warn once that the bone map was refused");
    REQUIRE(LoggedWith(test.headless, LogLevel::Warn, "the bone map was refused") == once);
    test.ApplyChanges();
    INFO("no ragdoll should be built");
    REQUIRE_FALSE(skeleton.HasComponent<Ragdoll>());
}

TEST_CASE("From C#, buffers too small build nothing and keep the handle, which then builds", "[ragdoll][scripting]")
{
    RagdollScriptingApp test;
    const Entity skeleton = test.SpawnHumanoid();
    const int handle = test.Call<int>(RagdollsType, "Describe", skeleton.GetId());
    INFO("the humanoid should describe sixteen parts");
    REQUIRE(handle >= 0);
    REQUIRE(test.Call<int>(RagdollsType, "PartCount", handle) == Parts);

    INFO("a body short, Build should refuse, warn once and keep the description");
    REQUIRE_FALSE(test.Call<bool>(RagdollsType, "BuildShort", skeleton.GetId(), handle));
    REQUIRE(LoggedWith(test.headless, LogLevel::Warn, std::string(RagdollScripting::BuffersTooSmall)) == once);
    REQUIRE(test.Call<int>(RagdollsType, "PartCount", handle) == Parts);

    INFO("sized by PartCount, Build should take the handle and build");
    REQUIRE(test.Call<int>(RagdollsType, "BuildFrom", skeleton.GetId(), handle) == Parts);
    REQUIRE(test.Call<int>(RagdollsType, "PartCount", handle) == 0);
    test.ApplyChanges();
    REQUIRE(skeleton.HasComponent<Ragdoll>());
}

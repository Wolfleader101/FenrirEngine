// The physics natives' rules, with no .NET and no fixture assembly: through FenrirScripting's NativeCallHarness, which
// runs the physics extension as shipped (Physics::ScriptingExtension()), each native is called from C++ by its
// registered name through the registration's own thunk, and what it returns, writes and warns is read back from the
// harness. The rules: with no physics world a native does nothing and returns false or zero; on an entity with no body
// a force does nothing; GetJointState on an entity without a Joint reads zeroed; and each warns once per native and
// message, under the native's name, in the words PhysicsScripting names. What a script sees of physics stays in
// PhysicsScriptingTests.cpp, on .NET.
#include <catch2/catch_test_macros.hpp>

#include "NativeCallHarness.hpp"

#include <cstdint>

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirPhysics/Components.hpp"
#include "FenrirPhysics/Joints.hpp"
#include "FenrirPhysics/Physics.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"
#include "FenrirPhysics/Queries.hpp"
#include "FenrirPhysicsScripting/PhysicsScripting.hpp"
#include "FenrirScripting/NativeGuard.hpp"
#include "PhysicsScriptingState.hpp"

namespace
{
    using namespace Fenrir;

    EntityList& Entities(Testing::NativeCallHarness& harness)
    {
        return harness.GetApp().GetActiveScene().GetEntityList();
    }

    // An entity with only the engine's components.
    std::uint32_t Place(Testing::NativeCallHarness& harness)
    {
        return Entities(harness).CreateEntity().GetId();
    }

    // A static unit box (a collider and no body): with one in the scene, a frame makes the physics world.
    std::uint32_t AddStaticBox(Testing::NativeCallHarness& harness)
    {
        Entity box = Entities(harness).CreateEntity();
        box.GetComponent<Transform>() =
            Transform(Math::Vec3(0.0f, -5.0f, 0.0f), Math::Quat(1.0f, 0.0f, 0.0f, 0.0f), Math::Vec3(1.0f));
        box.AddComponent<Collider>();
        return box.GetId();
    }

    // Physics installed and one frame run with a box in the scene, so the active scene has a physics world.
    Physics InstallWithWorld(Testing::NativeCallHarness& harness)
    {
        Physics physics = Physics::Install(harness.GetApp());
        AddStaticBox(harness);
        harness.GetApp().RunFrame(Testing::HeadlessApp::FrameDelta);
        REQUIRE(PhysicsWorld::Of(harness.GetApp().GetActiveScene()) != nullptr);
        return physics;
    }

    // The default filter, which the C# facade passes for a query given none.
    const QueryFilter DefaultFilter;
    const Math::Vec3 Up(0.0f, 1.0f, 0.0f);
    const Math::Vec3 Down(0.0f, -1.0f, 0.0f);
} // namespace

TEST_CASE("With no physics world, a physics native does nothing, returns false or zero and warns once under its name",
          "[physics][scripting][rules]")
{
    Testing::NativeCallHarness harness({Physics::ScriptingExtension()});
    const std::uint32_t entity = Place(harness);
    const Entity body = Entities(harness).HandleOf(entity);
    // No frame has run with a body in the scene, so there is no physics world.
    REQUIRE(PhysicsWorld::Of(harness.GetApp().GetActiveScene()) == nullptr);

    {
        for (int call = 0; call < 2; ++call)
        {
            INFO("An impulse with no world should return false, not throw");
            REQUIRE_FALSE(harness.Call<bool>("PhysicsNative.AddImpulse", &body, &Up));
        }
        INFO("Two impulses with no world did not warn exactly once, under the native's name");
        REQUIRE(harness.WarnedOnce("PhysicsNative.AddImpulse", PhysicsScripting::NoWorld));
    }
    {
        RaycastHit hit;
        INFO("A raycast with no world should find nothing");
        REQUIRE_FALSE(harness.Call<bool>("PhysicsNative.Raycast", &Up, &Down, 100.0f, &DefaultFilter, &hit));
        REQUIRE(harness.WarnedOnce("PhysicsNative.Raycast", PhysicsScripting::NoWorld));
    }
    {
        RaycastHit hits[4];
        INFO("An all-hit query with no world should write no hits");
        REQUIRE(harness.Call<std::int32_t>("PhysicsNative.RaycastAll", &Up, &Down, 100.0f, &DefaultFilter, hits, 4) ==
                0);
    }
    {
        const CollisionShape box;
        const Transform pose;
        MoveCastHit hit;
        INFO("A move cast with no world should block nothing");
        REQUIRE_FALSE(harness.Call<bool>("PhysicsNative.MoveCast", &box, &pose, &Up, &DefaultFilter, &hit));
        REQUIRE(harness.WarnedOnce("PhysicsNative.MoveCast", PhysicsScripting::NoWorld));
    }
    {
        // The registration zeroes `offset` before the native runs (ADR-0045), so a native that writes nothing reads
        // zero.
        const CollisionShape box;
        const Transform pose;
        Math::Vec3 offset(3.0f);
        INFO("Depenetrate with no world should return false and leave a zero offset");
        REQUIRE_FALSE(harness.Call<bool>("PhysicsNative.Depenetrate", &box, &pose, &DefaultFilter, &offset));
        REQUIRE(offset == Math::Vec3(0.0f));
    }
    {
        Contact contacts[4];
        INFO("Body state with no world should be zero or false");
        REQUIRE(harness.Call<std::int32_t>("PhysicsNative.ContactsOf", &body, contacts, 4) == 0);
        REQUIRE_FALSE(harness.Call<bool>("PhysicsNative.IsSleeping", &body));
    }
    {
        Math::Vec3 gravity(3.0f);
        INFO("Gravity with no world should return false and read zero");
        REQUIRE_FALSE(harness.Call<bool>("PhysicsNative.Gravity", &gravity));
        REQUIRE(gravity == Math::Vec3(0.0f));
    }
    {
        const Math::Vec3 vertices[3] = {Math::Vec3(0.0f), Math::Vec3(1.0f, 0.0f, 0.0f), Math::Vec3(0.0f, 0.0f, 1.0f)};
        const std::uint32_t indices[3] = {0, 1, 2};
        for (int call = 0; call < 2; ++call)
        {
            INFO("Cooking a mesh with no world should give the none handle, and releasing one false");
            REQUIRE(harness.Call<std::uint64_t>("PhysicsNative.CreateCollisionMesh", vertices, 3, indices, 3) == 0);
            REQUIRE_FALSE(harness.Call<bool>("PhysicsNative.ReleaseCollisionMesh", std::uint64_t{1}));
        }
        INFO("Two calls of each mesh native did not warn exactly once each, under its own name");
        REQUIRE(harness.WarnedOnce("PhysicsNative.CreateCollisionMesh", PhysicsScripting::NoWorld));
        REQUIRE(harness.WarnedOnce("PhysicsNative.ReleaseCollisionMesh", PhysicsScripting::NoWorld));
    }
}

TEST_CASE("On an entity without a body, a physics native does nothing, returns false or zero and warns once",
          "[physics][scripting][rules]")
{
    Testing::NativeCallHarness harness({Physics::ScriptingExtension()});
    const Physics installed = InstallWithWorld(harness);
    const std::uint32_t plain = Place(harness);
    const std::uint32_t dead = Place(harness);
    Entities(harness).DestroyEntity(dead);
    const Entity plainEntity = Entities(harness).HandleOf(plain);
    const Entity deadEntity = Entities(harness).HandleOf(dead);

    {
        for (int call = 0; call < 2; ++call)
        {
            INFO("An impulse on an entity with no body should return false");
            REQUIRE_FALSE(harness.Call<bool>("PhysicsNative.AddImpulse", &plainEntity, &Up));
        }
        INFO("Two impulses on an entity with no body did not warn exactly once, under the native's name");
        REQUIRE(harness.WarnedOnce("PhysicsNative.AddImpulse", PhysicsScripting::NoBody));
    }
    {
        Contact contacts[4];
        Math::AABB bounds;
        INFO("An entity with no body has no contacts, does not sleep and has no bounds");
        REQUIRE(harness.Call<std::int32_t>("PhysicsNative.ContactsOf", &plainEntity, contacts, 4) == 0);
        REQUIRE_FALSE(harness.Call<bool>("PhysicsNative.IsSleeping", &plainEntity));
        REQUIRE_FALSE(harness.Call<bool>("PhysicsNative.Bounds", &plainEntity, &bounds));
    }
    {
        for (int call = 0; call < 2; ++call)
        {
            INFO("An entity that is not alive should read as not sleeping");
            REQUIRE_FALSE(harness.Call<bool>("PhysicsNative.IsSleeping", &deadEntity));
        }
        INFO("An entity that is not alive did not warn exactly once through the guard's entity-alive rule");
        REQUIRE(harness.WarnedOnce("PhysicsNative.IsSleeping", NativeGuard::NotAlive));
    }
}

TEST_CASE("GetJointState reads zeroed with no world and on an entity without a Joint, warning once each",
          "[physics][scripting][rules][joints]")
{
    Testing::NativeCallHarness harness({Physics::ScriptingExtension()});
    const std::uint32_t plain = Place(harness);
    const Entity plainEntity = Entities(harness).HandleOf(plain);
    // Called as C# calls it, the registration zeroing `state` before the native runs (ADR-0045).
    const auto readJoint = [&](JointState& state) {
        harness.Call<void>("PhysicsNative.GetJointState", &plainEntity, &state);
    };
    const auto zeroed = [](const JointState& state) {
        return state.force == Math::Vec3(0.0f) && state.torque == Math::Vec3(0.0f) && state.position == 0.0f;
    };
    JointState state;
    state.force = Math::Vec3(3.0f);
    state.position = 3.0f;

    REQUIRE(PhysicsWorld::Of(harness.GetApp().GetActiveScene()) == nullptr);
    for (int call = 0; call < 2; ++call)
    {
        readJoint(state);
        INFO("With no physics world, the state should read zeroed");
        REQUIRE(zeroed(state));
    }
    {
        INFO("Two reads with no world did not warn exactly once");
        REQUIRE(harness.WarnedOnce("PhysicsNative.GetJointState", PhysicsScripting::NoWorld));
    }

    const Physics installed = InstallWithWorld(harness);
    for (int call = 0; call < 2; ++call)
    {
        state.force = Math::Vec3(3.0f);
        readJoint(state);
        INFO("On an entity with no Joint, the state should read zeroed");
        REQUIRE(zeroed(state));
    }
    Entities(harness).DestroyEntity(plain);
    state.force = Math::Vec3(3.0f);
    readJoint(state);
    {
        INFO("On an entity no longer alive, likewise");
        REQUIRE(zeroed(state));
    }
    {
        INFO("Reads of a non-joint and a dead entity did not warn exactly once that the entity has no Joint");
        REQUIRE(harness.WarnedOnce("PhysicsNative.GetJointState", PhysicsScripting::NoJoint));
    }
}

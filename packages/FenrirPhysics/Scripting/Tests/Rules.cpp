// The physics natives' rules, with no .NET and no fixture assembly: through FenrirScripting's NativeCallHarness, each
// native is called from C++ with a PhysicsScripting state this file makes, and what it returns, writes and warns is
// read back from the harness. The rules: with no physics world a native does nothing and returns false or zero; on an
// entity with no body a force does nothing; GetJointState on an entity without a Joint reads zeroed; and each warns
// once per native and message, under the native's name. What a script sees of physics stays in
// PhysicsScriptingTests.cpp, on .NET.
#include <catch2/catch_test_macros.hpp>

#include "NativeCallHarness.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirPhysics/Components.hpp"
#include "FenrirPhysics/Joints.hpp"
#include "FenrirPhysics/Physics.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"
#include "FenrirPhysics/Queries.hpp"
#include "FenrirScripting/EntityValue.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
#include "FenrirScripting/ZeroingOutputs.hpp"
#include "Generated/FenrirPhysicsNatives.hpp"
#include "PhysicsScriptingState.hpp"

namespace
{
    using namespace Fenrir;

    // A release build warns about nothing (WarnOnceRecord, ADR-0043); a debug build once per native and message.
#ifdef NDEBUG
    constexpr std::size_t once = 0;
#else
    constexpr std::size_t once = 1;
#endif

    constexpr const char* NoWorld = "the active scene has no physics world yet, so it does nothing";
    constexpr const char* NoBody = "the entity has no body, so it does nothing";
    constexpr const char* NotAlive = "the entity is not alive, so it does nothing";
    constexpr const char* NoJoint = "the entity has no Joint, so it reads zeroed";

    // How many warnings the app logged that read `text`.
    std::size_t Warnings(const Testing::NativeCallHarness& harness, const std::string& text)
    {
        const auto messages = harness.Logger().Messages();
        return static_cast<std::size_t>(
            std::count_if(messages.begin(), messages.end(), [&](const Testing::LoggedMessage& message) {
                return message.level == Testing::LogLevel::Warn && message.text.find(text) != std::string::npos;
            }));
    }

    // The whole line a physics native's warning `message` logs under the native `method` of PhysicsNative.
    std::string Logged(const std::string& method, const std::string& message)
    {
        return "[physics scripting] PhysicsNative." + method + ": " + message + " (warned once)";
    }

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
    Testing::NativeCallHarness harness;
    PhysicsScripting physics;
    const std::uint32_t entity = Place(harness);
    // No frame has run with a body in the scene, so there is no physics world.
    REQUIRE(PhysicsWorld::Of(harness.GetApp().GetActiveScene()) == nullptr);

    {
        const CallContext context = harness.Context("PhysicsNative.AddImpulse");
        for (int call = 0; call < 2; ++call)
        {
            INFO("An impulse with no world should return false, not throw");
            REQUIRE_FALSE(PhysicsNatives::AddImpulse(context, physics, EntityValue(entity), &Up));
        }
        INFO("Two impulses with no world did not warn exactly once, prefixed by the extension and the native's name");
        REQUIRE(Warnings(harness, Logged("AddImpulse", NoWorld)) == once);
        REQUIRE(Warnings(harness, "PhysicsNative.AddImpulse") == once);
    }
    {
        const CallContext context = harness.Context("PhysicsNative.Raycast");
        RaycastHit hit;
        INFO("A raycast with no world should find nothing");
        REQUIRE_FALSE(PhysicsNatives::Raycast(context, physics, &Up, &Down, 100.0f, &DefaultFilter, &hit));
        REQUIRE(Warnings(harness, Logged("Raycast", NoWorld)) == once);
    }
    {
        const CallContext context = harness.Context("PhysicsNative.RaycastAll");
        RaycastHit hits[4];
        INFO("An all-hit query with no world should write no hits");
        REQUIRE(PhysicsNatives::RaycastAll(context, physics, &Up, &Down, 100.0f, &DefaultFilter, hits, 4) == 0);
    }
    {
        const CallContext context = harness.Context("PhysicsNative.MoveCast");
        const CollisionShape box;
        const Transform pose;
        MoveCastHit hit;
        INFO("A move cast with no world should block nothing");
        REQUIRE_FALSE(PhysicsNatives::MoveCast(context, physics, &box, &pose, &Up, &DefaultFilter, &hit));
        REQUIRE(Warnings(harness, Logged("MoveCast", NoWorld)) == once);
    }
    {
        // Called as the registration calls it, with its output zeroed first (ADR-0045): index 5 is `offset`.
        const CallContext context = harness.Context("PhysicsNative.Depenetrate");
        const CollisionShape box;
        const Transform pose;
        Math::Vec3 offset(3.0f);
        INFO("Depenetrate with no world should return false and leave a zero offset");
        REQUIRE_FALSE(ZeroingOutputs<&PhysicsNatives::Depenetrate, 5>::Call(context, physics, &box, &pose,
                                                                            &DefaultFilter, &offset));
        REQUIRE(offset == Math::Vec3(0.0f));
    }
    {
        const CallContext context = harness.Context("PhysicsNative.ContactsOf");
        Contact contacts[4];
        INFO("Body state with no world should be zero or false");
        REQUIRE(PhysicsNatives::ContactsOf(context, physics, EntityValue(entity), contacts, 4) == 0);
        REQUIRE_FALSE(
            PhysicsNatives::IsSleeping(harness.Context("PhysicsNative.IsSleeping"), physics, EntityValue(entity)));
    }
    {
        // Index 2 is `gravity`.
        const CallContext context = harness.Context("PhysicsNative.Gravity");
        Math::Vec3 gravity(3.0f);
        INFO("Gravity with no world should return false and read zero");
        REQUIRE_FALSE(ZeroingOutputs<&PhysicsNatives::Gravity, 2>::Call(context, physics, &gravity));
        REQUIRE(gravity == Math::Vec3(0.0f));
    }
    {
        const CallContext create = harness.Context("PhysicsNative.CreateCollisionMesh");
        const CallContext release = harness.Context("PhysicsNative.ReleaseCollisionMesh");
        const Math::Vec3 vertices[3] = {Math::Vec3(0.0f), Math::Vec3(1.0f, 0.0f, 0.0f), Math::Vec3(0.0f, 0.0f, 1.0f)};
        const std::uint32_t indices[3] = {0, 1, 2};
        for (int call = 0; call < 2; ++call)
        {
            INFO("Cooking a mesh with no world should give the none handle, and releasing one false");
            REQUIRE(PhysicsNatives::CreateCollisionMesh(create, physics, vertices, 3, indices, 3) == 0);
            REQUIRE_FALSE(PhysicsNatives::ReleaseCollisionMesh(release, physics, 1));
        }
        INFO("Two calls of each mesh native did not warn exactly once each, under its own name");
        REQUIRE(Warnings(harness, Logged("CreateCollisionMesh", NoWorld)) == once);
        REQUIRE(Warnings(harness, Logged("ReleaseCollisionMesh", NoWorld)) == once);
    }
}

TEST_CASE("On an entity without a body, a physics native does nothing, returns false or zero and warns once",
          "[physics][scripting][rules]")
{
    Testing::NativeCallHarness harness;
    PhysicsScripting physics;
    const Physics installed = InstallWithWorld(harness);
    const std::uint32_t plain = Place(harness);
    const std::uint32_t dead = Place(harness);
    Entities(harness).DestroyEntity(dead);

    {
        const CallContext context = harness.Context("PhysicsNative.AddImpulse");
        for (int call = 0; call < 2; ++call)
        {
            INFO("An impulse on an entity with no body should return false");
            REQUIRE_FALSE(PhysicsNatives::AddImpulse(context, physics, EntityValue(plain), &Up));
        }
        INFO("Two impulses on an entity with no body did not warn exactly once, under the native's name");
        REQUIRE(Warnings(harness, Logged("AddImpulse", NoBody)) == once);
    }
    {
        Contact contacts[4];
        Math::AABB bounds;
        INFO("An entity with no body has no contacts, does not sleep and has no bounds");
        REQUIRE(PhysicsNatives::ContactsOf(harness.Context("PhysicsNative.ContactsOf"), physics, EntityValue(plain),
                                           contacts, 4) == 0);
        REQUIRE_FALSE(
            PhysicsNatives::IsSleeping(harness.Context("PhysicsNative.IsSleeping"), physics, EntityValue(plain)));
        REQUIRE_FALSE(
            PhysicsNatives::Bounds(harness.Context("PhysicsNative.Bounds"), physics, EntityValue(plain), &bounds));
    }
    {
        const CallContext context = harness.Context("PhysicsNative.IsSleeping");
        for (int call = 0; call < 2; ++call)
        {
            INFO("An entity that is not alive should read as not sleeping");
            REQUIRE_FALSE(PhysicsNatives::IsSleeping(context, physics, EntityValue(dead)));
        }
        INFO("An entity that is not alive did not warn exactly once through the guard's entity-alive rule");
        REQUIRE(Warnings(harness, Logged("IsSleeping", NotAlive)) == once);
    }
}

TEST_CASE("GetJointState reads zeroed with no world and on an entity without a Joint, warning once each",
          "[physics][scripting][rules][joints]")
{
    Testing::NativeCallHarness harness;
    PhysicsScripting physics;
    const CallContext context = harness.Context("PhysicsNative.GetJointState");
    const std::uint32_t plain = Place(harness);
    // Called as the registration calls it, with its output zeroed first (ADR-0045): index 3 is `state`.
    using Native = ZeroingOutputs<&PhysicsNatives::GetJointState, 3>;
    const auto zeroed = [](const JointState& state) {
        return state.force == Math::Vec3(0.0f) && state.torque == Math::Vec3(0.0f) && state.position == 0.0f;
    };
    JointState state;
    state.force = Math::Vec3(3.0f);
    state.position = 3.0f;

    REQUIRE(PhysicsWorld::Of(harness.GetApp().GetActiveScene()) == nullptr);
    for (int call = 0; call < 2; ++call)
    {
        Native::Call(context, physics, EntityValue(plain), &state);
        INFO("With no physics world, the state should read zeroed");
        REQUIRE(zeroed(state));
    }
    {
        INFO("Two reads with no world did not warn exactly once");
        REQUIRE(Warnings(harness, Logged("GetJointState", NoWorld)) == once);
    }

    const Physics installed = InstallWithWorld(harness);
    for (int call = 0; call < 2; ++call)
    {
        state.force = Math::Vec3(3.0f);
        Native::Call(context, physics, EntityValue(plain), &state);
        INFO("On an entity with no Joint, the state should read zeroed");
        REQUIRE(zeroed(state));
    }
    Entities(harness).DestroyEntity(plain);
    state.force = Math::Vec3(3.0f);
    Native::Call(context, physics, EntityValue(plain), &state);
    {
        INFO("On an entity no longer alive, likewise");
        REQUIRE(zeroed(state));
    }
    {
        INFO("Reads of a non-joint and a dead entity did not warn exactly once that the entity has no Joint");
        REQUIRE(Warnings(harness, Logged("GetJointState", NoJoint)) == once);
    }
}

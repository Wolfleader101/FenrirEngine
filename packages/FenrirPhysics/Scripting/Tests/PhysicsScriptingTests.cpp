// Physics from C# through the scripting fixture harness: an App with no window, stepped one frame at a time, with
// physics installed and scripting running the physics fixture script assembly, which uses the Fenrir.Physics contract.
// C++ calls the fixture's static methods through Iris, as the system runner calls scripts, and checks what the engine
// saw.

#include "HeadlessApp.hpp"
#include "JointStateMapping.hpp"
#include "QueryMirrors.hpp"
#include "ScriptingProbe.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "FenrirDebugDraw/DebugDraw.hpp"
#include "FenrirDebugDraw/DebugDrawing.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirPhysics/Characters.hpp"
#include "FenrirPhysics/Events.hpp"
#include "FenrirPhysics/Joints.hpp"
#include "FenrirPhysics/Vehicles.hpp"
#include "FenrirPhysicsScripting/PhysicsScripting.hpp"
#include "FenrirScripting/EngineMappings.hpp"
#include "FenrirScripting/Scripting.hpp"

using namespace Fenrir;
using Catch::Approx;

namespace
{
    std::filesystem::path FromEnvironment(const char* name)
    {
        const char* value = std::getenv(name);
        if (value == nullptr)
        {
            std::cerr << "Set " << name << " (CTest does): the physics scripting tests need the fixture assembly.\n";
            return {};
        }
        return std::filesystem::absolute(value);
    }

    const std::filesystem::path fixture = FromEnvironment("FENRIR_TEST_PHYSICS_FIXTURE");

    constexpr double Tick = Testing::HeadlessApp::FrameDelta;

    const Math::Quat Identity(1.0f, 0.0f, 0.0f, 0.0f);

    // Physics with interpolation off, so a body's Transform after a whole-tick frame is its latest step pose.
    PhysicsConfig StepPosesOnly()
    {
        PhysicsConfig config;
        config.interpolate = false;
        return config;
    }

    // An App with debug drawing and physics installed and scripting running the fixture with physics's extension.
    // Members end in reverse order: scripting, then physics, then debug drawing, then the app.
    struct PhysicsScriptingApp
    {
        PhysicsScriptingApp()
            : debugDrawing(DebugDrawing::Install(headless.Get())),
              physics(Physics::Install(headless.Get(), StepPosesOnly())),
              scripting(Scripting::Install(headless.Get(), Config(physics)))
        {
            INFO("The fixture did not load with physics's extension");
            REQUIRE(scripting.HasScripts());
        }

        static ScriptingConfig Config(const Physics& physics)
        {
            ScriptingConfig config;
            config.scriptAssembly = fixture;
            config.hotReload = false;
            config.extensions = {physics.ScriptingExtension()};
            return config;
        }

        Testing::HeadlessApp headless;
        DebugDrawing debugDrawing;
        Physics physics;
        Scripting scripting;

        EntityList& Entities()
        {
            return headless.Get().GetActiveScene().GetEntityList();
        }

        // An entity with only the engine's components, at `position`.
        std::uint32_t Place(const Math::Vec3& position)
        {
            Entity entity = Entities().CreateEntity();
            entity.GetComponent<Transform>() = Transform(position, Identity, Math::Vec3(1.0f));
            return entity.GetId();
        }

        void Run(int frames)
        {
            for (int frame = 0; frame < frames; ++frame)
            {
                headless.Get().RunFrame(Tick);
            }
        }

        // Plays back the structural changes the fixture recorded, as the system runner does after a system.
        void ApplyChanges()
        {
            ScriptingProbe::ApplyChanges(scripting);
        }

        // Calls a static method of a type in the fixture through Iris.
        template <typename Result, typename... Args>
        Result Call(const char* typeName, const char* method, Args... arguments)
        {
            auto* component = ScriptingProbe::Component(scripting);
            {
                INFO("There is no loaded script assembly");
                REQUIRE(component != nullptr);
            }
            const auto type = component->FindType(typeName);
            {
                INFO(std::string("The fixture type was not found: ") + type.diagnostic);
                REQUIRE(type.Succeeded());
            }
            const auto handle = type.value->FindMethod<Result(Args...)>(method);
            {
                INFO(std::string("The fixture method ") + method + " was not found: " + handle.diagnostic);
                REQUIRE(handle.Succeeded());
            }
            const auto result = handle.value->Invoke(arguments...);
            {
                INFO(std::string("The fixture method ") + method + " failed: " + result.diagnostic);
                REQUIRE(result.Succeeded());
            }
            if constexpr (!std::is_void_v<Result>)
            {
                return *result.value;
            }
        }
    };

    constexpr const char* BodiesType = "Fenrir.PhysicsScriptFixture.Bodies";
} // namespace

TEST_CASE("A script makes an entity a body with a shape set by value, and it falls", "[physics][scripting]")
{
    PhysicsScriptingApp test;
    const std::uint32_t entity = test.Place(Math::Vec3(0.0f, 10.0f, 0.0f));

    test.Call<void>(BodiesType, "MakeFallingSphere", entity, 0.25f);
    test.ApplyChanges();
    {
        const Collider* collider = test.Entities().GetEntity(entity).HasComponent<Collider>()
                                       ? &test.Entities().GetEntity(entity).GetComponent<Collider>()
                                       : nullptr;
        INFO("The script's Collider did not reach the entity");
        REQUIRE(collider != nullptr);
        INFO("The collider's shape is not the sphere of radius 0.25 the script assigned");
        REQUIRE(collider->shape.Is<SphereShape>());
        REQUIRE(collider->shape.As<SphereShape>().radius == 0.25f);
    }
    {
        const RigidBody& body = test.Entities().GetEntity(entity).GetComponent<RigidBody>();
        INFO("The script's new RigidBody does not have the C++ defaults (dynamic, 1 kg)");
        REQUIRE(body.motionType == MotionType::Dynamic);
        REQUIRE(body.mass == 1.0f);
    }

    test.Run(30);

    const float height = test.Call<float>(BodiesType, "Height", entity);
    INFO("After half a second the body should have fallen about 1.2 m from 10, but C# reads it at " << height);
    REQUIRE(height < 9.0f);
    REQUIRE(height > 8.5f);
}

namespace
{
    constexpr const char* WorldType = "Fenrir.PhysicsScriptFixture.World";

    // A dynamic unit box of `mass` kilograms at `position`, made in C++.
    std::uint32_t AddBody(PhysicsScriptingApp& test, const Math::Vec3& position, float mass = 1.0f)
    {
        Entity body = test.Entities().GetEntity(test.Place(position));
        body.AddComponent<RigidBody>(RigidBody{.mass = mass});
        body.AddComponent<Collider>();
        return body.GetId();
    }
} // namespace

TEST_CASE("An impulse from C# changes a body's velocity by impulse over mass", "[physics][scripting]")
{
    PhysicsScriptingApp test;
    const std::uint32_t box = AddBody(test, Math::Vec3(0.0f, 50.0f, 0.0f), 2.0f);
    test.Run(1);

    {
        INFO("AddImpulse on a body in a world with physics returned false");
        REQUIRE(test.Call<bool>(WorldType, "AddImpulse", box, 4.0f, 0.0f, 0.0f));
    }
    test.Run(1);

    const float speed = test.Entities().GetEntity(box).GetComponent<RigidBody>().linearVelocity.x;
    INFO("An impulse of 4 N s on 2 kg adds 2 m/s (less a tick of damping), but the body moves at " << speed);
    REQUIRE(speed == Approx(2.0f).margin(0.01));
}

namespace
{
    constexpr std::uint32_t NoEntity = 0xFFFFFFFFu;

    // A static unit box (a collider and no body) centred at `position`.
    std::uint32_t AddStaticBox(PhysicsScriptingApp& test, const Math::Vec3& position)
    {
        Entity box = test.Entities().GetEntity(test.Place(position));
        box.AddComponent<Collider>();
        return box.GetId();
    }
} // namespace

TEST_CASE("A raycast from C# finds the nearest body, with its point, normal and distance", "[physics][scripting]")
{
    PhysicsScriptingApp test;
    AddStaticBox(test, Math::Vec3(0.0f, 2.0f, 0.0f));
    const std::uint32_t upper = AddStaticBox(test, Math::Vec3(0.0f, 5.0f, 0.0f));
    test.Run(1);

    {
        INFO("A ray straight down from y = 10 hit nothing");
        REQUIRE(test.Call<bool>(WorldType, "Raycast", 0.0f, 10.0f, 0.0f, 0.0f, -1.0f, 0.0f, 100.0f));
    }
    {
        INFO("The ray did not hit the upper box first, as body and collider");
        REQUIRE(test.Call<std::uint32_t>(WorldType, "LastHitBody") == upper);
        REQUIRE(test.Call<std::uint32_t>(WorldType, "LastHitCollider") == upper);
    }
    {
        INFO("The upper box's top is at y = 5.5: 4.5 along the ray, with an upward normal");
        REQUIRE(test.Call<float>(WorldType, "LastHitDistance") == Approx(4.5f).margin(0.01));
        REQUIRE(test.Call<float>(WorldType, "LastHitPointY") == Approx(5.5f).margin(0.01));
        REQUIRE(test.Call<float>(WorldType, "LastHitNormalY") == Approx(1.0f).margin(0.01));
    }
    {
        INFO("A ray pointing away from every body hit something");
        REQUIRE_FALSE(test.Call<bool>(WorldType, "Raycast", 0.0f, 10.0f, 0.0f, 0.0f, 1.0f, 0.0f, 100.0f));
    }
}

TEST_CASE("RaycastAll from C# fills a span with the nearest hits first, as many as fit", "[physics][scripting]")
{
    PhysicsScriptingApp test;
    const std::uint32_t lower = AddStaticBox(test, Math::Vec3(0.0f, 2.0f, 0.0f));
    const std::uint32_t upper = AddStaticBox(test, Math::Vec3(0.0f, 5.0f, 0.0f));
    AddStaticBox(test, Math::Vec3(0.0f, -1.0f, 0.0f));
    test.Run(1);

    {
        INFO("A span of 8 should hold all three boxes under the ray");
        REQUIRE(test.Call<int>(WorldType, "RaycastAll", 0.0f, 10.0f, 0.0f, 0.0f, -1.0f, 0.0f, 100.0f, 8) == 3);
    }
    {
        INFO("A span of 2 should hold the two nearest boxes");
        REQUIRE(test.Call<int>(WorldType, "RaycastAll", 0.0f, 10.0f, 0.0f, 0.0f, -1.0f, 0.0f, 100.0f, 2) == 2);
    }
    {
        INFO("The hits are not the upper box then the lower one, nearest first");
        REQUIRE(test.Call<std::uint32_t>(WorldType, "AllHitBody", 0) == upper);
        REQUIRE(test.Call<std::uint32_t>(WorldType, "AllHitBody", 1) == lower);
        REQUIRE(test.Call<float>(WorldType, "AllHitDistance", 1) == Approx(7.5f).margin(0.01));
    }
}

TEST_CASE("Shape casts from C# find the bodies a swept shape touches, nearest first", "[physics][scripting]")
{
    PhysicsScriptingApp test;
    AddStaticBox(test, Math::Vec3(0.0f, 2.0f, 0.0f));
    const std::uint32_t upper = AddStaticBox(test, Math::Vec3(0.0f, 5.0f, 0.0f));
    test.Run(1);

    {
        INFO("A sphere of radius 0.5 swept down from y = 10 hit nothing");
        REQUIRE(test.Call<bool>(WorldType, "SphereCast", 0.0f, 10.0f, 0.0f, 0.5f, 0.0f, -1.0f, 0.0f, 100.0f));
    }
    {
        INFO("The sphere should first touch the upper box's top (y = 5.5) after 4, facing up");
        REQUIRE(test.Call<std::uint32_t>(WorldType, "LastCastBody") == upper);
        REQUIRE(test.Call<float>(WorldType, "LastCastDistance") == Approx(4.0f).margin(0.02));
        REQUIRE(test.Call<float>(WorldType, "LastCastNormalY") == Approx(1.0f).margin(0.01));
    }
    {
        INFO("The swept sphere should touch both boxes");
        REQUIRE(test.Call<int>(WorldType, "SphereCastAll", 0.0f, 10.0f, 0.0f, 0.5f, 0.0f, -1.0f, 0.0f, 100.0f, 8) == 2);
    }
}

TEST_CASE("Overlap queries from C# find the colliders a shape or point is in", "[physics][scripting]")
{
    PhysicsScriptingApp test;
    const std::uint32_t lower = AddStaticBox(test, Math::Vec3(0.0f, 2.0f, 0.0f));
    const std::uint32_t upper = AddStaticBox(test, Math::Vec3(0.0f, 5.0f, 0.0f));
    test.Run(1);

    {
        INFO("A small box inside the upper box should overlap it alone");
        REQUIRE(test.Call<int>(WorldType, "OverlapBox", 0.0f, 5.0f, 0.0f, 0.1f, 8) == 1);
        REQUIRE(test.Call<std::uint32_t>(WorldType, "OverlapBody", 0) == upper);
        REQUIRE(test.Call<std::uint32_t>(WorldType, "OverlapCollider", 0) == upper);
    }
    {
        INFO("The point (0, 2, 0) should be in the lower box alone");
        REQUIRE(test.Call<int>(WorldType, "OverlapPoint", 0.0f, 2.0f, 0.0f, 8) == 1);
        REQUIRE(test.Call<std::uint32_t>(WorldType, "OverlapBody", 0) == lower);
    }
    {
        INFO("A point between the boxes is in neither");
        REQUIRE(test.Call<int>(WorldType, "OverlapPoint", 0.0f, 3.5f, 0.0f, 8) == 0);
    }
}

TEST_CASE("Gravity is read and set from C#, and bodies fall by it", "[physics][scripting]")
{
    PhysicsScriptingApp test;
    const std::uint32_t box = AddBody(test, Math::Vec3(0.0f, 50.0f, 0.0f));
    test.Run(1);
    {
        INFO("C# should read the world's starting gravity, -9.81 down");
        REQUIRE(test.Call<float>(WorldType, "GravityY") == Approx(-9.81f));
    }

    test.Call<void>(WorldType, "SetGravity", 0.0f, -2.0f, 0.0f);
    {
        INFO("C# should read back the gravity it set");
        REQUIRE(test.Call<float>(WorldType, "GravityY") == -2.0f);
    }
    const float before = test.Entities().GetEntity(box).GetComponent<RigidBody>().linearVelocity.y;
    test.Run(60);

    const float gained = test.Entities().GetEntity(box).GetComponent<RigidBody>().linearVelocity.y - before;
    INFO("A second under gravity -2 should add about -2 m/s (less damping), but it added " << gained);
    REQUIRE(gained == Approx(-2.0f).margin(0.1));
}

TEST_CASE("C# reads a body's bounds, whether it sleeps and what it touches", "[physics][scripting]")
{
    PhysicsScriptingApp test;
    const std::uint32_t ground = test.Place(Math::Vec3(0.0f, -0.5f, 0.0f));
    test.Entities().GetEntity(ground).AddComponent<Collider>(Collider{BoxShape{Math::Vec3(10.0f, 0.5f, 10.0f)}});
    const std::uint32_t box = AddBody(test, Math::Vec3(0.0f, 0.6f, 0.0f));
    test.Run(1);
    {
        INFO("A body that has just been made is not asleep");
        REQUIRE_FALSE(test.Call<bool>(WorldType, "IsSleeping", box));
    }

    test.Run(180);

    {
        INFO("A box resting on the ground for three seconds should be asleep");
        REQUIRE(test.Call<bool>(WorldType, "IsSleeping", box));
    }
    {
        INFO("The resting unit box's bounds should span about y = 0 to 1");
        REQUIRE(test.Call<bool>(WorldType, "Bounds", box));
        REQUIRE(test.Call<float>(WorldType, "BoundsMinY") == Approx(0.0f).margin(0.1));
        REQUIRE(test.Call<float>(WorldType, "BoundsMaxY") == Approx(1.0f).margin(0.1));
    }
    {
        INFO("The resting box should touch the ground alone, below it, with the normal pointing down to the ground");
        REQUIRE(test.Call<int>(WorldType, "ContactsOf", box, 8) == 1);
        REQUIRE(test.Call<std::uint32_t>(WorldType, "ContactBody", 0) == box);
        REQUIRE(test.Call<std::uint32_t>(WorldType, "ContactOtherBody", 0) == ground);
        REQUIRE(test.Call<std::uint32_t>(WorldType, "ContactOtherCollider", 0) == ground);
        REQUIRE(test.Call<float>(WorldType, "ContactNormalY", 0) == Approx(-1.0f).margin(0.01));
        REQUIRE(test.Call<float>(WorldType, "ContactPointY", 0) == Approx(0.0f).margin(0.05));
    }
}

TEST_CASE("C# chooses the debug lines a world draws", "[physics][scripting]")
{
    PhysicsScriptingApp test;
    AddStaticBox(test, Math::Vec3(0.0f, 0.0f, 0.0f));
    test.Run(1);

    {
        INFO("SetDebugDraw in a scene with a physics world returned false");
        REQUIRE(test.Call<bool>(WorldType, "DrawColliders"));
    }
    test.Run(1);

    const PhysicsWorld* world = PhysicsWorld::Of(test.headless.Get().GetActiveScene());
    REQUIRE(world != nullptr);
    INFO("The world should draw its colliders, and only them, after C# chose them");
    REQUIRE(world->DebugDrawCategories() == PhysicsDebugCategories::Colliders);
    const DebugDraw* draw = test.headless.Get().GetActiveScene().GetResource<DebugDraw>();
    REQUIRE(draw != nullptr);
    REQUIRE_FALSE(draw->Tessellate().lines.empty());
}

TEST_CASE("C#'s debug-draw categories match C++'s, joints, characters and vehicles included", "[physics][scripting]")
{
    PhysicsScriptingApp test;
    AddStaticBox(test, Math::Vec3(0.0f, 0.0f, 0.0f));
    test.Run(1);
    const PhysicsWorld* world = PhysicsWorld::Of(test.headless.Get().GetActiveScene());
    REQUIRE(world != nullptr);

    REQUIRE(test.Call<bool>(WorldType, "DrawJoints"));
    INFO("C#'s Joints is C++'s Joints");
    REQUIRE(world->DebugDrawCategories() == PhysicsDebugCategories::Joints);

    REQUIRE(test.Call<bool>(WorldType, "DrawCharacters"));
    INFO("C#'s Characters is C++'s Characters");
    REQUIRE(world->DebugDrawCategories() == PhysicsDebugCategories::Characters);

    REQUIRE(test.Call<bool>(WorldType, "DrawVehicles"));
    INFO("C#'s Vehicles is C++'s Vehicles");
    REQUIRE(world->DebugDrawCategories() == PhysicsDebugCategories::Vehicles);

    REQUIRE(test.Call<bool>(WorldType, "DrawAll"));
    INFO("C#'s All is C++'s All: every category, joints, characters and vehicles included");
    REQUIRE(world->DebugDrawCategories() == PhysicsDebugCategories::All);
}

TEST_CASE("A collision mesh made from C# spans is usable by a collider, and released once", "[physics][scripting]")
{
    PhysicsScriptingApp test;
    const auto mesh = test.Call<std::uint64_t>(WorldType, "MakeGroundMesh");
    {
        INFO("Cooking a valid mesh from C# gave the none handle");
        REQUIRE(mesh != 0);
    }
    const std::uint32_t ground = test.Place(Math::Vec3(0.0f));
    test.Call<void>(WorldType, "GiveMeshCollider", ground, mesh);
    test.ApplyChanges();
    const std::uint32_t box = AddBody(test, Math::Vec3(0.0f, 2.0f, 0.0f));

    test.Run(120);

    {
        const float height = test.Entities().GetEntity(box).GetComponent<Transform>().pos.y;
        INFO("A unit box dropped on the mesh should rest with its centre at y = 0.5, but it is at " << height);
        REQUIRE(height == Approx(0.5f).margin(0.03));
    }
    {
        INFO("Releasing the mesh from C# should succeed once, then report that it is not alive");
        REQUIRE(test.Call<bool>(WorldType, "ReleaseMesh", mesh));
        REQUIRE_FALSE(test.Call<bool>(WorldType, "ReleaseMesh", mesh));
    }
    {
        INFO("A mesh whose indices are out of range should be refused with the none handle");
        REQUIRE(test.Call<std::uint64_t>(WorldType, "MakeBadMesh") == 0);
    }
}

namespace
{
    // How many warnings the app logged that mention `text`.
    std::ptrdiff_t WarningsAbout(const PhysicsScriptingApp& test, const std::string& text)
    {
        const auto messages = test.headless.Logger().Messages();
        return std::count_if(messages.begin(), messages.end(), [&](const Testing::LoggedMessage& message) {
            return message.level == Testing::LogLevel::Warn && message.text.find(text) != std::string::npos;
        });
    }
} // namespace

TEST_CASE("In a scene with no physics world, every function from C# does nothing and returns false or zero",
          "[physics][scripting]")
{
    PhysicsScriptingApp test;
    const std::uint32_t entity = test.Place(Math::Vec3(0.0f));
    // No frame has run with a body in the scene, so there is no physics world.
    REQUIRE(PhysicsWorld::Of(test.headless.Get().GetActiveScene()) == nullptr);

    {
        INFO("Forces with no world should return false, not throw");
        REQUIRE_FALSE(test.Call<bool>(WorldType, "AddImpulse", entity, 1.0f, 0.0f, 0.0f));
        REQUIRE_FALSE(test.Call<bool>(WorldType, "AddForce", entity));
        REQUIRE_FALSE(test.Call<bool>(WorldType, "AddTorque", entity));
        REQUIRE_FALSE(test.Call<bool>(WorldType, "AddAngularImpulse", entity));
    }
    {
        INFO("Queries with no world should find nothing");
        REQUIRE_FALSE(test.Call<bool>(WorldType, "Raycast", 0.0f, 10.0f, 0.0f, 0.0f, -1.0f, 0.0f, 100.0f));
        REQUIRE(test.Call<int>(WorldType, "RaycastAll", 0.0f, 10.0f, 0.0f, 0.0f, -1.0f, 0.0f, 100.0f, 4) == 0);
        REQUIRE_FALSE(test.Call<bool>(WorldType, "SphereCast", 0.0f, 10.0f, 0.0f, 0.5f, 0.0f, -1.0f, 0.0f, 100.0f));
        REQUIRE(test.Call<int>(WorldType, "SphereCastAll", 0.0f, 10.0f, 0.0f, 0.5f, 0.0f, -1.0f, 0.0f, 100.0f, 4) == 0);
        REQUIRE(test.Call<int>(WorldType, "OverlapBox", 0.0f, 0.0f, 0.0f, 1.0f, 4) == 0);
        REQUIRE(test.Call<int>(WorldType, "OverlapPoint", 0.0f, 0.0f, 0.0f, 4) == 0);
    }
    {
        INFO("Body state and world settings with no world should be false or zero");
        REQUIRE(test.Call<int>(WorldType, "ContactsOf", entity, 4) == 0);
        REQUIRE_FALSE(test.Call<bool>(WorldType, "IsSleeping", entity));
        REQUIRE_FALSE(test.Call<bool>(WorldType, "Bounds", entity));
        REQUIRE(test.Call<float>(WorldType, "GravityLength") == 0.0f);
        test.Call<void>(WorldType, "SetGravity", 0.0f, -1.0f, 0.0f);
        REQUIRE_FALSE(test.Call<bool>(WorldType, "DrawColliders"));
    }
#ifndef NDEBUG
    {
        test.Call<bool>(WorldType, "AddImpulse", entity, 1.0f, 0.0f, 0.0f);
        INFO("A debug build should warn once, not on every call, that AddImpulse had no world");
        REQUIRE(WarningsAbout(test, "AddImpulse") == 1);
    }
#endif
}

TEST_CASE("On an entity without a body, functions from C# do nothing and return false or zero", "[physics][scripting]")
{
    PhysicsScriptingApp test;
    AddStaticBox(test, Math::Vec3(0.0f, -5.0f, 0.0f));
    const std::uint32_t plain = test.Place(Math::Vec3(0.0f));
    test.Run(1);
    REQUIRE(PhysicsWorld::Of(test.headless.Get().GetActiveScene()) != nullptr);

    {
        INFO("An impulse on an entity with no body should return false");
        REQUIRE_FALSE(test.Call<bool>(WorldType, "AddImpulse", plain, 1.0f, 0.0f, 0.0f));
    }
    {
        INFO("An entity with no body has no contacts, does not sleep and has no bounds");
        REQUIRE(test.Call<int>(WorldType, "ContactsOf", plain, 4) == 0);
        REQUIRE_FALSE(test.Call<bool>(WorldType, "IsSleeping", plain));
        REQUIRE_FALSE(test.Call<bool>(WorldType, "Bounds", plain));
    }
    {
        INFO("An entity that does not exist is treated as one with no body");
        REQUIRE_FALSE(test.Call<bool>(WorldType, "AddImpulse", 0xFFFFFFFFu, 1.0f, 0.0f, 0.0f));
        REQUIRE_FALSE(test.Call<bool>(WorldType, "IsSleeping", 0x00ABCDEFu));
    }
#ifndef NDEBUG
    {
        INFO("A debug build should warn once that IsSleeping was asked about an entity with no body");
        REQUIRE(WarningsAbout(test, "IsSleeping") == 1);
    }
#endif
}

TEST_CASE("Collision meshes from C# do nothing once the Physics owner is gone", "[physics][scripting]")
{
    PhysicsScriptingApp test;
    {
        // Moved out and destroyed: the owner whose extension the scripts were given ends while scripting runs.
        Physics ended = std::move(test.physics);
    }
    {
        INFO("Cooking a mesh with no owner should give the none handle, not throw");
        REQUIRE(test.Call<std::uint64_t>(WorldType, "MakeGroundMesh") == 0);
    }
    {
        INFO("Releasing a mesh with no owner should return false");
        REQUIRE_FALSE(test.Call<bool>(WorldType, "ReleaseMesh", std::uint64_t{1}));
    }
}

TEST_CASE("C# edits physics components in place, and reading the wrong kind of shape throws", "[physics][scripting]")
{
    PhysicsScriptingApp test;
    const std::uint32_t box = AddBody(test, Math::Vec3(0.0f, 10.0f, 0.0f));

    test.Call<void>(BodiesType, "Edit", box, 2.0f, 5.0f, 0.9f);
    test.ApplyChanges();

    Entity entity = test.Entities().GetEntity(box);
    {
        INFO("The box's half height written through C#'s view did not reach C++");
        REQUIRE(entity.GetComponent<Collider>().shape.As<BoxShape>().halfExtents.y == 2.0f);
    }
    {
        INFO("The material and filter C# added do not hold what it wrote");
        REQUIRE(entity.GetComponent<PhysicsMaterial>().friction == 0.9f);
        REQUIRE(entity.GetComponent<PhysicsMaterial>().restitution == 0.0f);
        REQUIRE(entity.GetComponent<CollisionFilter>().layers == (1u << 3));
        REQUIRE(entity.GetComponent<CollisionFilter>().mask == (1u << 4));
        REQUIRE_FALSE(entity.GetComponent<ContactEvents>().enabled);
    }
    test.Run(1);
    {
        const float speed = entity.GetComponent<RigidBody>().linearVelocity.y;
        INFO("The upward velocity of 5 C# wrote should set the body moving up (less a tick of gravity), but it is "
             << speed);
        REQUIRE(speed == Approx(5.0f - 9.81f / 60.0f).margin(0.02));
    }
    {
        const auto message = test.Call<std::optional<std::string>>(BodiesType, "WrongKindMessage", box);
        INFO("Reading a box as a sphere from C# should throw, naming both kinds");
        REQUIRE(message.has_value());
        REQUIRE(*message == "The shape is a Box, not a Sphere.");
    }
}

TEST_CASE("A query filter from C# chooses layers, ignores a body and finds triggers", "[physics][scripting]")
{
    PhysicsScriptingApp test;
    const std::uint32_t lower = AddStaticBox(test, Math::Vec3(0.0f, 2.0f, 0.0f));
    const std::uint32_t upper = AddStaticBox(test, Math::Vec3(0.0f, 5.0f, 0.0f));
    test.Entities().GetEntity(upper).AddComponent<CollisionFilter>(CollisionFilter{.layers = 1u << 1});
    const std::uint32_t trigger = AddStaticBox(test, Math::Vec3(0.0f, 8.0f, 0.0f));
    test.Entities().GetEntity(trigger).AddComponent<Trigger>();
    test.Run(1);

    const auto down = [&](std::uint32_t mask, std::uint32_t ignore, bool hitTriggers) {
        return test.Call<std::uint32_t>(WorldType, "RaycastDownFiltered", 10.0f, mask, ignore, hitTriggers);
    };
    {
        INFO("The default filter should skip the trigger and find the upper box");
        REQUIRE(down(~0u, NoEntity, false) == upper);
    }
    {
        INFO("Ignoring the upper box should find the lower one");
        REQUIRE(down(~0u, upper, false) == lower);
    }
    {
        INFO("A mask of layer 0 alone should pass over the upper box, which is in layer 1");
        REQUIRE(down(1u, NoEntity, false) == lower);
    }
    {
        INFO("Asking for triggers should find the trigger above both boxes");
        REQUIRE(down(~0u, NoEntity, true) == trigger);
    }
}

namespace
{
    constexpr const char* EventsType = "Fenrir.PhysicsScriptFixture.Events";

    // A static ground slab whose top face is at y = 0.
    std::uint32_t AddGround(PhysicsScriptingApp& test)
    {
        const std::uint32_t ground = test.Place(Math::Vec3(0.0f, -0.5f, 0.0f));
        test.Entities().GetEntity(ground).AddComponent<Collider>(Collider{BoxShape{Math::Vec3(10.0f, 0.5f, 10.0f)}});
        return ground;
    }

    // Every `TEvent` C++ reads while it runs frames, read after each frame as a system would.
    template <typename TEvent>
    struct CppSees
    {
        EventReader<TEvent> reader;
        std::vector<TEvent> events;

        void Run(PhysicsScriptingApp& test, int frames)
        {
            for (int frame = 0; frame < frames; ++frame)
            {
                test.Run(1);
                for (const TEvent& event : reader.Read(test.headless.Get()))
                {
                    events.push_back(event);
                }
            }
        }
    };
} // namespace

TEST_CASE("A C# system reads one ContactBegin when a box with ContactEvents lands, as C++ sees it",
          "[physics][scripting][events]")
{
    PhysicsScriptingApp test;
    const std::uint32_t ground = AddGround(test);
    const std::uint32_t box = AddBody(test, Math::Vec3(0.0f, 2.0f, 0.0f));
    test.Entities().GetEntity(box).AddComponent<ContactEvents>();
    CppSees<ContactBegin> cpp;

    cpp.Run(test, 120);

    REQUIRE(cpp.events.size() == 1);
    const ContactBegin& seen = cpp.events[0];
    {
        INFO("The box lands once and stays, so C# should read one ContactBegin");
        REQUIRE(test.Call<int>(EventsType, "ContactBeginCount") == 1);
    }
    {
        INFO("C# should name the ground as A and the box as B, as body and as collider, as C++ does");
        REQUIRE(test.Call<std::uint32_t>(EventsType, "BeginBodyA", 0) == ground);
        REQUIRE(test.Call<std::uint32_t>(EventsType, "BeginBodyB", 0) == box);
        REQUIRE(test.Call<std::uint32_t>(EventsType, "BeginColliderA", 0) == ground);
        REQUIRE(test.Call<std::uint32_t>(EventsType, "BeginColliderB", 0) == box);
    }
    {
        INFO("C# should read the very point, normal and closing speed C++ read");
        REQUIRE(test.Call<float>(EventsType, "BeginPoint", 0, 0) == seen.point.x);
        REQUIRE(test.Call<float>(EventsType, "BeginPoint", 0, 1) == seen.point.y);
        REQUIRE(test.Call<float>(EventsType, "BeginPoint", 0, 2) == seen.point.z);
        REQUIRE(test.Call<float>(EventsType, "BeginNormal", 0, 0) == seen.normal.x);
        REQUIRE(test.Call<float>(EventsType, "BeginNormal", 0, 1) == seen.normal.y);
        REQUIRE(test.Call<float>(EventsType, "BeginNormal", 0, 2) == seen.normal.z);
        REQUIRE(test.Call<float>(EventsType, "BeginRelativeSpeed", 0) == seen.relativeSpeed);
    }
}

TEST_CASE("A C# system reads one TriggerEnter and one TriggerExit for a ball falling through a trigger",
          "[physics][scripting][events]")
{
    PhysicsScriptingApp test;
    const std::uint32_t zone = test.Place(Math::Vec3(0.0f, 3.0f, 0.0f));
    test.Entities().GetEntity(zone).AddComponent<Collider>(Collider{BoxShape{Math::Vec3(1.0f, 0.5f, 1.0f)}});
    test.Entities().GetEntity(zone).AddComponent<Trigger>();
    const std::uint32_t ball = test.Place(Math::Vec3(0.0f, 6.0f, 0.0f));
    test.Entities().GetEntity(ball).AddComponent<RigidBody>();
    test.Entities().GetEntity(ball).AddComponent<Collider>(Collider{SphereShape{0.25f}});

    test.Run(90);

    {
        INFO("The ball passes through the zone, so C# should read one enter and one exit");
        REQUIRE(test.Call<int>(EventsType, "TriggerEnterCount") == 1);
        REQUIRE(test.Call<int>(EventsType, "TriggerExitCount") == 1);
    }
    {
        INFO("Each should name the zone as the trigger and the ball as the body and collider");
        REQUIRE(test.Call<std::uint32_t>(EventsType, "EnterTrigger", 0) == zone);
        REQUIRE(test.Call<std::uint32_t>(EventsType, "EnterOtherBody", 0) == ball);
        REQUIRE(test.Call<std::uint32_t>(EventsType, "EnterOtherCollider", 0) == ball);
        REQUIRE(test.Call<std::uint32_t>(EventsType, "ExitTrigger", 0) == zone);
        REQUIRE(test.Call<std::uint32_t>(EventsType, "ExitOtherBody", 0) == ball);
        REQUIRE(test.Call<std::uint32_t>(EventsType, "ExitOtherCollider", 0) == ball);
    }
}

TEST_CASE("C# disabling ContactEvents ends the contact on the next tick", "[physics][scripting][events]")
{
    PhysicsScriptingApp test;
    const std::uint32_t ground = AddGround(test);
    const std::uint32_t box = AddBody(test, Math::Vec3(0.0f, 1.0f, 0.0f));
    test.Entities().GetEntity(box).AddComponent<ContactEvents>();
    test.Run(60);
    REQUIRE(test.Call<int>(EventsType, "ContactBeginCount") == 1);

    test.Call<void>(EventsType, "SetContactEvents", box, false);
    test.Run(1);

    {
        INFO("Switching reporting off from C# should end the contact on the next tick, read by C#");
        REQUIRE(test.Call<int>(EventsType, "ContactEndCount") == 1);
    }
    {
        INFO("The end should name the ground and the box, in the order its begin did");
        REQUIRE(test.Call<std::uint32_t>(EventsType, "EndBodyA", 0) == ground);
        REQUIRE(test.Call<std::uint32_t>(EventsType, "EndBodyB", 0) == box);
        REQUIRE(test.Call<std::uint32_t>(EventsType, "EndColliderA", 0) == ground);
        REQUIRE(test.Call<std::uint32_t>(EventsType, "EndColliderB", 0) == box);
    }
}

TEST_CASE("C# disabling a trigger exits what is inside on the next tick, and enabling it enters again",
          "[physics][scripting][events]")
{
    PhysicsScriptingApp test;
    AddGround(test);
    const std::uint32_t zone = test.Place(Math::Vec3(0.0f, 1.5f, 0.0f));
    test.Entities().GetEntity(zone).AddComponent<Collider>(Collider{BoxShape{Math::Vec3(2.0f, 1.4f, 2.0f)}});
    test.Entities().GetEntity(zone).AddComponent<Trigger>();
    const std::uint32_t box = AddBody(test, Math::Vec3(0.0f, 1.0f, 0.0f));
    test.Run(60);
    REQUIRE(test.Call<int>(EventsType, "TriggerEnterCount") == 1);

    test.Call<void>(EventsType, "SetTrigger", zone, false);
    test.Run(1);
    {
        INFO("Disabled from C#, the zone should report the box leaving on the next tick");
        REQUIRE(test.Call<int>(EventsType, "TriggerExitCount") == 1);
        REQUIRE(test.Call<std::uint32_t>(EventsType, "ExitOtherBody", 0) == box);
    }

    test.Call<void>(EventsType, "SetTrigger", zone, true);
    test.Run(1);
    {
        INFO("Enabled again from C#, the box inside should enter again on the next tick");
        REQUIRE(test.Call<int>(EventsType, "TriggerEnterCount") == 2);
    }
}

namespace
{
    // ContactBegin as a native side that drifted from the C# mirror would map it: RelativeSpeed has the wrong type.
    struct DriftedContactBegin
    {
        Detail::EntityLinkView bodyA;
        Detail::EntityLinkView bodyB;
        Detail::EntityLinkView colliderA;
        Detail::EntityLinkView colliderB;
        Math::Vec3 point;
        Math::Vec3 normal;
        std::int32_t relativeSpeed;
    };
} // namespace

template <>
struct Iris::StructMapping<DriftedContactBegin>
{
    static constexpr std::string_view typeName = "Fenrir.Physics.ContactBegin";
    static constexpr auto fields = Iris::StructFields(
        IRIS_FIELD_AS(DriftedContactBegin, bodyA, "BodyA"), IRIS_FIELD_AS(DriftedContactBegin, bodyB, "BodyB"),
        IRIS_FIELD_AS(DriftedContactBegin, colliderA, "ColliderA"),
        IRIS_FIELD_AS(DriftedContactBegin, colliderB, "ColliderB"), IRIS_FIELD_AS(DriftedContactBegin, point, "Point"),
        IRIS_FIELD_AS(DriftedContactBegin, normal, "Normal"),
        IRIS_FIELD_AS(DriftedContactBegin, relativeSpeed, "RelativeSpeed"));
};

TEST_CASE("The physics event mirrors are checked as scripts load, and a drifted one fails the load naming the field",
          "[physics][scripting][events]")
{
    Testing::HeadlessApp headless;
    Physics physics = Physics::Install(headless.Get(), StepPosesOnly());
    ScriptingConfig config = PhysicsScriptingApp::Config(physics);
    std::vector<EventRegistration>& events = config.extensions[0].events;
    {
        INFO("Physics's extension should share the contact and trigger events and JointBroken with C#");
        REQUIRE(events.size() == 5);
    }
    const auto contactBegin = std::find_if(events.begin(), events.end(), [](const EventRegistration& event) {
        return event.name == "Fenrir.Physics.ContactBegin";
    });
    REQUIRE(contactBegin != events.end());
    *contactBegin = MakeEvent<ContactBegin, DriftedContactBegin>("Fenrir.Physics.ContactBegin");

    Scripting scripting = Scripting::Install(headless.Get(), config);

    {
        INFO("A ContactBegin mirror that disagrees with the native layout left scripts loaded");
        REQUIRE_FALSE(scripting.HasScripts());
    }
    {
        INFO("The failed load did not name the event and the field");
        REQUIRE(headless.Logger().Contains(Testing::LogLevel::Error, "Fenrir.Physics.ContactBegin"));
        REQUIRE(headless.Logger().Contains(Testing::LogLevel::Error, "'RelativeSpeed'"));
    }
}

namespace
{
    constexpr const char* JointsType = "Fenrir.PhysicsScriptFixture.Joints";

    bool Same(const JointSpring& a, const JointSpring& b)
    {
        return a.frequency == b.frequency && a.damping == b.damping;
    }

    bool Same(const JointLimits& a, const JointLimits& b)
    {
        return a.enabled == b.enabled && a.min == b.min && a.max == b.max && Same(a.spring, b.spring);
    }

    bool Same(const JointMotor& a, const JointMotor& b)
    {
        return a.mode == b.mode && a.targetVelocity == b.targetVelocity && a.targetPosition == b.targetPosition &&
               a.maxForce == b.maxForce && Same(a.spring, b.spring);
    }

    bool Same(const SwingTwistMotor& a, const SwingTwistMotor& b)
    {
        return a.mode == b.mode && a.targetRotation == b.targetRotation &&
               a.targetAngularVelocity == b.targetAngularVelocity && a.maxTorque == b.maxTorque &&
               Same(a.spring, b.spring);
    }

    bool Same(const JointFrame& a, const JointFrame& b)
    {
        return a.position == b.position && a.rotation == b.rotation;
    }

    // Whether two kinds are the same kind with the same settings.
    bool Same(const JointKind& a, const JointKind& b)
    {
        if (a.Type() != b.Type())
        {
            return false;
        }
        switch (a.Type())
        {
        case JointType::Fixed:
        case JointType::Ball:
            return true;
        case JointType::Distance: {
            const auto& x = a.As<DistanceJoint>();
            const auto& y = b.As<DistanceJoint>();
            return x.minDistance == y.minDistance && x.maxDistance == y.maxDistance && Same(x.spring, y.spring);
        }
        case JointType::Hinge: {
            const auto& x = a.As<HingeJoint>();
            const auto& y = b.As<HingeJoint>();
            return Same(x.limits, y.limits) && Same(x.motor, y.motor) && x.friction == y.friction;
        }
        case JointType::Slider: {
            const auto& x = a.As<SliderJoint>();
            const auto& y = b.As<SliderJoint>();
            return Same(x.limits, y.limits) && Same(x.motor, y.motor) && x.friction == y.friction;
        }
        case JointType::SwingTwist: {
            const auto& x = a.As<SwingTwistJoint>();
            const auto& y = b.As<SwingTwistJoint>();
            return x.swingYLimit == y.swingYLimit && x.swingZLimit == y.swingZLimit && x.twistMin == y.twistMin &&
                   x.twistMax == y.twistMax && x.friction == y.friction && Same(x.motor, y.motor);
        }
        }
        return false;
    }

    const Joint& JointOn(PhysicsScriptingApp& test, std::uint32_t entity)
    {
        Entity holder = test.Entities().GetEntity(entity);
        INFO("The Joint C# added did not reach the entity");
        REQUIRE(holder.HasComponent<Joint>());
        return holder.GetComponent<Joint>();
    }

    // A static post at `position` and a dynamic door 1.5 along X, joined by a fixed joint at the gap, all made in C++.
    // Returns the joint entity.
    std::uint32_t AddFixedDoor(PhysicsScriptingApp& test, const Math::Vec3& position, std::uint32_t& door)
    {
        const std::uint32_t post = AddStaticBox(test, position);
        door = AddBody(test, position + Math::Vec3(1.5f, 0.0f, 0.0f));
        const Entity postEntity = test.Entities().GetEntity(post);
        const Entity doorEntity = test.Entities().GetEntity(door);
        const JointFrames frames =
            FrameFromWorld(postEntity, doorEntity, JointFrame{position + Math::Vec3(0.75f, 0.0f, 0.0f)});
        Entity joint = test.Entities().CreateEntity();
        joint.AddComponent<Joint>(
            Joint{.bodyA = postEntity, .bodyB = doorEntity, .frameA = frames.frameA, .frameB = frames.frameB});
        return joint.GetId();
    }
} // namespace

TEST_CASE("A new Joint from C# has C++'s defaults, for its own kind and every kind's new",
          "[physics][scripting][joints]")
{
    PhysicsScriptingApp test;
    const std::pair<int, JointKind> kinds[] = {
        {-1, JointKind()}, {0, FixedJoint{}},  {1, BallJoint{}},       {2, DistanceJoint{}},
        {3, HingeJoint{}}, {4, SliderJoint{}}, {5, SwingTwistJoint{}},
    };
    for (const auto& [number, kind] : kinds)
    {
        const std::uint32_t entity = test.Place(Math::Vec3(0.0f));
        test.Call<void>(JointsType, "AddNewJoint", entity, number);
        test.ApplyChanges();

        const Joint& joint = JointOn(test, entity);
        const Joint expected{};
        INFO("For C# kind number " << number << " (-1 is the Joint's own new kind)");
        {
            INFO("New, both links should be empty, as C++'s default Entity is");
            REQUIRE(joint.bodyA == Entity());
            REQUIRE(joint.bodyB == Entity());
        }
        {
            INFO("New, both frames should be the identity, as in C++");
            REQUIRE(Same(joint.frameA, expected.frameA));
            REQUIRE(Same(joint.frameB, expected.frameB));
        }
        {
            INFO("New, the joint should be enabled, not collide its bodies and never break, as in C++");
            REQUIRE(joint.enabled);
            REQUIRE_FALSE(joint.collideConnected);
            REQUIRE(joint.breakForce == std::numeric_limits<float>::infinity());
            REQUIRE(joint.breakTorque == std::numeric_limits<float>::infinity());
        }
        {
            INFO("The kind and every setting should be C++'s new one of that kind");
            REQUIRE(Same(joint.kind, kind));
        }
    }
}

TEST_CASE("A C# system makes two bodies and a hinge between them in one call, and the hinge holds after playback",
          "[physics][scripting][joints]")
{
    PhysicsScriptingApp test;
    const auto door = test.Call<std::uint32_t>(JointsType, "BuildHingedDoor", 0.0f, 5.0f, 0.0f);
    test.ApplyChanges();
    {
        Entity built = test.Entities().GetEntity(door);
        INFO("Playback did not make the door C# built");
        REQUIRE(built.HasComponent<RigidBody>());
    }

    test.Run(60);

    const Math::Vec3 at = test.Entities().GetEntity(door).GetComponent<Transform>().pos;
    INFO("Hinged about X to a post, the door should stay at 1.5, 5, 0 for a second instead of falling, but it is at "
         << at.x << ", " << at.y << ", " << at.z);
    REQUIRE(at.x == Approx(1.5f).margin(0.02));
    REQUIRE(at.y == Approx(5.0f).margin(0.02));
}

TEST_CASE("C# assigns a joint's kind and edits its motor through the hinge view, and physics follows",
          "[physics][scripting][joints]")
{
    PhysicsScriptingApp test;
    std::uint32_t door = 0;
    const std::uint32_t joint = AddFixedDoor(test, Math::Vec3(0.0f, 5.0f, 0.0f), door);
    test.Run(10);

    test.Call<void>(JointsType, "MakeMotorHinge", joint, 2.0f);
    {
        const Joint& written = JointOn(test, joint);
        INFO("The hinge C# assigned did not replace the fixed kind");
        REQUIRE(written.kind.Is<HingeJoint>());
        const JointMotor& motor = written.kind.As<HingeJoint>().motor;
        INFO("The motor should be driven at 2 rad/s, with C++'s other motor defaults");
        REQUIRE(motor.mode == JointMotorMode::Velocity);
        REQUIRE(motor.targetVelocity == 2.0f);
        REQUIRE(motor.maxForce == std::numeric_limits<float>::infinity());
        REQUIRE(Same(motor.spring, JointMotor{}.spring));
    }
    test.Run(30);
    {
        const float spin = test.Entities().GetEntity(door).GetComponent<RigidBody>().angularVelocity.x;
        INFO("The motor hinge C# assigned should turn the door at 2 rad/s about X, but it turns at " << spin);
        REQUIRE(spin == Approx(2.0f).margin(0.05));
    }

    test.Call<void>(JointsType, "SetHingeTarget", joint, -1.0f);
    {
        INFO("The target C# edited through the hinge view did not land in the component");
        REQUIRE(JointOn(test, joint).kind.As<HingeJoint>().motor.targetVelocity == -1.0f);
    }
    test.Run(30);
    {
        const float spin = test.Entities().GetEntity(door).GetComponent<RigidBody>().angularVelocity.x;
        INFO("After C# edited the target to -1 rad/s, the door should turn back at it, but it turns at " << spin);
        REQUIRE(spin == Approx(-1.0f).margin(0.05));
    }
    {
        const auto message = test.Call<std::optional<std::string>>(JointsType, "SliderViewMessage", joint);
        INFO("Reading a hinge as a slider from C# should throw, naming both kinds");
        REQUIRE(message.has_value());
        REQUIRE(*message == "The joint is a Hinge, not a Slider.");
    }
}

namespace
{
    // A body under a parent that is moved, turned about Z and scaled unevenly, itself moved and turned about Y.
    std::uint32_t BodyUnderTurnedScaledParent(PhysicsScriptingApp& test)
    {
        Entity parent = test.Entities().CreateEntity();
        parent.GetComponent<Transform>() =
            Transform(Math::Vec3(1.0f, 2.0f, 3.0f), Math::AngleAxis(0.5f, Math::Vec3(0.0f, 0.0f, 1.0f)),
                      Math::Vec3(2.0f, 1.0f, 0.5f));
        Entity body = test.Entities().CreateEntity();
        body.GetComponent<Transform>() =
            Transform(Math::Vec3(0.5f, 0.25f, 1.0f), Math::AngleAxis(0.3f, Math::Vec3(0.0f, 1.0f, 0.0f)),
                      Math::Vec3(1.0f, 1.5f, 1.0f));
        parent.AddChild(body);
        body.AddComponent<RigidBody>();
        body.AddComponent<Collider>();
        return body.GetId();
    }

    // Whether C#'s last FrameFromWorld gave exactly these frames.
    bool CSharpGave(PhysicsScriptingApp& test, const JointFrames& frames)
    {
        const JointFrame* both[] = {&frames.frameA, &frames.frameB};
        for (int frame = 0; frame < 2; ++frame)
        {
            const JointFrame& expected = *both[frame];
            const float values[] = {expected.position.x, expected.position.y, expected.position.z, expected.rotation.w,
                                    expected.rotation.x, expected.rotation.y, expected.rotation.z};
            for (int value = 0; value < 7; ++value)
            {
                if (test.Call<float>(JointsType, "LastFrame", frame, value) != values[value])
                {
                    return false;
                }
            }
        }
        return true;
    }
} // namespace

TEST_CASE("FrameFromWorld from C# matches C++ under a scaled, turned parent, and its world overload is an empty bodyB",
          "[physics][scripting][joints]")
{
    PhysicsScriptingApp test;
    const std::uint32_t body = BodyUnderTurnedScaledParent(test);
    const std::uint32_t other = AddBody(test, Math::Vec3(-2.0f, 4.0f, 1.0f));
    const JointFrame anchor{Math::Vec3(1.5f, 2.5f, 3.5f), Math::AngleAxis(0.7f, Math::Vec3(1.0f, 0.0f, 0.0f))};
    const auto [x, y, z] = std::tuple(anchor.position.x, anchor.position.y, anchor.position.z);
    const auto [w, qx, qy, qz] = std::tuple(anchor.rotation.w, anchor.rotation.x, anchor.rotation.y, anchor.rotation.z);
    const Entity bodyEntity = test.Entities().GetEntity(body);

    test.Call<void>(JointsType, "FrameFromWorld", body, other, x, y, z, w, qx, qy, qz);
    {
        const JointFrames cpp = FrameFromWorld(bodyEntity, test.Entities().GetEntity(other), anchor);
        INFO("C# should get the very frames C++ computes for the body under its scaled, turned parent");
        REQUIRE(CSharpGave(test, cpp));
    }

    test.Call<void>(JointsType, "FrameFromWorldToWorld", body, x, y, z, w, qx, qy, qz);
    {
        const JointFrames cpp = FrameFromWorld(bodyEntity, Entity(), anchor);
        INFO("C#'s world overload should get C++'s frames with an empty bodyB: frameB the anchor itself");
        REQUIRE(CSharpGave(test, cpp));
        REQUIRE(test.Call<float>(JointsType, "LastFrame", 1, 0) == anchor.position.x);
    }

    test.Entities().DestroyEntity(other);
    test.Call<void>(JointsType, "FrameFromWorld", body, other, x, y, z, w, qx, qy, qz);
    {
        INFO("A destroyed bodyB is not the world: C# should get identity frames, as C++ does");
        REQUIRE(CSharpGave(test, JointFrames{}));
    }
}

TEST_CASE("A default(Joint) from C# names no body of this scene, so it holds nothing and does not crash",
          "[physics][scripting][joints]")
{
    PhysicsScriptingApp test;
    const std::uint32_t box = AddBody(test, Math::Vec3(0.0f, 10.0f, 0.0f));
    const std::uint32_t holder = test.Place(Math::Vec3(0.0f));
    test.Call<void>(JointsType, "AddDefaultJoint", holder);
    test.ApplyChanges();
    {
        const Joint& joint = JointOn(test, holder);
        INFO("default(Joint) is all zero bytes: links to entity 0 with no scene, which are not alive");
        REQUIRE(joint.bodyA.GetId() == 0);
        REQUIRE_FALSE(joint.bodyA.IsValid());
        REQUIRE_FALSE(joint.bodyB.IsValid());
    }

    test.Run(30);

    {
        const float height = test.Entities().GetEntity(box).GetComponent<Transform>().pos.y;
        INFO("The joint holds nothing, so the body should fall freely from 10, but it is at " << height);
        REQUIRE(height < 9.0f);
    }
    {
        const PhysicsWorld* world = PhysicsWorld::Of(test.headless.Get().GetActiveScene());
        REQUIRE(world != nullptr);
        const JointState state = world->GetJointState(test.Entities().GetEntity(holder));
        INFO("An inactive joint reads as the zeroed state");
        REQUIRE(state.force == Math::Vec3(0.0f));
        REQUIRE(state.position == 0.0f);
    }
    {
        INFO("Physics should warn once that the joint is inactive, not on every step it stays so");
        REQUIRE(WarningsAbout(test, "The Joint on entity " + std::to_string(holder) + " is inactive") == 1);
    }
}

namespace
{
    // JointFrame's own mapping is the extension's, so the drifted Joint below has a stand-in of its own for it.
    struct DriftedFrame
    {
        Math::Vec3 position;
        Math::Quat rotation;
    };

    // Joint as a native side that drifted from the C# mirror would map it: its kind is raw bytes, not the tagged union.
    struct DriftedJoint
    {
        Detail::EntityLinkView bodyA;
        Detail::EntityLinkView bodyB;
        DriftedFrame frameA;
        DriftedFrame frameB;
        std::uint8_t kind[sizeof(JointKind)];
        bool enabled;
        bool collideConnected;
        float breakForce;
        float breakTorque;
    };
} // namespace

template <>
struct Iris::StructMapping<DriftedFrame>
{
    static constexpr std::string_view typeName = "Fenrir.Physics.JointFrame";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(DriftedFrame, position, "Position"),
                                                      IRIS_FIELD_AS(DriftedFrame, rotation, "Rotation"));
};

template <>
struct Iris::StructMapping<DriftedJoint>
{
    static constexpr std::string_view typeName = "Fenrir.Physics.Joint";
    static constexpr auto fields = Iris::StructFields(
        IRIS_FIELD_AS(DriftedJoint, bodyA, "BodyA"), IRIS_FIELD_AS(DriftedJoint, bodyB, "BodyB"),
        IRIS_FIELD_AS(DriftedJoint, frameA, "FrameA"), IRIS_FIELD_AS(DriftedJoint, frameB, "FrameB"),
        IRIS_FIELD_AS(DriftedJoint, kind, "Kind"), IRIS_FIELD_AS(DriftedJoint, enabled, "Enabled"),
        IRIS_FIELD_AS(DriftedJoint, collideConnected, "CollideConnected"),
        IRIS_FIELD_AS(DriftedJoint, breakForce, "BreakForce"), IRIS_FIELD_AS(DriftedJoint, breakTorque, "BreakTorque"));
};

TEST_CASE("The Joint mirror is checked as scripts load, and a drifted one fails the load naming the field",
          "[physics][scripting][joints]")
{
    Testing::HeadlessApp headless;
    Physics physics = Physics::Install(headless.Get(), StepPosesOnly());
    ScriptingConfig config = PhysicsScriptingApp::Config(physics);
    std::vector<ComponentRegistration>& components = config.extensions[0].components;
    const auto joint = std::find_if(components.begin(), components.end(), [](const ComponentRegistration& component) {
        return component.name == "Fenrir.Physics.Joint";
    });
    {
        INFO("Physics's extension should share the Joint component with C#");
        REQUIRE(joint != components.end());
    }
    *joint = MakeComponent<Joint, DriftedJoint>("Fenrir.Physics.Joint");

    Scripting scripting = Scripting::Install(headless.Get(), config);

    {
        INFO("A Joint mirror that disagrees with the native layout left scripts loaded");
        REQUIRE_FALSE(scripting.HasScripts());
    }
    {
        INFO("The failed load did not name the component and the field");
        REQUIRE(headless.Logger().Contains(Testing::LogLevel::Error, "Fenrir.Physics.Joint"));
        REQUIRE(headless.Logger().Contains(Testing::LogLevel::Error, "'Kind'"));
    }
}

namespace
{
    // A JointState's values in the order the fixture's JointStateValue numbers them: force, torque, the relative
    // frame's position, its rotation as W, X, Y, Z, then position.
    std::vector<float> ValuesOf(const JointState& state)
    {
        const Math::Vec3& at = state.relative.position;
        const Math::Quat& turn = state.relative.rotation;
        return {state.force.x, state.force.y, state.force.z, state.torque.x, state.torque.y, state.torque.z, at.x, at.y,
                at.z,          turn.w,        turn.x,        turn.y,         turn.z,         state.position};
    }

    // The values of the JointState C# read last, in ValuesOf's order.
    std::vector<float> CSharpRead(PhysicsScriptingApp& test)
    {
        std::vector<float> values;
        for (int value = 0; value < 14; ++value)
        {
            values.push_back(test.Call<float>(JointsType, "JointStateValue", value));
        }
        return values;
    }

    // JointState as a native side that drifted from the C# mirror would map it: position has the wrong type.
    struct DriftedJointState
    {
        Math::Vec3 force;
        Math::Vec3 torque;
        DriftedFrame relative;
        std::int32_t position;
    };
} // namespace

template <>
struct Iris::StructMapping<DriftedJointState>
{
    static constexpr std::string_view typeName = "Fenrir.Physics.JointState";
    static constexpr auto fields = Iris::StructFields(
        IRIS_FIELD_AS(DriftedJointState, force, "Force"), IRIS_FIELD_AS(DriftedJointState, torque, "Torque"),
        IRIS_FIELD_AS(DriftedJointState, relative, "Relative"), IRIS_FIELD_AS(DriftedJointState, position, "Position"));
};

TEST_CASE("The JointState mirror matches its native layout, and a drifted one is refused naming the field",
          "[physics][scripting][joints]")
{
    PhysicsScriptingApp test;
    const Iris::Component* component = ScriptingProbe::Component(test.scripting);
    REQUIRE(component != nullptr);
    {
        const auto checked = component->ValidateStruct<JointState>();
        INFO("The C# JointState should match the native one: " << checked.diagnostic);
        REQUIRE(checked.Succeeded());
    }
    {
        const auto drifted = component->ValidateStruct<DriftedJointState>();
        INFO("A JointState mapping that disagrees with the C# mirror should be refused, naming the field");
        REQUIRE_FALSE(drifted.Succeeded());
        REQUIRE(drifted.diagnostic.find("'Position'") != std::string::npos);
    }
}

TEST_CASE("GetJointState from C# reads a hinge's angle, load and pose, as C++ does", "[physics][scripting][joints]")
{
    PhysicsScriptingApp test;
    std::uint32_t door = 0;
    const std::uint32_t joint = AddFixedDoor(test, Math::Vec3(0.0f, 5.0f, 0.0f), door);
    test.Run(1);
    test.Entities().GetEntity(joint).GetComponent<Joint>().kind = HingeJoint{
        .motor = JointMotor{.mode = JointMotorMode::Position, .targetPosition = 0.5f, .spring = {.frequency = 4.0f}}};

    test.Run(90);
    test.Call<void>(JointsType, "ReadJointState", joint);

    const PhysicsWorld* world = PhysicsWorld::Of(test.headless.Get().GetActiveScene());
    REQUIRE(world != nullptr);
    const JointState cpp = world->GetJointState(test.Entities().GetEntity(joint));
    {
        const float angle = test.Call<float>(JointsType, "JointStateValue", 13);
        INFO("The motor holds the door at 0.5 rad, so C# should read that angle, but it read " << angle);
        REQUIRE(angle == Approx(0.5f).margin(0.02));
    }
    {
        INFO("Holding the door up, the hinge pulls on it, so the load C# read should not be zero");
        REQUIRE(test.Call<float>(JointsType, "JointStateValue", 1) > 1.0f);
    }
    {
        INFO("C# should read the very force, torque, relative pose and angle C++ reads");
        REQUIRE(CSharpRead(test) == ValuesOf(cpp));
    }
}

TEST_CASE("GetJointState from C# reads zeroed with no world and for a non-joint, warning once in debug",
          "[physics][scripting][joints]")
{
    PhysicsScriptingApp test;
    const std::uint32_t plain = test.Place(Math::Vec3(0.0f));
    REQUIRE(PhysicsWorld::Of(test.headless.Get().GetActiveScene()) == nullptr);
    const std::vector<float> zeroed = ValuesOf(JointState{});

    test.Call<void>(JointsType, "ReadJointState", plain);
    {
        INFO("With no physics world, C# should read the zeroed state, without throwing");
        REQUIRE(CSharpRead(test) == zeroed);
    }

    AddStaticBox(test, Math::Vec3(0.0f, -5.0f, 0.0f));
    test.Run(1);
    REQUIRE(PhysicsWorld::Of(test.headless.Get().GetActiveScene()) != nullptr);
    test.Call<void>(JointsType, "ReadJointState", plain);
    {
        INFO("On an entity with no Joint, C# should read the zeroed state, without throwing");
        REQUIRE(CSharpRead(test) == zeroed);
    }
    test.Entities().DestroyEntity(plain);
    test.Call<void>(JointsType, "ReadJointState", plain);
    {
        INFO("On an entity no longer alive, likewise");
        REQUIRE(CSharpRead(test) == zeroed);
    }
#ifndef NDEBUG
    {
        test.Call<void>(JointsType, "ReadJointState", plain);
        INFO("A debug build should warn once for no world and once for no Joint, not on every call");
        REQUIRE(WarningsAbout(test, "GetJointState: the active scene has no physics world") == 1);
        REQUIRE(WarningsAbout(test, "GetJointState: the entity has no Joint") == 1);
    }
#endif
}

namespace
{
    // A unit box hanging 0.5 beneath `hook` from a ball joint to the world, made in C++. Returns the joint entity.
    std::uint32_t AddHangingBox(PhysicsScriptingApp& test, const Math::Vec3& hook, std::uint32_t& box)
    {
        box = AddBody(test, hook - Math::Vec3(0.0f, 0.5f, 0.0f));
        const Entity boxEntity = test.Entities().GetEntity(box);
        const JointFrames frames = FrameFromWorld(boxEntity, Entity(), JointFrame{hook});
        Entity joint = test.Entities().CreateEntity();
        joint.AddComponent<Joint>(
            Joint{.bodyA = boxEntity, .frameA = frames.frameA, .frameB = frames.frameB, .kind = BallJoint{}});
        return joint.GetId();
    }
} // namespace

TEST_CASE("A C# system reads one JointBroken when a joint breaks, and writing Enabled = true from C# repairs it",
          "[physics][scripting][joints][events]")
{
    PhysicsScriptingApp test;
    const Math::Vec3 hook(0.0f, 10.0f, 0.0f);
    std::uint32_t box = 0;
    const std::uint32_t joint = AddHangingBox(test, hook, box);
    CppSees<JointBroken> cpp;
    cpp.Run(test, 30);
    REQUIRE(cpp.events.empty());

    // The box weighs 9.81 N, over this threshold.
    test.Entities().GetEntity(joint).GetComponent<Joint>().breakForce = 5.0f;
    cpp.Run(test, 2);

    REQUIRE(cpp.events.size() == 1);
    const JointBroken& seen = cpp.events[0];
    {
        INFO("The joint breaks once, so C# should read one JointBroken");
        REQUIRE(test.Call<int>(EventsType, "JointBrokenCount") == 1);
    }
    {
        INFO("C# should name the joint and the box, and read the world as an empty BodyB");
        REQUIRE(test.Call<std::uint32_t>(EventsType, "BrokenJoint", 0) == joint);
        REQUIRE(test.Call<std::uint32_t>(EventsType, "BrokenBodyA", 0) == box);
        REQUIRE(test.Call<bool>(EventsType, "BrokenBodyBIsWorld", 0));
    }
    {
        INFO("C# should read the very load C++ read, which broke the joint");
        REQUIRE(test.Call<float>(EventsType, "BrokenForce", 0, 0) == seen.force.x);
        REQUIRE(test.Call<float>(EventsType, "BrokenForce", 0, 1) == seen.force.y);
        REQUIRE(test.Call<float>(EventsType, "BrokenForce", 0, 2) == seen.force.z);
        REQUIRE(test.Call<float>(EventsType, "BrokenTorque", 0, 0) == seen.torque.x);
        REQUIRE(test.Call<float>(EventsType, "BrokenTorque", 0, 1) == seen.torque.y);
        REQUIRE(test.Call<float>(EventsType, "BrokenTorque", 0, 2) == seen.torque.z);
        REQUIRE(-test.Call<float>(EventsType, "BrokenForce", 0, 1) > 5.0f);
    }
    {
        INFO("C# reads the component in place, so it should see physics's write of Enabled = false");
        REQUIRE_FALSE(test.Call<bool>(JointsType, "IsEnabled", joint));
    }

    // A threshold the box's weight stays under, so the repaired joint holds.
    test.Entities().GetEntity(joint).GetComponent<Joint>().breakForce = 50.0f;
    test.Call<void>(JointsType, "Repair", joint);
    REQUIRE(JointOn(test, joint).enabled);
    cpp.Run(test, 120);

    {
        const Math::Vec3 at = test.Entities().GetEntity(box).GetComponent<Transform>().pos;
        INFO("Repaired from C#, the joint should pull the box back beneath the hook, but it is at " << at.y);
        REQUIRE(at.y == Approx(hook.y - 0.5f).margin(0.05));
        REQUIRE(at.x == Approx(0.0f).margin(0.05));
    }
    {
        INFO("Held under its new threshold, the repaired joint should not break again");
        REQUIRE(cpp.events.size() == 1);
        REQUIRE(test.Call<int>(EventsType, "JointBrokenCount") == 1);
        REQUIRE(test.Call<bool>(JointsType, "IsEnabled", joint));
    }
}

TEST_CASE("The QueryFilter, MoveCastHit and PenetrationHit mirrors match their native layouts",
          "[physics][scripting][motion]")
{
    PhysicsScriptingApp test;
    const Iris::Component* component = ScriptingProbe::Component(test.scripting);
    REQUIRE(component != nullptr);
    {
        const auto checked = component->ValidateStruct<Detail::ScriptQueryFilter>();
        INFO("The C# QueryFilter, Layers included, should match the native mirror: " << checked.diagnostic);
        REQUIRE(checked.Succeeded());
    }
    {
        const auto checked = component->ValidateStruct<Detail::ScriptMoveCastHit>();
        INFO("The C# MoveCastHit should match the native mirror: " << checked.diagnostic);
        REQUIRE(checked.Succeeded());
    }
    {
        const auto checked = component->ValidateStruct<Detail::ScriptPenetrationHit>();
        INFO("The C# PenetrationHit should match the native mirror: " << checked.diagnostic);
        REQUIRE(checked.Succeeded());
    }
}

namespace
{
    constexpr const char* MotionTestsType = "Fenrir.PhysicsScriptFixture.Motion";

    Transform At(const Math::Vec3& position)
    {
        return Transform(position, Identity, Math::Vec3(1.0f));
    }

    // The point, normal and distance (or depth) of the hit C# read last, as x, y and z of each, then the scalar.
    std::vector<float> CSharpHitValues(PhysicsScriptingApp& test)
    {
        std::vector<float> values;
        for (int value = 0; value < 7; ++value)
        {
            values.push_back(test.Call<float>(MotionTestsType, "HitValue", value));
        }
        return values;
    }

    std::vector<float> ValuesOf(const Math::Vec3& point, const Math::Vec3& normal, float scalar)
    {
        return {point.x, point.y, point.z, normal.x, normal.y, normal.z, scalar};
    }
} // namespace

TEST_CASE("MoveCast from C# stops a box short of a wall exactly as C++ does", "[physics][scripting][motion]")
{
    PhysicsScriptingApp test;
    const std::uint32_t wall = AddStaticBox(test, Math::Vec3(5.0f, 0.0f, 0.0f));
    test.Run(1);
    const PhysicsWorld* world = PhysicsWorld::Of(test.headless.Get().GetActiveScene());
    REQUIRE(world != nullptr);

    const std::optional<MoveCastHit> cpp =
        world->MoveCast(BoxShape{}, At(Math::Vec3(0.0f)), Math::Vec3(10.0f, 0.0f, 0.0f));
    REQUIRE(cpp.has_value());
    {
        INFO("A unit box moved 10 m along +x from C# was not blocked by the wall at x = 5");
        REQUIRE(test.Call<bool>(MotionTestsType, "MoveBox", 0.0f, 0.0f, 0.0f, 10.0f, 0.0f, 0.0f, NoEntity, 0u));
    }
    {
        INFO("C# should name the wall as body and collider");
        REQUIRE(test.Call<std::uint32_t>(MotionTestsType, "HitBody") == wall);
        REQUIRE(test.Call<std::uint32_t>(MotionTestsType, "HitCollider") == wall);
    }
    {
        INFO("C# should read the very point, normal and safe distance C++ reads (about 3.98 m, short of the wall)");
        REQUIRE(CSharpHitValues(test) == ValuesOf(cpp->point, cpp->normal, cpp->distance));
        REQUIRE(test.Call<float>(MotionTestsType, "HitValue", 6) == Approx(3.98f).margin(1e-3f));
    }
    {
        INFO("A motion clear of every body should find nothing from C#");
        REQUIRE_FALSE(test.Call<bool>(MotionTestsType, "MoveBox", 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 3.0f, NoEntity, 0u));
    }
}

TEST_CASE("Penetrations from C# lists what a box overlaps, deepest first, exactly as C++ does",
          "[physics][scripting][motion]")
{
    PhysicsScriptingApp test;
    const std::uint32_t ground = AddGround(test);
    // Its face at x = 0.3.
    const std::uint32_t wall = AddStaticBox(test, Math::Vec3(0.8f, 0.0f, 0.0f));
    test.Run(1);
    const PhysicsWorld* world = PhysicsWorld::Of(test.headless.Get().GetActiveScene());
    REQUIRE(world != nullptr);

    // A unit box 0.1 m down into the ground and 0.2 m into the wall.
    const Transform sunk = At(Math::Vec3(0.0f, 0.4f, 0.0f));
    std::array<PenetrationHit, 4> cpp;
    REQUIRE(world->Penetrations(BoxShape{}, sunk, cpp) == 2);
    {
        INFO("From C#, the box should overlap the ground and the wall");
        REQUIRE(test.Call<int>(MotionTestsType, "PenetrateBox", 0.0f, 0.4f, 0.0f, 4, NoEntity, 0u) == 2);
    }
    {
        INFO("C# should list the wall, 0.2 m deep, before the ground, 0.1 m deep, as C++ does");
        REQUIRE(test.Call<std::uint32_t>(MotionTestsType, "PenetrationBody", 0) == wall);
        REQUIRE(test.Call<std::uint32_t>(MotionTestsType, "PenetrationCollider", 0) == wall);
        REQUIRE(test.Call<std::uint32_t>(MotionTestsType, "PenetrationBody", 1) == ground);
    }
    for (std::size_t index = 0; index < 2; ++index)
    {
        test.Call<void>(MotionTestsType, "ReadPenetration", static_cast<int>(index));
        INFO("C# should read the very point, way out and depth C++ reads for overlap " << index);
        REQUIRE(CSharpHitValues(test) == ValuesOf(cpp[index].point, cpp[index].normal, cpp[index].depth));
    }
    {
        INFO("A span of one should hold the deepest overlap, the wall");
        REQUIRE(test.Call<int>(MotionTestsType, "PenetrateBox", 0.0f, 0.4f, 0.0f, 1, NoEntity, 0u) == 1);
        REQUIRE(test.Call<std::uint32_t>(MotionTestsType, "PenetrationBody", 0) == wall);
    }
    {
        INFO("A box clear of every body should overlap nothing from C#");
        REQUIRE(test.Call<int>(MotionTestsType, "PenetrateBox", 0.0f, 3.0f, 0.0f, 4, NoEntity, 0u) == 0);
    }
}

TEST_CASE("Depenetrate from C# moves a box out of the ground exactly as C++ does", "[physics][scripting][motion]")
{
    PhysicsScriptingApp test;
    AddGround(test);
    test.Run(1);
    const PhysicsWorld* world = PhysicsWorld::Of(test.headless.Get().GetActiveScene());
    REQUIRE(world != nullptr);

    // A unit box 0.1 m down into the ground.
    const Math::Vec3 cpp = world->Depenetrate(BoxShape{}, At(Math::Vec3(0.0f, 0.4f, 0.0f)));
    test.Call<void>(MotionTestsType, "DepenetrateBox", 0.0f, 0.4f, 0.0f, NoEntity, 0u);
    {
        const std::vector<float> offset = {test.Call<float>(MotionTestsType, "Offset", 0),
                                           test.Call<float>(MotionTestsType, "Offset", 1),
                                           test.Call<float>(MotionTestsType, "Offset", 2)};
        INFO("C# should read the very offset C++ reads: up 0.1 m and the skin, but it read ("
             << offset[0] << ", " << offset[1] << ", " << offset[2] << ")");
        REQUIRE(offset == std::vector<float>{cpp.x, cpp.y, cpp.z});
        REQUIRE(offset[1] == Approx(0.12f).margin(1e-3f));
    }
    test.Call<void>(MotionTestsType, "DepenetrateBox", 0.0f, 3.0f, 0.0f, NoEntity, 0u);
    {
        INFO("A box clear of every body should not be moved from C#");
        REQUIRE(test.Call<float>(MotionTestsType, "Offset", 1) == 0.0f);
    }
}

TEST_CASE("Motion tests from C# filtered like a body pass what that body would pass, and Layers 0 finds every body",
          "[physics][scripting][motion][layers]")
{
    PhysicsScriptingApp test;
    // Debris in layer 1 that collides only with layer 1, then a wall in layer 0.
    const std::uint32_t debris = AddStaticBox(test, Math::Vec3(3.0f, 0.0f, 0.0f));
    test.Entities().GetEntity(debris).AddComponent<CollisionFilter>(
        CollisionFilter{.layers = 1u << 1, .mask = 1u << 1});
    const std::uint32_t wall = AddStaticBox(test, Math::Vec3(6.0f, 0.0f, 0.0f));
    const std::uint32_t mover = AddStaticBox(test, Math::Vec3(0.0f));
    test.Run(1);
    // The mover's filter: a new CollisionFilter's layer 0.
    const std::uint32_t moverLayers = CollisionFilter{}.layers;

    {
        INFO("A box filtered like a body in layer 0 should pass the debris, which does not collide with layer 0, and "
             "its own body, and stop at the wall");
        REQUIRE(test.Call<bool>(MotionTestsType, "MoveBox", 0.0f, 0.0f, 0.0f, 10.0f, 0.0f, 0.0f, mover, moverLayers));
        REQUIRE(test.Call<std::uint32_t>(MotionTestsType, "HitBody") == wall);
    }
    {
        INFO("With Layers 0, the debris's mask is ignored, so it blocks the box");
        REQUIRE(test.Call<bool>(MotionTestsType, "MoveBox", 0.0f, 0.0f, 0.0f, 10.0f, 0.0f, 0.0f, mover, 0u));
        REQUIRE(test.Call<std::uint32_t>(MotionTestsType, "HitBody") == debris);
    }
    {
        INFO("A box inside the debris, filtered like the mover, should overlap nothing and need no offset");
        REQUIRE(test.Call<int>(MotionTestsType, "PenetrateBox", 3.0f, 0.0f, 0.0f, 4, mover, moverLayers) == 0);
        test.Call<void>(MotionTestsType, "DepenetrateBox", 3.0f, 0.2f, 0.0f, mover, moverLayers);
        REQUIRE(test.Call<float>(MotionTestsType, "Offset", 1) == 0.0f);
    }
    {
        INFO("With Layers 0, the same box should overlap the debris and be moved out of it");
        REQUIRE(test.Call<int>(MotionTestsType, "PenetrateBox", 3.0f, 0.0f, 0.0f, 4, mover, 0u) == 1);
        REQUIRE(test.Call<std::uint32_t>(MotionTestsType, "PenetrationBody", 0) == debris);
        test.Call<void>(MotionTestsType, "DepenetrateBox", 3.0f, 0.2f, 0.0f, mover, 0u);
        REQUIRE(test.Call<float>(MotionTestsType, "Offset", 1) > 0.0f);
    }
    {
        INFO("The foundation's queries take Layers too: a ray along +x in layer 0 should pass the debris");
        REQUIRE(test.Call<std::uint32_t>(MotionTestsType, "RaycastEast", mover, moverLayers) == wall);
        REQUIRE(test.Call<std::uint32_t>(MotionTestsType, "RaycastEast", mover, 0u) == debris);
    }
}

TEST_CASE("With no physics world, the motion tests from C# find nothing, warning once each in debug",
          "[physics][scripting][motion]")
{
    PhysicsScriptingApp test;
    // No frame has run with a body in the scene, so there is no physics world.
    REQUIRE(PhysicsWorld::Of(test.headless.Get().GetActiveScene()) == nullptr);

    for (int call = 0; call < 2; ++call)
    {
        INFO("With no world, MoveCast should return false, Penetrations 0 and Depenetrate zero, without throwing");
        REQUIRE_FALSE(test.Call<bool>(MotionTestsType, "MoveBox", 0.0f, 0.0f, 0.0f, 10.0f, 0.0f, 0.0f, NoEntity, 0u));
        REQUIRE(test.Call<int>(MotionTestsType, "PenetrateBox", 0.0f, 0.0f, 0.0f, 4, NoEntity, 0u) == 0);
        test.Call<void>(MotionTestsType, "DepenetrateBox", 0.0f, 0.0f, 0.0f, NoEntity, 0u);
        REQUIRE(test.Call<float>(MotionTestsType, "Offset", 0) == 0.0f);
        REQUIRE(test.Call<float>(MotionTestsType, "Offset", 1) == 0.0f);
        REQUIRE(test.Call<float>(MotionTestsType, "Offset", 2) == 0.0f);
    }
#ifndef NDEBUG
    {
        INFO("A debug build should warn once for each, not on every call, that it had no world");
        REQUIRE(WarningsAbout(test, "MoveCast: the active scene has no physics world") == 1);
        REQUIRE(WarningsAbout(test, "Penetrations: the active scene has no physics world") == 1);
        REQUIRE(WarningsAbout(test, "Depenetrate: the active scene has no physics world") == 1);
    }
#endif
}

namespace
{
    constexpr const char* CharactersType = "Fenrir.PhysicsScriptFixture.Characters";

    const CharacterBody& CharacterOn(PhysicsScriptingApp& test, std::uint32_t entity)
    {
        Entity holder = test.Entities().GetEntity(entity);
        INFO("The CharacterBody C# added did not reach the entity");
        REQUIRE(holder.HasComponent<CharacterBody>());
        return holder.GetComponent<CharacterBody>();
    }

    // Whether `state` is CharacterState{}, field by field.
    bool IsNew(const CharacterState& state)
    {
        const CharacterState fresh{};
        return state.ground == fresh.ground && state.groundBody == fresh.groundBody &&
               state.groundCollider == fresh.groundCollider && state.groundPoint == fresh.groundPoint &&
               state.groundNormal == fresh.groundNormal && state.groundVelocity == fresh.groundVelocity &&
               state.groundAngularVelocity == fresh.groundAngularVelocity &&
               state.movedVelocity == fresh.movedVelocity && state.carriedVelocity == fresh.carriedVelocity &&
               state.onWall == fresh.onWall && state.onCeiling == fresh.onCeiling &&
               state.shapeBlocked == fresh.shapeBlocked;
    }
} // namespace

TEST_CASE("A new CharacterBody from C# has C++'s defaults, its ground links empty", "[physics][scripting][characters]")
{
    PhysicsScriptingApp test;
    const std::uint32_t entity = test.Place(Math::Vec3(0.0f));
    test.Call<void>(CharactersType, "AddNewCharacter", entity);
    test.ApplyChanges();

    const CharacterBody& body = CharacterOn(test, entity);
    const CharacterBody expected{};
    {
        INFO("New, the settings should be C++'s: still, 45 degrees, 0.4 m steps, 0.5 m snap, 100 N, 70 kg");
        REQUIRE(body.velocity == expected.velocity);
        REQUIRE(body.maxSlope == expected.maxSlope);
        REQUIRE(body.stepHeight == expected.stepHeight);
        REQUIRE(body.floorSnap == expected.floorSnap);
        REQUIRE(body.pushForce == expected.pushForce);
        REQUIRE(body.mass == expected.mass);
    }
    {
        INFO("New, the character should stop on slopes, interpolate and be enabled, as in C++");
        REQUIRE(body.stopOnSlope == expected.stopOnSlope);
        REQUIRE(body.interpolate == expected.interpolate);
        REQUIRE(body.enabled == expected.enabled);
    }
    {
        INFO("New, the state should be CharacterState{}: in the air, both ground links empty Entities, all zero");
        REQUIRE(body.state.groundBody == Entity());
        REQUIRE(body.state.groundCollider == Entity());
        REQUIRE(IsNew(body.state));
    }
}

namespace
{
    // CharacterBody as a native side that drifted from the C# mirror would map it: its state is raw bytes.
    struct DriftedCharacterBody
    {
        Math::Vec3 velocity;
        float maxSlope;
        float stepHeight;
        float floorSnap;
        float pushForce;
        float mass;
        bool stopOnSlope;
        bool interpolate;
        bool enabled;
        alignas(CharacterState) std::uint8_t state[sizeof(CharacterState)];
    };
} // namespace

template <>
struct Iris::StructMapping<DriftedCharacterBody>
{
    using Drifted = DriftedCharacterBody;
    static constexpr std::string_view typeName = "Fenrir.Physics.CharacterBody";
    static constexpr auto fields = Iris::StructFields(
        IRIS_FIELD_AS(Drifted, velocity, "Velocity"), IRIS_FIELD_AS(Drifted, maxSlope, "MaxSlope"),
        IRIS_FIELD_AS(Drifted, stepHeight, "StepHeight"), IRIS_FIELD_AS(Drifted, floorSnap, "FloorSnap"),
        IRIS_FIELD_AS(Drifted, pushForce, "PushForce"), IRIS_FIELD_AS(Drifted, mass, "Mass"),
        IRIS_FIELD_AS(Drifted, stopOnSlope, "StopOnSlope"), IRIS_FIELD_AS(Drifted, interpolate, "Interpolate"),
        IRIS_FIELD_AS(Drifted, enabled, "Enabled"), IRIS_FIELD_AS(Drifted, state, "State"));
};

TEST_CASE("The CharacterBody mirror is checked as scripts load, and a drifted one fails the load naming the field",
          "[physics][scripting][characters]")
{
    Testing::HeadlessApp headless;
    Physics physics = Physics::Install(headless.Get(), StepPosesOnly());
    ScriptingConfig config = PhysicsScriptingApp::Config(physics);
    std::vector<ComponentRegistration>& components = config.extensions[0].components;
    const auto character =
        std::find_if(components.begin(), components.end(), [](const ComponentRegistration& component) {
            return component.name == "Fenrir.Physics.CharacterBody";
        });
    {
        INFO("Physics's extension should share the CharacterBody component with C#");
        REQUIRE(character != components.end());
    }
    *character = MakeComponent<CharacterBody, DriftedCharacterBody>("Fenrir.Physics.CharacterBody");

    Scripting scripting = Scripting::Install(headless.Get(), config);

    {
        INFO("A CharacterBody mirror that disagrees with the native layout left scripts loaded");
        REQUIRE_FALSE(scripting.HasScripts());
    }
    {
        INFO("The failed load did not name the component and the field");
        REQUIRE(headless.Logger().Contains(Testing::LogLevel::Error, "Fenrir.Physics.CharacterBody"));
        REQUIRE(headless.Logger().Contains(Testing::LogLevel::Error, "'State'"));
    }
}

TEST_CASE("A C# system makes a character, adds gravity and lands it, then walks it by writing Velocity",
          "[physics][scripting][characters]")
{
    PhysicsScriptingApp test;
    const std::uint32_t ground = AddGround(test);
    const auto character = test.Call<std::uint32_t>(CharactersType, "MakeCharacter", 0.0f, 2.0f, 0.0f);
    test.ApplyChanges();

    test.Run(90);

    {
        INFO("Falling by the gravity the C# system adds, the character should land and read OnGround in C#");
        REQUIRE(test.Call<int>(CharactersType, "Ground", character) == static_cast<int>(GroundState::OnGround));
        REQUIRE(test.Call<std::uint32_t>(CharactersType, "GroundBody", character) == ground);
    }
    {
        const float height = test.Call<float>(CharactersType, "Height", character);
        INFO("A capsule 0.6 + 0.3 from its centre to its feet should stand at 0.9 on the ground, but it is at "
             << height);
        REQUIRE(height == Approx(0.9f).margin(0.05));
    }
    {
        const CharacterState& state = CharacterOn(test, character).state;
        INFO("C++ should read the same state C# read");
        REQUIRE(state.ground == GroundState::OnGround);
        REQUIRE(state.groundBody.GetId() == ground);
    }

    test.Call<void>(CharactersType, "SetWalk", 2.0f, 0.0f);
    test.Run(30);

    const float east = test.Call<float>(CharactersType, "East", character);
    INFO("Walking at 2 m/s for half a second should move the character about 1 m along x, but it is at " << east);
    REQUIRE(east == Approx(1.0f).margin(0.1));
    REQUIRE(test.Call<int>(CharactersType, "Ground", character) == static_cast<int>(GroundState::OnGround));
}

TEST_CASE("A C# crouch writes the Collider once, a stand-up under a box is blocked, and it stands once the box is gone",
          "[physics][scripting][characters]")
{
    PhysicsScriptingApp test;
    AddGround(test);
    const auto character = test.Call<std::uint32_t>(CharactersType, "MakeCharacter", 0.0f, 1.0f, 0.0f);
    test.ApplyChanges();
    test.Run(60);
    REQUIRE(test.Call<int>(CharactersType, "Ground", character) == static_cast<int>(GroundState::OnGround));

    // Crouch to 1 m tall, once.
    test.Call<void>(CharactersType, "SetCapsule", character, 0.2f);
    test.Run(10);
    {
        const float height = test.Call<float>(CharactersType, "Height", character);
        INFO("Crouched on ground the feet stay down, so the centre should drop to 0.5, but it is at " << height);
        REQUIRE(height == Approx(0.5f).margin(0.05));
        REQUIRE_FALSE(test.Call<bool>(CharactersType, "ShapeBlocked", character));
    }

    // A static slab whose underside is at 1.3: room to crouch (1 m), none to stand (1.8 m).
    Entity slab = test.Entities().GetEntity(test.Place(Math::Vec3(0.0f, 1.55f, 0.0f)));
    slab.AddComponent<Collider>(Collider{BoxShape{Math::Vec3(1.0f, 0.25f, 1.0f)}});
    test.Run(2);
    // Stand up, once.
    test.Call<void>(CharactersType, "SetCapsule", character, 0.6f);
    test.Run(10);
    {
        INFO("Standing up under the slab does not fit, so C# should read ShapeBlocked and the crouched height");
        REQUIRE(test.Call<bool>(CharactersType, "ShapeBlocked", character));
        REQUIRE(test.Call<float>(CharactersType, "Height", character) == Approx(0.5f).margin(0.05));
    }

    test.Entities().DestroyEntity(slab);
    test.Run(10);
    {
        INFO("With the slab gone, physics should retry the stand-up with no new write from C#, and it should fit");
        REQUIRE_FALSE(test.Call<bool>(CharactersType, "ShapeBlocked", character));
        const float height = test.Call<float>(CharactersType, "Height", character);
        INFO("Standing, the centre should be back at 0.9, but it is at " << height);
        REQUIRE(height == Approx(0.9f).margin(0.05));
    }
}

TEST_CASE("A character landing sends a ContactBegin C# reads naming it, and ContactsOf from C# lists its ground",
          "[physics][scripting][characters][events]")
{
    PhysicsScriptingApp test;
    const std::uint32_t ground = AddGround(test);
    const auto character = test.Call<std::uint32_t>(CharactersType, "MakeCharacter", 0.0f, 1.5f, 0.0f);
    test.Call<void>(CharactersType, "ReportContacts", character);
    test.ApplyChanges();

    test.Run(60);

    {
        INFO("The character lands once and stays, so C# should read one ContactBegin, the ground as A, the character "
             "as B, as body and as collider");
        REQUIRE(test.Call<int>(EventsType, "ContactBeginCount") == 1);
        REQUIRE(test.Call<std::uint32_t>(EventsType, "BeginBodyA", 0) == ground);
        REQUIRE(test.Call<std::uint32_t>(EventsType, "BeginBodyB", 0) == character);
        REQUIRE(test.Call<std::uint32_t>(EventsType, "BeginColliderB", 0) == character);
    }
    {
        INFO("ContactsOf the character from C# should list its move's contact with the ground, pointing down to it");
        REQUIRE(test.Call<int>(WorldType, "ContactsOf", character, 4) == 1);
        REQUIRE(test.Call<std::uint32_t>(WorldType, "ContactBody", 0) == character);
        REQUIRE(test.Call<std::uint32_t>(WorldType, "ContactOtherBody", 0) == ground);
        REQUIRE(test.Call<std::uint32_t>(WorldType, "ContactOtherCollider", 0) == ground);
        REQUIRE(test.Call<float>(WorldType, "ContactNormalY", 0) == Approx(-1.0f).margin(0.01));
    }
}

TEST_CASE("Disabled, inactive and default(CharacterBody) characters read the default State from C#, and nothing throws",
          "[physics][scripting][characters]")
{
    PhysicsScriptingApp test;
    AddGround(test);
    const auto disabled = test.Call<std::uint32_t>(CharactersType, "MakeCharacter", -2.0f, 1.0f, 0.0f);
    const auto inactive = test.Call<std::uint32_t>(CharactersType, "MakeCharacter", 2.0f, 1.0f, 0.0f);
    test.ApplyChanges();
    test.Run(60);
    {
        INFO("Both characters should have landed first, so their state is not the default");
        REQUIRE_FALSE(test.Call<bool>(CharactersType, "StateIsNew", disabled));
        REQUIRE_FALSE(test.Call<bool>(CharactersType, "StateIsNew", inactive));
    }

    test.Call<void>(CharactersType, "Disable", disabled);
    // A RigidBody beside the CharacterBody makes the character inactive.
    test.Call<void>(CharactersType, "AddRigidBody", inactive);
    const std::uint32_t zeroed = test.Place(Math::Vec3(0.0f, 5.0f, 5.0f));
    test.Entities().GetEntity(zeroed).AddComponent<Collider>();
    test.Call<void>(CharactersType, "AddDefaultCharacter", zeroed);
    test.ApplyChanges();
    test.Run(5);

    {
        INFO("A disabled character's state should read as a new CharacterState in C#: in the air, links empty, zeros");
        REQUIRE(test.Call<bool>(CharactersType, "StateIsNew", disabled));
        REQUIRE(IsNew(CharacterOn(test, disabled).state));
    }
    {
        INFO("An inactive character's state should read as the defaults too, with physics's one warning");
        REQUIRE(test.Call<bool>(CharactersType, "StateIsNew", inactive));
        REQUIRE(WarningsAbout(test, "The CharacterBody of entity " + std::to_string(inactive) + " is inactive") == 1);
    }
    {
        INFO("default(CharacterBody) is a disabled character: its state reads as the defaults, it does not move, and "
             "nothing warns about it");
        REQUIRE(test.Call<bool>(CharactersType, "StateIsNew", zeroed));
        REQUIRE(test.Entities().GetEntity(zeroed).GetComponent<Transform>().pos.y == 5.0f);
        REQUIRE(WarningsAbout(test, "The CharacterBody of entity " + std::to_string(zeroed)) == 0);
    }
}

namespace
{
    constexpr const char* VehiclesType = "Fenrir.PhysicsScriptFixture.Vehicles";

    template <typename T>
    const T& VehicleComponentOn(PhysicsScriptingApp& test, std::uint32_t entity)
    {
        Entity holder = test.Entities().GetEntity(entity);
        INFO("The vehicle component C# added did not reach the entity");
        REQUIRE(holder.HasComponent<T>());
        return holder.GetComponent<T>();
    }

    bool Same(const VehicleInput& a, const VehicleInput& b)
    {
        return a.forward == b.forward && a.steer == b.steer && a.brake == b.brake && a.handbrake == b.handbrake &&
               a.leftTrack == b.leftTrack && a.rightTrack == b.rightTrack && a.gear == b.gear && a.clutch == b.clutch;
    }

    bool Same(const VehicleEngine& a, const VehicleEngine& b)
    {
        return a.maxTorque == b.maxTorque && a.minRpm == b.minRpm && a.maxRpm == b.maxRpm &&
               a.peakTorqueRpm == b.peakTorqueRpm && a.torqueAtMinRpm == b.torqueAtMinRpm &&
               a.torqueAtMaxRpm == b.torqueAtMaxRpm && a.inertia == b.inertia && a.angularDamping == b.angularDamping;
    }

    bool Same(const VehicleTransmission& a, const VehicleTransmission& b)
    {
        return a.mode == b.mode && a.gearCount == b.gearCount && a.firstGear == b.firstGear && a.topGear == b.topGear &&
               a.reverseGear == b.reverseGear && a.shiftUpRpm == b.shiftUpRpm && a.shiftDownRpm == b.shiftDownRpm &&
               a.switchTime == b.switchTime && a.clutchReleaseTime == b.clutchReleaseTime &&
               a.switchLatency == b.switchLatency && a.clutchStrength == b.clutchStrength;
    }

    bool Same(const VehicleTrack& a, const VehicleTrack& b)
    {
        return a.inertia == b.inertia && a.angularDamping == b.angularDamping && a.maxBrakeTorque == b.maxBrakeTorque &&
               a.differentialRatio == b.differentialRatio && a.longitudinalFriction == b.longitudinalFriction &&
               a.lateralFriction == b.lateralFriction;
    }

    bool Same(const VehicleTyre& a, const VehicleTyre& b)
    {
        return a.peakSlip == b.peakSlip && a.peakFriction == b.peakFriction && a.slideSlip == b.slideSlip &&
               a.slideFriction == b.slideFriction;
    }

    // Whether two kinds are the same kind with the same settings.
    bool Same(const VehicleKind& a, const VehicleKind& b)
    {
        if (a.Type() != b.Type())
        {
            return false;
        }
        switch (a.Type())
        {
        case VehicleType::Wheeled: {
            const auto& x = a.As<WheeledVehicle>();
            const auto& y = b.As<WheeledVehicle>();
            return Same(x.engine, y.engine) && Same(x.transmission, y.transmission) &&
                   x.limitedSlipRatio == y.limitedSlipRatio;
        }
        case VehicleType::Motorcycle: {
            const auto& x = a.As<MotorcycleVehicle>();
            const auto& y = b.As<MotorcycleVehicle>();
            return Same(x.engine, y.engine) && Same(x.transmission, y.transmission) &&
                   x.limitedSlipRatio == y.limitedSlipRatio && x.lean.enabled == y.lean.enabled &&
                   x.lean.maxAngle == y.lean.maxAngle && x.lean.limitSteering == y.lean.limitSteering;
        }
        case VehicleType::Tracked: {
            const auto& x = a.As<TrackedVehicle>();
            const auto& y = b.As<TrackedVehicle>();
            return Same(x.engine, y.engine) && Same(x.transmission, y.transmission) && Same(x.left, y.left) &&
                   Same(x.right, y.right);
        }
        case VehicleType::Trailer:
            return true;
        }
        return false;
    }

    // Whether `state` is VehicleState{}, field by field.
    bool IsNew(const VehicleState& state)
    {
        const VehicleState fresh{};
        return state.forwardSpeed == fresh.forwardSpeed && state.engineRpm == fresh.engineRpm &&
               state.gear == fresh.gear && state.clutch == fresh.clutch && state.shifting == fresh.shifting &&
               state.wheelsOnGround == fresh.wheelsOnGround && state.leanAngle == fresh.leanAngle &&
               state.leftTrackSpeed == fresh.leftTrackSpeed && state.rightTrackSpeed == fresh.rightTrackSpeed;
    }

    // Whether `state` is VehicleWheelState{}, field by field.
    bool IsNew(const VehicleWheelState& state)
    {
        const VehicleWheelState fresh{};
        return state.onGround == fresh.onGround && state.groundBody == fresh.groundBody &&
               state.groundCollider == fresh.groundCollider && state.contactPoint == fresh.contactPoint &&
               state.contactNormal == fresh.contactNormal && state.groundVelocity == fresh.groundVelocity &&
               state.compression == fresh.compression && state.suspensionForce == fresh.suspensionForce &&
               state.longitudinalSlip == fresh.longitudinalSlip && state.lateralSlip == fresh.lateralSlip &&
               state.angularVelocity == fresh.angularVelocity && state.steerAngle == fresh.steerAngle;
    }

    // A static floor 200 m across whose top is at y = 0.
    std::uint32_t AddFloor(PhysicsScriptingApp& test)
    {
        const std::uint32_t floor = test.Place(Math::Vec3(0.0f, -0.5f, 0.0f));
        test.Entities().GetEntity(floor).AddComponent<Collider>(Collider{BoxShape{Math::Vec3(100.0f, 0.5f, 100.0f)}});
        return floor;
    }
} // namespace

TEST_CASE("A new Vehicle from C# has C++'s defaults, for its own kind and every kind's new",
          "[physics][scripting][vehicles]")
{
    PhysicsScriptingApp test;
    const std::pair<int, VehicleKind> kinds[] = {
        {-1, VehicleKind()},   {0, WheeledVehicle{}}, {1, MotorcycleVehicle{}},
        {2, TrackedVehicle{}}, {3, TrailerVehicle{}},
    };
    for (const auto& [number, kind] : kinds)
    {
        const std::uint32_t entity = test.Place(Math::Vec3(0.0f));
        test.Call<void>(VehiclesType, "AddNewVehicle", entity, number);
        test.ApplyChanges();

        const Vehicle& vehicle = VehicleComponentOn<Vehicle>(test, entity);
        const Vehicle expected{};
        INFO("For C# kind number " << number << " (-1 is the Vehicle's own new kind, a wheeled one)");
        {
            INFO("The kind and every setting should be C++'s new one of that kind");
            REQUIRE(Same(vehicle.kind, kind));
        }
        {
            INFO("New, the input, probe, tilt limit (pi) and switch should be C++'s");
            REQUIRE(Same(vehicle.input, expected.input));
            REQUIRE(vehicle.probe == VehicleProbe::Cylinder);
            REQUIRE(vehicle.maxTilt == std::numbers::pi_v<float>);
            REQUIRE(vehicle.enabled);
        }
        {
            INFO("New, the state should be VehicleState{}");
            REQUIRE(IsNew(vehicle.state));
        }
    }
}

TEST_CASE("A new VehicleWheel, VehicleDifferential and VehicleAntiRollBar from C# have C++'s defaults, links empty",
          "[physics][scripting][vehicles]")
{
    PhysicsScriptingApp test;
    const std::uint32_t wheelEntity = test.Place(Math::Vec3(0.0f));
    const std::uint32_t differentialEntity = test.Place(Math::Vec3(0.0f));
    const std::uint32_t barEntity = test.Place(Math::Vec3(0.0f));
    test.Call<void>(VehiclesType, "AddNewWheel", wheelEntity);
    test.Call<void>(VehiclesType, "AddNewDifferential", differentialEntity);
    test.Call<void>(VehiclesType, "AddNewAntiRollBar", barEntity);
    test.ApplyChanges();

    {
        const VehicleWheel& wheel = VehicleComponentOn<VehicleWheel>(test, wheelEntity);
        const VehicleWheel expected{};
        INFO("New, the wheel should name no vehicle and have every C++ setting");
        REQUIRE(wheel.vehicle == Entity());
        REQUIRE(wheel.position == expected.position);
        REQUIRE(wheel.track == expected.track);
        REQUIRE(wheel.radius == expected.radius);
        REQUIRE(wheel.width == expected.width);
        REQUIRE(wheel.inertia == expected.inertia);
        REQUIRE(wheel.angularDamping == expected.angularDamping);
        REQUIRE(wheel.caster == expected.caster);
        REQUIRE(wheel.camber == expected.camber);
        REQUIRE(wheel.toe == expected.toe);
        REQUIRE(wheel.suspension.travel == expected.suspension.travel);
        REQUIRE(Same(wheel.suspension.spring, expected.suspension.spring));
        REQUIRE(wheel.suspension.preload == expected.suspension.preload);
        REQUIRE(wheel.maxSteerAngle == expected.maxSteerAngle);
        REQUIRE(wheel.maxBrakeTorque == expected.maxBrakeTorque);
        REQUIRE(wheel.maxHandbrakeTorque == expected.maxHandbrakeTorque);
        REQUIRE(Same(wheel.longitudinal, expected.longitudinal));
        REQUIRE(Same(wheel.lateral, expected.lateral));
        INFO("New, its state should be VehicleWheelState{}, both ground links empty Entities");
        REQUIRE(wheel.state.groundBody == Entity());
        REQUIRE(wheel.state.groundCollider == Entity());
        REQUIRE(IsNew(wheel.state));
    }
    {
        const VehicleDifferential& differential = VehicleComponentOn<VehicleDifferential>(test, differentialEntity);
        const VehicleDifferential expected{};
        INFO("New, the differential should name no wheels and have C++'s settings");
        REQUIRE(differential.leftWheel == Entity());
        REQUIRE(differential.rightWheel == Entity());
        REQUIRE(differential.ratio == expected.ratio);
        REQUIRE(differential.leftRightSplit == expected.leftRightSplit);
        REQUIRE(differential.limitedSlipRatio == expected.limitedSlipRatio);
        REQUIRE(differential.torqueShare == expected.torqueShare);
    }
    {
        const VehicleAntiRollBar& bar = VehicleComponentOn<VehicleAntiRollBar>(test, barEntity);
        INFO("New, the anti-roll bar should name no wheels, at C++'s stiffness");
        REQUIRE(bar.leftWheel == Entity());
        REQUIRE(bar.rightWheel == Entity());
        REQUIRE(bar.stiffness == VehicleAntiRollBar{}.stiffness);
    }
}

namespace
{
    // Vehicle as a native side that drifted from the C# mirror would map it: its kind is raw bytes (and so are the
    // structs whose mappings are the extension's own, as for the drifted Joint's frames).
    struct DriftedVehicle
    {
        alignas(VehicleKind) std::uint8_t kind[sizeof(VehicleKind)];
        alignas(VehicleInput) std::uint8_t input[sizeof(VehicleInput)];
        std::uint8_t probe;
        float maxTilt;
        bool enabled;
        alignas(VehicleState) std::uint8_t state[sizeof(VehicleState)];
    };
    static_assert(sizeof(DriftedVehicle) == sizeof(Vehicle) && alignof(DriftedVehicle) == alignof(Vehicle));
} // namespace

template <>
struct Iris::StructMapping<DriftedVehicle>
{
    using Drifted = DriftedVehicle;
    static constexpr std::string_view typeName = "Fenrir.Physics.Vehicle";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Drifted, kind, "Kind"), IRIS_FIELD_AS(Drifted, input, "Input"),
                           IRIS_FIELD_AS(Drifted, probe, "Probe"), IRIS_FIELD_AS(Drifted, maxTilt, "MaxTilt"),
                           IRIS_FIELD_AS(Drifted, enabled, "Enabled"), IRIS_FIELD_AS(Drifted, state, "State"));
};

TEST_CASE("The four vehicle mirrors are shared with C#, and a drifted Vehicle fails the load naming the field",
          "[physics][scripting][vehicles]")
{
    Testing::HeadlessApp headless;
    Physics physics = Physics::Install(headless.Get(), StepPosesOnly());
    ScriptingConfig config = PhysicsScriptingApp::Config(physics);
    std::vector<ComponentRegistration>& components = config.extensions[0].components;
    for (const std::string_view name : {"Fenrir.Physics.Vehicle", "Fenrir.Physics.VehicleWheel",
                                        "Fenrir.Physics.VehicleDifferential", "Fenrir.Physics.VehicleAntiRollBar"})
    {
        INFO("Physics's extension should share " << name << " with C#");
        REQUIRE(std::ranges::any_of(components,
                                    [name](const ComponentRegistration& component) { return component.name == name; }));
    }
    const auto vehicle = std::ranges::find_if(
        components, [](const ComponentRegistration& component) { return component.name == "Fenrir.Physics.Vehicle"; });
    *vehicle = MakeComponent<Vehicle, DriftedVehicle>("Fenrir.Physics.Vehicle");

    Scripting scripting = Scripting::Install(headless.Get(), config);

    {
        INFO("A Vehicle mirror that disagrees with the native layout left scripts loaded");
        REQUIRE_FALSE(scripting.HasScripts());
    }
    {
        INFO("The failed load did not name the component and the field");
        REQUIRE(headless.Logger().Contains(Testing::LogLevel::Error, "Fenrir.Physics.Vehicle"));
        REQUIRE(headless.Logger().Contains(Testing::LogLevel::Error, "'Kind'"));
    }
}

TEST_CASE("A C# system builds a car and its four linked wheels in one call, and Input.Forward drives it",
          "[physics][scripting][vehicles]")
{
    PhysicsScriptingApp test;
    const std::uint32_t floor = AddFloor(test);
    const auto car = test.Call<std::uint32_t>(VehiclesType, "BuildCar", 0.0f, 1.0f, 0.0f, false);
    test.ApplyChanges();
    test.Run(60);

    for (int index = 0; index < 4; ++index)
    {
        const auto wheel = test.Call<std::uint32_t>(VehiclesType, "Wheel", index);
        INFO("Wheel " << index << ", linked from C#, should stand on the floor, read in place from C#");
        REQUIRE(test.Call<bool>(VehiclesType, "OnGround", wheel));
        REQUIRE(test.Call<std::uint32_t>(VehiclesType, "GroundBody", wheel) == floor);
        REQUIRE(test.Call<std::uint32_t>(VehiclesType, "GroundCollider", wheel) == floor);
    }
    REQUIRE(test.Call<int>(VehiclesType, "WheelsOnGround", car) == 4);
    {
        const float resting = test.Call<float>(VehiclesType, "ForwardSpeed", car);
        INFO("At rest, the car should not be moving, but reads " << resting << " m/s");
        REQUIRE(std::abs(resting) < 0.1f);
    }

    test.Call<void>(VehiclesType, "SetForward", car, 1.0f);
    test.Run(120);

    const float speed = test.Call<float>(VehiclesType, "ForwardSpeed", car);
    INFO("Two seconds of full throttle written through Input.Forward should drive the car forward, but it reads "
         << speed << " m/s");
    REQUIRE(speed > 5.0f);
    {
        const VehicleState& state = VehicleComponentOn<Vehicle>(test, car).state;
        INFO("C++ should read the state C# read");
        REQUIRE(state.forwardSpeed == speed);
        REQUIRE(state.engineRpm == test.Call<float>(VehiclesType, "EngineRpm", car));
        REQUIRE(state.engineRpm > 0.0f);
    }
    REQUIRE(WarningsAbout(test, "inactive") == 0);
}

TEST_CASE("C# swaps a car to Manual through its wheeled view and picks a gear, then assigns it a trailer kind",
          "[physics][scripting][vehicles]")
{
    PhysicsScriptingApp test;
    AddFloor(test);
    const auto car = test.Call<std::uint32_t>(VehiclesType, "BuildCar", 0.0f, 1.0f, 0.0f, false);
    test.ApplyChanges();
    test.Run(30);

    test.Call<void>(VehiclesType, "ShiftManual", car, 2);
    {
        const Vehicle& vehicle = VehicleComponentOn<Vehicle>(test, car);
        INFO("The Manual mode C# wrote through the view did not land in the component");
        REQUIRE(vehicle.kind.As<WheeledVehicle>().transmission.mode == TransmissionMode::Manual);
        REQUIRE(vehicle.input.gear == 2);
    }
    test.Run(1);
    {
        INFO("In Manual the state echoes the gear C# chose, from the tick it is set");
        REQUIRE(test.Call<int>(VehiclesType, "Gear", car) == 2);
    }

    test.Call<void>(VehiclesType, "SetForward", car, 1.0f);
    test.Call<void>(VehiclesType, "MakeTrailer", car);
    {
        INFO("The trailer kind C# assigned did not replace the wheeled one");
        REQUIRE(VehicleComponentOn<Vehicle>(test, car).kind.Is<TrailerVehicle>());
        REQUIRE(test.Call<int>(VehiclesType, "KindType", car) == static_cast<int>(VehicleType::Trailer));
    }
    test.Run(60);
    {
        INFO("Physics should have rebuilt it as a trailer: no engine or gear, and the throttle moves it nowhere");
        REQUIRE(test.Call<float>(VehiclesType, "EngineRpm", car) == 0.0f);
        REQUIRE(test.Call<int>(VehiclesType, "Gear", car) == 0);
        REQUIRE(std::abs(test.Call<float>(VehiclesType, "ForwardSpeed", car)) < 0.1f);
    }
    {
        const auto message = test.Call<std::optional<std::string>>(VehiclesType, "WheeledViewMessage", car);
        INFO("Reading a trailer as a wheeled vehicle from C# should throw, naming both kinds");
        REQUIRE(message.has_value());
        REQUIRE(*message == "The vehicle is a Trailer, not a Wheeled.");
    }
}

TEST_CASE("A rear VehicleDifferential built from C# drives only its two wheels", "[physics][scripting][vehicles]")
{
    PhysicsScriptingApp test;
    // A stand under the chassis, so every wheel is in the air: a driven wheel spins up, an undriven one stays still.
    const std::uint32_t stand = test.Place(Math::Vec3(0.0f, 0.5f, 0.0f));
    test.Entities().GetEntity(stand).AddComponent<Collider>(Collider{BoxShape{Math::Vec3(0.5f, 0.5f, 1.0f)}});
    const auto car = test.Call<std::uint32_t>(VehiclesType, "BuildCar", 0.0f, 1.3f, 0.0f, true);
    test.ApplyChanges();
    test.Call<void>(VehiclesType, "SetForward", car, 0.5f);
    test.Run(60);

    for (int index = 0; index < 4; ++index)
    {
        const auto wheel = test.Call<std::uint32_t>(VehiclesType, "Wheel", index);
        const float spin = test.Call<float>(VehiclesType, "Spin", wheel);
        if (index < 2)
        {
            INFO("Front wheel " << index << ", in no differential, should stay still, but spins at " << spin);
            REQUIRE(std::abs(spin) < 1e-3f);
        }
        else
        {
            INFO("Rear wheel " << index << ", in the differential C# built, should spin up, but spins at " << spin);
            REQUIRE(spin > 1.0f);
        }
    }
    REQUIRE(WarningsAbout(test, "inactive") == 0);
}

TEST_CASE("Disabled, inactive and default(...) vehicles read reset State from C#, and nothing throws",
          "[physics][scripting][vehicles]")
{
    PhysicsScriptingApp test;
    AddFloor(test);
    const auto disabled = test.Call<std::uint32_t>(VehiclesType, "BuildCar", -5.0f, 1.0f, 0.0f, false);
    const auto disabledWheel = test.Call<std::uint32_t>(VehiclesType, "Wheel", 0);
    const auto inactive = test.Call<std::uint32_t>(VehiclesType, "BuildCar", 5.0f, 1.0f, 0.0f, false);
    const auto inactiveWheel = test.Call<std::uint32_t>(VehiclesType, "Wheel", 0);
    test.ApplyChanges();
    test.Run(60);
    {
        INFO("Both cars should be standing on their wheels first, so their states are not the defaults");
        REQUIRE_FALSE(test.Call<bool>(VehiclesType, "StateIsNew", disabled));
        REQUIRE_FALSE(test.Call<bool>(VehiclesType, "WheelStateIsNew", disabledWheel));
        REQUIRE_FALSE(test.Call<bool>(VehiclesType, "StateIsNew", inactive));
    }

    test.Call<void>(VehiclesType, "Disable", disabled);
    // A kinematic chassis makes the vehicle inactive.
    test.Call<void>(VehiclesType, "MakeKinematic", inactive);
    const std::uint32_t zeroed = test.Place(Math::Vec3(0.0f, 5.0f, 10.0f));
    test.Entities().GetEntity(zeroed).AddComponent<RigidBody>();
    test.Entities().GetEntity(zeroed).AddComponent<Collider>();
    test.Call<void>(VehiclesType, "AddDefaultVehicle", zeroed);
    const std::uint32_t zeroedWheel = test.Place(Math::Vec3(0.0f, 5.0f, -10.0f));
    test.Call<void>(VehiclesType, "AddDefaultWheel", zeroedWheel);
    test.ApplyChanges();
    test.Run(5);

    {
        INFO("A disabled vehicle's state and its wheels' should read as new in C#");
        REQUIRE(test.Call<bool>(VehiclesType, "StateIsNew", disabled));
        REQUIRE(test.Call<bool>(VehiclesType, "WheelStateIsNew", disabledWheel));
        REQUIRE(IsNew(VehicleComponentOn<Vehicle>(test, disabled).state));
    }
    {
        INFO("An inactive vehicle's state and its wheels' should read as new too, with physics's one warning");
        REQUIRE(test.Call<bool>(VehiclesType, "StateIsNew", inactive));
        REQUIRE(test.Call<bool>(VehiclesType, "WheelStateIsNew", inactiveWheel));
        REQUIRE(WarningsAbout(test, "The Vehicle on entity " + std::to_string(inactive) + " is inactive") == 1);
    }
    {
        INFO("default(Vehicle) is a disabled vehicle: its state reads as new, and nothing warns about it");
        REQUIRE(test.Call<bool>(VehiclesType, "StateIsNew", zeroed));
        REQUIRE_FALSE(VehicleComponentOn<Vehicle>(test, zeroed).enabled);
        REQUIRE(WarningsAbout(test, "The Vehicle on entity " + std::to_string(zeroed)) == 0);
    }
    {
        INFO("default(VehicleWheel) names entity 0 with no scene: an inactive wheel, its state new, with one warning");
        REQUIRE(test.Call<bool>(VehiclesType, "WheelStateIsNew", zeroedWheel));
        REQUIRE(WarningsAbout(test, "The VehicleWheel on entity " + std::to_string(zeroedWheel) + " is inactive") == 1);
    }
}

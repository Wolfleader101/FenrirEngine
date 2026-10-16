// Physics from C# through the scripting fixture harness: an App with no window, stepped one frame at a time, with
// physics installed and scripting running the physics fixture script assembly, which uses the Fenrir.Physics contract.
// C++ calls the fixture's static methods through Iris, as the system runner calls scripts, and checks what the engine
// saw.

#include "HeadlessApp.hpp"
#include "ScriptingProbe.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <type_traits>

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirPhysicsScripting/PhysicsScripting.hpp"
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

    // An App with physics installed and scripting running the fixture with physics's extension. Members end in reverse
    // order: scripting, then physics, then the app.
    struct PhysicsScriptingApp
    {
        PhysicsScriptingApp()
            : physics(Physics::Install(headless.Get(), StepPosesOnly())),
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
    REQUIRE(world->DebugDrawCategories() == DebugDraw::Colliders);
    REQUIRE_FALSE(world->DebugLines().empty());
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

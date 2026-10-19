// Character bodies switched off: disabled by game code (`enabled = false`), or inactive because the entity cannot be a
// character (a RigidBody beside it, no Collider, a mesh), each warned about once. Also what a character's children,
// joints, destroyed ground and a paused world do to it.

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "CharacterTest.hpp"
#include "FenrirPhysics/Characters.hpp"
#include "FenrirPhysics/Events.hpp"
#include "FenrirPhysics/Joints.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"

using namespace Fenrir;
using namespace PhysicsTest;
using Catch::Approx;

namespace
{
    PhysicsWorld& World(PhysicsApp& test)
    {
        return *PhysicsWorld::Of(test.app.GetActiveScene());
    }

    // Whether a ray across where the character stands, at its middle, meets it.
    bool RaySees(PhysicsApp& test, const Entity& character)
    {
        const Math::Vec3 middle = PositionOf(character);
        const std::optional<RaycastHit> hit =
            World(test).Raycast(middle - Math::Vec3(3.0f, 0.0f, 0.0f), Math::Vec3(1.0f, 0.0f, 0.0f), 6.0f);
        return hit.has_value() && hit->body == character;
    }

    void RequireDefaultState(const CharacterState& state)
    {
        REQUIRE(state.ground == GroundState::InAir);
        REQUIRE_FALSE(state.groundBody.IsValid());
        REQUIRE_FALSE(state.groundCollider.IsValid());
        REQUIRE(state.groundPoint == Math::Vec3(0.0f));
        REQUIRE(state.groundNormal == Math::Vec3(0.0f));
        REQUIRE(state.movedVelocity == Math::Vec3(0.0f));
        REQUIRE(state.carriedVelocity == Math::Vec3(0.0f));
        REQUIRE_FALSE(state.onWall);
        REQUIRE_FALSE(state.onCeiling);
        REQUIRE_FALSE(state.shapeBlocked);
    }

    // Every TriggerEnter sent while it runs frames.
    struct Enters
    {
        EventReader<TriggerEnter> reader;
        std::vector<TriggerEnter> events;

        void Run(PhysicsApp& test, double seconds)
        {
            const int frames = static_cast<int>(seconds / tick + 0.5);
            for (int i = 0; i < frames; ++i)
            {
                test.app.RunFrame(tick);
                for (const TriggerEnter& event : reader.Read(test.app))
                {
                    events.push_back(event);
                }
            }
        }
    };
} // namespace

TEST_CASE("A disabled character is not moved, not seen by bodies, rays or triggers, and its velocity and Transform are "
          "game code's",
          "[physics][characters][switching]")
{
    PhysicsApp test;
    Entity floor = AddGround(test.Entities());
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f, 0.0f, 0.0f));
    std::shared_ptr<Input> input = Drive(test.app, character);
    test.Run(0.5);
    REQUIRE(character.GetComponent<CharacterBody>().state.ground == GroundState::OnGround);
    REQUIRE(character.GetComponent<CharacterBody>().state.groundBody == floor);
    REQUIRE(RaySees(test, character));

    // Off, and left standing a metre up, with game code's own velocity in it that physics never writes back.
    CharacterBody& body = character.GetComponent<CharacterBody>();
    body.enabled = false;
    input->walk = Math::Vec3(2.0f, 0.0f, 0.0f);
    character.GetComponent<Transform>().pos = Math::Vec3(0.0f, 1.0f + footToCentre, 0.0f);
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 3.0f, 0.0f), BoxShape{Math::Vec3(0.2f)});
    Entity trigger = Place(test.Entities(), PositionOf(character));
    trigger.AddComponent<Collider>(Collider{BoxShape{Math::Vec3(0.5f)}});
    trigger.AddComponent<Trigger>();
    Enters enters;
    enters.Run(test, 1.0);

    INFO("it stayed where game code put it, unmoved by its velocity and by no gravity of physics's");
    REQUIRE(PositionOf(character).x == Approx(0.0f).margin(0.0));
    REQUIRE(PositionOf(character).y == Approx(1.0f + footToCentre).margin(0.0));
    INFO("its velocity is what game code wrote: a walk and a second of falling, never trimmed; " << body.velocity.y);
    REQUIRE(body.velocity.x == Approx(2.0f).margin(0.0));
    REQUIRE(body.velocity.y < -9.0f);
    INFO("its state is the defaults");
    RequireDefaultState(body.state);
    INFO("a box dropped on it fell through to the floor; it is at y = " << PositionOf(box).y);
    REQUIRE(PositionOf(box).y == Approx(0.2f).margin(restSlop));
    INFO("a ray across it misses it");
    REQUIRE_FALSE(RaySees(test, character));
    INFO("the trigger around it was set off by the box falling through, never by the character");
    for (const TriggerEnter& event : enters.events)
    {
        REQUIRE(event.otherBody != character);
    }
    REQUIRE(test.warnings.empty());
}

TEST_CASE("A re-enabled character finds its ground that tick, where game code left it",
          "[physics][characters][switching]")
{
    PhysicsApp test;
    Entity floor = AddGround(test.Entities());
    // A block whose top is 1 m up, away from where the character stands.
    Entity block = AddStatic(test.Entities(), Math::Vec3(5.0f, 0.5f, 0.0f), Math::Vec3(1.0f, 0.5f, 1.0f));
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f, 0.0f, 0.0f));
    Drive(test.app, character);
    test.Run(0.5);
    REQUIRE(character.GetComponent<CharacterBody>().state.groundBody == floor);

    CharacterBody& body = character.GetComponent<CharacterBody>();
    body.enabled = false;
    test.Run(0.25);
    character.GetComponent<Transform>().pos = Math::Vec3(5.0f, 1.0f + footToCentre, 0.0f);
    test.Run(0.25);
    RequireDefaultState(body.state);

    body.velocity = Math::Vec3(0.0f);
    body.enabled = true;
    test.Run(tick);
    INFO("one tick after re-enabling, it stands on the block where it was put, not on the floor it left");
    REQUIRE(body.state.ground == GroundState::OnGround);
    REQUIRE(body.state.groundBody == block);
    REQUIRE(PositionOf(character).x == Approx(5.0f).margin(1.0e-3));
    REQUIRE(FeetOf(character) == Approx(1.0f).margin(restSlop));
    INFO("and a ray sees it again");
    REQUIRE(RaySees(test, character));
    REQUIRE(test.warnings.empty());
}

TEST_CASE("A re-enabled character is shown where it is, never blended from where it was disabled",
          "[physics][characters][switching][interpolation]")
{
    PhysicsApp test{PhysicsConfig{}};
    AddGround(test.Entities());
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f, 0.0f, 0.0f));
    std::shared_ptr<Input> input = Drive(test.app, character);
    input->walk = Math::Vec3(2.0f, 0.0f, 0.0f);
    test.Run(0.5);
    REQUIRE(PositionOf(character).x > 0.5f);

    character.GetComponent<CharacterBody>().enabled = false;
    test.Run(0.25);
    character.GetComponent<Transform>().pos = Math::Vec3(-5.0f, footToCentre, 3.0f);
    test.Run(0.25);
    character.GetComponent<CharacterBody>().enabled = true;
    for (int frame = 0; frame < 20; ++frame)
    {
        test.app.RunFrame(shortFrame);
        const Math::Vec3 shown = PositionOf(character);
        INFO("frame " << frame << " after re-enabling: shown at " << shown.x << ", " << shown.z
                      << "; never between where it was disabled (x > 0.5, z = 0) and where it is (-5, 3)");
        REQUIRE(shown.z == Approx(3.0f).margin(1e-4));
        REQUIRE(shown.x >= -5.0f);
        REQUIRE(shown.x < -4.5f);
    }
}

TEST_CASE("Each kind of inactive character warns once when it goes inactive, and is a character again once valid",
          "[physics][characters][switching][inactive]")
{
    struct Case
    {
        const char* what;
        // Makes the character inactive, and makes it valid again.
        std::function<void(PhysicsApp&, Entity&)> invalidate;
        std::function<void(PhysicsApp&, Entity&)> fix;
    };
    const Case cases[] = {
        {"a RigidBody beside it",
         [](PhysicsApp&, Entity& c) { c.AddComponent<RigidBody>(RigidBody{.motionType = MotionType::Kinematic}); },
         [](PhysicsApp&, Entity& c) { c.RemoveComponent<RigidBody>(); }},
        {"its Collider removed", [](PhysicsApp&, Entity& c) { c.RemoveComponent<Collider>(); },
         [](PhysicsApp&, Entity& c) {
             c.AddComponent<Collider>(Collider{CapsuleShape{.halfHeight = 0.5f, .radius = 0.3f}});
         }},
        {"its Collider made a mesh",
         [](PhysicsApp& test, Entity& c) {
             const Math::Vec3 vertices[] = {Math::Vec3(-1.0f, 0.0f, -1.0f), Math::Vec3(1.0f, 0.0f, -1.0f),
                                            Math::Vec3(0.0f, 0.0f, 1.0f)};
             const std::uint32_t indices[] = {0, 2, 1};
             c.GetComponent<Collider>().shape = MeshShape{test.physics.CreateCollisionMesh(vertices, indices)};
         },
         [](PhysicsApp&, Entity& c) {
             c.GetComponent<Collider>().shape = CapsuleShape{.halfHeight = 0.5f, .radius = 0.3f};
         }},
    };
    for (const Case& c : cases)
    {
        PhysicsApp test;
        AddGround(test.Entities());
        Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f, 0.0f, 0.0f));
        std::shared_ptr<Input> input = Drive(test.app, character);
        test.Run(0.5);
        REQUIRE(test.warnings.empty());

        c.invalidate(test, character);
        input->walk = Math::Vec3(2.0f, 0.0f, 0.0f);
        test.Run(0.5);
        const CharacterBody& body = character.GetComponent<CharacterBody>();
        INFO(c.what << ": one warning on going inactive, and no more while it stayed inactive");
        REQUIRE(test.warnings.size() == 1);
        INFO("the warning names the character's entity: " << test.warnings.front());
        REQUIRE(test.warnings.front().find("entity " + std::to_string(character.GetId())) != std::string::npos);
        INFO("it did not walk, its velocity was not written back, and its state is the defaults");
        REQUIRE(PositionOf(character).x == Approx(0.0f).margin(0.0));
        REQUIRE(body.velocity.y < -4.0f);
        RequireDefaultState(body.state);
        if (!character.HasComponent<RigidBody>())
        {
            INFO("nothing sees it");
            REQUIRE_FALSE(RaySees(test, character));
        }

        c.fix(test, character);
        character.GetComponent<CharacterBody>().velocity = Math::Vec3(0.0f);
        test.Run(0.5);
        INFO("valid again, it walks and stands on the floor, with no further warning");
        REQUIRE(PositionOf(character).x == Approx(1.0f).margin(0.05));
        REQUIRE(character.GetComponent<CharacterBody>().state.ground == GroundState::OnGround);
        REQUIRE(RaySees(test, character));
        REQUIRE(test.warnings.size() == 1);

        c.invalidate(test, character);
        test.Run(tick);
        INFO("going inactive again warns again");
        REQUIRE(test.warnings.size() == 2);
        REQUIRE(test.errors.empty());
    }
}

TEST_CASE("A RigidBody beside a CharacterBody works as a body: its Collider falls as a dynamic body",
          "[physics][characters][switching][inactive]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f, 2.0f, 0.0f));
    character.AddComponent<RigidBody>();
    test.Run(2.0);

    INFO("one warning for the character body");
    REQUIRE(test.warnings.size() == 1);
    INFO("the capsule fell and lies on the floor as a dynamic body; its centre is at y = " << PositionOf(character).y);
    REQUIRE(PositionOf(character).y < 1.0f);
    REQUIRE(PositionOf(character).y > 0.2f);
    const std::optional<RaycastHit> down =
        World(test).Raycast(PositionOf(character) + Math::Vec3(0.0f, 3.0f, 0.0f), Math::Vec3(0.0f, -1.0f, 0.0f), 10.0f);
    REQUIRE(down.has_value());
    REQUIRE(down->body == character);
    RequireDefaultState(character.GetComponent<CharacterBody>().state);
}

TEST_CASE("A solid collider beneath a character is not part of it: it makes no body, with one warning",
          "[physics][characters][switching][children]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f, 0.0f, 0.0f));
    std::shared_ptr<Input> input = Drive(test.app, character);
    // A box held out 1 m in front of the character, at its middle.
    Entity held = Place(test.Entities(), Math::Vec3(1.0f, 0.0f, 0.0f));
    character.AddChild(held);
    held.AddComponent<Collider>(Collider{BoxShape{Math::Vec3(0.2f)}});
    input->walk = Math::Vec3(1.0f, 0.0f, 0.0f);
    test.Run(1.0);

    INFO("one warning, naming the child's entity");
    REQUIRE(test.warnings.size() == 1);
    REQUIRE(test.warnings.front().find("entity " + std::to_string(held.GetId())) != std::string::npos);
    INFO("the character walked a metre with it, unhindered by a body of its own child");
    REQUIRE(PositionOf(character).x == Approx(1.0f).margin(0.02));
    OverlapHit hits[4];
    const std::size_t count = World(test).OverlapPoint(PositionOf(held), hits);
    INFO("nothing is found where the child is");
    REQUIRE(count == 0);

    test.Run(0.5);
    INFO("no more warnings while it stays there");
    REQUIRE(test.warnings.size() == 1);
}

TEST_CASE("A trigger beneath a character follows it and is entered by bodies, never by the character",
          "[physics][characters][switching][children]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f, 0.0f, 0.0f));
    std::shared_ptr<Input> input = Drive(test.app, character);
    // A trigger 2 m wide around the character's middle.
    Entity zone = Place(test.Entities(), Math::Vec3(0.0f));
    character.AddChild(zone);
    zone.AddComponent<Collider>(Collider{BoxShape{Math::Vec3(1.0f, 0.5f, 1.0f)}});
    zone.AddComponent<Trigger>();
    input->walk = Math::Vec3(2.0f, 0.0f, 0.0f);
    Enters enters;
    enters.Run(test, 1.0);
    REQUIRE(enters.events.empty());

    // A box dropped where the character now stands, to its side, inside the zone.
    input->walk = Math::Vec3(0.0f);
    Entity box =
        AddBody(test.Entities(), PositionOf(character) + Math::Vec3(0.0f, 2.0f, 0.7f), BoxShape{Math::Vec3(0.1f)});
    enters.Run(test, 1.0);
    INFO("the box falling into the zone where the character walked to entered it, the only enter");
    REQUIRE(enters.events.size() == 1);
    REQUIRE(enters.events.front().trigger == zone);
    REQUIRE(enters.events.front().otherBody == box);
    REQUIRE(test.warnings.empty());
}

TEST_CASE("A joint naming a character is inactive, with one warning saying so, and the character walks on",
          "[physics][characters][switching][joints]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f, 0.0f, 0.0f));
    std::shared_ptr<Input> input = Drive(test.app, character);
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 4.0f, 0.0f), BoxShape{Math::Vec3(0.2f)});
    const JointFrames frames = FrameFromWorld(character, box, JointFrame{Math::Vec3(0.0f, 3.0f, 0.0f)});
    Entity joint = test.Entities().CreateEntity();
    joint.AddComponent<Joint>(
        Joint{.bodyA = character, .bodyB = box, .frameA = frames.frameA, .frameB = frames.frameB, .kind = BallJoint{}});
    input->walk = Math::Vec3(1.0f, 0.0f, 0.0f);
    test.Run(1.0);

    INFO("one warning, naming the joint entity and saying it names a character: "
         << (test.warnings.empty() ? std::string() : test.warnings.front()));
    REQUIRE(test.warnings.size() == 1);
    REQUIRE(test.warnings.front().find("entity " + std::to_string(joint.GetId())) != std::string::npos);
    REQUIRE(test.warnings.front().find("character") != std::string::npos);
    INFO("the joint kept its configuration");
    REQUIRE(joint.GetComponent<Joint>().bodyA == character);
    INFO("the box fell freely, held by nothing; it is at y = " << PositionOf(box).y);
    REQUIRE(PositionOf(box).y == Approx(0.2f).margin(restSlop));
    INFO("the character walked a metre");
    REQUIRE(PositionOf(character).x == Approx(1.0f).margin(0.02));
}

TEST_CASE("A character's groundBody naming a platform destroyed since reads as not alive",
          "[physics][characters][switching]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity platform = AddStatic(test.Entities(), Math::Vec3(0.0f, 0.25f, 0.0f), Math::Vec3(1.0f, 0.25f, 1.0f));
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f, 0.5f, 0.0f));
    Drive(test.app, character);
    test.Run(0.5);
    const CharacterState& state = character.GetComponent<CharacterBody>().state;
    REQUIRE(state.groundBody == platform);

    // Destroyed after the step, before game code reads the state, and a new entity made that may reuse its id.
    test.Entities().DestroyEntity(platform);
    Entity reused = test.Entities().CreateEntity();
    INFO("the state still names the platform, and the handle tells it is gone");
    REQUIRE_FALSE(state.groundBody.IsValid());
    REQUIRE_FALSE(state.groundCollider.IsValid());
    REQUIRE(state.groundBody != reused);

    test.Run(1.0);
    INFO("the next moves fall to the floor and name it");
    REQUIRE(state.ground == GroundState::OnGround);
    REQUIRE(state.groundBody.IsValid());
}

TEST_CASE("A paused world moves no character and keeps its velocity and state; on resume it carries on",
          "[physics][characters][switching][worlds]")
{
    PhysicsApp test;
    const std::string first = test.app.GetActiveScene().GetName();
    Entity floor = AddGround(test.Entities());
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f, 0.0f, 0.0f));
    std::shared_ptr<Input> input = Drive(test.app, character);
    input->walk = Math::Vec3(1.0f, 0.0f, 0.0f);
    test.Run(0.5);
    const CharacterBody& body = character.GetComponent<CharacterBody>();
    const Math::Vec3 paused = PositionOf(character);
    const CharacterBody before = body;
    REQUIRE(before.state.ground == GroundState::OnGround);

    test.app.CreateScene("Other");
    test.app.ChangeActiveScene("Other");
    test.Run(1.0);
    INFO("while the scene is inactive, the character is where it was, with the same velocity and state");
    REQUIRE(PositionOf(character) == paused);
    REQUIRE(body.velocity == before.velocity);
    REQUIRE(body.state.ground == GroundState::OnGround);
    REQUIRE(body.state.groundBody == floor);
    REQUIRE(body.state.groundPoint == before.state.groundPoint);
    REQUIRE(body.state.movedVelocity == before.state.movedVelocity);

    test.app.ChangeActiveScene(first);
    test.Run(0.5);
    INFO("on resume it walked on from where it stood, half a metre in half a second, on the floor");
    REQUIRE(PositionOf(character).x == Approx(paused.x + 0.5f).margin(0.02));
    REQUIRE(FeetOf(character) == Approx(0.0f).margin(restSlop));
    REQUIRE(body.state.ground == GroundState::OnGround);
    REQUIRE(test.warnings.empty());
}

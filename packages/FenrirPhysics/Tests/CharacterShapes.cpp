// Character shape changes (character spec, "shape changes and crouching"): game code edits the character's Collider or
// world scale once, and physics applies it just before the next move if it fits, keeping the feet in place on the
// ground and the head in place in the air; one that doesn't fit waits, with shapeBlocked set, until it does.

#include <cstdint>
#include <memory>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "FenrirPhysics/Characters.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"
#include "PhysicsTestApp.hpp"

using namespace Fenrir;
using namespace PhysicsTest;
using Catch::Approx;

namespace
{
    // Standing, a capsule 1.6 m tall and 0.3 m round; crouched, 1.0 m tall.
    const CapsuleShape standing{.halfHeight = 0.5f, .radius = 0.3f};
    const CapsuleShape crouched{.halfHeight = 0.2f, .radius = 0.3f};

    // The capsule's half height to its very top or bottom, rounding included.
    float ReachOf(const CapsuleShape& capsule)
    {
        return capsule.halfHeight + capsule.radius;
    }

    Entity AddCharacter(EntityList& entities, const Math::Vec3& feet, const CapsuleShape& shape = standing)
    {
        Entity character = Place(entities, feet + Math::Vec3(0.0f, ReachOf(shape), 0.0f));
        character.AddComponent<Collider>(Collider{shape});
        character.AddComponent<CharacterBody>();
        return character;
    }

    Entity AddStatic(EntityList& entities, const Math::Vec3& centre, const Math::Vec3& halfExtents)
    {
        Entity block = Place(entities, centre);
        block.AddComponent<Collider>(Collider{BoxShape{halfExtents}});
        return block;
    }

    // Where a capsule character's feet and head are, by the capsule it has now.
    float FeetOf(const Entity& character)
    {
        return PositionOf(character).y - ReachOf(character.GetComponent<Collider>().shape.As<CapsuleShape>());
    }

    // What game code asks of a character each tick: horizontal speed, with gravity added to what physics wrote back.
    struct Input
    {
        Math::Vec3 walk = Math::Vec3(0.0f);
    };

    std::shared_ptr<Input> Drive(App& app, const Entity& character)
    {
        auto input = std::make_shared<Input>();
        app.AddSequentialSystems(SchedulePriority::Tick, {[input, character](App& running) {
                                     const PhysicsWorld* world = PhysicsWorld::Of(running.GetActiveScene());
                                     if (world == nullptr || !character.IsValid())
                                     {
                                         return;
                                     }
                                     const float dt = static_cast<float>(running.GetTime().tickRate);
                                     CharacterBody& body = character.GetComponent<CharacterBody>();
                                     body.velocity.x = input->walk.x;
                                     body.velocity.z = input->walk.z;
                                     body.velocity.y += world->Gravity().y * dt;
                                 }});
        return input;
    }
} // namespace

TEST_CASE("A crouch on the ground fits at once and lowers the head, the feet staying where they were",
          "[physics][characters][shapes]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f));
    Drive(test.app, character);
    test.Run(0.5);
    const float feet = PositionOf(character).y - ReachOf(standing);

    character.GetComponent<Collider>().shape = crouched;
    test.Run(tick);

    const float centre = PositionOf(character).y;
    INFO("the centre went from " << feet + ReachOf(standing) << " to " << centre);
    REQUIRE(centre - ReachOf(crouched) == Approx(feet).margin(1.0e-3));
    REQUIRE(centre + ReachOf(crouched) == Approx(feet + 1.0f).margin(1.0e-3));
    const CharacterBody& body = character.GetComponent<CharacterBody>();
    REQUIRE_FALSE(body.state.shapeBlocked);
    REQUIRE(body.state.ground == GroundState::OnGround);

    INFO("bodies and queries see the crouched shape: nothing is where the standing head was");
    PhysicsWorld& world = *PhysicsWorld::Of(test.app.GetActiveScene());
    OverlapHit hits[4];
    REQUIRE(world.OverlapPoint(Math::Vec3(0.0f, feet + 1.4f, 0.0f), hits) == 0);
    REQUIRE(world.OverlapPoint(Math::Vec3(0.0f, feet + 0.8f, 0.0f), hits) == 1);
    REQUIRE(test.warnings.empty());
}

TEST_CASE("A crouch in the air tucks the feet up, the head staying where it was", "[physics][characters][shapes]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    // No game code adds gravity: it hangs 3 m up, still.
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f, 3.0f, 0.0f));
    test.Run(tick);
    INFO("hanging 3 m up, it is in the air");
    REQUIRE(character.GetComponent<CharacterBody>().state.ground == GroundState::InAir);
    const float head = PositionOf(character).y + ReachOf(standing);

    character.GetComponent<Collider>().shape = crouched;
    test.Run(tick);

    INFO("the head is still at " << head << "; the feet are 0.6 m higher, at " << FeetOf(character));
    REQUIRE(PositionOf(character).y + ReachOf(crouched) == Approx(head).margin(1.0e-4));
    REQUIRE(FeetOf(character) == Approx(head - 1.0f).margin(1.0e-4));
    REQUIRE_FALSE(character.GetComponent<CharacterBody>().state.shapeBlocked);
}

namespace
{
    // The underside of a low roof: above a crouched character's head, below a standing one's.
    constexpr float roof = 1.3f;

    // A crouched character on the ground at the origin, settled, with game code adding gravity.
    Entity AddCrouched(PhysicsApp& test)
    {
        AddGround(test.Entities());
        Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f), crouched);
        Drive(test.app, character);
        return character;
    }

    Entity AddRoof(EntityList& entities)
    {
        return AddStatic(entities, Math::Vec3(0.0f, roof + 0.25f, 0.0f), Math::Vec3(1.0f, 0.25f, 1.0f));
    }

    float HeadOf(const Entity& character, const CapsuleShape& shape)
    {
        return PositionOf(character).y + ReachOf(shape);
    }
} // namespace

TEST_CASE("A stand-up under a box waits with shapeBlocked set, and stands by itself once the box is gone",
          "[physics][characters][shapes]")
{
    PhysicsApp test;
    Entity character = AddCrouched(test);
    Entity box = AddRoof(test.Entities());
    test.Run(0.5);
    const float crouchedHead = HeadOf(character, crouched);
    INFO("crouched, its head at " << crouchedHead << " is clear of the roof");
    REQUIRE(crouchedHead < roof);

    character.GetComponent<Collider>().shape = standing;
    for (int t = 0; t < 30; ++t)
    {
        test.Run(tick);
        const CharacterBody& body = character.GetComponent<CharacterBody>();
        INFO("tick " << t << ": head at " << HeadOf(character, crouched));
        REQUIRE(body.state.shapeBlocked);
        REQUIRE(HeadOf(character, crouched) == Approx(crouchedHead).margin(1.0e-3));
        REQUIRE(body.state.ground == GroundState::OnGround);
    }
    INFO("the Collider still holds the standing shape game code asked for");
    REQUIRE(character.GetComponent<Collider>().shape.As<CapsuleShape>().halfHeight == Approx(standing.halfHeight));

    INFO("bodies and queries see the crouched shape: nothing of the character is above its crouched head");
    PhysicsWorld& world = *PhysicsWorld::Of(test.app.GetActiveScene());
    OverlapHit hits[4];
    REQUIRE(world.OverlapPoint(Math::Vec3(0.0f, crouchedHead + 0.1f, 0.0f), hits) == 0);
    REQUIRE(world.OverlapPoint(Math::Vec3(0.0f, crouchedHead - 0.1f, 0.0f), hits) == 1);
    REQUIRE(hits[0].body == character);

    test.Entities().DestroyEntity(box);
    test.Run(tick);
    INFO("with the box gone, the next move stands it up, its feet where they were");
    const CharacterBody& body = character.GetComponent<CharacterBody>();
    REQUIRE_FALSE(body.state.shapeBlocked);
    REQUIRE(HeadOf(character, standing) == Approx(crouchedHead + 0.6f).margin(1.0e-3));
    REQUIRE(world.OverlapPoint(Math::Vec3(0.0f, crouchedHead + 0.4f, 0.0f), hits) == 1);
    REQUIRE(test.warnings.empty());
}

TEST_CASE("A trigger never blocks a stand-up", "[physics][characters][shapes]")
{
    PhysicsApp test;
    Entity character = AddCrouched(test);
    AddRoof(test.Entities()).AddComponent<Trigger>();
    test.Run(0.5);
    const float crouchedHead = HeadOf(character, crouched);

    character.GetComponent<Collider>().shape = standing;
    test.Run(tick);

    INFO("it stands up at once inside the trigger, its head at " << HeadOf(character, standing));
    REQUIRE_FALSE(character.GetComponent<CharacterBody>().state.shapeBlocked);
    REQUIRE(HeadOf(character, standing) == Approx(crouchedHead + 0.6f).margin(1.0e-3));
}

TEST_CASE("Another character overhead blocks a stand-up", "[physics][characters][shapes]")
{
    PhysicsApp test;
    Entity character = AddCrouched(test);
    // No game code adds its gravity: it hangs with its feet at the roof's height.
    AddCharacter(test.Entities(), Math::Vec3(0.0f, roof, 0.0f));
    test.Run(0.5);

    character.GetComponent<Collider>().shape = standing;
    test.Run(tick);

    INFO("the other character's body is in the way of the standing head");
    REQUIRE(character.GetComponent<CharacterBody>().state.shapeBlocked);
}

TEST_CASE("A scale edit is a shape edit: checked for room, and grown upward from the feet once it fits",
          "[physics][characters][shapes]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f));
    Drive(test.app, character);
    // 0.4 m above the standing head: room to stand, not to stand half as tall again.
    Entity box = AddStatic(test.Entities(), Math::Vec3(0.0f, 2.25f, 0.0f), Math::Vec3(1.0f, 0.25f, 1.0f));
    test.Run(0.5);
    const float head = HeadOf(character, standing);

    character.GetComponent<Transform>().scale = Math::Vec3(1.5f);
    test.Run(0.25);
    INFO("blocked, it keeps its old size: its head is still at " << HeadOf(character, standing));
    REQUIRE(character.GetComponent<CharacterBody>().state.shapeBlocked);
    REQUIRE(HeadOf(character, standing) == Approx(head).margin(1.0e-3));
    REQUIRE(character.GetComponent<Transform>().scale == Math::Vec3(1.5f));

    test.Entities().DestroyEntity(box);
    test.Run(tick);
    INFO("with the box gone it grows: 2.4 m tall, its feet where they were");
    REQUIRE_FALSE(character.GetComponent<CharacterBody>().state.shapeBlocked);
    REQUIRE(PositionOf(character).y + 1.5f * ReachOf(standing) == Approx(head + 0.8f).margin(1.0e-3));
}

TEST_CASE("Box, sphere, capsule and hull characters walk, and each reshapes keeping its feet in place",
          "[physics][characters][shapes]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    // A unit cube about the origin, for the hull.
    const std::vector<Math::Vec3> corners = {{-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f},
                                             {0.5f, 0.5f, -0.5f},   {-0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, 0.5f},
                                             {-0.5f, 0.5f, 0.5f},   {0.5f, 0.5f, 0.5f}};
    const std::vector<std::uint32_t> indices = {0, 1, 2};
    const CollisionMesh cube = test.physics.CreateCollisionMesh(corners, indices);

    struct Kind
    {
        const char* name;
        CollisionShape before;
        CollisionShape after;
        Math::Vec3 scaleBefore;
        Math::Vec3 scaleAfter;
        // From the centre down to the lowest point, before and after.
        float lowBefore;
        float lowAfter;
    };
    const Kind kinds[] = {
        {"box", BoxShape{Math::Vec3(0.3f, 0.8f, 0.3f)}, BoxShape{Math::Vec3(0.3f, 0.5f, 0.3f)}, Math::Vec3(1.0f),
         Math::Vec3(1.0f), 0.8f, 0.5f},
        {"sphere", SphereShape{0.5f}, SphereShape{0.3f}, Math::Vec3(1.0f), Math::Vec3(1.0f), 0.5f, 0.3f},
        {"capsule", standing, crouched, Math::Vec3(1.0f), Math::Vec3(1.0f), ReachOf(standing), ReachOf(crouched)},
        {"hull", ConvexHullShape{cube}, ConvexHullShape{cube}, Math::Vec3(0.6f, 1.6f, 0.6f),
         Math::Vec3(0.6f, 1.0f, 0.6f), 0.8f, 0.5f},
    };

    for (const Kind& kind : kinds)
    {
        DYNAMIC_SECTION(kind.name)
        {
            Entity character = Place(test.Entities(), Math::Vec3(0.0f, kind.lowBefore + 0.5f, 0.0f), kind.scaleBefore);
            character.AddComponent<Collider>(Collider{kind.before});
            character.AddComponent<CharacterBody>();
            std::shared_ptr<Input> input = Drive(test.app, character);
            input->walk = Math::Vec3(1.0f, 0.0f, 0.0f);
            test.Run(1.0);

            INFO("it fell, landed and walked about a metre");
            const CharacterBody& body = character.GetComponent<CharacterBody>();
            REQUIRE(body.state.ground == GroundState::OnGround);
            REQUIRE(PositionOf(character).x == Approx(1.0f).margin(0.05));
            const float feet = PositionOf(character).y - kind.lowBefore;
            REQUIRE(feet == Approx(0.0f).margin(restSlop));

            character.GetComponent<Collider>().shape = kind.after;
            character.GetComponent<Transform>().scale = kind.scaleAfter;
            test.Run(tick);
            INFO("reshaped, its lowest point stayed at " << feet);
            REQUIRE_FALSE(body.state.shapeBlocked);
            REQUIRE(PositionOf(character).y - kind.lowAfter == Approx(feet).margin(1.0e-3));
            REQUIRE(test.warnings.empty());
        }
    }
}

TEST_CASE("A crouch's shift is not a teleport: velocity is kept, and the shift is left out of movedVelocity",
          "[physics][characters][shapes]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f));
    std::shared_ptr<Input> input = Drive(test.app, character);
    input->walk = Math::Vec3(1.5f, 0.0f, 0.0f);
    test.Run(0.5);

    character.GetComponent<Collider>().shape = crouched;
    const float before = PositionOf(character).y;
    test.Run(tick);

    const CharacterBody& body = character.GetComponent<CharacterBody>();
    INFO("the centre dropped " << before - PositionOf(character).y << " m; movedVelocity.y "
                               << body.state.movedVelocity.y);
    REQUIRE(before - PositionOf(character).y == Approx(0.3f).margin(1.0e-3));
    REQUIRE(body.state.movedVelocity.x == Approx(1.5f).margin(1.0e-3));
    REQUIRE(body.state.movedVelocity.y == Approx(0.0f).margin(1.0e-2));
    REQUIRE(body.velocity.x == Approx(1.5f).margin(1.0e-3));
    REQUIRE(body.state.ground == GroundState::OnGround);
}

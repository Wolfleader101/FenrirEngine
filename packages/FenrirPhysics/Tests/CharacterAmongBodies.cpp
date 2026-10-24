// Characters among other bodies through their inner body: triggers see a character, its CollisionFilter filters both
// its moves and what meets it, its PhysicsMaterial is what bodies on it feel, and characters block and ride each
// other.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "CharacterTest.hpp"
#include "FenrirPhysics/Characters.hpp"
#include "FenrirPhysics/Events.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"
#include "FenrirPhysics/Queries.hpp"

using namespace Fenrir;
using namespace PhysicsTest;
using Catch::Approx;

namespace
{
    constexpr std::uint32_t Layer(int index)
    {
        return 1u << index;
    }

    PhysicsWorld& WorldOf(PhysicsApp& test)
    {
        return *PhysicsWorld::Of(test.app.GetActiveScene());
    }

    // Every TriggerEnter and TriggerExit sent while it runs frames.
    struct TriggerLog
    {
        EventReader<TriggerEnter> enterReader;
        EventReader<TriggerExit> exitReader;
        std::vector<TriggerEnter> enters;
        std::vector<TriggerExit> exits;

        void Run(PhysicsApp& test, double seconds)
        {
            const int frames = static_cast<int>(seconds / tick + 0.5);
            for (int i = 0; i < frames; ++i)
            {
                test.app.RunFrame(tick);
                for (const TriggerEnter& event : enterReader.Read(test.app))
                {
                    enters.push_back(event);
                }
                for (const TriggerExit& event : exitReader.Read(test.app))
                {
                    exits.push_back(event);
                }
            }
        }

        [[nodiscard]] bool Entered(const Entity& trigger, const Entity& body) const
        {
            for (const TriggerEnter& event : enters)
            {
                if (event.trigger == trigger && event.otherBody == body)
                {
                    return true;
                }
            }
            return false;
        }
    };

    Entity AddTrigger(EntityList& entities, const Math::Vec3& centre, const Math::Vec3& halfExtents)
    {
        Entity zone = Place(entities, centre);
        zone.AddComponent<Collider>(Collider{BoxShape{halfExtents}});
        zone.AddComponent<Trigger>();
        return zone;
    }

    // A character shaped as a box 1 m on each side, with a flat top to rest things on, its feet at `feet`.
    Entity AddBoxCharacter(EntityList& entities, const Math::Vec3& feet)
    {
        Entity character = Place(entities, feet + Math::Vec3(0.0f, 0.5f, 0.0f));
        character.AddComponent<Collider>(Collider{BoxShape{Math::Vec3(0.5f)}});
        character.AddComponent<CharacterBody>();
        return character;
    }
} // namespace

TEST_CASE("A character walking through a trigger enters and exits it once, naming itself as body and collider",
          "[physics][characters][triggers]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity character = AddCharacter(test.Entities(), Math::Vec3(-3.0f, 0.0f, 0.0f));
    Drive(test.app, character)->walk = Math::Vec3(2.0f, 0.0f, 0.0f);
    // Clear of the ground, which it would otherwise overlap.
    Entity zone = AddTrigger(test.Entities(), Math::Vec3(0.0f, 1.1f, 0.0f), Math::Vec3(0.5f, 1.0f, 1.0f));
    TriggerLog log;

    log.Run(test, 3.0);

    INFO("one enter and one exit as it walks through, got " << log.enters.size() << " and " << log.exits.size());
    REQUIRE(log.enters.size() == 1);
    REQUIRE(log.exits.size() == 1);
    INFO("each names the trigger, and the character's entity as body and collider");
    REQUIRE(log.enters[0].trigger == zone);
    REQUIRE(log.enters[0].otherBody == character);
    REQUIRE(log.enters[0].otherCollider == character);
    REQUIRE(log.exits[0].trigger == zone);
    REQUIRE(log.exits[0].otherBody == character);
    REQUIRE(log.exits[0].otherCollider == character);
    INFO("the move never met the trigger: 3 s at 2 m/s from x = -3 took it to " << PositionOf(character).x);
    REQUIRE(PositionOf(character).x == Approx(3.0f).margin(0.05f));
}

TEST_CASE("A query ignoring a character's entity skips it in rays, shape casts and overlaps",
          "[physics][characters][queries]")
{
    PhysicsApp test;
    // No game code: it hangs where it was made, its middle at y = 0.8.
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f));
    test.Run(tick);
    PhysicsWorld& world = WorldOf(test);
    const QueryFilter ignoring{.ignoreBody = character};
    const Math::Vec3 middle(0.0f, footToCentre, 0.0f);
    const Math::Vec3 east(1.0f, 0.0f, 0.0f);
    const Transform west(Math::Vec3(-3.0f, footToCentre, 0.0f), identity, Math::Vec3(1.0f));
    const Transform atMiddle(middle, identity, Math::Vec3(1.0f));
    std::array<OverlapHit, 4> hits;

    INFO("without the filter, each query finds the character");
    REQUIRE(world.Raycast(west.pos, east, 10.0f)->body == character);
    REQUIRE(world.ShapeCast(SphereShape{0.2f}, west, east, 10.0f)->body == character);
    REQUIRE(world.Overlap(SphereShape{0.2f}, atMiddle, hits) == 1);
    REQUIRE(world.OverlapPoint(middle, hits) == 1);

    INFO("ignoring it, none does");
    REQUIRE_FALSE(world.Raycast(west.pos, east, 10.0f, ignoring).has_value());
    REQUIRE_FALSE(world.ShapeCast(SphereShape{0.2f}, west, east, 10.0f, ignoring).has_value());
    REQUIRE(world.Overlap(SphereShape{0.2f}, atMiddle, hits, ignoring) == 0);
    REQUIRE(world.OverlapPoint(middle, hits, ignoring) == 0);
}

TEST_CASE("A character whose mask leaves out a layer walks through that layer's walls, and triggers and queries "
          "masking it out pass through it",
          "[physics][characters][layers]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    // The character is in layer 1 and collides with all but layer 2.
    Entity character = AddCharacter(test.Entities(), Math::Vec3(-3.0f, 0.0f, 0.0f));
    character.AddComponent<CollisionFilter>(CollisionFilter{.layers = Layer(1), .mask = ~Layer(2)});
    std::shared_ptr<Input> input = Drive(test.app, character);
    input->walk = Math::Vec3(2.0f, 0.0f, 0.0f);
    // A wall in layer 2 across its path.
    Entity wall = AddStatic(test.Entities(), Math::Vec3(-1.0f, 1.0f, 0.0f), Math::Vec3(0.1f, 1.0f, 2.0f));
    wall.AddComponent<CollisionFilter>(CollisionFilter{.layers = Layer(2), .mask = ~0u});
    // Two triggers on its path, clear of the ground: one whose mask leaves out layer 1, and a plain one.
    Entity blind = AddTrigger(test.Entities(), Math::Vec3(0.5f, 1.1f, 0.0f), Math::Vec3(0.3f, 1.0f, 1.0f));
    blind.AddComponent<CollisionFilter>(CollisionFilter{.layers = Layer(0), .mask = ~Layer(1)});
    Entity plain = AddTrigger(test.Entities(), Math::Vec3(2.0f, 1.1f, 0.0f), Math::Vec3(0.3f, 1.0f, 1.0f));
    TriggerLog log;

    log.Run(test, 3.0);

    INFO("it walked through the wall in layer 2: from x = -3 at 2 m/s for 3 s it reached " << PositionOf(character).x);
    REQUIRE(PositionOf(character).x == Approx(3.0f).margin(0.05f));
    INFO("the trigger whose mask leaves out layer 1 never saw it; the plain one did");
    REQUIRE_FALSE(log.Entered(blind, character));
    REQUIRE(log.Entered(plain, character));

    // Standing still: a box in layer 3 that does not collide with layer 1 is dropped on it, and one that does.
    input->walk = Math::Vec3(0.0f);
    test.Run(0.2);
    const Math::Vec3 at = PositionOf(character);
    Entity passing = AddBody(test.Entities(), at + Math::Vec3(0.0f, 2.0f, 0.0f), BoxShape{Math::Vec3(0.1f)});
    passing.AddComponent<CollisionFilter>(CollisionFilter{.layers = Layer(3), .mask = ~Layer(1)});
    Entity landing = AddBody(test.Entities(), at + Math::Vec3(0.0f, 2.0f, 2.0f), BoxShape{Math::Vec3(0.1f)});
    test.Run(1.5);
    INFO("the box masking out layer 1 fell through it to the ground: it is at y = " << PositionOf(passing).y);
    REQUIRE(PositionOf(passing).y == Approx(0.1f).margin(restSlop));
    INFO("the plain box beside it fell to the ground too (a control): y = " << PositionOf(landing).y);
    REQUIRE(PositionOf(landing).y == Approx(0.1f).margin(restSlop));

    PhysicsWorld& world = WorldOf(test);
    const Math::Vec3 from = PositionOf(character) - Math::Vec3(3.0f, 0.0f, 0.0f);
    const Math::Vec3 east(1.0f, 0.0f, 0.0f);
    INFO("a ray looking for layer 1 hits it, one looking for every other layer misses it");
    REQUIRE(world.Raycast(from, east, 6.0f, QueryFilter{.mask = Layer(1)})->body == character);
    REQUIRE_FALSE(world.Raycast(from, east, 6.0f, QueryFilter{.mask = ~Layer(1)}).has_value());
    INFO("a ray in layer 2, which its mask leaves out, misses it; one in layer 0 hits it");
    REQUIRE_FALSE(world.Raycast(from, east, 6.0f, QueryFilter{.layers = Layer(2)}).has_value());
    REQUIRE(world.Raycast(from, east, 6.0f, QueryFilter{.layers = Layer(0)})->body == character);
}

TEST_CASE("A box dropped on a character falls through it when its mask leaves out the character's layer",
          "[physics][characters][layers]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f));
    character.AddComponent<CollisionFilter>(CollisionFilter{.layers = Layer(1), .mask = ~0u});
    Drive(test.app, character);
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 3.0f, 0.0f), BoxShape{Math::Vec3(0.1f)});
    box.AddComponent<CollisionFilter>(CollisionFilter{.layers = Layer(0), .mask = ~Layer(1)});

    test.Run(1.5);

    INFO("the box fell through the character's head to the ground: it is at y = " << PositionOf(box).y);
    REQUIRE(PositionOf(box).y == Approx(0.1f).margin(restSlop));
}

TEST_CASE("Changing a character's CollisionFilter takes effect on the next tick, for its moves and its body",
          "[physics][characters][layers]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f));
    Drive(test.app, character);
    test.Run(0.5);
    REQUIRE(character.GetComponent<CharacterBody>().state.ground == GroundState::OnGround);
    PhysicsWorld& world = WorldOf(test);
    const Math::Vec3 from(-3.0f, footToCentre, 0.0f);
    const Math::Vec3 east(1.0f, 0.0f, 0.0f);
    REQUIRE(world.Raycast(from, east, 6.0f, QueryFilter{.mask = Layer(0)})->body == character);

    // Into layer 4, colliding with nothing in layer 0: the ground.
    character.AddComponent<CollisionFilter>(CollisionFilter{.layers = Layer(4), .mask = ~Layer(0)});
    test.Run(tick);
    INFO("its body is in layer 4 at once: a ray for layer 0 misses it, one for layer 4 hits it");
    REQUIRE_FALSE(world.Raycast(from, east, 6.0f, QueryFilter{.mask = Layer(0)}).has_value());
    REQUIRE(world.Raycast(from, east, 6.0f, QueryFilter{.mask = Layer(4)})->body == character);
    test.Run(1.0);
    INFO("its moves no longer meet the ground: it fell through to y = " << FeetOf(character));
    REQUIRE(FeetOf(character) < -2.0f);
}

namespace
{
    // How far a box sliding at 3 m/s across the flat top of a standing character goes in a second, when the
    // character's PhysicsMaterial has `friction` and the box's has friction 1. The material is the character's from
    // the start, or `edited` in once it has stood a while.
    float SlideOnCharacter(float friction, bool edited)
    {
        PhysicsApp test;
        AddGround(test.Entities());
        Entity character = AddBoxCharacter(test.Entities(), Math::Vec3(0.0f));
        if (!edited)
        {
            character.AddComponent<PhysicsMaterial>(PhysicsMaterial{.friction = friction});
        }
        // It pushes nothing, so only friction slows the box.
        character.GetComponent<CharacterBody>().pushForce = 0.0f;
        Drive(test.app, character);
        test.Run(0.25);
        if (edited)
        {
            character.AddComponent<PhysicsMaterial>(PhysicsMaterial{.friction = friction});
        }

        Entity box = AddBody(test.Entities(), Math::Vec3(-0.25f, 1.25f, 0.0f), BoxShape{Math::Vec3(0.2f)});
        box.AddComponent<PhysicsMaterial>(PhysicsMaterial{.friction = 1.0f});
        box.GetComponent<RigidBody>().linearVelocity = Math::Vec3(3.0f, 0.0f, 0.0f);
        test.Run(1.0);
        return PositionOf(box).x + 0.25f;
    }
} // namespace

TEST_CASE("A box sliding on a character feels the character's PhysicsMaterial friction, from the next tick after an "
          "edit",
          "[physics][characters]")
{
    const bool edited = GENERATE(false, true);
    const float rough = SlideOnCharacter(1.0f, edited);
    const float slippery = SlideOnCharacter(0.0f, edited);

    INFO("material edited in later: " << edited);
    INFO("on a character with friction 1 the box stops within 3^2 / (2 x 9.81) = 0.46 m; it went " << rough);
    REQUIRE(rough < 0.6f);
    INFO("on a frictionless character it slides off the top, well over a metre; it went " << slippery);
    REQUIRE(slippery > 1.5f);
}

TEST_CASE("Two characters block each other, and neither pushes the other", "[physics][characters][others]")
{
    // Made in either order, so the walker's move comes before the other's or after it.
    const bool walkerFirst = GENERATE(true, false);
    PhysicsApp test;
    AddGround(test.Entities());
    Entity walker;
    Entity stander;
    if (walkerFirst)
    {
        walker = AddCharacter(test.Entities(), Math::Vec3(-1.5f, 0.0f, 0.0f));
        stander = AddCharacter(test.Entities(), Math::Vec3(1.0f, 0.0f, 0.0f));
    }
    else
    {
        stander = AddCharacter(test.Entities(), Math::Vec3(1.0f, 0.0f, 0.0f));
        walker = AddCharacter(test.Entities(), Math::Vec3(-1.5f, 0.0f, 0.0f));
    }
    Drive(test.app, walker)->walk = Math::Vec3(2.0f, 0.0f, 0.0f);
    Drive(test.app, stander);

    for (int t = 0; t < 120; ++t)
    {
        test.Run(tick);
        INFO("walker first: " << walkerFirst << ", tick " << t
                              << ": the standing character is at x = " << PositionOf(stander).x);
        REQUIRE(PositionOf(stander).x == Approx(1.0f).margin(1.0e-3f));
    }
    INFO("the walker stopped against it, the two capsules 0.6 m apart at their middles: it is at x = "
         << PositionOf(walker).x);
    REQUIRE(PositionOf(walker).x < 0.42f);
    REQUIRE(PositionOf(walker).x > 0.3f);
    INFO("and its velocity into the other was trimmed: " << walker.GetComponent<CharacterBody>().velocity.x);
    REQUIRE(std::abs(walker.GetComponent<CharacterBody>().velocity.x) < 1.0e-3f);
    REQUIRE(walker.GetComponent<CharacterBody>().state.onWall);
}

TEST_CASE("A character standing on another that walks rides it, naming it as its ground",
          "[physics][characters][others]")
{
    // Made in either order: the rider sees the walker's last movedVelocity, from this tick or the last.
    const bool riderFirst = GENERATE(true, false);
    PhysicsApp test;
    AddGround(test.Entities());
    Entity rider;
    Entity walker;
    if (riderFirst)
    {
        rider = AddCharacter(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
        walker = AddBoxCharacter(test.Entities(), Math::Vec3(0.0f));
    }
    else
    {
        walker = AddBoxCharacter(test.Entities(), Math::Vec3(0.0f));
        rider = AddCharacter(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    }
    std::shared_ptr<Input> walking = Drive(test.app, walker);
    Drive(test.app, rider);
    test.Run(0.5);
    REQUIRE(rider.GetComponent<CharacterBody>().state.groundBody == walker);

    walking->walk = Math::Vec3(1.0f, 0.0f, 0.0f);
    test.Run(2.0);

    const CharacterState& state = rider.GetComponent<CharacterBody>().state;
    INFO("rider first: " << riderFirst << "; the walker went to x = " << PositionOf(walker).x << ", the rider to "
                         << PositionOf(rider).x);
    REQUIRE(PositionOf(walker).x == Approx(2.0f).margin(0.05f));
    REQUIRE(PositionOf(rider).x == Approx(PositionOf(walker).x).margin(0.05f));
    INFO("it stands on the walker, moving at the walker's speed: ground velocity " << state.groundVelocity.x);
    REQUIRE(state.ground == GroundState::OnGround);
    REQUIRE(state.groundBody == walker);
    REQUIRE(state.groundCollider == walker);
    REQUIRE(state.groundVelocity.x == Approx(1.0f).margin(1.0e-3f));
    INFO("none of the ride is in its own velocity: " << rider.GetComponent<CharacterBody>().velocity.x);
    REQUIRE(rider.GetComponent<CharacterBody>().velocity.x == Approx(0.0f).margin(1.0e-4f));
}

TEST_CASE("A character carrying another on its head still jumps, lifting the rider with it",
          "[physics][characters][others]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity carrier = AddBoxCharacter(test.Entities(), Math::Vec3(0.0f));
    Entity rider = AddCharacter(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    std::shared_ptr<Input> jumping = Drive(test.app, carrier);
    Drive(test.app, rider);
    test.Run(0.5);
    REQUIRE(rider.GetComponent<CharacterBody>().state.groundBody == carrier);
    const float carrierFeet = PositionOf(carrier).y - 0.5f;
    const float riderFeet = FeetOf(rider);

    jumping->jump = true;
    float highest = carrierFeet;
    float riderHighest = riderFeet;
    for (int t = 0; t < 30; ++t)
    {
        test.Run(tick);
        highest = std::max(highest, PositionOf(carrier).y - 0.5f);
        riderHighest = std::max(riderHighest, FeetOf(rider));
    }
    INFO("a 5 m/s jump lifts the feet 5^2 / (2 x 9.81) = 1.27 m; the carrier's rose " << highest - carrierFeet);
    REQUIRE(highest - carrierFeet > 1.0f);
    INFO("the rider went up with it, at least as far: " << riderHighest - riderFeet);
    REQUIRE(riderHighest - riderFeet > 1.0f);
}

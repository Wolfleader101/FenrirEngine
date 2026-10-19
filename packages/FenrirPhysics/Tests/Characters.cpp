// Character bodies (ADR-0014): a CharacterBody beside its entity's own Collider, moved by its velocity in PostTick
// before the step, its velocity written back trimmed by what it touches and its state written into the component.

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
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
    // A capsule 1.6 m tall and 0.3 m round: its centre is 0.8 m above its feet.
    constexpr float footToCentre = 0.8f;

    Entity AddCharacter(EntityList& entities, const Math::Vec3& feet)
    {
        Entity character = Place(entities, feet + Math::Vec3(0.0f, footToCentre, 0.0f));
        character.AddComponent<Collider>(Collider{CapsuleShape{.halfHeight = 0.5f, .radius = 0.3f}});
        character.AddComponent<CharacterBody>();
        return character;
    }

    Entity AddStatic(EntityList& entities, const Math::Vec3& centre, const Math::Vec3& halfExtents)
    {
        Entity block = Place(entities, centre);
        block.AddComponent<Collider>(Collider{BoxShape{halfExtents}});
        return block;
    }

    float FeetOf(const Entity& character)
    {
        return PositionOf(character).y - footToCentre;
    }

    // What game code asks of a character each tick.
    struct Input
    {
        Math::Vec3 walk = Math::Vec3(0.0f);
        bool jump = false;
    };

    constexpr float jumpSpeed = 5.0f;

    // The game code the spec asks for, in Tick: horizontal speed from input, gravity added to what physics wrote back,
    // and a jump only from walkable ground.
    std::shared_ptr<Input> Drive(App& app, const Entity& character)
    {
        auto input = std::make_shared<Input>();
        app.AddSequentialSystems(SchedulePriority::Tick, {[input, character](App& running) {
                                     const PhysicsWorld* world = PhysicsWorld::Of(running.GetActiveScene());
                                     if (world == nullptr || !character.IsValid() ||
                                         !character.HasComponent<CharacterBody>())
                                     {
                                         return;
                                     }
                                     const float dt = static_cast<float>(running.GetTime().tickRate);
                                     CharacterBody& body = character.GetComponent<CharacterBody>();
                                     body.velocity.x = input->walk.x;
                                     body.velocity.z = input->walk.z;
                                     body.velocity.y += world->Gravity().y * dt;
                                     if (input->jump && body.state.ground == GroundState::OnGround)
                                     {
                                         body.velocity.y = jumpSpeed;
                                         input->jump = false;
                                     }
                                 }});
        return input;
    }
} // namespace

TEST_CASE("A CharacterBody made with no arguments has the spec's defaults", "[physics][characters]")
{
    const CharacterBody body{};

    INFO("it stands still, and its rules are on at the spec's values");
    REQUIRE(body.velocity == Math::Vec3(0.0f));
    REQUIRE(body.maxSlope == Approx(0.785f));
    REQUIRE(body.stepHeight == Approx(0.4f));
    REQUIRE(body.floorSnap == Approx(0.5f));
    REQUIRE(body.pushForce == Approx(100.0f));
    REQUIRE(body.mass == Approx(70.0f));
    REQUIRE(body.stopOnSlope);
    REQUIRE(body.interpolate);
    REQUIRE(body.enabled);

    INFO("its state is in the air, touching nothing, with no ground");
    const CharacterState& state = body.state;
    REQUIRE(state.ground == GroundState::InAir);
    REQUIRE_FALSE(state.groundBody.IsValid());
    REQUIRE_FALSE(state.groundCollider.IsValid());
    REQUIRE(state.groundPoint == Math::Vec3(0.0f));
    REQUIRE(state.groundNormal == Math::Vec3(0.0f));
    REQUIRE(state.groundVelocity == Math::Vec3(0.0f));
    REQUIRE(state.groundAngularVelocity == Math::Vec3(0.0f));
    REQUIRE(state.movedVelocity == Math::Vec3(0.0f));
    REQUIRE(state.carriedVelocity == Math::Vec3(0.0f));
    REQUIRE_FALSE(state.onWall);
    REQUIRE_FALSE(state.onCeiling);
    REQUIRE_FALSE(state.shapeBlocked);
}

TEST_CASE(
    "A character beside a Collider falls under game code's gravity and lands, its falling speed cleared each tick",
    "[physics][characters]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f, 2.0f, 0.0f));
    Drive(test.app, character);

    test.Run(0.25);
    INFO("a quarter of a second in, it has fallen under the gravity game code added");
    REQUIRE(FeetOf(character) < 1.8f);
    REQUIRE(character.GetComponent<CharacterBody>().velocity.y < -1.0f);

    test.Run(1.25);
    for (int t = 0; t < 60; ++t)
    {
        test.Run(tick);
        const CharacterBody& body = character.GetComponent<CharacterBody>();
        INFO("tick " << t << " after landing: feet at " << FeetOf(character) << ", velocity.y " << body.velocity.y);
        REQUIRE(std::abs(FeetOf(character)) < restSlop);
        // At most this tick's gravity, which the trim took: the last tick's did not pile up.
        REQUIRE(std::abs(body.velocity.y) < 1.0e-3f);
    }
}

TEST_CASE("A character in the air has no ground, and on the ground its ground fields name the floor",
          "[physics][characters]")
{
    PhysicsApp test;
    Entity floor = AddGround(test.Entities());
    Entity character = AddCharacter(test.Entities(), Math::Vec3(1.0f, 1.0f, -2.0f));
    Drive(test.app, character);

    test.Run(tick);
    {
        const CharacterState& state = character.GetComponent<CharacterBody>().state;
        INFO("a metre up, it is in the air, with every ground field empty");
        REQUIRE(state.ground == GroundState::InAir);
        REQUIRE_FALSE(state.groundBody.IsValid());
        REQUIRE_FALSE(state.groundCollider.IsValid());
        REQUIRE(state.groundPoint == Math::Vec3(0.0f));
        REQUIRE(state.groundNormal == Math::Vec3(0.0f));
    }

    test.Run(1.0);
    const CharacterState& state = character.GetComponent<CharacterBody>().state;
    INFO("landed, it is on the ground, touching the floor beneath its feet, the floor's normal up");
    REQUIRE(state.ground == GroundState::OnGround);
    REQUIRE(state.groundBody == floor);
    REQUIRE(state.groundCollider == floor);
    REQUIRE(state.groundPoint.x == Approx(1.0f).margin(1.0e-3));
    REQUIRE(state.groundPoint.y == Approx(0.0f).margin(restSlop));
    REQUIRE(state.groundPoint.z == Approx(-2.0f).margin(1.0e-3));
    REQUIRE(state.groundNormal.x == Approx(0.0f).margin(1.0e-4));
    REQUIRE(state.groundNormal.y == Approx(1.0f).margin(1.0e-4));
    REQUIRE(state.groundNormal.z == Approx(0.0f).margin(1.0e-4));
    REQUIRE_FALSE(state.onWall);
    REQUIRE_FALSE(state.onCeiling);
}

TEST_CASE("A character's movedVelocity is its last move's displacement over the tick", "[physics][characters]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f, 0.0f, 0.0f));
    std::shared_ptr<Input> input = Drive(test.app, character);
    test.Run(0.5);

    input->walk = Math::Vec3(1.5f, 0.0f, -0.5f);
    for (int t = 0; t < 30; ++t)
    {
        const Math::Vec3 before = PositionOf(character);
        test.Run(tick);
        const Math::Vec3 moved = (PositionOf(character) - before) / static_cast<float>(tick);
        const Math::Vec3 reported = character.GetComponent<CharacterBody>().state.movedVelocity;
        INFO("tick " << t << ": moved at (" << moved.x << ", " << moved.y << ", " << moved.z << "), reported ("
                     << reported.x << ", " << reported.y << ", " << reported.z << ")");
        REQUIRE(reported.x == Approx(moved.x).margin(1.0e-3));
        REQUIRE(reported.y == Approx(moved.y).margin(1.0e-3));
        REQUIRE(reported.z == Approx(moved.z).margin(1.0e-3));
    }
    INFO("walking on flat ground, it moves at the speed game code asked for");
    const Math::Vec3 walked = character.GetComponent<CharacterBody>().state.movedVelocity;
    REQUIRE(walked.x == Approx(1.5f).margin(1.0e-3));
    REQUIRE(walked.z == Approx(-0.5f).margin(1.0e-3));
}

TEST_CASE("A jump into a ceiling clears the character's upward speed and sets onCeiling", "[physics][characters]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    // Its underside 2.0 m up: 0.4 m above the standing character's head.
    AddStatic(test.Entities(), Math::Vec3(0.0f, 2.25f, 0.0f), Math::Vec3(2.0f, 0.25f, 2.0f));
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f, 0.0f, 0.0f));
    std::shared_ptr<Input> input = Drive(test.app, character);
    test.Run(0.5);

    input->jump = true;
    bool hit = false;
    float highestHead = 0.0f;
    for (int t = 0; t < 30 && !hit; ++t)
    {
        test.Run(tick);
        highestHead = std::max(highestHead, PositionOf(character).y + footToCentre);
        hit = character.GetComponent<CharacterBody>().state.onCeiling;
    }
    const CharacterBody& body = character.GetComponent<CharacterBody>();
    INFO("the head rose to " << highestHead << " m; velocity.y " << body.velocity.y);
    REQUIRE(hit);
    REQUIRE(highestHead < 2.0f + restSlop);
    // The jump's 5 m/s less a few ticks of gravity would still be rising: the ceiling took all of it.
    REQUIRE(body.velocity.y <= 1.0e-3f);
    REQUIRE_FALSE(body.state.onWall);

    test.Run(tick);
    INFO("falling away, it no longer touches the ceiling");
    REQUIRE_FALSE(character.GetComponent<CharacterBody>().state.onCeiling);
}

TEST_CASE("Walking into a wall trims the speed going into it and sets onWall", "[physics][characters]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    // A wall across +x, its face at x = 2.
    AddStatic(test.Entities(), Math::Vec3(2.5f, 1.5f, 0.0f), Math::Vec3(0.5f, 1.5f, 3.0f));
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f, 0.0f, 0.0f));
    std::shared_ptr<Input> input = Drive(test.app, character);
    test.Run(0.5);

    // Into the wall and along it.
    input->walk = Math::Vec3(3.0f, 0.0f, 1.0f);
    test.Run(1.5);
    const CharacterBody& body = character.GetComponent<CharacterBody>();
    INFO("at x " << PositionOf(character).x << ": velocity (" << body.velocity.x << ", " << body.velocity.y << ", "
                 << body.velocity.z << ")");
    REQUIRE(PositionOf(character).x < 2.0f - 0.3f + restSlop);
    REQUIRE(body.state.onWall);
    REQUIRE(body.state.ground == GroundState::OnGround);
    INFO("the wall took the speed going into it, and left the speed along it");
    REQUIRE(body.velocity.x == Approx(0.0f).margin(1.0e-3));
    REQUIRE(body.velocity.z == Approx(1.0f).margin(1.0e-3));
    REQUIRE(body.state.movedVelocity.z == Approx(1.0f).margin(1.0e-2));

    input->walk = Math::Vec3(-1.0f, 0.0f, 0.0f);
    test.Run(0.25);
    INFO("walking away, it no longer touches the wall");
    REQUIRE_FALSE(character.GetComponent<CharacterBody>().state.onWall);
}

TEST_CASE("Raycasts, shape casts and overlaps hit a character, naming its entity as body and collider",
          "[physics][characters]")
{
    PhysicsApp test;
    Entity floor = AddGround(test.Entities());
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f, 0.0f, 0.0f));
    Drive(test.app, character);
    test.Run(0.5);
    PhysicsWorld& world = *PhysicsWorld::Of(test.app.GetActiveScene());

    const std::optional<RaycastHit> ray =
        world.Raycast(Math::Vec3(-3.0f, 0.8f, 0.0f), Math::Vec3(1.0f, 0.0f, 0.0f), 10.0f);
    INFO("a ray across the character's middle meets its capsule's side");
    REQUIRE(ray.has_value());
    REQUIRE(ray->body == character);
    REQUIRE(ray->collider == character);
    REQUIRE(ray->point.x == Approx(-0.3f).margin(restSlop));

    const std::optional<ShapeCastHit> cast =
        world.ShapeCast(SphereShape{0.2f}, Transform(Math::Vec3(0.0f, 0.8f, 3.0f), identity, Math::Vec3(1.0f)),
                        Math::Vec3(0.0f, 0.0f, -1.0f), 10.0f);
    INFO("a sphere cast towards it hits it");
    REQUIRE(cast.has_value());
    REQUIRE(cast->body == character);
    REQUIRE(cast->collider == character);

    OverlapHit hits[4];
    const std::size_t count = world.Overlap(BoxShape{Math::Vec3(0.2f)},
                                            Transform(Math::Vec3(0.0f, 1.2f, 0.0f), identity, Math::Vec3(1.0f)), hits);
    INFO("a box overlapping its chest finds only the character");
    REQUIRE(count == 1);
    REQUIRE(hits[0].body == character);
    REQUIRE(hits[0].collider == character);

    INFO("the floor beneath it is found by a ray down past it");
    const std::optional<RaycastHit> down = world.Raycast(Math::Vec3(0.0f, 3.0f, 0.0f), Math::Vec3(0.0f, -1.0f, 0.0f),
                                                         10.0f, QueryFilter{.ignoreBody = character});
    REQUIRE(down.has_value());
    REQUIRE(down->body == floor);
}

TEST_CASE("A character's Collider makes no static body where the character was made", "[physics][characters]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    const Math::Vec3 start(0.0f, 3.0f, 0.0f);
    Entity character = AddCharacter(test.Entities(), start);
    Drive(test.app, character);
    test.Run(1.5);

    PhysicsWorld& world = *PhysicsWorld::Of(test.app.GetActiveScene());
    OverlapHit hits[4];
    const std::size_t where = world.OverlapPoint(start + Math::Vec3(0.0f, footToCentre, 0.0f), hits);
    INFO("nothing is left where it started, 3 m up");
    REQUIRE(where == 0);
    const std::size_t now = world.OverlapPoint(PositionOf(character), hits);
    INFO("where it landed, only the character is found");
    REQUIRE(now == 1);
    REQUIRE(hits[0].body == character);
}

TEST_CASE("A CharacterBody removed and added again in one tick is rebuilt", "[physics][characters]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f, 0.0f, 0.0f));
    std::shared_ptr<Input> input = Drive(test.app, character);
    test.Run(0.5);

    character.RemoveComponent<CharacterBody>();
    character.AddComponent<CharacterBody>();
    input->walk = Math::Vec3(1.0f, 0.0f, 0.0f);
    test.Run(1.0);

    const CharacterBody& body = character.GetComponent<CharacterBody>();
    INFO("the new CharacterBody is a character: it walked a metre in a second and stands on the floor");
    REQUIRE(PositionOf(character).x == Approx(1.0f).margin(0.02));
    REQUIRE(FeetOf(character) == Approx(0.0f).margin(restSlop));
    REQUIRE(body.state.ground == GroundState::OnGround);
    OverlapHit hits[4];
    const std::size_t count = PhysicsWorld::Of(test.app.GetActiveScene())->OverlapPoint(PositionOf(character), hits);
    INFO("one body stands for it, not the old one as well");
    REQUIRE(count == 1);
    REQUIRE(hits[0].body == character);
    REQUIRE(test.errors.empty());
    REQUIRE(test.warnings.empty());
}

TEST_CASE("A CharacterBody removed leaves its Collider a static body where the character stood",
          "[physics][characters]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f, 0.0f, 0.0f));
    Drive(test.app, character);
    test.Run(0.5);
    const Math::Vec3 stood = PositionOf(character);

    character.RemoveComponent<CharacterBody>();
    Entity box = AddBody(test.Entities(), stood + Math::Vec3(0.0f, 2.0f, 0.0f), BoxShape{Math::Vec3(0.2f)});
    test.Run(1.5);

    INFO("a box dropped on it rests on the capsule's top, 0.8 m above its centre; it is at " << PositionOf(box).y);
    REQUIRE(PositionOf(box).y == Approx(stood.y + footToCentre + 0.2f).margin(restSlop));
    REQUIRE(PositionOf(character).y == Approx(stood.y).margin(0.0));
}

TEST_CASE("Destroying a character's entity, its scene's world or the Physics owner is clean", "[physics][characters]")
{
    std::vector<std::string> errors;
    App app(std::make_unique<SilentLogger>(nullptr, &errors));
    std::optional<Physics> physics(Physics::Install(app, StepPosesOnly()));
    EntityList& entities = app.GetActiveScene().GetEntityList();
    AddGround(entities);
    Entity gone = AddCharacter(entities, Math::Vec3(0.0f, 0.0f, 0.0f));
    AddCharacter(entities, Math::Vec3(2.0f, 1.0f, 0.0f));
    for (int i = 0; i < 30; ++i)
    {
        app.RunFrame(tick);
    }

    entities.DestroyEntity(gone);
    app.RunFrame(tick);
    OverlapHit hits[4];
    INFO("the destroyed character's body is gone with it");
    REQUIRE(PhysicsWorld::Of(app.GetActiveScene())->OverlapPoint(Math::Vec3(0.0f, footToCentre, 0.0f), hits) == 0);

    Scene& other = app.CreateScene("Other");
    AddCharacter(other.GetEntityList(), Math::Vec3(0.0f, 1.0f, 0.0f));
    const std::string first = app.GetActiveScene().GetName();
    app.ChangeActiveScene("Other");
    app.RunFrame(tick);
    app.ChangeActiveScene(first);
    app.DestroyScene("Other");
    app.RunFrame(tick);

    INFO("the Physics owner ends the world holding the other character");
    physics.reset();
    REQUIRE(PhysicsWorld::Of(app.GetActiveScene()) == nullptr);
    REQUIRE(errors.empty());
}

namespace
{
    // Where a character walking into a pile of boxes and jumping was, and how fast, each tick for five seconds.
    std::vector<Math::Vec3> WalkIntoBoxes(const PhysicsConfig& config)
    {
        PhysicsApp test(config);
        AddGround(test.Entities());
        for (int i = 0; i < 4; ++i)
        {
            AddBody(test.Entities(),
                    Math::Vec3(2.0f, 0.25f + 0.5f * static_cast<float>(i), 0.1f * static_cast<float>(i)),
                    BoxShape{Math::Vec3(0.25f)});
        }
        Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f, 0.5f, 0.0f));
        std::shared_ptr<Input> input = Drive(test.app, character);
        input->walk = Math::Vec3(2.0f, 0.0f, 0.3f);

        std::vector<Math::Vec3> trace;
        for (int t = 0; t < 300; ++t)
        {
            input->jump = t % 70 == 40;
            test.Run(tick);
            trace.push_back(PositionOf(character));
            trace.push_back(character.GetComponent<CharacterBody>().velocity);
            trace.push_back(character.GetComponent<CharacterBody>().state.movedVelocity);
        }
        return trace;
    }

    bool BitIdentical(const std::vector<Math::Vec3>& a, const std::vector<Math::Vec3>& b)
    {
        return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(Math::Vec3)) == 0;
    }
} // namespace

TEST_CASE("A character walking into boxes and jumping moves bit for bit the same on every run, on one thread or many",
          "[physics][characters]")
{
    PhysicsConfig single = StepPosesOnly();
    single.useWorkerThreads = false;
    PhysicsConfig workers = StepPosesOnly();
    workers.useWorkerThreads = true;

    const std::vector<Math::Vec3> first = WalkIntoBoxes(single);
    INFO("two runs on one thread agree");
    REQUIRE(BitIdentical(first, WalkIntoBoxes(single)));
    INFO("a run on the worker threads agrees with one on one thread");
    REQUIRE(BitIdentical(first, WalkIntoBoxes(workers)));
}

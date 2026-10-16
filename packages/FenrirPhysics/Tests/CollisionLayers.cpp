// Collision layers and masks through the frame seam: two bodies touch only when each one's mask includes a layer of
// the other.

#include <cstdint>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "PhysicsTestApp.hpp"

using namespace Fenrir;
using namespace PhysicsTest;
using Catch::Approx;

namespace
{
    constexpr std::uint32_t Layer(int index)
    {
        return 1u << index;
    }

    constexpr std::uint32_t everything = ~0u;
} // namespace

TEST_CASE("A body whose mask excludes the ground's layers falls through it", "[physics][layers]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 2.0f, 0.0f));
    box.AddComponent<CollisionFilter>(CollisionFilter{.layers = Layer(1), .mask = Layer(1)});

    test.Run(2.0);

    INFO("the ground is in layer 0, which the box's mask leaves out, so the box falls through it; it is at "
         << PositionOf(box).y);
    REQUIRE(PositionOf(box).y < -5.0f);
}

TEST_CASE("A one-sided mask does not make two bodies collide", "[physics][layers]")
{
    PhysicsApp test;
    Entity ground = AddGround(test.Entities());
    ground.AddComponent<CollisionFilter>(CollisionFilter{.layers = Layer(0), .mask = Layer(0)});
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 2.0f, 0.0f));
    box.AddComponent<CollisionFilter>(CollisionFilter{.layers = Layer(1), .mask = everything});

    test.Run(2.0);

    INFO("the box's mask includes the ground's layer, but the ground's mask leaves out the box's layer, and both must "
         "agree: the box falls through; it is at "
         << PositionOf(box).y);
    REQUIRE(PositionOf(box).y < -5.0f);
}

TEST_CASE("A body without a CollisionFilter collides with a body in any layer that collides with layer 0",
          "[physics][layers]")
{
    PhysicsApp test;
    Entity ground = AddGround(test.Entities());
    ground.AddComponent<CollisionFilter>(CollisionFilter{.layers = Layer(31), .mask = Layer(0)});
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 2.0f, 0.0f));

    test.Run(3.0);

    INFO("the box is in layer 0 and collides with every layer, so it rests on the ground in layer 31; it is at "
         << PositionOf(box).y);
    REQUIRE(PositionOf(box).y == Approx(0.5f).margin(restSlop));
}

TEST_CASE("Bodies in more than 16 layers at once each collide only with their own layer", "[physics][layers]")
{
    PhysicsApp test;
    constexpr int layerCount = 32;
    std::vector<Entity> resting;
    std::vector<Entity> falling;
    for (int i = 0; i < layerCount; ++i)
    {
        // A pad in layer i, a box in layer i above it, and a box in the next layer beside that.
        const float x = static_cast<float>(i) * 4.0f;
        Entity pad = Place(test.Entities(), Math::Vec3(x, -0.5f, 0.0f));
        pad.AddComponent<Collider>(Collider{BoxShape{Math::Vec3(1.5f, 0.5f, 1.5f)}});
        pad.AddComponent<CollisionFilter>(CollisionFilter{.layers = Layer(i), .mask = Layer(i)});

        Entity same = AddBody(test.Entities(), Math::Vec3(x - 0.75f, 2.0f, 0.0f));
        same.AddComponent<CollisionFilter>(CollisionFilter{.layers = Layer(i), .mask = Layer(i)});
        resting.push_back(same);

        const int next = (i + 1) % layerCount;
        Entity other = AddBody(test.Entities(), Math::Vec3(x + 0.75f, 2.0f, 0.0f));
        other.AddComponent<CollisionFilter>(CollisionFilter{.layers = Layer(next), .mask = Layer(next)});
        falling.push_back(other);
    }

    test.Run(3.0);

    for (int i = 0; i < layerCount; ++i)
    {
        INFO("the box in layer " << i << " rests on the pad in its own layer; it is at " << PositionOf(resting[i]).y);
        REQUIRE(PositionOf(resting[i]).y == Approx(0.5f).margin(restSlop));
        INFO("the box beside it, in layer " << (i + 1) % layerCount << ", falls through; it is at "
                                            << PositionOf(falling[i]).y);
        REQUIRE(PositionOf(falling[i]).y < -5.0f);
    }
}

TEST_CASE("Changing a resting body's mask at run time takes effect on the next tick", "[physics][layers]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 0.5f, 0.0f));
    box.AddComponent<CollisionFilter>();
    test.Run(3.0); // long enough to fall asleep
    REQUIRE(PositionOf(box).y == Approx(0.5f).margin(restSlop));

    box.GetComponent<CollisionFilter>().mask = Layer(1);
    test.Run(tick);
    const float speed = box.GetComponent<RigidBody>().linearVelocity.y;
    INFO("the tick after its mask leaves out the ground's layer, the box starts to fall (it moves at " << speed << ")");
    REQUIRE(speed < -0.1f);

    test.Run(2.0);
    INFO("and falls through the ground; it is at " << PositionOf(box).y);
    REQUIRE(PositionOf(box).y < -5.0f);
}

TEST_CASE("Changing the ground's layers at run time takes effect on the next tick", "[physics][layers]")
{
    PhysicsApp test;
    Entity ground = AddGround(test.Entities());
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 0.5f, 0.0f));
    box.AddComponent<CollisionFilter>(CollisionFilter{.layers = Layer(0), .mask = Layer(0)});
    test.Run(3.0); // long enough to fall asleep
    REQUIRE(PositionOf(box).y == Approx(0.5f).margin(restSlop));

    ground.AddComponent<CollisionFilter>(CollisionFilter{.layers = Layer(2), .mask = everything});
    test.Run(tick);
    const float speed = box.GetComponent<RigidBody>().linearVelocity.y;
    INFO("the tick after the ground moves to a layer the box's mask leaves out, the box starts to fall (it moves at "
         << speed << ")");
    REQUIRE(speed < -0.1f);

    ground.GetComponent<CollisionFilter>().layers = Layer(0);
    test.Run(tick);
    const float stopped = box.GetComponent<RigidBody>().linearVelocity.y;
    INFO("moved back to layer 0 the next tick, the ground catches the box again (it moves at " << stopped << ")");
    REQUIRE(stopped > -0.1f);
}

TEST_CASE("Collision layers named in PhysicsConfig are looked up by name", "[physics][layers]")
{
    App app{std::make_unique<SilentLogger>()};
    PhysicsConfig config;
    config.collisionLayers = {{"Default"}, {"Player", Layer(0) | Layer(2)}, {"Enemy", Layer(0) | Layer(1)}};
    Physics physics = Physics::Install(app, config);
    config.collisionLayers.push_back({"Late"});

    const CollisionLayers& layers = physics.Layers();
    INFO("Player is the second name, so it is layer 1");
    REQUIRE(layers.Bit("Player") == Layer(1));
    REQUIRE(layers.Name(1) == "Player");
    INFO("a filter for Enemy puts a body in layer 2 with Enemy's default mask");
    REQUIRE(layers.Filter("Enemy").layers == Layer(2));
    REQUIRE(layers.Filter("Enemy").mask == (Layer(0) | Layer(1)));
    INFO("a layer named without a mask collides with every layer by default");
    REQUIRE(layers.Filter("Default").mask == everything);
    INFO("the names are fixed at Install: one added to the config afterwards is not a layer");
    REQUIRE(layers.Bit("Late") == 0u);
    REQUIRE(layers.Name(3).empty());
    INFO("an unknown name is in no layer, so a filter made from it collides with nothing");
    REQUIRE(layers.Filter("Nobody").layers == 0u);
}

TEST_CASE("Bodies given filters by layer name collide by their layers' default masks", "[physics][layers]")
{
    App app{std::make_unique<SilentLogger>()};
    PhysicsConfig config;
    config.collisionLayers = {{"Default"}, {"Ghost", Layer(1)}};
    Physics physics = Physics::Install(app, config);
    EntityList& entities = app.GetActiveScene().GetEntityList();
    AddGround(entities);
    Entity ghost = AddBody(entities, Math::Vec3(0.0f, 2.0f, 0.0f));
    ghost.AddComponent<CollisionFilter>(physics.Layers().Filter("Ghost"));

    for (int i = 0; i < 120; ++i)
    {
        app.RunFrame(tick);
    }

    INFO("a Ghost collides only with Ghosts by default, so it falls through the ground; it is at "
         << PositionOf(ghost).y);
    REQUIRE(PositionOf(ghost).y < -5.0f);
}

TEST_CASE("Running out of collision filters logs the filter that missed out by its layers' names", "[physics][layers]")
{
    PhysicsConfig config = StepPosesOnly();
    config.collisionLayers = {CollisionLayer{.name = "Ground"}, CollisionLayer{.name = "Player"},
                              CollisionLayer{.name = "Enemy"}};
    PhysicsApp test(config);
    // The backend holds 65535 filters at once, a few of them its own: this many distinct masks (on layers 3 and up,
    // which have no names) uses them all.
    constexpr std::uint32_t fillers = 65535;
    for (std::uint32_t i = 0; i < fillers; ++i)
    {
        Entity filler = Place(test.Entities(), Math::Vec3(0.0f, -1000.0f, 0.0f));
        filler.AddComponent<Collider>();
        filler.AddComponent<CollisionFilter>(CollisionFilter{.layers = Layer(3), .mask = i << 3});
    }
    Entity player = AddBody(test.Entities(), Math::Vec3(0.0f, 2.0f, 0.0f));
    player.AddComponent<CollisionFilter>(CollisionFilter{.layers = Layer(1), .mask = Layer(0) | Layer(2)});

    test.Run(tick);

    INFO("running out of collision filters did not log an error");
    REQUIRE_FALSE(test.errors.empty());
    const std::string& error = test.errors.back();
    INFO("the error does not name the player's filter by its layers' names: " << error);
    REQUIRE(error.find("Player") != std::string::npos);
    REQUIRE(error.find("Ground") != std::string::npos);
    REQUIRE(error.find("Enemy") != std::string::npos);
}

#pragma once

// What the character tests share: a standing capsule character, static blocks, and the game code that drives it.

#include <memory>

#include "FenrirPhysics/Characters.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"
#include "PhysicsTestApp.hpp"

namespace PhysicsTest
{
    // A capsule 1.6 m tall and 0.3 m round: its centre is 0.8 m above its feet.
    inline constexpr float footToCentre = 0.8f;

    inline Fenrir::Entity AddCharacter(Fenrir::EntityList& entities, const Fenrir::Math::Vec3& feet)
    {
        Fenrir::Entity character = Place(entities, feet + Fenrir::Math::Vec3(0.0f, footToCentre, 0.0f));
        character.AddComponent<Fenrir::Collider>(
            Fenrir::Collider{Fenrir::CapsuleShape{.halfHeight = 0.5f, .radius = 0.3f}});
        character.AddComponent<Fenrir::CharacterBody>();
        return character;
    }

    inline Fenrir::Entity AddStatic(Fenrir::EntityList& entities, const Fenrir::Math::Vec3& centre,
                                    const Fenrir::Math::Vec3& halfExtents)
    {
        Fenrir::Entity block = Place(entities, centre);
        block.AddComponent<Fenrir::Collider>(Fenrir::Collider{Fenrir::BoxShape{halfExtents}});
        return block;
    }

    inline float FeetOf(const Fenrir::Entity& character)
    {
        return PositionOf(character).y - footToCentre;
    }

    // What game code asks of a character each tick.
    struct Input
    {
        Fenrir::Math::Vec3 walk = Fenrir::Math::Vec3(0.0f);
        bool jump = false;
    };

    inline constexpr float jumpSpeed = 5.0f;

    // The game code the spec asks for, in Tick: horizontal speed from input, gravity added to what physics wrote back,
    // and a jump only from walkable ground.
    inline std::shared_ptr<Input> Drive(Fenrir::App& app, const Fenrir::Entity& character)
    {
        auto input = std::make_shared<Input>();
        app.AddSequentialSystems(
            Fenrir::SchedulePriority::Tick, {[input, character](Fenrir::App& running) {
                const Fenrir::PhysicsWorld* world = Fenrir::PhysicsWorld::Of(running.GetActiveScene());
                if (world == nullptr || !character.IsValid() || !character.HasComponent<Fenrir::CharacterBody>())
                {
                    return;
                }
                const float dt = static_cast<float>(running.GetTime().tickRate);
                Fenrir::CharacterBody& body = character.GetComponent<Fenrir::CharacterBody>();
                body.velocity.x = input->walk.x;
                body.velocity.z = input->walk.z;
                body.velocity.y += world->Gravity().y * dt;
                if (input->jump && body.state.ground == Fenrir::GroundState::OnGround)
                {
                    body.velocity.y = jumpSpeed;
                    input->jump = false;
                }
            }});
        return input;
    }
} // namespace PhysicsTest

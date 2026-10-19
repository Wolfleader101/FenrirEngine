#pragma once

#include <cstdint>

#include "FenrirECS/Entity.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir
{
    /** @brief What a character body stood on at the end of its last move. */
    enum class GroundState : std::uint8_t
    {
        /** @brief Touching nothing. */
        InAir,
        /** @brief Standing on walkable ground: no steeper than its `maxSlope`. */
        OnGround,
        /** @brief Standing on ground steeper than its `maxSlope`. */
        OnSteepGround,
        /** @brief Touching something, a wall or a ledge, with nothing underneath. */
        Touching
    };

    /**
     * @brief How a character body's last move left it: written by physics into CharacterBody::state after every move,
     * against the bodies' poses before that tick's step. Game code's edits to it are overwritten.
     */
    struct CharacterState
    {
        GroundState ground = GroundState::InAir;
        /** @brief The body entity of the ground (a rigid body or a character body), on ground only; empty otherwise. */
        Entity groundBody;
        /** @brief The collider entity of the ground touched, on ground only; empty otherwise. */
        Entity groundCollider;
        /** @brief World space, where the character touches its ground, on ground only. */
        Math::Vec3 groundPoint = Math::Vec3(0.0f);
        /** @brief World space, unit length, up from the ground, averaged over the ground contacts, on ground only. */
        Math::Vec3 groundNormal = Math::Vec3(0.0f);
        /** @brief m/s, the ground's velocity at the character, on ground only. */
        Math::Vec3 groundVelocity = Math::Vec3(0.0f);
        /** @brief rad/s, world space, the ground's angular velocity, on ground only. */
        Math::Vec3 groundAngularVelocity = Math::Vec3(0.0f);
        /**
         * @brief m/s, the last move's displacement over the tick's time: how fast the character actually moved, which
         * `velocity` (trimmed, and never measured from displacement) does not say.
         */
        Math::Vec3 movedVelocity = Math::Vec3(0.0f);
        /** @brief m/s, the velocity of the ground the character left, added to each move until it lands. */
        Math::Vec3 carriedVelocity = Math::Vec3(0.0f);
        /** @brief Touching a wall at the end of the move: a surface too steep to stand on that is not its ground. */
        bool onWall = false;
        /** @brief Touching a ceiling at the end of the move: a surface facing down. */
        bool onCeiling = false;
        /** @brief The Collider's shape or scale does not fit where the character is yet: it keeps the old one. */
        bool shapeBlocked = false;
    };

    /**
     * @brief Makes the entity's own Collider a character body (ADR-0014): collide-and-slide, moved by `velocity`, and
     * seen by other bodies, queries and triggers as a kinematic body of its shape, naming its entity. There is no move
     * call: game code writes `velocity` in `Tick`, and physics moves the character by it once in `PostTick`, before
     * the step, characters one at a time in entity order.
     *
     * `velocity` is input and output, like RigidBody::linearVelocity. After the move physics writes it back with the
     * parts that went into what the character touches removed: ground takes only the falling part, and a wall or
     * ceiling the part going into it. Landing clears falling speed and a ceiling clears upward speed. It is never
     * measured from displacement, so game code wanting the speed it asked for keeps its own copy, and
     * `state.movedVelocity` is how fast the character actually moved. Gravity is game code's: physics adds none, so add
     * `PhysicsWorld::Gravity() * dt` each tick, and set `velocity.y` to jump.
     *
     * Its shape is the Collider on its entity, whose PhysicsMaterial and CollisionFilter apply to it as to any body;
     * that Collider makes no static body. The Transform sits at the collider's centre. Physics writes back only the
     * position; the shape takes the entity's full world rotation, and up is always world +Y.
     */
    struct CharacterBody
    {
        /** @brief World space, m/s, read fresh every tick and written back trimmed by what the character touches. */
        Math::Vec3 velocity = Math::Vec3(0.0f);
        /** @brief Radians (45°): ground steeper than this is not walkable. 0 turns slope limiting off. */
        float maxSlope = 0.785f;
        /** @brief Metres: the highest step the character walks up. 0 turns stair walking off. */
        float stepHeight = 0.4f;
        /** @brief Metres: how far down the character sticks to the ground walking down slopes and steps. 0 turns it
         * off. */
        float floorSnap = 0.5f;
        /** @brief Newtons: the most force a push applies to a dynamic body. 0 turns pushing off. */
        float pushForce = 100.0f;
        /** @brief Kilograms: only the character's weight on its ground. 0 turns it off. */
        float mass = 70.0f;
        /** @brief On walkable ground, the falling part of `velocity` does not slide the character down a slope. */
        bool stopOnSlope = true;
        /** @brief As RigidBody::interpolate: position and rotation blend between the last two steps. */
        bool interpolate = true;
        /** @brief Disabled: not moved, invisible to bodies, queries and triggers, and nothing written back. */
        bool enabled = true;
        /** @brief Read-back: overwritten by physics after every move. */
        CharacterState state;
    };
} // namespace Fenrir

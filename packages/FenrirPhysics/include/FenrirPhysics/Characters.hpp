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
        /** @brief m/s, the ground's velocity at the character, on ground only. Physics adds it to the next move on
         * walkable ground, never to `velocity`. */
        Math::Vec3 groundVelocity = Math::Vec3(0.0f);
        /** @brief rad/s, world space, the ground's angular velocity, on ground only. Physics never turns the character
         * with its ground: game code that wants it turned with a turntable turns it by this. */
        Math::Vec3 groundAngularVelocity = Math::Vec3(0.0f);
        /**
         * @brief m/s, the last move's displacement over the tick's time: how fast the character actually moved, which
         * `velocity` (trimmed, and never measured from displacement) does not say.
         */
        Math::Vec3 movedVelocity = Math::Vec3(0.0f);
        /**
         * @brief m/s, the whole velocity of the walkable ground the character left (walking or jumping off), added to
         * each move and trimmed by what it hits, as `velocity` is, until it lands. A teleport clears it. It is never in
         * `velocity`; `movedVelocity` includes it.
         */
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
     * A character rides the walkable ground it stands on, a lift, a shuttle, a raft or a turntable, with no game code:
     * the ground's velocity at the character is added to its move, never to `velocity`, which is trimmed in the
     * ground's frame, so a descending lift's speed is never taken for falling. Steep ground is not ridden. Leaving the
     * ground keeps its velocity as `state.carriedVelocity` until landing. Moving before the step, a character trails a
     * descending platform by one tick of travel (2.5 cm at 1.5 m/s), still on the ground; where a lift turns from down
     * to up, that gap leaves it in the air for 1 to 4 ticks.
     *
     * Its shape is the Collider on its entity, whose PhysicsMaterial and CollisionFilter apply to it as to any body;
     * that Collider makes no static body. The Transform sits at the collider's centre. Physics writes back only the
     * position; the shape takes the entity's full world rotation, and up is always world +Y.
     *
     * Writing the Transform's world position (moving its parent included) teleports the character: no sweep, its
     * contacts and `state` found from that tick's move where it now is, `velocity` kept, and no blend across the jump.
     * Writing only its rotation turns the shape where it stands, with no sweep, and is not a teleport: it blends, and
     * the next move pushes the shape out of anything it now overlaps. Turn it in `Tick`, as a kinematic body is moved.
     * A parented character moves in world space: its local Transform is written back against its parent's world
     * transform, parents before children.
     *
     * It is inactive beside a RigidBody (which works instead, its Collider then the RigidBody's), with no Collider, or
     * with a MeshShape: the component is kept, one warning is logged each time it becomes so, and it is a character
     * again by itself once it can be. A solid collider on a child entity with no RigidBody of its own is not part of
     * the character and makes no body, with one warning; a child Trigger works as beneath any body, and the character
     * never sets it off. A joint naming a character is inactive. While disabled or inactive, nothing is written back
     * but `state`, which reads as the defaults.
     *
     * Other bodies: a move that hits a dynamic body pushes it towards the character's speed at the contact, aiming at
     * 0.9 × the closing speed (so a light box keeps pace and is never launched), with at most `pushForce` on each body
     * and never downward; static and kinematic bodies are not pushed. A move that ends on ground presses the ground
     * body with `mass` under the world's gravity, scaled by how the ground faces it, so a character tips a seesaw or
     * sinks a raft. Both act in the step after the move. A dynamic body never pushes a character: it meets the
     * character as an immovable wall and stops or bounces off, and knockback is game code writing `velocity`. A
     * kinematic body (a door, a paddle) moving into a character shoves it at exactly the body's speed, whatever its
     * `mass`, and that speed shows in the written-back `velocity` until game code overwrites it. Stated limits: a fast
     * kinematic body may overlap a character for a tick until the next move pushes it out; a character caught between
     * a kinematic body and a wall is not crushed or detected, but stays overlapped and stuck (`state.onWall` and the
     * contacts show it); and a body resting on a character (a box on its head) is not carried along, but slips or is
     * pushed out, though a jump still pushes it up.
     *
     * Bodies, queries and triggers meet a character as a kinematic body of its shape, named by its entity as body and
     * collider: triggers report it during the step, `QueryFilter::ignoreBody` skips it, and hits carry no character
     * flag. Its entity's CollisionFilter filters both what meets it and its own moves, under the one symmetric rule, so
     * a character ignoring a layer walks through that layer's bodies, and those bodies, triggers and queries pass
     * through it; its entity's PhysicsMaterial is what bodies hitting or resting on it feel, and never changes how it
     * moves. Edits to either apply from the next tick. Characters block each other and neither pushes the other: one
     * standing on another rides it (`state.groundBody` names it), and one standing on this character never holds it
     * down, so a jump lifts it. In entity order, a character's move meets the others where their last moves left them.
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
        /** @brief Newtons: the most force a move's push applies to each dynamic body it hits. 0 turns pushing off. */
        float pushForce = 100.0f;
        /** @brief Kilograms: only the character's weight on its ground; it does not change pushing or being pushed. 0
         * turns it off. */
        float mass = 70.0f;
        /** @brief On walkable ground, the falling part of `velocity` does not slide the character down a slope. */
        bool stopOnSlope = true;
        /** @brief As RigidBody::interpolate: between ticks the Transform holds its position and rotation blended
         * between the last two steps, stair step-ups and ground snaps included, while `Tick` systems see the true one.
         * A teleport and a new character snap. PhysicsConfig can turn interpolation off for every character. */
        bool interpolate = true;
        /** @brief Disabled: not moved, invisible to bodies, queries and triggers, `velocity` and the Transform left to
         * game code, and `state` reset. Enabled again, it finds its contacts and ground where it is that tick, as a
         * teleport does, and is not blended from where it was. */
        bool enabled = true;
        /** @brief Read-back: overwritten by physics after every move. */
        CharacterState state;
    };
} // namespace Fenrir

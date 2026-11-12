#pragma once

#include <string>
#include <vector>

#include "FenrirECS/Entity.hpp"
#include "FenrirRagdoll/Description.hpp"

namespace Fenrir
{
    class App;
    class Scene;

    /** @brief What `BuildRagdoll` spawned: game code's to edit, query and draw, and `DestroyRagdoll`'s to remove. */
    struct SpawnedRagdoll
    {
        /** @brief The entity the bodies and joints sit under, at the top of the scene. */
        Entity parent;
        /** @brief One body entity per part, in the description's order: the root part's first. */
        std::vector<Entity> bodies;
        /** @brief One joint entity per part but the root, in the description's order. */
        std::vector<Entity> joints;
        /** @brief Why nothing was spawned; empty on success. */
        std::string error;
    };

    /**
     * @brief Spawns `description` as a ragdoll of `skeleton`, a skeleton entity in `scene`; `app` reaches the asset
     * store for the skeleton asset's rest pose.
     *
     * Under a new parent entity it spawns one body entity per part, root first: a kinematic `RigidBody` with the
     * part's mass, a `Collider` with its shape, a `RagdollBody` with its bone, offset and strength scale, and a
     * `CollisionGroup` with the description's group, or the skeleton entity's id when that is 0 (`Entity::Null` for
     * the id 0, which is no group). Each body is placed at once at its bone's rest pose × offset under the skeleton
     * entity's world transform, and the first tick's follow moves it onto the pose. Then one `Joint` entity per part
     * but the root, root first, joining the parent part's body (`bodyA`) to the part's (`bodyB`), with frames from the
     * part's `jointFrame` rotation and `jointAnchor` through the bodies' rest world poses (`FrameFromWorld`),
     * `collideConnected` false, friction `jointFriction` and the motor off. Last it adds `Ragdoll` to the skeleton
     * entity: `Following`, the group, the root part's body as `rootBody`, the description's strength.
     *
     * Spawns nothing and returns an `error` for a description with an error or no parts, a skeleton entity without a
     * `Ready` `SkeletonPose`, one that already has a `Ragdoll`, a part naming a bone the skeleton lacks, or a part
     * whose parent is not an earlier part's bone. A structural change: call it outside the pose pipeline and the
     * physics step, as any entity creation.
     */
    SpawnedRagdoll BuildRagdoll(App& app, Scene& scene, Entity skeleton, const RagdollDescription& description);

    /**
     * @brief Removes `skeleton`'s ragdoll: the parent entity `BuildRagdoll` spawned (the root body's parent) and every
     * entity under it, the pose override entries on those bodies' bones (game code's entries on other bones stay),
     * and its `Ragdoll`. Bodies game code added elsewhere are left, as inactive ragdoll bodies. Does nothing to an
     * entity with no `Ragdoll`.
     */
    void DestroyRagdoll(Scene& scene, Entity skeleton);
} // namespace Fenrir

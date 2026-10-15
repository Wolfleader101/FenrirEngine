#pragma once

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"

namespace Fenrir
{
    /**
     * @brief The entity's world transform: where it is in the scene, composed from its own Transform and its
     * ancestors' through Relationship, computed on each call (never stored, so it is always current).
     *
     * Composed one parent at a time as
     * `pos = parentPos + parentRot * (parentScale * localPos)`, `rot = parentRot * localRot`,
     * `scale = parentScale * localScale` (component-wise). The scale is approximate when a parent's non-uniform scale
     * meets a child's rotation (shear, which position, rotation and scale cannot hold); GetWorldMatrix is exact.
     *
     * An entity with no parent gives its Transform. A missing Transform counts as the identity.
     */
    [[nodiscard]] Transform GetWorldTransform(const Entity& entity);

    /**
     * @brief The entity's exact world matrix: the product of the local matrices (TransformToMat4) from the root down
     * to the entity, shear included, computed on each call. For rendering.
     *
     * It agrees with TransformToMat4(GetWorldTransform(entity)) except under shear: a parent's non-uniform scale with
     * a rotated child, where a collider built from the world transform can differ from the rendered mesh.
     */
    [[nodiscard]] Math::Mat4 GetWorldMatrix(const Entity& entity);

    /**
     * @brief Sets the entity's Transform so that GetWorldTransform gives `world`: the inverse of its composition
     * against the parent's world transform,
     * `local.pos = inverse(parentRot) * (world.pos - parentPos) / parentScale`, `local.rot = inverse(parentRot) *
     * world.rot`, `local.scale = world.scale / parentScale` (component-wise). With no parent it sets the Transform to
     * `world`. Does nothing to an entity with no Transform.
     *
     * A parent scale with a zero component has no inverse; the result is then not finite.
     */
    void SetWorldTransform(const Entity& entity, const Transform& world);
} // namespace Fenrir

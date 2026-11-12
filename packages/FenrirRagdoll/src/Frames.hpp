#pragma once

// Pose arithmetic the description, the builder and the glue share: rest poses in model space, and position-rotation
// poses composed with offsets. Private to FenrirRagdoll.

#include <cmath>
#include <cstddef>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "FenrirAssets/SkeletonData.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir::Detail
{
    /** @brief A position and rotation: a bone's or body's pose, scale dropped. */
    struct Pose
    {
        Math::Vec3 pos = Math::Vec3(0.0f);
        Math::Quat rot = Math::Quat(1.0f, 0.0f, 0.0f, 0.0f);
    };

    /** @brief Every bone's rest pose in model space, parents first (rest scales are dropped). */
    inline std::vector<Pose> RestPoses(const SkeletonData& skeleton)
    {
        std::vector<Pose> poses(skeleton.BoneCount());
        for (std::size_t i = 0; i < poses.size(); ++i)
        {
            const SkeletonBone& bone = skeleton.bones[i];
            const Pose parent = bone.parent == NoBone ? Pose{} : poses[static_cast<std::size_t>(bone.parent)];
            poses[i].pos = parent.pos + Math::Rotate(parent.rot, bone.rest.pos);
            poses[i].rot = Math::Normalized(parent.rot * bone.rest.rot);
        }
        return poses;
    }

    /** @brief `outer × inner`: `inner` given relative to `outer`, in `outer`'s space. */
    inline Pose Compose(const Pose& outer, const Pose& inner)
    {
        return Pose{outer.pos + Math::Rotate(outer.rot, inner.pos), Math::Normalized(outer.rot * inner.rot)};
    }

    /** @brief `bone × offset`: a body's pose from its bone's. */
    inline Pose Compose(const Pose& bone, const Transform& offset)
    {
        return Compose(bone, Pose{offset.pos, offset.rot});
    }

    /** @brief The position and rotation of an affine matrix with no shear; its scale is dropped. */
    inline Pose PoseOf(const Math::Mat4& matrix)
    {
        const Math::Mat3 axes(Math::Normalized(Math::Vec3(matrix[0])), Math::Normalized(Math::Vec3(matrix[1])),
                              Math::Normalized(Math::Vec3(matrix[2])));
        return Pose{Math::Vec3(matrix[3]), Math::Normalized(glm::quat_cast(axes))};
    }

    /** @brief The shortest rotation turning unit `from` onto unit `to`. */
    inline Math::Quat FromTo(const Math::Vec3& from, const Math::Vec3& to)
    {
        const float dot = Math::Dot(from, to);
        if (dot < -0.9999f)
        {
            const Math::Vec3 other =
                std::abs(from.x) < 0.9f ? Math::Vec3(1.0f, 0.0f, 0.0f) : Math::Vec3(0.0f, 0.0f, 1.0f);
            return Math::AngleAxis(glm::pi<float>(), Math::Normalized(Math::Cross(from, other)));
        }
        const Math::Vec3 axis = Math::Cross(from, to);
        return Math::Normalized(Math::Quat(1.0f + dot, axis.x, axis.y, axis.z));
    }

    /** @brief The rotation whose X axis is unit `x`, with its Z as near `reference` as a right angle to `x` allows. */
    inline Math::Quat BasisWithX(const Math::Vec3& x, Math::Vec3 reference)
    {
        if (std::abs(Math::Dot(x, reference)) > 0.9f)
        {
            reference = std::abs(x.y) < 0.9f ? Math::Vec3(0.0f, 1.0f, 0.0f) : Math::Vec3(1.0f, 0.0f, 0.0f);
        }
        const Math::Vec3 y = Math::Normalized(Math::Cross(reference, x));
        const Math::Vec3 z = Math::Cross(x, y);
        return Math::Normalized(glm::quat_cast(Math::Mat3(x, y, z)));
    }
} // namespace Fenrir::Detail

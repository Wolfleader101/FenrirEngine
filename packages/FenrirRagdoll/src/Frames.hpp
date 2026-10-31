#pragma once

// Rest-pose and frame arithmetic the describer, the builder and the glue share. Private to FenrirRagdoll.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "FenrirAssets/SkeletonData.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir::Detail
{
    /** @brief A bone's rest pose in model space: position and rotation (rest scale is dropped). */
    struct RestPose
    {
        Math::Vec3 pos = Math::Vec3(0.0f);
        Math::Quat rot = Math::Quat(1.0f, 0.0f, 0.0f, 0.0f);
    };

    /** @brief Every bone's rest pose in model space, parents first. */
    inline std::vector<RestPose> RestPoses(const SkeletonData& skeleton)
    {
        std::vector<RestPose> poses(skeleton.BoneCount());
        for (std::size_t i = 0; i < poses.size(); ++i)
        {
            const SkeletonBone& bone = skeleton.bones[i];
            RestPose parent;
            if (bone.parent != NoBone)
            {
                parent = poses[static_cast<std::size_t>(bone.parent)];
            }
            poses[i].pos = parent.pos + Math::Rotate(parent.rot, bone.rest.pos);
            poses[i].rot = Math::Normalized(parent.rot * bone.rest.rot);
        }
        return poses;
    }

    /** @brief The rotation turning unit `from` onto unit `to`, about the axis perpendicular to both. */
    inline Math::Quat FromTo(const Math::Vec3& from, const Math::Vec3& to)
    {
        const float dot = Math::Dot(from, to);
        if (dot < -0.9999f)
        {
            const Math::Vec3 axis =
                std::abs(from.x) < 0.9f ? Math::Vec3(1.0f, 0.0f, 0.0f) : Math::Vec3(0.0f, 0.0f, 1.0f);
            return glm::angleAxis(glm::pi<float>(), Math::Normalized(Math::Cross(from, axis)));
        }
        const Math::Vec3 axis = Math::Cross(from, to);
        return Math::Normalized(Math::Quat(1.0f + dot, axis.x, axis.y, axis.z));
    }

    /** @brief The rotation whose X axis is unit `x`, with its Z as near `reference` as the right angle allows. */
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

    /** @brief The position and rotation of an affine matrix with no shear (its scale is dropped). */
    inline RestPose PoseOf(const Math::Mat4& matrix)
    {
        const Math::Mat3 axes(Math::Normalized(Math::Vec3(matrix[0])), Math::Normalized(Math::Vec3(matrix[1])),
                              Math::Normalized(Math::Vec3(matrix[2])));
        return RestPose{Math::Vec3(matrix[3]), Math::Normalized(glm::quat_cast(axes))};
    }

    /** @brief `bone × offset`: the body's pose from its bone's. */
    inline RestPose Compose(const RestPose& bone, const Transform& offset)
    {
        return RestPose{bone.pos + Math::Rotate(bone.rot, offset.pos), Math::Normalized(bone.rot * offset.rot)};
    }

    /** @brief `body × offset⁻¹`: the bone's pose from its body's. */
    inline RestPose Uncompose(const RestPose& body, const Transform& offset)
    {
        const Math::Quat rot = Math::Normalized(body.rot * Math::Conjugate(offset.rot));
        return RestPose{body.pos - Math::Rotate(rot, offset.pos), rot};
    }

    /** @brief The twist of `rotation` about its X axis, in radians, in (-pi, pi]. */
    inline float TwistAboutX(const Math::Quat& rotation)
    {
        const Math::Quat q = rotation.w < 0.0f ? -rotation : rotation;
        const float length = std::sqrt(q.w * q.w + q.x * q.x);
        if (length < 1e-6f)
        {
            return 0.0f;
        }
        return 2.0f * std::atan2(q.x / length, q.w / length);
    }
} // namespace Fenrir::Detail

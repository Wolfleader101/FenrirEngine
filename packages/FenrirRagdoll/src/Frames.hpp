#pragma once

// Pose arithmetic the description, the builder and the glue share: rest-pose model matrices, and the position and
// rotation of a matrix. Bodies are placed through full matrices (bone × offset), scale included, and only then reduced
// to a position and rotation, so a rig with a scaled armature puts its bodies where its bones are. Private to
// FenrirRagdoll.

#include <cmath>
#include <cstddef>
#include <string_view>
#include <vector>

#include "FenrirAssets/SkeletonData.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirSkeleton/BoneAttachment.hpp"

namespace Fenrir::Detail
{
    /** @brief A position and rotation: a body's pose once its matrix is reduced, scale dropped. */
    struct Pose
    {
        Math::Vec3 pos = Math::Vec3(0.0f);
        Math::Quat rot = Math::Quat(1.0f, 0.0f, 0.0f, 0.0f);
    };

    /** @brief A bone name's text, empty when its buffer is malformed. */
    inline std::string_view NameOf(const BoneName& name)
    {
        return name.Get().value_or(std::string_view());
    }

    /** @brief Every bone's rest-pose model matrix, parents first: the parent's × the bone's rest transform. */
    inline std::vector<Math::Mat4> RestModels(const SkeletonData& skeleton)
    {
        std::vector<Math::Mat4> models(skeleton.BoneCount());
        for (std::size_t i = 0; i < models.size(); ++i)
        {
            const SkeletonBone& bone = skeleton.bones[i];
            const Math::Mat4 local = TransformToMat4(bone.rest);
            models[i] = bone.parent == NoBone ? local : models[static_cast<std::size_t>(bone.parent)] * local;
        }
        return models;
    }

    /** @brief The position and rotation of an affine matrix with no shear; its scale is dropped. */
    inline Pose PoseOf(const Math::Mat4& matrix)
    {
        Math::Mat4 axes(1.0f);
        for (int column = 0; column < 3; ++column)
        {
            axes[column] = Math::Vec4(Math::Normalized(Math::Vec3(matrix[column])), 0.0f);
        }
        return Pose{Math::Vec3(matrix[3]), Math::Normalized(Math::Mat4ToQuat(axes))};
    }

    /** @brief The shortest rotation turning unit `from` onto unit `to`. */
    inline Math::Quat FromTo(const Math::Vec3& from, const Math::Vec3& to)
    {
        const float dot = Math::Dot(from, to);
        if (dot < -0.9999f)
        {
            const Math::Vec3 other =
                std::abs(from.x) < 0.9f ? Math::Vec3(1.0f, 0.0f, 0.0f) : Math::Vec3(0.0f, 0.0f, 1.0f);
            return Math::AngleAxis(Math::DegToRad(180.0f), Math::Normalized(Math::Cross(from, other)));
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
        Math::Mat4 axes(1.0f);
        axes[0] = Math::Vec4(x, 0.0f);
        axes[1] = Math::Vec4(y, 0.0f);
        axes[2] = Math::Vec4(z, 0.0f);
        return Math::Normalized(Math::Mat4ToQuat(axes));
    }
} // namespace Fenrir::Detail

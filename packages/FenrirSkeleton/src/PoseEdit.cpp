#include <algorithm>
#include <cstddef>
#include <format>

#include "FenrirAssets/SkeletonData.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirSkeleton/PoseStages.hpp"
#include "SkeletonPoses.hpp"

namespace Fenrir
{
    namespace
    {
        bool Has(Channels mask, Channels channel)
        {
            return (mask & channel) != Channels::None;
        }

        // Rotation blends along the shortest arc: q and -q are one rotation, so the target is flipped to the near one.
        Math::Quat Nlerp(const Math::Quat& from, Math::Quat to, float weight)
        {
            if (glm::dot(from, to) < 0.0f)
            {
                to = -to;
            }
            return glm::normalize(Math::Quat(from.w + (to.w - from.w) * weight, from.x + (to.x - from.x) * weight,
                                             from.y + (to.y - from.y) * weight, from.z + (to.z - from.z) * weight));
        }
    } // namespace

    PoseEdit ModifierContext::Edit(Entity skeleton) const
    {
        Detail::SkeletonInstance* instance = m_poses->FindReady(skeleton);
        if (instance == nullptr || m_poses->step != Detail::PipelineStep::Modify)
        {
            return {};
        }
        // An instance prepare skipped (clean last frame) is prepared now, so this frame's edits start from its base.
        if (!instance->prepared)
        {
            Detail::SkeletonPoses::Prepare(*instance);
        }
        if (instance->worldInverseFrame != m_poses->frame || instance->worldInverseFrame == 0)
        {
            const Math::Mat4 world = GetWorldMatrix(skeleton);
            instance->worldInverse =
                Detail::Invertible(world) ? std::optional<Math::Mat4>(Math::Inverse(world)) : std::nullopt;
            instance->worldInverseFrame = m_poses->frame;
        }
        instance->modifiedThisFrame = true;
        return PoseEdit(*m_poses, skeleton, m_call);
    }

    bool PoseEdit::IsEmpty() const
    {
        return m_poses == nullptr || m_poses->step != Detail::PipelineStep::Modify || m_poses->modifierCall != m_call ||
               m_poses->FindReady(m_entity) == nullptr;
    }

    std::span<const Transform> PoseEdit::Base() const
    {
        if (IsEmpty())
        {
            return {};
        }
        return m_poses->FindReady(m_entity)->base;
    }

    std::optional<Transform> PoseEdit::Local(std::int32_t bone) const
    {
        if (IsEmpty())
        {
            return std::nullopt;
        }
        const Detail::SkeletonInstance& instance = *m_poses->FindReady(m_entity);
        if (bone < 0 || static_cast<std::size_t>(bone) >= instance.final.size())
        {
            return std::nullopt;
        }
        return instance.final[static_cast<std::size_t>(bone)];
    }

    std::optional<Math::Mat4> PoseEdit::Model(std::int32_t bone) const
    {
        if (IsEmpty())
        {
            return std::nullopt;
        }
        Detail::SkeletonInstance& instance = *m_poses->FindReady(m_entity);
        if (bone < 0 || static_cast<std::size_t>(bone) >= instance.final.size())
        {
            return std::nullopt;
        }
        return Detail::SkeletonPoses::ModelOf(instance, bone);
    }

    bool PoseEdit::Write(std::int32_t bone, const Target& target)
    {
        if (IsEmpty())
        {
            return false;
        }
        Detail::SkeletonInstance& instance = *m_poses->FindReady(m_entity);
        if (bone < 0 || static_cast<std::size_t>(bone) >= instance.final.size())
        {
            return false;
        }
        const float weight = std::clamp(target.weight, 0.0f, 1.0f);
        if (weight <= 0.0f)
        {
            return true;
        }

        Transform wanted = target.trs;
        if (target.space != PoseSpace::Local)
        {
            Math::Mat4 matrix = TransformToMat4(target.trs);
            if (target.space == PoseSpace::World)
            {
                if (!instance.worldInverse.has_value())
                {
                    m_poses->WarnRateLimited(
                        m_entity, bone,
                        std::format("FenrirSkeleton: a world-space pose target on bone {} of the skeleton entity {} "
                                    "was skipped: the entity's world matrix has no inverse",
                                    bone, Detail::Describe(m_entity)));
                    return false;
                }
                matrix = *instance.worldInverse * matrix;
            }
            const std::int32_t parent = instance.parents[static_cast<std::size_t>(bone)];
            if (parent != NoBone)
            {
                const Math::Mat4& above = Detail::SkeletonPoses::ModelOf(instance, parent);
                if (!Detail::Invertible(above))
                {
                    m_poses->WarnRateLimited(
                        m_entity, bone,
                        std::format("FenrirSkeleton: a pose target on bone {} of the skeleton entity {} was skipped: "
                                    "its parent bone's model matrix has no inverse",
                                    bone, Detail::Describe(m_entity)));
                    return false;
                }
                matrix = Math::Inverse(above) * matrix;
            }
            // Under a parent's non-uniform scale and rotation the local matrix may shear; TRS cannot hold that, so it
            // is dropped here.
            Math::Decompose(matrix, wanted.pos, wanted.rot, wanted.scale);
            wanted.rot = glm::normalize(wanted.rot);
        }

        Transform& local = instance.final[static_cast<std::size_t>(bone)];
        if (Has(target.channels, Channels::Translation))
        {
            local.pos = glm::mix(local.pos, wanted.pos, weight);
        }
        if (Has(target.channels, Channels::Rotation))
        {
            local.rot = Nlerp(local.rot, wanted.rot, weight);
        }
        if (Has(target.channels, Channels::Scale))
        {
            local.scale = glm::mix(local.scale, wanted.scale, weight);
        }
        Detail::SkeletonPoses::Invalidate(instance, bone);
        return true;
    }
} // namespace Fenrir

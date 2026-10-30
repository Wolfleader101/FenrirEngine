#include "Attachments.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <functional>
#include <queue>
#include <string_view>

#include "FenrirAssets/SkeletonData.hpp"
#include "FenrirECS/ComponentType.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirScene/Scene.hpp"
#include "SkeletonPoses.hpp"

namespace Fenrir::Detail
{
    namespace
    {
        // Separates one attachment's part of the dependency signature from the next.
        constexpr std::uint32_t Separator = 0xFFFFFFFFU;
        // A hierarchy deeper than this is taken to loop; the entity list keeps it from doing so.
        constexpr std::size_t MaxDepth = 4096;

        Entity ParentOf(const Entity& entity)
        {
            return entity.HasComponent<Relationship>() ? entity.GetComponent<Relationship>().parent : Entity();
        }

        // Appends the ids of `from` and each of its ancestors.
        void AppendChain(Entity from, std::vector<std::uint32_t>& ids)
        {
            for (std::size_t depth = 0; from.IsValid() && depth < MaxDepth; ++depth)
            {
                ids.push_back(from.GetId());
                from = ParentOf(from);
            }
        }

        std::string_view CauseText(AttachmentCause cause)
        {
            switch (cause)
            {
            case AttachmentCause::SkeletonMissing:
                return "its skeleton entity is dead or has no SkeletonPose (SkeletonMissing)";
            case AttachmentCause::SkeletonUnavailable:
                return "its skeleton entity's pose is unavailable (SkeletonUnavailable)";
            case AttachmentCause::BoneNotFound:
                return "the skeleton has no bone of that name (BoneNotFound)";
            case AttachmentCause::MalformedName:
                return "its bone name has no terminator, so it is never resolved (BoneNotFound)";
            case AttachmentCause::TransformMissing:
                return "the entity has no Transform to place (TransformMissing)";
            case AttachmentCause::Cycle:
                return "it is in a dependency cycle with its skeleton entity or parent (Unplaceable)";
            case AttachmentCause::NoInverse:
                return "its parent's world matrix has no inverse (Unplaceable)";
            default:
                return "";
            }
        }

        AttachmentState StateFor(AttachmentCause cause)
        {
            switch (cause)
            {
            case AttachmentCause::SkeletonMissing:
                return AttachmentState::SkeletonMissing;
            case AttachmentCause::SkeletonUnavailable:
                return AttachmentState::SkeletonUnavailable;
            case AttachmentCause::BoneNotFound:
            case AttachmentCause::MalformedName:
                return AttachmentState::BoneNotFound;
            case AttachmentCause::TransformMissing:
                return AttachmentState::TransformMissing;
            case AttachmentCause::Cycle:
            case AttachmentCause::NoInverse:
                return AttachmentState::Unplaceable;
            default:
                return AttachmentState::Attached;
            }
        }
    } // namespace

    void Attachments::Place(Scene& scene, SkeletonPoses& poses)
    {
        EntityList& entities = scene.GetEntityList();
        std::vector<Entity> attached;
        const std::array<ComponentType, 1> types{ComponentType::Native<BoneAttachment>()};
        entities.ForEachWith(types, [&](std::uint32_t id) { attached.push_back(entities.GetEntity(id)); });
        // Ties in the order go by entity index.
        std::ranges::sort(attached, {}, [](const Entity& entity) { return entity.GetId(); });

        // Records of entities that died or lost their component are swept.
        std::erase_if(m_records, [](const auto& entry) {
            const Entity& entity = entry.second.entity;
            return !entity.IsValid() || !entity.HasComponent<BoneAttachment>();
        });

        // Each attachment's dependencies: its ancestors, its skeleton entity and the skeleton entity's ancestors.
        std::vector<std::vector<std::uint32_t>> chains(attached.size());
        std::vector<std::uint32_t> signature;
        for (std::size_t i = 0; i < attached.size(); ++i)
        {
            const Entity& entity = attached[i];
            const Entity skeleton = entity.GetComponent<BoneAttachment>().skeleton;
            AppendChain(ParentOf(entity), chains[i]);
            AppendChain(skeleton, chains[i]);
            signature.push_back(entity.GetId());
            signature.insert(signature.end(), chains[i].begin(), chains[i].end());
            signature.push_back(Separator);
        }
        if (signature != m_signature)
        {
            Order(attached, chains);
            m_signature = std::move(signature);
        }

        for (std::size_t i = 0; i < m_order.size(); ++i)
        {
            const Entity& entity = m_order[i];
            auto [found, made] = m_records.try_emplace(entity.GetId());
            AttachmentRecord& record = found->second;
            if (made || record.entity != entity)
            {
                record = AttachmentRecord{.entity = entity};
            }
            record.state = PlaceOne(record, poses, m_cyclic[i] != 0);
        }
    }

    void Attachments::Order(const std::vector<Entity>& attached, const std::vector<std::vector<std::uint32_t>>& chains)
    {
        const std::size_t count = attached.size();
        std::unordered_map<std::uint32_t, std::size_t> indexOf;
        for (std::size_t i = 0; i < count; ++i)
        {
            indexOf.emplace(attached[i].GetId(), i);
        }
        // B depends on A when A is B's skeleton entity or parent, or an ancestor of either; A may be B itself.
        std::vector<std::vector<std::size_t>> dependents(count);
        std::vector<std::size_t> waiting(count, 0);
        for (std::size_t b = 0; b < count; ++b)
        {
            std::vector<std::size_t> needs;
            for (const std::uint32_t id : chains[b])
            {
                if (const auto found = indexOf.find(id); found != indexOf.end())
                {
                    needs.push_back(found->second);
                }
            }
            std::ranges::sort(needs);
            needs.erase(std::ranges::unique(needs).begin(), needs.end());
            for (const std::size_t a : needs)
            {
                dependents[a].push_back(b);
            }
            waiting[b] = needs.size();
        }

        // Kahn's order, the lowest entity index first among those ready. When it stalls, what is left is in a cycle
        // or below one: the cycle's members are marked and released, so what depends on them is still placed.
        m_order.clear();
        m_cyclic.clear();
        std::vector<std::uint8_t> done(count, 0);
        std::priority_queue<std::size_t, std::vector<std::size_t>, std::greater<>> ready;
        for (std::size_t i = 0; i < count; ++i)
        {
            if (waiting[i] == 0)
            {
                ready.push(i);
            }
        }
        const auto release = [&](std::size_t at, bool cyclic) {
            done[at] = 1;
            m_order.push_back(attached[at]);
            m_cyclic.push_back(cyclic ? 1 : 0);
            for (const std::size_t next : dependents[at])
            {
                if (done[next] == 0 && --waiting[next] == 0)
                {
                    ready.push(next);
                }
            }
        };
        while (m_order.size() < count)
        {
            while (!ready.empty())
            {
                const std::size_t at = ready.top();
                ready.pop();
                if (done[at] == 0)
                {
                    release(at, false);
                }
            }
            if (m_order.size() == count)
            {
                break;
            }
            // Stalled: an attachment left is in a cycle when it reaches itself through those left.
            std::vector<std::size_t> inCycle;
            for (std::size_t start = 0; start < count; ++start)
            {
                if (done[start] != 0)
                {
                    continue;
                }
                std::vector<std::uint8_t> seen(count, 0);
                std::vector<std::size_t> stack(dependents[start].begin(), dependents[start].end());
                bool reached = false;
                while (!stack.empty() && !reached)
                {
                    const std::size_t at = stack.back();
                    stack.pop_back();
                    if (done[at] != 0 || seen[at] != 0)
                    {
                        continue;
                    }
                    reached = at == start;
                    seen[at] = 1;
                    stack.insert(stack.end(), dependents[at].begin(), dependents[at].end());
                }
                if (reached)
                {
                    inCycle.push_back(start);
                }
            }
            for (const std::size_t at : inCycle)
            {
                if (done[at] == 0)
                {
                    release(at, true);
                }
            }
        }
    }

    AttachmentState Attachments::PlaceOne(AttachmentRecord& record, SkeletonPoses& poses, bool cyclic)
    {
        const Entity& entity = record.entity;
        const BoneAttachment attachment = entity.GetComponent<BoneAttachment>();
        const Entity& skeleton = attachment.skeleton;

        AttachmentCause cause = AttachmentCause::None;
        const SkeletonInstance* found =
            skeleton.IsValid() && skeleton.HasComponent<SkeletonPose>() ? poses.Find(skeleton) : nullptr;
        if (cyclic)
        {
            cause = AttachmentCause::Cycle;
        }
        else if (found == nullptr)
        {
            cause = AttachmentCause::SkeletonMissing;
        }
        else if (!found->ready)
        {
            cause = AttachmentCause::SkeletonUnavailable;
        }
        else if (!attachment.bone.IsTerminated())
        {
            cause = AttachmentCause::MalformedName;
            record.resolved = false;
        }
        else
        {
            // Resolved again by engine name when the skeleton entity, the name or the instance's build changes; a
            // bone that was not found is not looked for again until one does.
            const std::string_view name = *attachment.bone.Get();
            if (!record.resolved || record.skeleton != skeleton || record.name != name || record.built != found->built)
            {
                const auto bone = found->bonesByName.find(std::string(name));
                record.resolved = true;
                record.skeleton = skeleton;
                record.name = std::string(name);
                record.built = found->built;
                record.bone = bone == found->bonesByName.end() ? NoBone : bone->second;
            }
            if (record.bone == NoBone)
            {
                cause = AttachmentCause::BoneNotFound;
            }
            else if (!entity.HasComponent<Transform>())
            {
                cause = AttachmentCause::TransformMissing;
            }
        }

        if (cause == AttachmentCause::None)
        {
            SkeletonInstance& instance = *poses.Find(skeleton);
            const Math::Mat4 atBone =
                SkeletonPoses::ModelOf(instance, record.bone) * TransformToMat4(attachment.offset);
            const Entity parent = ParentOf(entity);
            std::optional<Math::Mat4> local;
            if (parent == skeleton)
            {
                // The fast path, as SpawnSkeletons leaves it: the skeleton entity's space is the bone's model space.
                local = atBone;
            }
            else
            {
                const Math::Mat4 world = GetWorldMatrix(skeleton) * atBone;
                if (!parent.IsValid())
                {
                    local = world;
                }
                else if (const Math::Mat4 above = GetWorldMatrix(parent); Invertible(above))
                {
                    local = Math::Inverse(above) * world;
                }
            }
            if (local.has_value())
            {
                // Decomposing to TRS drops any shear (ADR-0037).
                Transform& transform = entity.GetComponent<Transform>();
                Math::Decompose(*local, transform.pos, transform.rot, transform.scale);
                transform.rot = glm::normalize(transform.rot);
                record.logged = AttachmentCause::None;
                return AttachmentState::Attached;
            }
            cause = AttachmentCause::NoInverse;
        }

        // The entity keeps its last Transform; each cause is logged once until the attachment is next placed.
        if (record.logged != cause)
        {
            record.logged = cause;
            poses.Logger().Warn("{}", std::format("FenrirSkeleton: the bone attachment on {} (bone '{}' of {}) was not "
                                                  "placed and keeps its last Transform: {}",
                                                  Describe(entity), attachment.bone.Get().value_or("<malformed>"),
                                                  Describe(skeleton), CauseText(cause)));
        }
        return StateFor(cause);
    }

    AttachmentState Attachments::StateOf(Entity entity) const
    {
        if (!entity.IsValid() || !entity.HasComponent<BoneAttachment>())
        {
            return AttachmentState::NotAttached;
        }
        const auto found = m_records.find(entity.GetId());
        return found == m_records.end() || found->second.entity != entity ? AttachmentState::NotAttached
                                                                          : found->second.state;
    }

    void Attachments::Forget(Entity entity)
    {
        const auto found = m_records.find(entity.GetId());
        if (found != m_records.end() && found->second.entity == entity)
        {
            m_records.erase(found);
        }
    }
} // namespace Fenrir::Detail

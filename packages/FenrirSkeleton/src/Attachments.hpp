#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "FenrirECS/Entity.hpp"
#include "FenrirSkeleton/BoneAttachment.hpp"

namespace Fenrir
{
    class Scene;

    namespace Detail
    {
        class SkeletonPoses;

        /** @brief Why an attachment was last left unplaced, for logging each cause once. */
        enum class AttachmentCause : std::uint8_t
        {
            None,
            SkeletonMissing,
            SkeletonUnavailable,
            BoneNotFound,
            MalformedName,
            TransformMissing,
            Cycle,
            NoInverse,
        };

        /** @brief One attached entity's private record: its resolution and its state. */
        struct AttachmentRecord
        {
            Entity entity;
            // What `bone` was resolved against: the skeleton entity, the name and the instance's build.
            bool resolved = false;
            Entity skeleton;
            std::string name;
            std::uint64_t built = 0;
            std::int32_t bone = -1;

            AttachmentState state = AttachmentState::NotAttached;
            // The cause last logged, so each is logged once until the attachment is next placed.
            AttachmentCause logged = AttachmentCause::None;
        };

        /**
         * @brief A scene's bone attachments (ADR-0037): each attached entity's resolution and state, and the cached
         * order they are placed in. Part of `SkeletonPoses`, so it goes with the scene's instances.
         */
        class Attachments
        {
          public:
            /** @brief The Attach step: places every attachment in the scene, in dependency order. */
            void Place(Scene& scene, SkeletonPoses& poses);

            /** @brief The entity's state as of the last Attach; `NotAttached` with no record or no component. */
            [[nodiscard]] AttachmentState StateOf(Entity entity) const;

            /** @brief Drops the entity's record, once its component is removed. */
            void Forget(Entity entity);

          private:
            void Order(const std::vector<Entity>& attached, const std::vector<std::vector<std::uint32_t>>& chains);
            AttachmentState PlaceOne(AttachmentRecord& record, SkeletonPoses& poses, bool cyclic);

            std::unordered_map<std::uint32_t, AttachmentRecord> m_records;
            // The dependency signature the order was built from: each attachment's id, its skeleton entity's and its
            // ancestors', so any change to the hierarchy or a skeleton entity rebuilds the order.
            std::vector<std::uint32_t> m_signature;
            std::vector<Entity> m_order;
            std::vector<std::uint8_t> m_cyclic;
        };
    } // namespace Detail
} // namespace Fenrir

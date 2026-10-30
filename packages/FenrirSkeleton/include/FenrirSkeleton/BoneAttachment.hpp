#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"

namespace Fenrir
{
    class ILogger;

    /**
     * @brief A bone's engine name as a component field (ADR-0037): a fixed 256-byte UTF-8 buffer, like `Name`, holding
     * at most 255 bytes and a terminator. `Set` refuses a longer name rather than truncating it, since a truncated name
     * could match another bone. A buffer written raw with no terminator is malformed: `Get` gives nothing, and an
     * attachment naming it is diagnosed and never resolved.
     */
    struct BoneName
    {
        /** @brief The most bytes a name may hold, its terminator aside. */
        static constexpr std::size_t MaxBytes = 255;

        /**
         * @brief Sets the name to `name`. A name over 255 bytes, or holding a zero byte, is refused with an error to
         * `logger`, leaving the field unchanged; false then.
         */
        bool Set(std::string_view name, ILogger& logger);

        /** @brief The name; `nullopt` when the buffer has no terminator. */
        [[nodiscard]] std::optional<std::string_view> Get() const noexcept;

        /** @brief Whether the buffer holds a terminator, so `Get` gives a name. */
        [[nodiscard]] bool IsTerminated() const noexcept;

      private:
        /** @brief The bytes, zero-terminated when well-formed. */
        char m_bytes[MaxBytes + 1] = {0};
    };

    /**
     * @brief A bone attachment (ADR-0037): plain data placing its entity at a bone of a skeleton entity every frame,
     * at `GetBoneWorldMatrix(skeleton, bone) × offset`, under whatever ECS parent the entity has. The full bone
     * transform is inherited, translation, rotation and scale.
     *
     * While an entity has one, FenrirSkeleton owns its local `Transform`, written in the pose pipeline's Attach step:
     * move the object relative to its bone through `offset`, since a direct write to the `Transform` is overwritten at
     * the next Attach. Any field may be edited at any time; the bone is looked up again by engine name whenever the
     * skeleton entity, the name or the skeleton's instance changes. `Skeletons::GetAttachmentState` reports whether it
     * placed the entity, and why not.
     */
    struct BoneAttachment
    {
        /** @brief The skeleton entity: an entity with a `SkeletonPose`. */
        Entity skeleton;
        /** @brief The bone, by engine name. */
        BoneName bone;
        /** @brief The entity's place relative to the bone. */
        Transform offset;
    };

    /** @brief Whether the last Attach placed a bone attachment's entity, and why not (ADR-0037). */
    enum class AttachmentState : std::uint8_t
    {
        /** @brief The entity has no `BoneAttachment`, or no Attach has seen it yet. */
        NotAttached,
        /** @brief Placed at its bone this frame. */
        Attached,
        /** @brief The skeleton entity is dead, or has no `SkeletonPose`. */
        SkeletonMissing,
        /** @brief The skeleton entity's instance is `Unavailable`. */
        SkeletonUnavailable,
        /** @brief No bone has that engine name, or the name is malformed. */
        BoneNotFound,
        /** @brief The entity has no `Transform` to place. */
        TransformMissing,
        /** @brief The parent's world matrix has no inverse, or the attachment is in a dependency cycle. */
        Unplaceable,
    };

    /** @brief What `Skeletons::Detach` keeps (ADR-0037). */
    enum class DetachMode : std::uint8_t
    {
        /** @brief Removes the `BoneAttachment` only: the entity keeps its local `Transform` and its parent. */
        KeepLocal,
        /** @brief Reparents the entity, keeping where it is in the world now. */
        KeepWorld,
    };

    /** @brief What `Skeletons::Detach` did. Every result but `Detached` changed nothing. */
    enum class DetachResult : std::uint8_t
    {
        Detached,
        /** @brief The entity is dead or has no `BoneAttachment`. */
        NotAttached,
        /** @brief Called while the pose pipeline runs, where structural changes are refused. */
        InsidePipeline,
        /** @brief The new parent is dead, the entity itself or one of its descendants. */
        InvalidParent,
        /** @brief The new parent's world matrix has no inverse. */
        ParentNotInvertible,
        /** @brief The world transform cannot be held as position, rotation and scale under the new parent (shear). */
        NotRepresentable,
    };
} // namespace Fenrir

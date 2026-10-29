#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirSkeleton/SkeletonPose.hpp"

namespace Fenrir
{
    class Scene;

    namespace Detail
    {
        class SkeletonPoses;
        struct PosePipeline;
    } // namespace Detail

    /** @brief The space a pose target's transform is given in (ADR-0028, ADR-0033). */
    enum class PoseSpace : std::uint8_t
    {
        /** @brief Relative to the bone's parent bone; for a root bone, to the skeleton entity. */
        Local,
        /** @brief Relative to the skeleton entity: model space, never including the entity's own `Transform`. */
        Model,
        /** @brief The scene's world space. */
        World,
    };

    /** @brief Which parts of a bone's local transform a pose target writes: a mask, combined with `|`. */
    enum class Channels : std::uint8_t
    {
        None = 0,
        Translation = 1,
        Rotation = 2,
        Scale = 4,
        All = Translation | Rotation | Scale,
    };

    /** @brief Both masks' channels. */
    [[nodiscard]] constexpr Channels operator|(Channels a, Channels b) noexcept
    {
        return static_cast<Channels>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
    }

    /** @brief The channels both masks share. */
    [[nodiscard]] constexpr Channels operator&(Channels a, Channels b) noexcept
    {
        return static_cast<Channels>(static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(b));
    }

    /**
     * @brief A pose target (ADR-0033): one write a pose modifier makes to a bone. It is resolved into the bone's local
     * transform when written, and blended into it by `weight` for the masked channels (translation and scale lerp,
     * rotation nlerps along the shortest arc). It is met when written, not held against later moves of the bone's
     * ancestors.
     */
    struct Target
    {
        /** @brief The space `trs` is in. */
        PoseSpace space = PoseSpace::Local;
        /** @brief The transform the bone should have, in `space`. */
        Transform trs;
        /** @brief The channels written; the others keep the bone's current local values. */
        Channels channels = Channels::All;
        /** @brief How far towards `trs` the bone moves, clamped to [0, 1]: 0 writes nothing, 1 writes it exactly. */
        float weight = 1.0f;
    };

    /** @brief The order constants FenrirSkeleton's own modifiers run at, so others can run before or after them. */
    namespace ModifierOrder
    {
        /** @brief The pose override's order (ADR-0033). */
        inline constexpr int Override = 1000;
    } // namespace ModifierOrder

    /**
     * @brief A pose edit (ADR-0033): a pose modifier's only access to one skeleton instance's final pose while the
     * `Modify` stage builds it. Opened by `ModifierContext::Edit`, and valid only during the modifier call that opened
     * it: kept and used later, it is empty. Empty, it reads nothing and writes nothing.
     */
    class PoseEdit
    {
      public:
        /** @brief An empty edit. */
        PoseEdit() = default;

        /** @brief Whether the edit reaches no instance: not `Ready`, or used outside the call that opened it. */
        [[nodiscard]] bool IsEmpty() const;

        /** @brief The skeleton entity the edit is on. */
        [[nodiscard]] Entity GetEntity() const noexcept
        {
            return m_entity;
        }

        /** @brief The base pose's local transforms, one per bone, as `Animate` left them. Empty when the edit is. */
        [[nodiscard]] std::span<const Transform> Base() const;

        /** @brief The bone's current local transform in the final pose, with every write so far applied. */
        [[nodiscard]] std::optional<Transform> Local(std::int32_t bone) const;

        /**
         * @brief The bone's current model-space matrix, with every write so far applied; recomputed on read for the
         * bones a write has invalidated, parents first.
         */
        [[nodiscard]] std::optional<Math::Mat4> Model(std::int32_t bone) const;

        /**
         * @brief Writes `target` to `bone` now: a `World` target is taken into model space through the inverse of the
         * skeleton entity's world matrix as it was when `Modify` began, a `Model` target into the bone's local space
         * through the inverse of its parent's current model matrix, and the result decomposed into translation,
         * rotation and scale (shear is dropped) and blended in. Later writes and modifiers see the result. False when
         * nothing was written: an empty edit, a bone out of range, or an inverse that does not exist, which skips the
         * target with a warning naming the entity and bone (rate-limited, so a transform held degenerate for many
         * frames does not flood the log).
         */
        bool Write(std::int32_t bone, const Target& target);

      private:
        friend class ModifierContext;

        PoseEdit(Detail::SkeletonPoses& poses, Entity entity, std::uint64_t call) noexcept
            : m_poses(&poses), m_entity(entity), m_call(call)
        {
        }

        Detail::SkeletonPoses* m_poses = nullptr;
        Entity m_entity;
        std::uint64_t m_call = 0;
    };

    /**
     * @brief What an `Animate` item is given (ADR-0033, ADR-0040): the pass it runs in, the pass's step and the
     * instances of that pass's timing, the only ones it may write. Only the frame pass runs for now.
     */
    class AnimateContext
    {
      public:
        /** @brief The scene being posed: the active scene. */
        [[nodiscard]] Scene& GetScene() const noexcept
        {
            return *m_scene;
        }

        /** @brief The pass's timing: `Frame` in the frame pass. */
        [[nodiscard]] PoseTiming Timing() const noexcept
        {
            return m_timing;
        }

        /** @brief The pass's step in seconds: `Time::deltaTime` in the frame pass, `Time::tickRate` in a tick pass. */
        [[nodiscard]] double Step() const noexcept
        {
            return m_step;
        }

        /** @brief The `Ready` skeleton entities of this pass's timing. */
        [[nodiscard]] std::span<const Entity> Instances() const noexcept
        {
            return m_instances;
        }

      private:
        friend struct Detail::PosePipeline;

        AnimateContext(Scene& scene, PoseTiming timing, double step, std::span<const Entity> instances) noexcept
            : m_scene(&scene), m_timing(timing), m_step(step), m_instances(instances)
        {
        }

        Scene* m_scene;
        PoseTiming m_timing;
        double m_step;
        std::span<const Entity> m_instances;
    };

    /** @brief What a pose modifier is given in `Modify`: the scene, and pose edits on its instances. */
    class ModifierContext
    {
      public:
        /** @brief The scene being posed: the active scene. */
        [[nodiscard]] Scene& GetScene() const noexcept
        {
            return *m_scene;
        }

        /**
         * @brief Opens a pose edit on `skeleton`, valid until this modifier call returns, and marks the instance
         * modified this frame. Empty for an entity that is not a `Ready` instance.
         */
        [[nodiscard]] PoseEdit Edit(Entity skeleton) const;

      private:
        friend struct Detail::PosePipeline;

        ModifierContext(Scene& scene, Detail::SkeletonPoses& poses, std::uint64_t call) noexcept
            : m_scene(&scene), m_poses(&poses), m_call(call)
        {
        }

        Scene* m_scene;
        Detail::SkeletonPoses* m_poses;
        std::uint64_t m_call;
    };

    /** @brief What a `Late` item is given: the scene, whose poses are final for this frame and read-only. */
    class LateContext
    {
      public:
        /** @brief The scene being posed: the active scene. */
        [[nodiscard]] Scene& GetScene() const noexcept
        {
            return *m_scene;
        }

      private:
        friend struct Detail::PosePipeline;

        explicit LateContext(Scene& scene) noexcept : m_scene(&scene)
        {
        }

        Scene* m_scene;
    };

    /** @brief Names work added to a pose stage, for `Skeletons::Remove`. Plain data; 0 names nothing. */
    struct PoseStageHandle
    {
        /** @brief The work's id, unique for one install. */
        std::uint64_t id = 0;

        friend constexpr bool operator==(const PoseStageHandle&, const PoseStageHandle&) = default;
    };
} // namespace Fenrir

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/SkinningSource.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirScripting/NativeGuard.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
#include "FenrirScripting/WarnOnceRecord.hpp"
#include "FenrirSkeleton/PoseStages.hpp"
#include "FenrirSkeleton/Skeletons.hpp"

namespace Fenrir
{
    /**
     * @brief The skeleton extension's state, which every skeleton native function is given second: the warnings it has
     * logged, the pose stage work it placed for the loaded script, and while one of its stage groups runs, the stage's
     * context, which the stage natives (PoseStages.Timing, Skeletons.Edit and the pose edit's calls) read. Made by
     * Skeletons::ScriptingExtension and kept by the Scripting given the extension. In a header of its own because the
     * generated natives header (Generated/FenrirSkeletonNatives.hpp) declares the natives, which name it.
     *
     * Its lookups are built on the native guard (NativeGuard.hpp): each warns once per native function and message,
     * under the native's name. The texts the natives warn with are named here, one per rule, so the package's rules
     * read in one place and its rules tests name them; a skeleton or model handle's rule is Assets' (AssetResolve.hpp).
     */
    struct SkeletonScripting
    {
        /** @brief The stages the extension declares (ADR-0039), as C#'s PoseStages names them. */
        static constexpr std::string_view AnimateStage = "Fenrir.Skeleton.Animate";
        static constexpr std::string_view ModifyStage = "Fenrir.Skeleton.Modify";
        static constexpr std::string_view LateStage = "Fenrir.Skeleton.Late";

        /** @brief Warned when the entity is alive but not a Ready skeleton entity: the native does nothing. */
        static constexpr std::string_view NotReady = "the entity is not a Ready skeleton entity, so it does nothing";

        /** @brief Warned when the bone is outside the skeleton (or the asset's bones): the native does nothing. */
        static constexpr std::string_view NoSuchBone = "the bone is out of range, so it does nothing";

        /**
         * @brief Warned by PoseStages.Timing and PoseStages.Step outside an Animate stage system: they give the frame
         * pass's timing and a step of 0.
         */
        static constexpr std::string_view OutsideAnimate =
            "it is read inside a Fenrir.Skeleton.Animate stage system only, so it gives Frame and 0";

        /** @brief Warned by Skeletons.Edit outside a Modify stage system: the edit is empty and writes nothing. */
        static constexpr std::string_view OutsideModify =
            "a pose edit opens inside a Fenrir.Skeleton.Modify stage system only, so it writes nothing";

        /**
         * @brief Warned by a pose edit's call through a token that is not the running Modify group's (kept past the
         * group that opened it): it reads and writes nothing.
         */
        static constexpr std::string_view StaleEdit =
            "the pose edit is stale (kept past the Modify stage system that opened it), so it does nothing";

        /**
         * @brief Warned by SetBaseLocal and SetBasePose when FenrirSkeleton refuses the write (in Modify or Late, or
         * on a skeleton of the other timing during Animate; FenrirSkeleton logs why): it writes nothing.
         */
        static constexpr std::string_view BaseWriteRefused =
            "FenrirSkeleton refused the base-pose write here, so it writes nothing";

        /** @brief Warned by Detach on an entity that is alive but has no BoneAttachment: it detaches nothing. */
        static constexpr std::string_view NotAttached = "the entity has no BoneAttachment, so it detaches nothing";

        /** @brief Warned by Detach keeping the world under a new parent that is not alive: it detaches nothing. */
        static constexpr std::string_view DeadParent = "the new parent is not alive, so it detaches nothing";

        /** @brief Logged when a script's pose stage systems load with FenrirSkeleton not installed: they never run. */
        static constexpr std::string_view NoPipeline = "[skeleton scripting] FenrirSkeleton is not installed, so the "
                                                       "script's pose stage systems never run (install "
                                                       "it before Scripting)";

        /** @brief `entity` as the active scene's handle, if it is alive there and a Ready skeleton entity. */
        std::optional<Entity> ReadyOf(const CallContext& context, const Entity& entity)
        {
            const NativeGuard guard(context, warnings);
            const std::optional<Entity> alive = guard.Alive(entity);
            if (!alive)
            {
                return std::nullopt;
            }
            if (Skeletons::GetPoseState(context.GetApp().GetActiveScene(), *alive) != PoseState::Ready)
            {
                guard.Warn(std::string(NotReady));
                return std::nullopt;
            }
            return alive;
        }

        /** @brief Whether `bone` is one of `count` bones, warning once that the native does nothing if not. */
        bool InRange(const CallContext& context, std::int32_t bone, std::size_t count)
        {
            if (bone >= 0 && static_cast<std::size_t>(bone) < count)
            {
                return true;
            }
            const NativeGuard guard(context, warnings);
            guard.Warn(std::string(NoSuchBone));
            return false;
        }

        /** @brief What the natives have warned about, so that each place warns once. */
        WarnOnceRecord warnings{"skeleton scripting"};

        /** @brief The pose stage work placed for the loaded script, removed when it is released. */
        std::vector<PoseStageHandle> placed;
        /** @brief The loaded script's stage changes, played back when each pass of the pipeline returns. */
        std::optional<StageChanges> changes;

        /** @brief The running Animate group's context; null outside one. */
        const AnimateContext* animate = nullptr;
        /** @brief The running Modify group's context; null outside one. */
        const ModifierContext* modifier = nullptr;
        /**
         * @brief The running Modify group's edit token: advanced as each group starts and again as it ends, so a
         * token kept past its group never matches. 0 is never a running group's.
         */
        std::uint64_t editToken = 0;
    };
} // namespace Fenrir

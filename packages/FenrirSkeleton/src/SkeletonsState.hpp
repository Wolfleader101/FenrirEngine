#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "FenrirAssets/SkinningSource.hpp"
#include "FenrirSkeleton/PoseStages.hpp"

namespace Fenrir
{
    class App;

    namespace Detail
    {
        /** @brief Work added to one pose stage. */
        template <typename Context>
        struct StageWork
        {
            std::uint64_t id = 0;
            std::string name;
            int order = 0;
            // Shared, so the pipeline's snapshot of a stage runs the same callable, whose captured state persists.
            std::shared_ptr<std::function<void(Context&)>> work;
            // Placed through the App (Skeletons::HostAnimate and the rest): after the owner's work at its order.
            bool hosted = false;
        };

        /**
         * @brief One pose stage's work, kept sorted by order, ties in the order added, with hosted work after the
         * owner's at an equal order.
         */
        template <typename Context>
        struct Stage
        {
            std::vector<StageWork<Context>> work;

            void Add(StageWork<Context> added)
            {
                // After every entry of a lower or equal order (hosted work counting as later than the owner's): equal
                // orders keep the order they were added in.
                const auto key = [](const StageWork<Context>& entry) { return std::pair(entry.order, entry.hosted); };
                const auto at = std::ranges::upper_bound(work, key(added), {}, key);
                work.insert(at, std::move(added));
            }

            bool Remove(std::uint64_t id)
            {
                return std::erase_if(work, [id](const StageWork<Context>& entry) { return entry.id == id; }) != 0;
            }
        };

        /** @brief What one `Skeletons` install holds, shared with its system, which holds it weakly. */
        struct SkeletonsState
        {
            explicit SkeletonsState(App& installedIn) noexcept : app(&installedIn)
            {
            }

            App* app;
            Stage<AnimateContext> animate;
            Stage<ModifierContext> modify;
            Stage<LateContext> late;
            // Run when each pass returns (Skeletons::HostPassEnd), in the order added.
            std::vector<std::pair<std::uint64_t, std::shared_ptr<std::function<void(PoseTiming)>>>> passEnds;
            std::uint64_t nextId = 1;
            // True while the pose pipeline runs, when stage work may not be removed.
            bool running = false;
            std::unique_ptr<SkinningSource> source;
        };

        /**
         * @brief The app resource through which the App reaches its install (Skeletons::HostAnimate and the rest),
         * added by Install and removed with the owner's state.
         */
        struct SkeletonsHost
        {
            std::weak_ptr<SkeletonsState> state;
        };
    } // namespace Detail
} // namespace Fenrir

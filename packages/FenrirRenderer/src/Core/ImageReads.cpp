#include "Core/ImageReads.hpp"

#include <algorithm>
#include <cstddef>

namespace Fenrir::Render
{
    namespace
    {
        /** @brief Whether `from` waits, directly or through other views, on `to`, by each view's `waitsOn`. */
        bool Waits(const std::vector<std::vector<std::size_t>>& waitsOn, std::size_t from, std::size_t to)
        {
            std::vector<bool> seen(waitsOn.size(), false);
            std::vector<std::size_t> stack{from};
            while (!stack.empty())
            {
                const std::size_t view = stack.back();
                stack.pop_back();
                if (view == to)
                {
                    return true;
                }
                if (seen[view])
                {
                    continue;
                }
                seen[view] = true;
                stack.insert(stack.end(), waitsOn[view].begin(), waitsOn[view].end());
            }
            return false;
        }
    } // namespace

    std::vector<ImageReads> PlanImageReads(std::span<const AssetKey> targets, const std::set<AssetKey>& sampled)
    {
        std::vector<std::vector<std::size_t>> waitsOn(targets.size());
        std::vector<ImageReads> plan(targets.size());
        for (std::size_t v = 0; v < targets.size(); ++v)
        {
            for (const AssetKey image : sampled)
            {
                std::vector<std::size_t> writers;
                for (std::size_t w = 0; w < targets.size(); ++w)
                {
                    if (targets[w] == image)
                    {
                        writers.push_back(w);
                    }
                }
                const bool own = targets[v] == image;
                const bool cycle =
                    std::any_of(writers.begin(), writers.end(), [&](std::size_t w) { return Waits(waitsOn, w, v); });
                if (own || cycle)
                {
                    plan[v].replaced.push_back({image, own ? ReplacedBecause::OwnTarget : ReplacedBecause::Cycle});
                    continue;
                }
                waitsOn[v].insert(waitsOn[v].end(), writers.begin(), writers.end());
                plan[v].reads.push_back(image);
            }
        }
        return plan;
    }
} // namespace Fenrir::Render

#pragma once

#include <set>
#include <span>
#include <vector>

#include "Core/SceneFrame.hpp"

namespace Fenrir::Render
{
    /** @brief The render target images one view's materials sample (ADR-0026), as PlanImageReads splits them. */
    struct ImageReads
    {
        /** @brief Those it reads, after every view that draws into them. */
        std::vector<AssetKey> reads;
        /**
         * @brief Those it samples the default texture for instead: its own target, or one whose views would wait on
         * it, directly or through other views.
         */
        std::vector<AssetKey> replaced;
    };

    /**
     * @brief Which of `sampled`, the live render target images a frame's draws sample, each view may read, the views
     * drawing into `targets` (one per view, empty for the window). A view reads an image after every view that draws
     * into it, so the render graph orders them; one that would wait on itself gets the default texture for it instead,
     * the views taken in their order, each image in key order.
     */
    [[nodiscard]] std::vector<ImageReads> PlanImageReads(std::span<const AssetKey> targets,
                                                         const std::set<AssetKey>& sampled);
} // namespace Fenrir::Render

#pragma once

#include <cstdint>
#include <set>
#include <span>
#include <vector>

#include "Core/SceneFrame.hpp"

namespace Fenrir::Render
{
    /** @brief Why a view samples the default texture for a render target image instead of the image. */
    enum class ReplacedBecause : std::uint8_t
    {
        /** @brief The view draws into it. */
        OwnTarget,
        /** @brief Its views would wait on this one, directly or through other views. */
        Cycle,
    };

    /** @brief A render target image a view samples the default texture for, and why. */
    struct ReplacedImage
    {
        AssetKey image;
        ReplacedBecause reason = ReplacedBecause::OwnTarget;

        friend constexpr bool operator==(const ReplacedImage&, const ReplacedImage&) = default;
    };

    /** @brief The render target images one view's materials sample (ADR-0026), as PlanImageReads splits them. */
    struct ImageReads
    {
        /** @brief Those it reads, after every view that draws into them. */
        std::vector<AssetKey> reads;
        /** @brief Those it samples the default texture for instead. */
        std::vector<ReplacedImage> replaced;
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

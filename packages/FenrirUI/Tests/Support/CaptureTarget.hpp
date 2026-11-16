#pragma once

#include <optional>
#include <utility>
#include <vector>

#include "FenrirDraw2D/DrawList2D.hpp"
#include "FenrirUI/UIRoot.hpp"
#include "FenrirUI/UITarget.hpp"

namespace Fenrir::Testing
{
    /**
     * @brief The capturing test target: a 2D target that keeps every 2D draw list a root submits to it, for a test to
     * read back. Its size, and the blend space and scale rule it gives a root made without them, are the test's to
     * set; it takes window input and no pointer ray, as a target on the window does.
     */
    class CaptureTarget : public UITarget
    {
      public:
        /** @brief A target of `size`, giving a root UITarget's own defaults until the test sets others. */
        explicit CaptureTarget(UITargetSize size) : size(size)
        {
        }

        /** @brief The size `size` holds: a test resizes the target by setting it between frames. */
        [[nodiscard]] UITargetSize GetSize(App&) override
        {
            return size;
        }

        /** @brief Keeps `list` at the end of `lists`. */
        void Submit(App&, DrawList2D list) override
        {
            lists.push_back(std::move(list));
        }

        /** @brief `blendSpace` when the test set it, otherwise UITarget's `Gamma`. */
        [[nodiscard]] BlendSpace2D GetDefaultBlendSpace() const noexcept override
        {
            return blendSpace ? *blendSpace : UITarget::GetDefaultBlendSpace();
        }

        /** @brief `scaleRule` when the test set it, otherwise UITarget's `Height1080`. */
        [[nodiscard]] ScaleRule GetDefaultScaleRule() const noexcept override
        {
            return scaleRule ? *scaleRule : UITarget::GetDefaultScaleRule();
        }

        /** @brief The target's size: its logical units and content scale. */
        UITargetSize size;
        /** @brief The blend space a root made with no UIRootConfig::blendSpace paints in; none for UITarget's. */
        std::optional<BlendSpace2D> blendSpace;
        /** @brief The scale rule a root made with no UIRootConfig::scaleRule lays out by; none for UITarget's. */
        std::optional<ScaleRule> scaleRule;
        /** @brief Every 2D draw list submitted, oldest first: one a frame for each frame the root drew. */
        std::vector<DrawList2D> lists;
    };
} // namespace Fenrir::Testing

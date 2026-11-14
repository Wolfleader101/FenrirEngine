#pragma once

#include "FenrirDraw2D/DrawList2D.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir
{
    class App;

    /** @brief A 2D target's size: its logical units and its pixels per logical unit (the content scale). */
    struct UITargetSize
    {
        Math::Vec2 logicalSize{0.0f};
        float contentScale = 1.0f;

        friend bool operator==(const UITargetSize&, const UITargetSize&) = default;
    };

    /**
     * @brief Where a UI root draws: what size it is this frame, and what takes the root's draw list each frame. A root
     * made with none draws on its window's overlay layer (UIRootConfig::window and layer); one made with a
     * UIImageTarget draws into an image. A game, or a test that captures the list, may give a root a target of its own.
     */
    class UITarget
    {
      public:
        UITarget() = default;
        UITarget(const UITarget&) = delete;
        UITarget& operator=(const UITarget&) = delete;
        UITarget(UITarget&&) = delete;
        UITarget& operator=(UITarget&&) = delete;
        virtual ~UITarget() = default;

        /** @brief The target's size this frame; a zero size draws nothing. */
        [[nodiscard]] virtual UITargetSize GetSize(App& app) = 0;

        /** @brief Takes the root's draw list for this frame, in the target's logical units. */
        virtual void Submit(App& app, DrawList2D list) = 0;

        /**
         * @brief The blend space a root made with no UIRootConfig::blendSpace paints in (ADR-0065): `Gamma`, as on the
         * window overlay, unless the target says otherwise; an image target says `Linear`.
         */
        [[nodiscard]] virtual BlendSpace2D GetDefaultBlendSpace() const noexcept
        {
            return BlendSpace2D::Gamma;
        }

        /**
         * @brief Whether a root drawing here takes its window's input through an input router (ADR-0027), as one on
         * the window overlay does. False for a target that is not on the window, such as an image: its root's elements
         * then see only what code sends them.
         */
        [[nodiscard]] virtual bool TakesWindowInput() const noexcept
        {
            return true;
        }
    };
} // namespace Fenrir

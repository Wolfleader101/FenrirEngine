#pragma once

#include <cstdint>
#include <optional>

#include "FenrirDraw2D/DrawList2D.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir
{
    class App;
    /** @brief How a UI root maps its UI units to its target's logical units; declared in FenrirUI/UIRoot.hpp. */
    enum class ScaleRule : std::uint8_t;

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
     * UIImageTarget draws into an image, and one made with a UIWorldTarget on a quad in the world. A game, or a test
     * that captures the list, may give a root a target of its own.
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
         * @brief The scale rule a root made with no UIRootConfig::scaleRule lays out by: `Height1080` unless the target
         * says otherwise; a world quad, whose logical units are UI units, says `ContentScale`.
         */
        [[nodiscard]] virtual ScaleRule GetDefaultScaleRule() const noexcept;

        /**
         * @brief Whether a root drawing here takes its window's input through an input router (ADR-0027), as one on
         * the window overlay does. False for a target that is not on the window, such as an image: its root's elements
         * then see only what code sends them.
         */
        [[nodiscard]] virtual bool TakesWindowInput() const noexcept
        {
            return true;
        }

        /**
         * @brief Whether a root drawing here takes its pointer from the ray the game gives it (UIRoot::SetPointerRay)
         * instead of from its window's mouse: true for a quad in the world, whose root's router then passes the
         * window's mouse and takes only its keys, text and gamepads.
         */
        [[nodiscard]] virtual bool TakesPointerRay() const noexcept
        {
            return false;
        }

        /**
         * @brief Where `ray` meets the target's plane, in its logical units from its top-left (outside its size when
         * it meets the plane past an edge); none when it runs along the plane or away from it, or the target is not
         * in the world.
         */
        [[nodiscard]] virtual std::optional<Math::Vec2> PointerAt(App& app, const Math::Ray& ray)
        {
            (void)app;
            (void)ray;
            return std::nullopt;
        }
    };
} // namespace Fenrir

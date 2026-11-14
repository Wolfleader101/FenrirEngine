#pragma once

#include <optional>

#include "FenrirDraw2D/DrawList2D.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirUI/UITarget.hpp"

namespace Fenrir
{
    class App;

    /**
     * @brief A UI root's world quad (ADR-0060's world-space 2D target, ADR-0066's one entity): an entity of the active
     * scene carrying the renderer's `WorldQuad2D` (FenrirRenderer/WorldTargets2D.hpp), whose size in world units and
     * UI units per world unit it reads each frame, and which the root draws on through the renderer's WorldTargets2D.
     * Give it to a root as UIRootConfig::target; the game owns the entity and its transform, and keeps the entity while
     * the root lives.
     *
     * Its logical units are UI units: the root lays out over `size * unitsPerWorldUnit` at a scale of 1 (its default
     * scale rule is `ContentScale`), from the quad's top-left, Y down. It blends linear by default (ADR-0065), as the
     * lit scene does. An entity that is not valid, or has no WorldQuad2D, gives a zero size, so the root draws nothing.
     *
     * **Input.** The root takes its pointer from the ray the game gives it each frame (UIRoot::SetPointerRay), never
     * from the window's mouse: the game decides which camera and cursor, or controller, the ray comes from (ADR-0057's
     * line). Its router takes the window's keys, text and gamepads, so a focused world root navigates, types and claims
     * the gamepad as any root does.
     */
    class UIWorldTarget final : public UITarget
    {
      public:
        /** @brief The quad on `entity`, an entity of `app`'s active scene. */
        UIWorldTarget(App& app, Entity entity);

        /** @brief The entity whose quad the root draws on. */
        [[nodiscard]] Entity GetEntity() const noexcept
        {
            return m_entity;
        }

        /** @brief The quad's size in UI units, at a content scale of 1; zero without a valid entity and WorldQuad2D. */
        [[nodiscard]] UITargetSize GetSize(App& app) override;

        /** @brief Submits `list` to the entity through the renderer's WorldTargets2D, when the app has it. */
        void Submit(App& app, DrawList2D list) override;

        /** @brief `Linear`: the quad is drawn among the lit scene's transparent items (ADR-0065). */
        [[nodiscard]] BlendSpace2D GetDefaultBlendSpace() const noexcept override
        {
            return BlendSpace2D::Linear;
        }

        /** @brief `ContentScale`: the quad's logical units are its UI units. */
        [[nodiscard]] ScaleRule GetDefaultScaleRule() const noexcept override;

        /** @brief True: the root's pointer is the game's ray. */
        [[nodiscard]] bool TakesPointerRay() const noexcept override
        {
            return true;
        }

        /**
         * @brief Where `ray` meets the quad's plane, from either side, in UI units from its top-left (past an edge
         * when it meets the plane outside the quad); none when it runs along the plane or points away from it, or
         * the entity has no quad.
         */
        [[nodiscard]] std::optional<Math::Vec2> PointerAt(App& app, const Math::Ray& ray) override;

      private:
        Entity m_entity;
    };
} // namespace Fenrir

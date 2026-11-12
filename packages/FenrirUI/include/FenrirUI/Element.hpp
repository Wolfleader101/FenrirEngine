#pragma once

#include <functional>
#include <memory>

#include "FenrirDraw2D/DrawList2D.hpp"
#include "FenrirInput/InputRouter.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirUI/ElementId.hpp"
#include "FenrirUI/Style.hpp"

namespace Fenrir
{
    class App;
    class Painter;
    class UIRoot;

    /** @brief How a measure constraint bounds a size: not at all, to exactly it, or to at most it. */
    enum class MeasureMode : std::uint8_t
    {
        Undefined,
        Exactly,
        AtMost,
    };

    /** @brief The space layout offers a measured element's content box, in UI units. */
    struct MeasureConstraints
    {
        float width = 0.0f;
        MeasureMode widthMode = MeasureMode::Undefined;
        float height = 0.0f;
        MeasureMode heightMode = MeasureMode::Undefined;
    };

    /**
     * @brief An element's laid-out rectangles, in UI units from its root's top-left, before any transform: its border
     * box, padding box (inside the border) and content box (inside the padding), and the clip its ancestors' `overflow:
     * hidden` sets on it (their padding boxes intersected; the root's whole box when none clips).
     */
    struct ElementBoxes
    {
        Rect2D border;
        Rect2D padding;
        Rect2D content;
        Rect2D clip;
    };

    /** @brief What an element paints its content with: its rectangles and resolved style. */
    struct ElementPaint
    {
        const ElementBoxes& boxes;
        const ResolvedStyle& style;
        /**
         * @brief What to multiply the content's alpha by: the element's opacity when it has no offscreen group of its
         * own (a leaf, or groups switched off), 1 otherwise.
         */
        float opacity = 1.0f;
    };

    /**
     * @brief One element of a UI root's tree (ADR-0066). The root keeps the tree: the tag name, id string, classes,
     * pseudo-classes, styles, children and rectangles, all reached through UIRoot by the element's id. This object is
     * the element's behaviour, which a custom element (and each control) overrides: how its content measures, what it
     * paints over its box, and its input. A plain Element measures as an empty box and paints only its box.
     *
     * Register a subclass under a tag name with UIRoots::RegisterElement; creating that tag makes one through its
     * factory.
     */
    class Element
    {
      public:
        Element() = default;
        Element(const Element&) = delete;
        Element& operator=(const Element&) = delete;
        Element(Element&&) = delete;
        Element& operator=(Element&&) = delete;
        virtual ~Element() = default;

        /** @brief The element's id in its root. */
        [[nodiscard]] ElementId GetId() const noexcept
        {
            return m_id;
        }

        /** @brief The root it belongs to. */
        [[nodiscard]] UIRoot& GetRoot() const noexcept
        {
            return *m_root;
        }

        /**
         * @brief Whether layout measures the content through Measure. Only an element with no children is measured: one
         * with children is sized by them. Read when the element is made.
         */
        [[nodiscard]] virtual bool HasMeasure() const
        {
            return false;
        }

        /**
         * @brief The content box's size in UI units for `constraints`, when HasMeasure is true. Layout caches the
         * result; UIRoot::InvalidateMeasure asks for it again.
         */
        [[nodiscard]] virtual Math::Vec2 Measure(const MeasureConstraints& constraints)
        {
            (void)constraints;
            return Math::Vec2(0.0f);
        }

        /**
         * @brief Paints the content, after the element's shadows, background and border and before its children, under
         * its transform, its own overflow clip and its ancestors' clips. Coordinates are UI units from the root's
         * top-left. It must not create, remove or move elements: `paint` refers into the root's tree.
         */
        virtual void Paint(Painter& painter, const ElementPaint& paint)
        {
            (void)painter;
            (void)paint;
        }

        /**
         * @brief Sees an input routed to the element; returns Consumed when it took it. The root's input router, which
         * a later part of the FenrirUI spec builds, calls it; nothing does yet.
         */
        virtual RouteResult OnInput(App& app, const RoutedInput& input)
        {
            (void)app;
            (void)input;
            return RouteResult::Passed;
        }

      private:
        friend class UIRoot;

        ElementId m_id;
        UIRoot* m_root = nullptr;
    };

    /** @brief Makes the element a registered tag names. */
    using ElementFactory = std::function<std::unique_ptr<Element>()>;
} // namespace Fenrir

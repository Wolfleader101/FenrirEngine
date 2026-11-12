#pragma once

#include <cstddef>
#include <cstdint>

#include "FenrirDraw2D/DrawList2D.hpp"
#include "FenrirPainter/Paint.hpp"
#include "FenrirUI/Style.hpp"

namespace Fenrir
{
    class Element;
    class FontLibrary;
} // namespace Fenrir

namespace Fenrir::Detail
{
    /** @brief A Yoga node, named without Yoga's types. */
    struct LayoutNode;
    class ElementText;

    /** @brief A node's laid-out rectangle relative to its parent's border box, and its border and padding widths. */
    struct LayoutResult
    {
        Rect2D rect;
        Sides border;
        Sides padding;
    };

    /**
     * @brief The layout bridge (ADR-0064): the one place Yoga is used. A tree of its nodes, each with the resolved
     * style written into Yoga's style, measured leaves calling their element, and rectangles read back in UI units.
     * Rounding is off (the root snaps the rectangles to its target's pixels), and the defaults are Yoga's own.
     * Replacing Yoga is rewriting this file and LayoutBridge.cpp.
     */
    class LayoutTree
    {
      public:
        LayoutTree();
        LayoutTree(const LayoutTree&) = delete;
        LayoutTree& operator=(const LayoutTree&) = delete;
        LayoutTree(LayoutTree&&) = delete;
        LayoutTree& operator=(LayoutTree&&) = delete;
        ~LayoutTree();

        /**
         * @brief A new node with no parent, measured when it is a leaf: through its text, once SetText gives it one,
         * else through `element` when it has a measure.
         */
        [[nodiscard]] LayoutNode* CreateNode(Element* element);
        /** @brief Frees `node`, taking it out of its parent first; its children are left without a parent. */
        void DestroyNode(LayoutNode* node);
        /** @brief Puts `child` under `parent` at `index`, making `parent` unmeasured. */
        void InsertChild(LayoutNode* parent, LayoutNode* child, std::size_t index);
        /** @brief Takes `child` out of `parent`; a parent left with no children is measured again if it has a measure.
         */
        void RemoveChild(LayoutNode* parent, LayoutNode* child);

        /** @brief Writes `style`'s layout properties into the node; Yoga marks it dirty only if one changed. */
        void ApplyStyle(LayoutNode* node, const ResolvedStyle& style);
        /** @brief Fixes the node's size (the root element's, which fills its target), whatever its style says. */
        void SetFixedSize(LayoutNode* node, float width, float height);
        /** @brief Has a measured leaf measured again. */
        void InvalidateMeasure(LayoutNode* node);
        /**
         * @brief Measures the node, while it has no children, through `text` (null for none, which measures through
         * its element again), and has it measured again.
         */
        void SetText(LayoutNode* node, ElementText* text);
        /** @brief What text measures lay their paragraphs out with; null lays nothing out. */
        void SetFonts(FontLibrary* fonts) noexcept;

        /** @brief Lays the tree under `root` out if anything in it is dirty; true when it did. */
        bool Layout(LayoutNode* root);
        /** @brief The node's last layout. */
        [[nodiscard]] LayoutResult Result(const LayoutNode* node) const;

        /** @brief Measure calls made since the last TakeMeasureCount. */
        [[nodiscard]] std::uint32_t TakeMeasureCount() noexcept;

        /** @brief Yoga's configuration and the measure count, which the measure callback reaches through Yoga. */
        struct Config;

      private:
        Config* m_config = nullptr;
    };
} // namespace Fenrir::Detail

// The only file that includes Yoga (ADR-0064).
#include "LayoutBridge.hpp"

#include <yoga/Yoga.h>

#include "FenrirUI/Element.hpp"

namespace Fenrir::Detail
{
    struct LayoutTree::Config
    {
        YGConfigRef config = nullptr;
        std::uint32_t measures = 0;
    };

    namespace
    {
        YGNodeRef Node(LayoutNode* node)
        {
            return reinterpret_cast<YGNodeRef>(node);
        }

        YGNodeConstRef Node(const LayoutNode* node)
        {
            return reinterpret_cast<YGNodeConstRef>(node);
        }

        MeasureMode ModeOf(YGMeasureMode mode)
        {
            switch (mode)
            {
            case YGMeasureModeExactly:
                return MeasureMode::Exactly;
            case YGMeasureModeAtMost:
                return MeasureMode::AtMost;
            case YGMeasureModeUndefined:
                break;
            }
            return MeasureMode::Undefined;
        }

        YGSize MeasureElement(YGNodeConstRef node, float width, YGMeasureMode widthMode, float height,
                              YGMeasureMode heightMode)
        {
            auto* element = static_cast<Element*>(YGNodeGetContext(node));
            auto* config = static_cast<LayoutTree::Config*>(YGConfigGetContext(
                YGNodeGetConfig(const_cast<YGNodeRef>(node)))); // YGNodeGetConfig takes a mutable node; it reads only.
            ++config->measures;
            const MeasureConstraints constraints{width, ModeOf(widthMode), height, ModeOf(heightMode)};
            const Math::Vec2 size = element->Measure(constraints);
            return {size.x, size.y};
        }

        /** @brief Sets the node to measure through its element when it has a measure and no children. */
        void UpdateMeasure(YGNodeRef node)
        {
            const auto* element = static_cast<const Element*>(YGNodeGetContext(node));
            const bool measured = element != nullptr && element->HasMeasure() && YGNodeGetChildCount(node) == 0;
            if (measured != YGNodeHasMeasureFunc(node))
            {
                YGNodeSetMeasureFunc(node, measured ? &MeasureElement : nullptr);
            }
        }

        YGFlexDirection ToYoga(FlexDirection direction)
        {
            switch (direction)
            {
            case FlexDirection::Column:
                return YGFlexDirectionColumn;
            case FlexDirection::ColumnReverse:
                return YGFlexDirectionColumnReverse;
            case FlexDirection::Row:
                return YGFlexDirectionRow;
            case FlexDirection::RowReverse:
                return YGFlexDirectionRowReverse;
            }
            return YGFlexDirectionColumn;
        }

        YGWrap ToYoga(FlexWrap wrap)
        {
            switch (wrap)
            {
            case FlexWrap::NoWrap:
                return YGWrapNoWrap;
            case FlexWrap::Wrap:
                return YGWrapWrap;
            case FlexWrap::WrapReverse:
                return YGWrapWrapReverse;
            }
            return YGWrapNoWrap;
        }

        YGAlign ToYoga(Align align)
        {
            switch (align)
            {
            case Align::Auto:
                return YGAlignAuto;
            case Align::FlexStart:
                return YGAlignFlexStart;
            case Align::Center:
                return YGAlignCenter;
            case Align::FlexEnd:
                return YGAlignFlexEnd;
            case Align::Stretch:
                return YGAlignStretch;
            case Align::Baseline:
                return YGAlignBaseline;
            case Align::SpaceBetween:
                return YGAlignSpaceBetween;
            case Align::SpaceAround:
                return YGAlignSpaceAround;
            case Align::SpaceEvenly:
                return YGAlignSpaceEvenly;
            }
            return YGAlignAuto;
        }

        YGJustify ToYoga(Justify justify)
        {
            switch (justify)
            {
            case Justify::FlexStart:
                return YGJustifyFlexStart;
            case Justify::Center:
                return YGJustifyCenter;
            case Justify::FlexEnd:
                return YGJustifyFlexEnd;
            case Justify::SpaceBetween:
                return YGJustifySpaceBetween;
            case Justify::SpaceAround:
                return YGJustifySpaceAround;
            case Justify::SpaceEvenly:
                return YGJustifySpaceEvenly;
            }
            return YGJustifyFlexStart;
        }

        /** @brief A length through Yoga's three setters for a property: points, percent and auto. */
        template <typename Points, typename Percent, typename Auto>
        void SetLength(const Length& length, Points points, Percent percent, Auto automatic)
        {
            switch (length.unit)
            {
            case LengthUnit::Px:
                points(length.value);
                return;
            case LengthUnit::Percent:
                percent(length.value);
                return;
            case LengthUnit::Auto:
                automatic();
                return;
            }
        }

        void SetEdge(YGNodeRef node, YGEdge edge, const Length& inset)
        {
            SetLength(
                inset, [&](float v) { YGNodeStyleSetPosition(node, edge, v); },
                [&](float v) { YGNodeStyleSetPositionPercent(node, edge, v); },
                [&] { YGNodeStyleSetPositionAuto(node, edge); });
        }

        void SetMargin(YGNodeRef node, YGEdge edge, const Length& margin)
        {
            SetLength(
                margin, [&](float v) { YGNodeStyleSetMargin(node, edge, v); },
                [&](float v) { YGNodeStyleSetMarginPercent(node, edge, v); },
                [&] { YGNodeStyleSetMarginAuto(node, edge); });
        }

        void SetPadding(YGNodeRef node, YGEdge edge, const Length& padding)
        {
            // Padding has no auto: it is none.
            SetLength(
                padding, [&](float v) { YGNodeStyleSetPadding(node, edge, v); },
                [&](float v) { YGNodeStyleSetPaddingPercent(node, edge, v); },
                [&] { YGNodeStyleSetPadding(node, edge, 0.0f); });
        }

        void SetGap(YGNodeRef node, YGGutter gutter, const Length& gap)
        {
            SetLength(
                gap, [&](float v) { YGNodeStyleSetGap(node, gutter, v); },
                [&](float v) { YGNodeStyleSetGapPercent(node, gutter, v); },
                [&] { YGNodeStyleSetGap(node, gutter, 0.0f); });
        }
    } // namespace

    LayoutTree::LayoutTree() : m_config(new Config)
    {
        m_config->config = YGConfigNew();
        // The root snaps the rectangles itself, to its target's pixels, so Yoga's rounding is off.
        YGConfigSetPointScaleFactor(m_config->config, 0.0f);
        YGConfigSetContext(m_config->config, m_config);
    }

    LayoutTree::~LayoutTree()
    {
        YGConfigFree(m_config->config);
        delete m_config;
    }

    LayoutNode* LayoutTree::CreateNode(Element* element)
    {
        YGNodeRef node = YGNodeNewWithConfig(m_config->config);
        YGNodeSetContext(node, element);
        UpdateMeasure(node);
        return reinterpret_cast<LayoutNode*>(node);
    }

    void LayoutTree::DestroyNode(LayoutNode* node)
    {
        YGNodeRef yoga = Node(node);
        if (YGNodeRef owner = YGNodeGetOwner(yoga))
        {
            YGNodeRemoveChild(owner, yoga);
            UpdateMeasure(owner);
        }
        YGNodeFree(yoga);
    }

    void LayoutTree::InsertChild(LayoutNode* parent, LayoutNode* child, std::size_t index)
    {
        YGNodeRef yogaParent = Node(parent);
        // Yoga refuses children on a measured node, so the measure goes first.
        if (YGNodeHasMeasureFunc(yogaParent))
        {
            YGNodeSetMeasureFunc(yogaParent, nullptr);
        }
        YGNodeInsertChild(yogaParent, Node(child), index);
    }

    void LayoutTree::RemoveChild(LayoutNode* parent, LayoutNode* child)
    {
        YGNodeRemoveChild(Node(parent), Node(child));
        UpdateMeasure(Node(parent));
    }

    void LayoutTree::ApplyStyle(LayoutNode* layoutNode, const ResolvedStyle& style)
    {
        YGNodeRef node = Node(layoutNode);
        YGNodeStyleSetDisplay(node, style.display == Display::None ? YGDisplayNone : YGDisplayFlex);
        YGNodeStyleSetPositionType(node, style.position == Position::Absolute ? YGPositionTypeAbsolute
                                                                              : YGPositionTypeRelative);
        SetEdge(node, YGEdgeTop, style.top);
        SetEdge(node, YGEdgeRight, style.right);
        SetEdge(node, YGEdgeBottom, style.bottom);
        SetEdge(node, YGEdgeLeft, style.left);
        SetLength(
            style.width, [&](float v) { YGNodeStyleSetWidth(node, v); },
            [&](float v) { YGNodeStyleSetWidthPercent(node, v); }, [&] { YGNodeStyleSetWidthAuto(node); });
        SetLength(
            style.height, [&](float v) { YGNodeStyleSetHeight(node, v); },
            [&](float v) { YGNodeStyleSetHeightPercent(node, v); }, [&] { YGNodeStyleSetHeightAuto(node); });
        // A minimum or maximum of auto is none.
        SetLength(
            style.minWidth, [&](float v) { YGNodeStyleSetMinWidth(node, v); },
            [&](float v) { YGNodeStyleSetMinWidthPercent(node, v); },
            [&] { YGNodeStyleSetMinWidth(node, YGUndefined); });
        SetLength(
            style.minHeight, [&](float v) { YGNodeStyleSetMinHeight(node, v); },
            [&](float v) { YGNodeStyleSetMinHeightPercent(node, v); },
            [&] { YGNodeStyleSetMinHeight(node, YGUndefined); });
        SetLength(
            style.maxWidth, [&](float v) { YGNodeStyleSetMaxWidth(node, v); },
            [&](float v) { YGNodeStyleSetMaxWidthPercent(node, v); },
            [&] { YGNodeStyleSetMaxWidth(node, YGUndefined); });
        SetLength(
            style.maxHeight, [&](float v) { YGNodeStyleSetMaxHeight(node, v); },
            [&](float v) { YGNodeStyleSetMaxHeightPercent(node, v); },
            [&] { YGNodeStyleSetMaxHeight(node, YGUndefined); });
        YGNodeStyleSetAspectRatio(node, style.aspectRatio > 0.0f ? style.aspectRatio : YGUndefined);
        SetMargin(node, YGEdgeTop, style.marginTop);
        SetMargin(node, YGEdgeRight, style.marginRight);
        SetMargin(node, YGEdgeBottom, style.marginBottom);
        SetMargin(node, YGEdgeLeft, style.marginLeft);
        SetPadding(node, YGEdgeTop, style.paddingTop);
        SetPadding(node, YGEdgeRight, style.paddingRight);
        SetPadding(node, YGEdgeBottom, style.paddingBottom);
        SetPadding(node, YGEdgeLeft, style.paddingLeft);
        YGNodeStyleSetBorder(node, YGEdgeTop, style.borderTopWidth);
        YGNodeStyleSetBorder(node, YGEdgeRight, style.borderRightWidth);
        YGNodeStyleSetBorder(node, YGEdgeBottom, style.borderBottomWidth);
        YGNodeStyleSetBorder(node, YGEdgeLeft, style.borderLeftWidth);
        YGNodeStyleSetFlexDirection(node, ToYoga(style.flexDirection));
        YGNodeStyleSetFlexWrap(node, ToYoga(style.flexWrap));
        YGNodeStyleSetFlexGrow(node, style.flexGrow);
        YGNodeStyleSetFlexShrink(node, style.flexShrink);
        SetLength(
            style.flexBasis, [&](float v) { YGNodeStyleSetFlexBasis(node, v); },
            [&](float v) { YGNodeStyleSetFlexBasisPercent(node, v); }, [&] { YGNodeStyleSetFlexBasisAuto(node); });
        YGNodeStyleSetAlignItems(node, ToYoga(style.alignItems));
        YGNodeStyleSetAlignSelf(node, ToYoga(style.alignSelf));
        YGNodeStyleSetAlignContent(node, ToYoga(style.alignContent));
        YGNodeStyleSetJustifyContent(node, ToYoga(style.justifyContent));
        SetGap(node, YGGutterRow, style.rowGap);
        SetGap(node, YGGutterColumn, style.columnGap);
        YGNodeStyleSetOverflow(node, style.overflow == Overflow::Hidden ? YGOverflowHidden : YGOverflowVisible);
    }

    void LayoutTree::SetFixedSize(LayoutNode* node, float width, float height)
    {
        YGNodeStyleSetWidth(Node(node), width);
        YGNodeStyleSetHeight(Node(node), height);
    }

    void LayoutTree::InvalidateMeasure(LayoutNode* node)
    {
        // Only a measured node may be marked dirty; any other is laid out from its children.
        if (YGNodeHasMeasureFunc(Node(node)))
        {
            YGNodeMarkDirty(Node(node));
        }
    }

    bool LayoutTree::Layout(LayoutNode* root)
    {
        if (!YGNodeIsDirty(Node(root)))
        {
            return false;
        }
        YGNodeCalculateLayout(Node(root), YGUndefined, YGUndefined, YGDirectionLTR);
        return true;
    }

    LayoutResult LayoutTree::Result(const LayoutNode* layoutNode) const
    {
        const YGNodeConstRef node = Node(layoutNode);
        LayoutResult result;
        result.rect = {YGNodeLayoutGetLeft(node), YGNodeLayoutGetTop(node), YGNodeLayoutGetWidth(node),
                       YGNodeLayoutGetHeight(node)};
        result.border = {YGNodeLayoutGetBorder(node, YGEdgeTop), YGNodeLayoutGetBorder(node, YGEdgeRight),
                         YGNodeLayoutGetBorder(node, YGEdgeBottom), YGNodeLayoutGetBorder(node, YGEdgeLeft)};
        result.padding = {YGNodeLayoutGetPadding(node, YGEdgeTop), YGNodeLayoutGetPadding(node, YGEdgeRight),
                          YGNodeLayoutGetPadding(node, YGEdgeBottom), YGNodeLayoutGetPadding(node, YGEdgeLeft)};
        return result;
    }

    std::uint32_t LayoutTree::TakeMeasureCount() noexcept
    {
        const std::uint32_t count = m_config->measures;
        m_config->measures = 0;
        return count;
    }
} // namespace Fenrir::Detail

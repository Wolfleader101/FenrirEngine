#include "FenrirUI/Style.hpp"

#include <utility>

#include "StyleValues.hpp"

namespace Fenrir
{
    namespace
    {
        /** @brief Copies `property`'s field from `from` to `to`. */
        void CopyProperty(StyleProperty property, const ResolvedStyle& from, ResolvedStyle& to)
        {
            switch (property)
            {
#define FENRIR_UI_COPY_PROPERTY(Enum, Name, Field, Parse) \
    case StyleProperty::Enum:                             \
        to.Field = from.Field;                            \
        break;
                FENRIR_UI_STYLE_PROPERTIES(FENRIR_UI_COPY_PROPERTY)
#undef FENRIR_UI_COPY_PROPERTY
            case StyleProperty::Count:
                break;
            }
        }
    } // namespace

    bool StyleDeclarations::IsSet(StyleProperty property) const noexcept
    {
        return property != StyleProperty::Count && m_set.test(static_cast<std::size_t>(property));
    }

    StyleDeclarations& StyleDeclarations::Unset(StyleProperty property) noexcept
    {
        if (property != StyleProperty::Count)
        {
            m_set.reset(static_cast<std::size_t>(property));
            CopyProperty(property, ResolvedStyle{}, m_values);
        }
        return *this;
    }

    void StyleDeclarations::Clear() noexcept
    {
        m_set.reset();
        m_values = ResolvedStyle{};
    }

    void StyleDeclarations::ApplyTo(ResolvedStyle& style) const
    {
        for (std::size_t i = 0; i < StylePropertyCount; ++i)
        {
            if (m_set.test(i))
            {
                CopyProperty(static_cast<StyleProperty>(i), m_values, style);
            }
        }
    }

    StyleDeclarations& StyleDeclarations::CopyFrom(StyleProperty property, const ResolvedStyle& from)
    {
        if (property != StyleProperty::Count)
        {
            CopyProperty(property, from, m_values);
            m_set.set(static_cast<std::size_t>(property));
        }
        return *this;
    }

    StyleDeclarations& StyleDeclarations::SetDisplay(Fenrir::Display value)
    {
        return Set(StyleProperty::Display, &ResolvedStyle::display, value);
    }

    StyleDeclarations& StyleDeclarations::SetPosition(Fenrir::Position value)
    {
        return Set(StyleProperty::Position, &ResolvedStyle::position, value);
    }

    StyleDeclarations& StyleDeclarations::SetTop(Length value)
    {
        return Set(StyleProperty::Top, &ResolvedStyle::top, value);
    }

    StyleDeclarations& StyleDeclarations::SetRight(Length value)
    {
        return Set(StyleProperty::Right, &ResolvedStyle::right, value);
    }

    StyleDeclarations& StyleDeclarations::SetBottom(Length value)
    {
        return Set(StyleProperty::Bottom, &ResolvedStyle::bottom, value);
    }

    StyleDeclarations& StyleDeclarations::SetLeft(Length value)
    {
        return Set(StyleProperty::Left, &ResolvedStyle::left, value);
    }

    StyleDeclarations& StyleDeclarations::SetWidth(Length value)
    {
        return Set(StyleProperty::Width, &ResolvedStyle::width, value);
    }

    StyleDeclarations& StyleDeclarations::SetHeight(Length value)
    {
        return Set(StyleProperty::Height, &ResolvedStyle::height, value);
    }

    StyleDeclarations& StyleDeclarations::SetMinWidth(Length value)
    {
        return Set(StyleProperty::MinWidth, &ResolvedStyle::minWidth, value);
    }

    StyleDeclarations& StyleDeclarations::SetMinHeight(Length value)
    {
        return Set(StyleProperty::MinHeight, &ResolvedStyle::minHeight, value);
    }

    StyleDeclarations& StyleDeclarations::SetMaxWidth(Length value)
    {
        return Set(StyleProperty::MaxWidth, &ResolvedStyle::maxWidth, value);
    }

    StyleDeclarations& StyleDeclarations::SetMaxHeight(Length value)
    {
        return Set(StyleProperty::MaxHeight, &ResolvedStyle::maxHeight, value);
    }

    StyleDeclarations& StyleDeclarations::SetAspectRatio(float value)
    {
        return Set(StyleProperty::AspectRatio, &ResolvedStyle::aspectRatio, value);
    }

    StyleDeclarations& StyleDeclarations::SetMargin(Length top, Length right, Length bottom, Length left)
    {
        Set(StyleProperty::MarginTop, &ResolvedStyle::marginTop, top);
        Set(StyleProperty::MarginRight, &ResolvedStyle::marginRight, right);
        Set(StyleProperty::MarginBottom, &ResolvedStyle::marginBottom, bottom);
        return Set(StyleProperty::MarginLeft, &ResolvedStyle::marginLeft, left);
    }

    StyleDeclarations& StyleDeclarations::SetPadding(Length top, Length right, Length bottom, Length left)
    {
        Set(StyleProperty::PaddingTop, &ResolvedStyle::paddingTop, top);
        Set(StyleProperty::PaddingRight, &ResolvedStyle::paddingRight, right);
        Set(StyleProperty::PaddingBottom, &ResolvedStyle::paddingBottom, bottom);
        return Set(StyleProperty::PaddingLeft, &ResolvedStyle::paddingLeft, left);
    }

    StyleDeclarations& StyleDeclarations::SetBorderWidths(const Sides& widths)
    {
        Set(StyleProperty::BorderTopWidth, &ResolvedStyle::borderTopWidth, widths.top);
        Set(StyleProperty::BorderRightWidth, &ResolvedStyle::borderRightWidth, widths.right);
        Set(StyleProperty::BorderBottomWidth, &ResolvedStyle::borderBottomWidth, widths.bottom);
        return Set(StyleProperty::BorderLeftWidth, &ResolvedStyle::borderLeftWidth, widths.left);
    }

    StyleDeclarations& StyleDeclarations::SetFlexDirection(Fenrir::FlexDirection value)
    {
        return Set(StyleProperty::FlexDirection, &ResolvedStyle::flexDirection, value);
    }

    StyleDeclarations& StyleDeclarations::SetFlexWrap(Fenrir::FlexWrap value)
    {
        return Set(StyleProperty::FlexWrap, &ResolvedStyle::flexWrap, value);
    }

    StyleDeclarations& StyleDeclarations::SetFlexGrow(float value)
    {
        return Set(StyleProperty::FlexGrow, &ResolvedStyle::flexGrow, value);
    }

    StyleDeclarations& StyleDeclarations::SetFlexShrink(float value)
    {
        return Set(StyleProperty::FlexShrink, &ResolvedStyle::flexShrink, value);
    }

    StyleDeclarations& StyleDeclarations::SetFlexBasis(Length value)
    {
        return Set(StyleProperty::FlexBasis, &ResolvedStyle::flexBasis, value);
    }

    StyleDeclarations& StyleDeclarations::SetAlignItems(Align value)
    {
        return Set(StyleProperty::AlignItems, &ResolvedStyle::alignItems, value);
    }

    StyleDeclarations& StyleDeclarations::SetAlignSelf(Align value)
    {
        return Set(StyleProperty::AlignSelf, &ResolvedStyle::alignSelf, value);
    }

    StyleDeclarations& StyleDeclarations::SetAlignContent(Align value)
    {
        return Set(StyleProperty::AlignContent, &ResolvedStyle::alignContent, value);
    }

    StyleDeclarations& StyleDeclarations::SetJustifyContent(Justify value)
    {
        return Set(StyleProperty::JustifyContent, &ResolvedStyle::justifyContent, value);
    }

    StyleDeclarations& StyleDeclarations::SetRowGap(Length value)
    {
        return Set(StyleProperty::RowGap, &ResolvedStyle::rowGap, value);
    }

    StyleDeclarations& StyleDeclarations::SetColumnGap(Length value)
    {
        return Set(StyleProperty::ColumnGap, &ResolvedStyle::columnGap, value);
    }

    StyleDeclarations& StyleDeclarations::SetBackgroundColor(const Colour& value)
    {
        return Set(StyleProperty::BackgroundColor, &ResolvedStyle::backgroundColor, value);
    }

    StyleDeclarations& StyleDeclarations::SetBackgroundImage(const Fenrir::BackgroundImage& value)
    {
        return Set(StyleProperty::BackgroundImage, &ResolvedStyle::backgroundImage, value);
    }

    StyleDeclarations& StyleDeclarations::SetBackgroundSize(Fenrir::BackgroundSize value)
    {
        return Set(StyleProperty::BackgroundSize, &ResolvedStyle::backgroundSize, value);
    }

    StyleDeclarations& StyleDeclarations::SetBorderImage(AssetHandle<Image> image, const Sides& slice)
    {
        Set(StyleProperty::BorderImageSource, &ResolvedStyle::borderImageSource, image);
        return Set(StyleProperty::BorderImageSlice, &ResolvedStyle::borderImageSlice, slice);
    }

    StyleDeclarations& StyleDeclarations::SetBorderColor(const Colour& value)
    {
        Set(StyleProperty::BorderTopColor, &ResolvedStyle::borderTopColor, value);
        Set(StyleProperty::BorderRightColor, &ResolvedStyle::borderRightColor, value);
        Set(StyleProperty::BorderBottomColor, &ResolvedStyle::borderBottomColor, value);
        return Set(StyleProperty::BorderLeftColor, &ResolvedStyle::borderLeftColor, value);
    }

    StyleDeclarations& StyleDeclarations::SetBorderRadius(const CornerRadii& radii)
    {
        Set(StyleProperty::BorderTopLeftRadius, &ResolvedStyle::borderTopLeftRadius, radii.topLeft);
        Set(StyleProperty::BorderTopRightRadius, &ResolvedStyle::borderTopRightRadius, radii.topRight);
        Set(StyleProperty::BorderBottomRightRadius, &ResolvedStyle::borderBottomRightRadius, radii.bottomRight);
        return Set(StyleProperty::BorderBottomLeftRadius, &ResolvedStyle::borderBottomLeftRadius, radii.bottomLeft);
    }

    StyleDeclarations& StyleDeclarations::SetBoxShadow(std::vector<Shadow> shadows)
    {
        return Set(StyleProperty::BoxShadow, &ResolvedStyle::boxShadow, std::move(shadows));
    }

    StyleDeclarations& StyleDeclarations::SetOpacity(float value)
    {
        return Set(StyleProperty::Opacity, &ResolvedStyle::opacity, value);
    }

    StyleDeclarations& StyleDeclarations::SetTransform(std::vector<TransformStep> steps)
    {
        return Set(StyleProperty::Transform, &ResolvedStyle::transform, std::move(steps));
    }

    StyleDeclarations& StyleDeclarations::SetTransformOrigin(Length x, Length y)
    {
        Set(StyleProperty::TransformOriginX, &ResolvedStyle::transformOriginX, x);
        return Set(StyleProperty::TransformOriginY, &ResolvedStyle::transformOriginY, y);
    }

    StyleDeclarations& StyleDeclarations::SetOverflow(Fenrir::Overflow value)
    {
        return Set(StyleProperty::Overflow, &ResolvedStyle::overflow, value);
    }

    StyleDeclarations& StyleDeclarations::SetZIndex(std::int32_t value)
    {
        return Set(StyleProperty::ZIndex, &ResolvedStyle::zIndex, value);
    }

    StyleDeclarations& StyleDeclarations::SetPointerEvents(Fenrir::PointerEvents value)
    {
        return Set(StyleProperty::PointerEvents, &ResolvedStyle::pointerEvents, value);
    }

    StyleDeclarations& StyleDeclarations::SetCursor(Fenrir::Cursor value)
    {
        return Set(StyleProperty::Cursor, &ResolvedStyle::cursor, value);
    }
} // namespace Fenrir

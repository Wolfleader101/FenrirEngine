#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirUI/Style.hpp"

namespace Fenrir::Detail
{
    /** @brief What reading a value may need: the asset store's load for `url()`, and where to say what went wrong. */
    struct StyleParseContext
    {
        /** @brief Loads the image `url()` names; an empty handle when it cannot. */
        std::function<AssetHandle<Image>(std::string_view path)> loadImage;
        /** @brief Why a value was refused. */
        std::string error;
        /** @brief Only check that values read: a `url()` needs a path, and nothing is loaded. */
        bool checkOnly = false;
    };

    /** @brief Whether `c` is CSS whitespace. */
    [[nodiscard]] bool IsSpace(char c);
    /** @brief `text` without the CSS whitespace at its ends. */
    [[nodiscard]] std::string_view Trim(std::string_view text);
    /** @brief `c` in lower case, for ASCII. */
    [[nodiscard]] char Lower(char c);
    /** @brief `text` in lower case, for ASCII: CSS's names are not case-sensitive. */
    [[nodiscard]] std::string Lowered(std::string_view text);

    /** @brief What a declaration's property name is to FenrirUI. */
    enum class PropertyKind : std::uint8_t
    {
        /** @brief Not in the spec's property set. */
        Unknown,
        /** @brief A longhand the style holds (or `transform-origin`), read by ParseStyleProperty. */
        Styled,
        /** @brief A shorthand, expanded by ExpandShorthand. */
        Shorthand,
        /**
         * @brief A longhand of the spec's property set that the style does not hold yet (text, filters, scrollbars,
         * background-position): parsed and kept, and styling nothing until the ticket that adds it.
         */
        NotYetStyled,
    };

    /** @brief One longhand and its value, as a shorthand expands to them. */
    using Longhand = std::pair<std::string, std::string>;

    /** @brief What `name` (lower case) is; a custom property (`--x`) is Unknown here. */
    [[nodiscard]] PropertyKind ClassifyProperty(std::string_view name) noexcept;

    /**
     * @brief Expands the shorthand `name`'s `value` into its longhands, as CSS does, each value as text. False, with
     * `context.error` saying why, for a value it cannot split. The longhands' values are not read here.
     */
    bool ExpandShorthand(std::string_view name, std::string_view value, std::vector<Longhand>& out,
                         StyleParseContext& context);

    /**
     * @brief Reads a declaration, longhand or shorthand, into `into`: all of a shorthand's longhands or none of them.
     * A NotYetStyled longhand reads as nothing held. False, leaving `into` as it was, as ParseStyleProperty is.
     */
    bool ParseStyleDeclaration(std::string_view name, std::string_view value, StyleDeclarations& into,
                               StyleParseContext& context);

    /** @brief The properties of the style a declaration of `name` (a longhand or a shorthand) gives. */
    [[nodiscard]] std::vector<StyleProperty> PropertiesOf(std::string_view name);

    /** @brief Whether `value` holds a `var()`. */
    [[nodiscard]] bool ContainsVar(std::string_view value) noexcept;

    /** @brief Whether every `var()` in `value` names a custom property, with an optional fallback after a comma. */
    bool CheckVarSyntax(std::string_view value, std::string& error);

    /**
     * @brief `value` with every `var()` replaced by its custom property's value through `lookup` (null for none), or
     * its fallback; nothing when one has neither, or substitution nests too deep (a cycle).
     */
    [[nodiscard]] std::optional<std::string> SubstituteVars(
        std::string_view value, const std::function<const std::string*(std::string_view)>& lookup);

    /**
     * @brief Reads `value` as the inline style property `name` (a CSS longhand, or `transform-origin`, which gives both
     * of its parts) into `into`. False, leaving `into` as it was and `context.error` saying why, for an unknown name or
     * a value it cannot read. The stylesheet's declarations read through the same parsers.
     */
    bool ParseStyleProperty(std::string_view name, std::string_view value, StyleDeclarations& into,
                            StyleParseContext& context);

    /** @brief Copies `property`'s field of `from` to `to`. */
    void CopyProperty(StyleProperty property, const ResolvedStyle& from, ResolvedStyle& to);

    /** @brief Whether `property` is inherited: pointer-events, cursor and the text properties. */
    [[nodiscard]] bool IsInherited(StyleProperty property) noexcept;

    /** @brief Copies every inherited property of `parent` to `style`. */
    void Inherit(const ResolvedStyle& parent, ResolvedStyle& style);
} // namespace Fenrir::Detail

// Every property: its enumerator, its CSS name, its ResolvedStyle field and the parser that reads it. The table drives
// copying a property, reading it by name, and which properties inherit.
#define FENRIR_UI_STYLE_PROPERTIES(X)                                                                         \
    X(Display, "display", display, ParseDisplay)                                                              \
    X(Position, "position", position, ParsePosition)                                                          \
    X(Top, "top", top, ParseLength)                                                                           \
    X(Right, "right", right, ParseLength)                                                                     \
    X(Bottom, "bottom", bottom, ParseLength)                                                                  \
    X(Left, "left", left, ParseLength)                                                                        \
    X(Width, "width", width, ParseLength)                                                                     \
    X(Height, "height", height, ParseLength)                                                                  \
    X(MinWidth, "min-width", minWidth, ParseLength)                                                           \
    X(MinHeight, "min-height", minHeight, ParseLength)                                                        \
    X(MaxWidth, "max-width", maxWidth, ParseLength)                                                           \
    X(MaxHeight, "max-height", maxHeight, ParseLength)                                                        \
    X(AspectRatio, "aspect-ratio", aspectRatio, ParseRatio)                                                   \
    X(MarginTop, "margin-top", marginTop, ParseLength)                                                        \
    X(MarginRight, "margin-right", marginRight, ParseLength)                                                  \
    X(MarginBottom, "margin-bottom", marginBottom, ParseLength)                                               \
    X(MarginLeft, "margin-left", marginLeft, ParseLength)                                                     \
    X(PaddingTop, "padding-top", paddingTop, ParseLength)                                                     \
    X(PaddingRight, "padding-right", paddingRight, ParseLength)                                               \
    X(PaddingBottom, "padding-bottom", paddingBottom, ParseLength)                                            \
    X(PaddingLeft, "padding-left", paddingLeft, ParseLength)                                                  \
    X(BorderTopWidth, "border-top-width", borderTopWidth, ParsePixels)                                        \
    X(BorderRightWidth, "border-right-width", borderRightWidth, ParsePixels)                                  \
    X(BorderBottomWidth, "border-bottom-width", borderBottomWidth, ParsePixels)                               \
    X(BorderLeftWidth, "border-left-width", borderLeftWidth, ParsePixels)                                     \
    X(FlexDirection, "flex-direction", flexDirection, ParseFlexDirection)                                     \
    X(FlexWrap, "flex-wrap", flexWrap, ParseFlexWrap)                                                         \
    X(FlexGrow, "flex-grow", flexGrow, ParseNumber)                                                           \
    X(FlexShrink, "flex-shrink", flexShrink, ParseNumber)                                                     \
    X(FlexBasis, "flex-basis", flexBasis, ParseLength)                                                        \
    X(AlignItems, "align-items", alignItems, ParseAlign)                                                      \
    X(AlignSelf, "align-self", alignSelf, ParseAlign)                                                         \
    X(AlignContent, "align-content", alignContent, ParseAlign)                                                \
    X(JustifyContent, "justify-content", justifyContent, ParseJustify)                                        \
    X(RowGap, "row-gap", rowGap, ParseLength)                                                                 \
    X(ColumnGap, "column-gap", columnGap, ParseLength)                                                        \
    X(BackgroundColor, "background-color", backgroundColor, ParseColour)                                      \
    X(BackgroundImage, "background-image", backgroundImage, ParseBackgroundImage)                             \
    X(BackgroundSize, "background-size", backgroundSize, ParseBackgroundSize)                                 \
    X(BorderImageSource, "border-image-source", borderImageSource, ParseImageUrl)                             \
    X(BorderImageSlice, "border-image-slice", borderImageSlice, ParseSlice)                                   \
    X(BorderTopColor, "border-top-color", borderTopColor, ParseColour)                                        \
    X(BorderRightColor, "border-right-color", borderRightColor, ParseColour)                                  \
    X(BorderBottomColor, "border-bottom-color", borderBottomColor, ParseColour)                               \
    X(BorderLeftColor, "border-left-color", borderLeftColor, ParseColour)                                     \
    X(BorderTopLeftRadius, "border-top-left-radius", borderTopLeftRadius, ParsePixels)                        \
    X(BorderTopRightRadius, "border-top-right-radius", borderTopRightRadius, ParsePixels)                     \
    X(BorderBottomRightRadius, "border-bottom-right-radius", borderBottomRightRadius, ParsePixels)            \
    X(BorderBottomLeftRadius, "border-bottom-left-radius", borderBottomLeftRadius, ParsePixels)               \
    X(BoxShadow, "box-shadow", boxShadow, ParseBoxShadow)                                                     \
    X(Opacity, "opacity", opacity, ParseOpacity)                                                              \
    X(Transform, "transform", transform, ParseTransform)                                                      \
    X(TransformOriginX, "transform-origin-x", transformOriginX, ParseLength)                                  \
    X(TransformOriginY, "transform-origin-y", transformOriginY, ParseLength)                                  \
    X(Overflow, "overflow", overflow, ParseOverflow)                                                          \
    X(ZIndex, "z-index", zIndex, ParseInteger)                                                                \
    X(PointerEvents, "pointer-events", pointerEvents, ParsePointerEvents)                                     \
    X(Cursor, "cursor", cursor, ParseCursor)                                                                  \
    X(Color, "color", color, ParseColour)                                                                     \
    X(FontFamily, "font-family", fontFamily, ParseFontFamily)                                                 \
    X(FontSize, "font-size", fontSize, ParsePixels)                                                           \
    X(FontWeight, "font-weight", fontWeight, ParseFontWeight)                                                 \
    X(FontStyle, "font-style", fontStyle, ParseFontStyle)                                                     \
    X(TextAlign, "text-align", textAlign, ParseTextAlign)                                                     \
    X(LineHeight, "line-height", lineHeight, ParseLineHeight)                                                 \
    X(LetterSpacing, "letter-spacing", letterSpacing, ParseLetterSpacing)                                     \
    X(TextShadow, "text-shadow", textShadow, ParseTextShadow)                                                 \
    X(WhiteSpace, "white-space", whiteSpace, ParseWhiteSpace)                                                 \
    X(TextOverflow, "text-overflow", textOverflow, ParseTextOverflow)                                         \
    X(TransitionProperty, "transition-property", transitionProperty, ParseTransitionProperty)                 \
    X(TransitionDuration, "transition-duration", transitionDuration, ParseDurations)                          \
    X(TransitionTimingFunction, "transition-timing-function", transitionTimingFunction, ParseTimingFunctions) \
    X(TransitionDelay, "transition-delay", transitionDelay, ParseDelays)                                      \
    X(AnimationName, "animation-name", animationName, ParseAnimationNames)                                    \
    X(AnimationDuration, "animation-duration", animationDuration, ParseDurations)                             \
    X(AnimationTimingFunction, "animation-timing-function", animationTimingFunction, ParseTimingFunctions)    \
    X(AnimationDelay, "animation-delay", animationDelay, ParseDelays)                                         \
    X(AnimationIterationCount, "animation-iteration-count", animationIterationCount, ParseIterationCounts)    \
    X(AnimationDirection, "animation-direction", animationDirection, ParseDirections)                         \
    X(AnimationFillMode, "animation-fill-mode", animationFillMode, ParseFillModes)

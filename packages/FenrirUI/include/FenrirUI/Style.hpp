#pragma once

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirMath/Colour.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirPainter/Paint.hpp"
#include "FenrirText/TextStyle.hpp"

namespace Fenrir
{
    /** @brief What a Length's value counts: nothing (`auto`, or `none` for a maximum), UI units (`px`), or percent. */
    enum class LengthUnit : std::uint8_t
    {
        Auto,
        Px,
        Percent,
    };

    /**
     * @brief A length of the style: UI units (CSS `px`), a percentage of what the property measures against (the
     * containing block for sizes, insets, margins, paddings and gaps, as CSS does), or `auto`.
     */
    struct Length
    {
        float value = 0.0f;
        LengthUnit unit = LengthUnit::Px;

        /** @brief `value` UI units. */
        [[nodiscard]] static constexpr Length Px(float value) noexcept
        {
            return {value, LengthUnit::Px};
        }

        /** @brief `value` percent. */
        [[nodiscard]] static constexpr Length Percent(float value) noexcept
        {
            return {value, LengthUnit::Percent};
        }

        /** @brief `auto`, which is `none` for a maximum size. */
        [[nodiscard]] static constexpr Length Auto() noexcept
        {
            return {0.0f, LengthUnit::Auto};
        }

        friend constexpr bool operator==(const Length&, const Length&) noexcept = default;
    };

    /** @brief `display`: laid out as a flex container, or not at all (and not painted). */
    enum class Display : std::uint8_t
    {
        Flex,
        None,
    };

    /** @brief `position`: in the flow, or placed by its insets in its parent's padding box and out of the flow. */
    enum class Position : std::uint8_t
    {
        Relative,
        Absolute,
    };

    /** @brief `flex-direction`. */
    enum class FlexDirection : std::uint8_t
    {
        Column,
        ColumnReverse,
        Row,
        RowReverse,
    };

    /** @brief `flex-wrap`. */
    enum class FlexWrap : std::uint8_t
    {
        NoWrap,
        Wrap,
        WrapReverse,
    };

    /** @brief `align-items`, `align-self` (where `Auto` is the parent's `align-items`) and `align-content`. */
    enum class Align : std::uint8_t
    {
        Auto,
        FlexStart,
        Center,
        FlexEnd,
        Stretch,
        Baseline,
        SpaceBetween,
        SpaceAround,
        SpaceEvenly,
    };

    /** @brief `justify-content`. */
    enum class Justify : std::uint8_t
    {
        FlexStart,
        Center,
        FlexEnd,
        SpaceBetween,
        SpaceAround,
        SpaceEvenly,
    };

    /** @brief `overflow`: children drawn past the padding box, or clipped to it (with its inner radii). */
    enum class Overflow : std::uint8_t
    {
        Visible,
        Hidden,
    };

    /** @brief `pointer-events` (ADR-0065): an element takes no pointer input unless it is `All`. Inherited. */
    enum class PointerEvents : std::uint8_t
    {
        None,
        All,
    };

    /** @brief `cursor`: the shape the pointer takes over the element. Inherited. */
    enum class Cursor : std::uint8_t
    {
        Auto,
        Default,
        Pointer,
        Text,
        Move,
        NotAllowed,
        EwResize,
        NsResize,
        NwseResize,
        NeswResize,
        None,
    };

    /** @brief `background-size`: how a background image fits its element's border box. */
    enum class BackgroundSize : std::uint8_t
    {
        /** @brief Stretched over the box. */
        Stretch,
        /** @brief Scaled evenly to cover the box, centred, the overhang cut off. */
        Cover,
        /** @brief Scaled evenly to fit inside the box, centred. */
        Contain,
    };

    /**
     * @brief CSS's `linear-gradient(angle, stops)`: along the line through the box's centre at `degrees` (0 up, 90
     * right, 180 down, as CSS), from where the line meets the box's corner behind it to the one ahead.
     */
    struct StyleLinearGradient
    {
        float degrees = 180.0f;
        std::vector<GradientStop> stops;
    };

    /** @brief CSS's `radial-gradient(stops)`: an ellipse about the box's centre reaching its farthest corner. */
    struct StyleRadialGradient
    {
        std::vector<GradientStop> stops;
    };

    /** @brief `background-image`: none, an image asset (`url()`), or a gradient. */
    using BackgroundImage = std::variant<std::monostate, AssetHandle<Image>, StyleLinearGradient, StyleRadialGradient>;

    /** @brief One function of a `transform` list. */
    struct TransformStep
    {
        /** @brief Which function. */
        enum class Kind : std::uint8_t
        {
            Translate,
            Scale,
            Rotate,
        };

        Kind kind = Kind::Translate;
        /** @brief A translation's lengths; a percentage is of the element's border box. */
        Length x;
        Length y;
        /** @brief A scale's factors. */
        Math::Vec2 factors{1.0f};
        /** @brief A rotation, clockwise on screen. */
        float radians = 0.0f;

        /** @brief `translate(x, y)`. */
        [[nodiscard]] static TransformStep Translate(Length x, Length y)
        {
            TransformStep step;
            step.kind = Kind::Translate;
            step.x = x;
            step.y = y;
            return step;
        }

        /** @brief `scale(x, y)`. */
        [[nodiscard]] static TransformStep Scale(Math::Vec2 factors)
        {
            TransformStep step;
            step.kind = Kind::Scale;
            step.factors = factors;
            return step;
        }

        /** @brief `rotate(angle)`. */
        [[nodiscard]] static TransformStep Rotate(float radians)
        {
            TransformStep step;
            step.kind = Kind::Rotate;
            step.radians = radians;
            return step;
        }
    };

    /**
     * @brief CSS's easing function, which maps a transition's or a keyframe interval's progress (0 to 1) to how far
     * between its two values it shows: a cubic BÃ©zier from (0, 0) to (1, 1) through two control points, or a run of
     * steps. The named functions are BÃ©zier curves (`linear` is the straight one).
     */
    struct TimingFunction
    {
        /** @brief Which kind of curve. */
        enum class Kind : std::uint8_t
        {
            CubicBezier,
            Steps,
        };

        /** @brief Where a `steps()` function jumps: CSS's `jump-start`, `jump-end`, `jump-none` and `jump-both`. */
        enum class StepPosition : std::uint8_t
        {
            JumpStart,
            JumpEnd,
            JumpNone,
            JumpBoth,
        };

        Kind kind = Kind::CubicBezier;
        /** @brief A BÃ©zier's control points; their x lie in [0, 1]. `ease` by default. */
        float x1 = 0.25f;
        float y1 = 0.1f;
        float x2 = 0.25f;
        float y2 = 1.0f;
        /** @brief A `steps()` function's number of intervals, 1 or more, and where it jumps. */
        std::uint32_t steps = 1;
        StepPosition position = StepPosition::JumpEnd;

        /** @brief `cubic-bezier(x1, y1, x2, y2)`. */
        [[nodiscard]] static constexpr TimingFunction CubicBezier(float x1, float y1, float x2, float y2) noexcept
        {
            TimingFunction function;
            function.x1 = x1;
            function.y1 = y1;
            function.x2 = x2;
            function.y2 = y2;
            return function;
        }

        /** @brief `steps(count, position)`. */
        [[nodiscard]] static constexpr TimingFunction Steps(std::uint32_t count,
                                                            StepPosition position = StepPosition::JumpEnd) noexcept
        {
            TimingFunction function;
            function.kind = Kind::Steps;
            function.steps = count;
            function.position = position;
            return function;
        }

        /** @brief `linear`. */
        [[nodiscard]] static constexpr TimingFunction Linear() noexcept
        {
            return CubicBezier(0.0f, 0.0f, 1.0f, 1.0f);
        }

        /** @brief `ease`, CSS's initial timing function. */
        [[nodiscard]] static constexpr TimingFunction Ease() noexcept
        {
            return CubicBezier(0.25f, 0.1f, 0.25f, 1.0f);
        }

        /** @brief `ease-in`. */
        [[nodiscard]] static constexpr TimingFunction EaseIn() noexcept
        {
            return CubicBezier(0.42f, 0.0f, 1.0f, 1.0f);
        }

        /** @brief `ease-out`. */
        [[nodiscard]] static constexpr TimingFunction EaseOut() noexcept
        {
            return CubicBezier(0.0f, 0.0f, 0.58f, 1.0f);
        }

        /** @brief `ease-in-out`. */
        [[nodiscard]] static constexpr TimingFunction EaseInOut() noexcept
        {
            return CubicBezier(0.42f, 0.0f, 0.58f, 1.0f);
        }

        /** @brief The output at `progress` (0 to 1) as CSS's easing functions define it. */
        [[nodiscard]] float Apply(float progress) const noexcept;

        friend constexpr bool operator==(const TimingFunction&, const TimingFunction&) noexcept = default;
    };

    /** @brief `text-overflow`: what a line cut off at its element's width ends with. */
    enum class TextOverflow : std::uint8_t
    {
        /** @brief Nothing: the line runs on past the width (and is cut only by an `overflow: hidden` ancestor). */
        Clip,
        /** @brief An ellipsis, `…`, inside the width. */
        Ellipsis,
    };

    /** @brief `animation-direction`: which way each iteration plays. */
    enum class AnimationDirection : std::uint8_t
    {
        Normal,
        Reverse,
        /** @brief Forwards on even iterations (counting from 0), backwards on odd ones. */
        Alternate,
        AlternateReverse,
    };

    /** @brief `animation-fill-mode`: whether an animation's values hold before it starts and after it ends. */
    enum class AnimationFillMode : std::uint8_t
    {
        None,
        /** @brief The last iteration's end values hold once it ends. */
        Forwards,
        /** @brief The first iteration's start values hold through its delay. */
        Backwards,
        Both,
    };

    /**
     * @brief Every property of an element's style with a value: the resolved style, and the values a StyleDeclarations
     * holds. Defaults are Yoga's, which are those of React Native and Unity's USS rather than the web's:
     * `flex-direction` column, `flex-shrink` 0, `align-content` flex-start, and every element a flex container. Lengths
     * are UI units.
     */
    struct ResolvedStyle
    {
        Display display = Display::Flex;
        Position position = Position::Relative;
        Length top = Length::Auto();
        Length right = Length::Auto();
        Length bottom = Length::Auto();
        Length left = Length::Auto();
        Length width = Length::Auto();
        Length height = Length::Auto();
        Length minWidth = Length::Auto();
        Length minHeight = Length::Auto();
        Length maxWidth = Length::Auto();
        Length maxHeight = Length::Auto();
        /** @brief Width over height; zero or less is none. */
        float aspectRatio = 0.0f;
        Length marginTop;
        Length marginRight;
        Length marginBottom;
        Length marginLeft;
        Length paddingTop;
        Length paddingRight;
        Length paddingBottom;
        Length paddingLeft;
        float borderTopWidth = 0.0f;
        float borderRightWidth = 0.0f;
        float borderBottomWidth = 0.0f;
        float borderLeftWidth = 0.0f;
        FlexDirection flexDirection = FlexDirection::Column;
        FlexWrap flexWrap = FlexWrap::NoWrap;
        float flexGrow = 0.0f;
        float flexShrink = 0.0f;
        Length flexBasis = Length::Auto();
        Align alignItems = Align::Stretch;
        Align alignSelf = Align::Auto;
        Align alignContent = Align::FlexStart;
        Justify justifyContent = Justify::FlexStart;
        Length rowGap;
        Length columnGap;

        /** @brief Straight alpha; transparent by default. */
        Colour backgroundColor{0.0f, 0.0f, 0.0f, 0.0f};
        BackgroundImage backgroundImage;
        BackgroundSize backgroundSize = BackgroundSize::Stretch;
        /** @brief `border-image-source`: an image nine-sliced over the border box. */
        AssetHandle<Image> borderImageSource;
        /** @brief `border-image-slice`: the nine-slice insets, in the image's pixels. */
        Sides borderImageSlice;
        Colour borderTopColor = Colour::Black;
        Colour borderRightColor = Colour::Black;
        Colour borderBottomColor = Colour::Black;
        Colour borderLeftColor = Colour::Black;
        float borderTopLeftRadius = 0.0f;
        float borderTopRightRadius = 0.0f;
        float borderBottomRightRadius = 0.0f;
        float borderBottomLeftRadius = 0.0f;
        /** @brief `box-shadow`: outset shadows, the first drawn on top, as CSS. */
        std::vector<Shadow> boxShadow;
        float opacity = 1.0f;
        std::vector<TransformStep> transform;
        /** @brief `transform-origin`, from the border box's top-left; a percentage is of its size. */
        Length transformOriginX = Length::Percent(50.0f);
        Length transformOriginY = Length::Percent(50.0f);
        Overflow overflow = Overflow::Visible;
        /** @brief Siblings paint in increasing `z-index`, equal ones in tree order. */
        std::int32_t zIndex = 0;
        PointerEvents pointerEvents = PointerEvents::None;
        Cursor cursor = Cursor::Auto;

        /** @name Text
         * How an element's text (UIRoot::SetText) is laid out and drawn, through FenrirText (ADR-0062). Every one is
         * inherited. Sizes are UI units; the root lays the paragraph out in its target's pixels.
         * @{
         */
        /** @brief `color`: the text's colour, straight alpha. */
        Colour color = Colour::Black;
        /**
         * @brief `font-family`: a family registered with the app's FontLibrary; one with no faces is the embedded
         * default face, as is `default`. A list in a stylesheet takes its first name.
         */
        std::string fontFamily = "default";
        /** @brief `font-size`: the em square's height. */
        float fontSize = 16.0f;
        /** @brief `font-weight`, 1 to 1000; the family's nearest face is used. */
        std::uint16_t fontWeight = FontWeightNormal;
        /** @brief `font-style`. */
        FontStyle fontStyle = FontStyle::Normal;
        /** @brief `text-align`: where the lines sit across the content box. */
        TextAlign textAlign = TextAlign::Start;
        /**
         * @brief `line-height`: `normal` (auto) for the fonts' own height, a percentage (a plain number is its
         * hundredfold) of the element's font-size, or UI units.
         */
        Length lineHeight = Length::Auto();
        /** @brief `letter-spacing`: added after each glyph, and may be negative. */
        float letterSpacing = 0.0f;
        /**
         * @brief `text-shadow`: the text drawn again under itself, the first on top, as CSS. A shadow's blur is kept
         * and not drawn yet: shadows are sharp, offset and tinted.
         */
        std::vector<Shadow> textShadow;
        /** @brief `white-space`: Normal wraps at the width, NoWrap keeps one line; both collapse spaces. */
        WhiteSpace whiteSpace = WhiteSpace::Normal;
        /** @brief `text-overflow`. */
        TextOverflow textOverflow = TextOverflow::Clip;
        /** @} */

        /** @name Transitions and animations
         * Comma-separated lists, as CSS's: the i-th transition (animation) takes the i-th entry of each other list,
         * which repeats when it is shorter. An empty list is the property's initial value: `all`, `0s`, `ease`, `0s`;
         * no animation, `0s`, `ease`, `0s`, `1`, `normal`, `none`.
         * @{
         */
        /** @brief Property names in lower case (a shorthand names its longhands), `all`, or `none` alone. */
        std::vector<std::string> transitionProperty;
        /** @brief Seconds, zero or more. */
        std::vector<float> transitionDuration;
        std::vector<TimingFunction> transitionTimingFunction;
        /** @brief Seconds; a negative delay starts the transition part way through. */
        std::vector<float> transitionDelay;
        /** @brief `@keyframes` names, as written; `none` names no animation but keeps its place in the lists. */
        std::vector<std::string> animationName;
        /** @brief Seconds per iteration, zero or more. */
        std::vector<float> animationDuration;
        std::vector<TimingFunction> animationTimingFunction;
        /** @brief Seconds; a negative delay starts the animation part way through. */
        std::vector<float> animationDelay;
        /** @brief Iterations, zero or more, fractions allowed; infinity for `infinite`. */
        std::vector<float> animationIterationCount;
        std::vector<AnimationDirection> animationDirection;
        std::vector<AnimationFillMode> animationFillMode;
        /** @} */
    };

    /** @brief The properties of the style, each a ResolvedStyle field, named as CSS's longhands are. */
    enum class StyleProperty : std::uint8_t
    {
        Display,
        Position,
        Top,
        Right,
        Bottom,
        Left,
        Width,
        Height,
        MinWidth,
        MinHeight,
        MaxWidth,
        MaxHeight,
        AspectRatio,
        MarginTop,
        MarginRight,
        MarginBottom,
        MarginLeft,
        PaddingTop,
        PaddingRight,
        PaddingBottom,
        PaddingLeft,
        BorderTopWidth,
        BorderRightWidth,
        BorderBottomWidth,
        BorderLeftWidth,
        FlexDirection,
        FlexWrap,
        FlexGrow,
        FlexShrink,
        FlexBasis,
        AlignItems,
        AlignSelf,
        AlignContent,
        JustifyContent,
        RowGap,
        ColumnGap,
        BackgroundColor,
        BackgroundImage,
        BackgroundSize,
        BorderImageSource,
        BorderImageSlice,
        BorderTopColor,
        BorderRightColor,
        BorderBottomColor,
        BorderLeftColor,
        BorderTopLeftRadius,
        BorderTopRightRadius,
        BorderBottomRightRadius,
        BorderBottomLeftRadius,
        BoxShadow,
        Opacity,
        Transform,
        TransformOriginX,
        TransformOriginY,
        Overflow,
        ZIndex,
        PointerEvents,
        Cursor,
        Color,
        FontFamily,
        FontSize,
        FontWeight,
        FontStyle,
        TextAlign,
        LineHeight,
        LetterSpacing,
        TextShadow,
        WhiteSpace,
        TextOverflow,
        // The transition and animation lists come last: transitions and animations move every property before them.
        TransitionProperty,
        TransitionDuration,
        TransitionTimingFunction,
        TransitionDelay,
        AnimationName,
        AnimationDuration,
        AnimationTimingFunction,
        AnimationDelay,
        AnimationIterationCount,
        AnimationDirection,
        AnimationFillMode,
        /** @brief How many there are; not a property. */
        Count,
    };

    /** @brief How many StyleProperty values there are. */
    inline constexpr std::size_t StylePropertyCount = static_cast<std::size_t>(StyleProperty::Count);

    /**
     * @brief A set of property values, some properties given and the rest not: an element's inline style. Resolving an
     * element's style starts from the defaults, takes the inherited properties (pointer-events, cursor, the text
     * properties and the custom properties) from its parent's resolved style, writes the matching stylesheet rules over
     * them, then every property given here. Each typed setter gives one property; UIRoot::SetStyleProperty gives one by
     * its CSS name and a value string.
     */
    class StyleDeclarations
    {
      public:
        /** @brief Whether `property` is given. */
        [[nodiscard]] bool IsSet(StyleProperty property) const noexcept;
        /** @brief Gives `property` back to the defaults and inheritance. */
        StyleDeclarations& Unset(StyleProperty property) noexcept;
        /** @brief Gives every property back. */
        void Clear() noexcept;
        /** @brief The values: those of the properties given, defaults for the rest. */
        [[nodiscard]] const ResolvedStyle& Values() const noexcept
        {
            return m_values;
        }
        /** @brief Writes every property given over `style`. */
        void ApplyTo(ResolvedStyle& style) const;
        /** @brief Gives `property` the value `from` holds for it. */
        StyleDeclarations& CopyFrom(StyleProperty property, const ResolvedStyle& from);

        /** @name Typed setters
         * Each gives the one property its name says and returns this, to chain.
         * @{
         */
        /** @brief `display`. */
        StyleDeclarations& SetDisplay(Fenrir::Display value);
        /** @brief `position`. */
        StyleDeclarations& SetPosition(Fenrir::Position value);
        /** @brief `top`. */
        StyleDeclarations& SetTop(Length value);
        /** @brief `right`. */
        StyleDeclarations& SetRight(Length value);
        /** @brief `bottom`. */
        StyleDeclarations& SetBottom(Length value);
        /** @brief `left`. */
        StyleDeclarations& SetLeft(Length value);
        /** @brief `width`. */
        StyleDeclarations& SetWidth(Length value);
        /** @brief `height`. */
        StyleDeclarations& SetHeight(Length value);
        /** @brief `min-width`. */
        StyleDeclarations& SetMinWidth(Length value);
        /** @brief `min-height`. */
        StyleDeclarations& SetMinHeight(Length value);
        /** @brief `max-width`. */
        StyleDeclarations& SetMaxWidth(Length value);
        /** @brief `max-height`. */
        StyleDeclarations& SetMaxHeight(Length value);
        /** @brief `aspect-ratio`, width over height. */
        StyleDeclarations& SetAspectRatio(float value);
        /** @brief `margin-top`, `margin-right`, `margin-bottom` and `margin-left`. */
        StyleDeclarations& SetMargin(Length top, Length right, Length bottom, Length left);
        /** @brief `padding-top`, `padding-right`, `padding-bottom` and `padding-left`. */
        StyleDeclarations& SetPadding(Length top, Length right, Length bottom, Length left);
        /** @brief The four `border-*-width`s. */
        StyleDeclarations& SetBorderWidths(const Sides& widths);
        /** @brief `flex-direction`. */
        StyleDeclarations& SetFlexDirection(Fenrir::FlexDirection value);
        /** @brief `flex-wrap`. */
        StyleDeclarations& SetFlexWrap(Fenrir::FlexWrap value);
        /** @brief `flex-grow`. */
        StyleDeclarations& SetFlexGrow(float value);
        /** @brief `flex-shrink`. */
        StyleDeclarations& SetFlexShrink(float value);
        /** @brief `flex-basis`. */
        StyleDeclarations& SetFlexBasis(Length value);
        /** @brief `align-items`. */
        StyleDeclarations& SetAlignItems(Align value);
        /** @brief `align-self`. */
        StyleDeclarations& SetAlignSelf(Align value);
        /** @brief `align-content`. */
        StyleDeclarations& SetAlignContent(Align value);
        /** @brief `justify-content`. */
        StyleDeclarations& SetJustifyContent(Justify value);
        /** @brief `row-gap`. */
        StyleDeclarations& SetRowGap(Length value);
        /** @brief `column-gap`. */
        StyleDeclarations& SetColumnGap(Length value);
        /** @brief `background-color`. */
        StyleDeclarations& SetBackgroundColor(const Colour& value);
        /** @brief `background-image`. */
        StyleDeclarations& SetBackgroundImage(const Fenrir::BackgroundImage& value);
        /** @brief `background-size`. */
        StyleDeclarations& SetBackgroundSize(Fenrir::BackgroundSize value);
        /** @brief `border-image-source` and `border-image-slice`: `image` nine-sliced at `slice`. */
        StyleDeclarations& SetBorderImage(AssetHandle<Image> image, const Sides& slice);
        /** @brief The four `border-*-color`s, one colour. */
        StyleDeclarations& SetBorderColor(const Colour& value);
        /** @brief The four `border-*-radius`es. */
        StyleDeclarations& SetBorderRadius(const CornerRadii& radii);
        /** @brief `box-shadow`. */
        StyleDeclarations& SetBoxShadow(std::vector<Shadow> shadows);
        /** @brief `opacity`, 0 to 1. */
        StyleDeclarations& SetOpacity(float value);
        /** @brief `transform`. */
        StyleDeclarations& SetTransform(std::vector<TransformStep> steps);
        /** @brief `transform-origin`. */
        StyleDeclarations& SetTransformOrigin(Length x, Length y);
        /** @brief `overflow`. */
        StyleDeclarations& SetOverflow(Fenrir::Overflow value);
        /** @brief `z-index`. */
        StyleDeclarations& SetZIndex(std::int32_t value);
        /** @brief `pointer-events`. */
        StyleDeclarations& SetPointerEvents(Fenrir::PointerEvents value);
        /** @brief `cursor`. */
        StyleDeclarations& SetCursor(Fenrir::Cursor value);
        /** @brief `color`. */
        StyleDeclarations& SetColor(const Colour& value);
        /** @brief `font-family`: one family's name. */
        StyleDeclarations& SetFontFamily(std::string value);
        /** @brief `font-size`. */
        StyleDeclarations& SetFontSize(float value);
        /** @brief `font-weight`, 1 to 1000. */
        StyleDeclarations& SetFontWeight(std::uint16_t value);
        /** @brief `font-style`. */
        StyleDeclarations& SetFontStyle(Fenrir::FontStyle value);
        /** @brief `text-align`. */
        StyleDeclarations& SetTextAlign(Fenrir::TextAlign value);
        /** @brief `line-height`: auto for `normal`, a percentage of the font-size, or UI units. */
        StyleDeclarations& SetLineHeight(Length value);
        /** @brief `letter-spacing`. */
        StyleDeclarations& SetLetterSpacing(float value);
        /** @brief `text-shadow`. */
        StyleDeclarations& SetTextShadow(std::vector<Shadow> shadows);
        /** @brief `white-space`: Normal or NoWrap. */
        StyleDeclarations& SetWhiteSpace(Fenrir::WhiteSpace value);
        /** @brief `text-overflow`. */
        StyleDeclarations& SetTextOverflow(Fenrir::TextOverflow value);
        /** @brief `transition-property`: lower-case property names, `all`, or `none` alone. */
        StyleDeclarations& SetTransitionProperty(std::vector<std::string> names);
        /** @brief `transition-duration`, in seconds. */
        StyleDeclarations& SetTransitionDuration(std::vector<float> seconds);
        /** @brief `transition-timing-function`. */
        StyleDeclarations& SetTransitionTimingFunction(std::vector<TimingFunction> functions);
        /** @brief `transition-delay`, in seconds. */
        StyleDeclarations& SetTransitionDelay(std::vector<float> seconds);
        /** @brief `animation-name`: `@keyframes` names. */
        StyleDeclarations& SetAnimationName(std::vector<std::string> names);
        /** @brief `animation-duration`, in seconds per iteration. */
        StyleDeclarations& SetAnimationDuration(std::vector<float> seconds);
        /** @brief `animation-timing-function`. */
        StyleDeclarations& SetAnimationTimingFunction(std::vector<TimingFunction> functions);
        /** @brief `animation-delay`, in seconds. */
        StyleDeclarations& SetAnimationDelay(std::vector<float> seconds);
        /** @brief `animation-iteration-count`; infinity for `infinite`. */
        StyleDeclarations& SetAnimationIterationCount(std::vector<float> counts);
        /** @brief `text-overflow`: what a line cut off at its element's width ends with. */
        enum class TextOverflow : std::uint8_t
        {
            /** @brief Nothing: the line runs on past the width (and is cut only by an `overflow: hidden` ancestor). */
            Clip,
            /** @brief An ellipsis, `…`, inside the width. */
            Ellipsis,
        };

        /** @brief `animation-direction`. */
        StyleDeclarations& SetAnimationDirection(std::vector<Fenrir::AnimationDirection> directions);
        /** @brief `animation-fill-mode`. */
        StyleDeclarations& SetAnimationFillMode(std::vector<Fenrir::AnimationFillMode> modes);
        /** @} */

        /** @brief Gives `property` through `field`, its field of ResolvedStyle, the value `value`. */
        template <typename T>
        StyleDeclarations& Set(StyleProperty property, T ResolvedStyle::* field, T value)
        {
            m_values.*field = std::move(value);
            m_set.set(static_cast<std::size_t>(property));
            return *this;
        }

      private:
        ResolvedStyle m_values;
        std::bitset<StylePropertyCount> m_set;
    };
} // namespace Fenrir

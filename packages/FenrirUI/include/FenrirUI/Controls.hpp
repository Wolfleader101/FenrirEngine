#pragma once

#include <cstdint>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirUI/Element.hpp"
#include "FenrirUI/ElementId.hpp"

// FenrirUI's controls (spec: "FenrirUI" controls): built-in elements with behaviour of their own, each registered under
// its tag name by UIRoots and styled by the default stylesheet (DefaultStylesheet), which a game's sheets override. A
// control's value is read and set through its element: UIRoot::GetElement and a cast to its class.
namespace Fenrir
{
    struct Image;

    /**
     * @brief What the interactive controls share: they make themselves focusable, and `disabled` (with `:disabled`)
     * takes their input and focus away.
     */
    class Control : public Element
    {
      public:
        /**
         * @brief Disables or enables it: a disabled control carries `:disabled`, cannot take the focus (it loses it
         * if it has it), and its root sends it no press, release, click or scroll.
         */
        void SetDisabled(bool disabled);
        /** @brief Whether it is disabled. */
        [[nodiscard]] bool IsDisabled() const noexcept
        {
            return m_disabled;
        }

      protected:
        void OnCreate() override;

      private:
        bool m_disabled = false;
    };

    /**
     * @brief `button`: its text is the element's (UIRoot::SetText), measured as a label's. A press and release inside
     * it is a Click, which the root sends; Enter and Space while it has the focus send a Click too.
     */
    class Button : public Control
    {
      public:
        RouteResult OnInput(App& app, const RoutedInput& input) override;
    };

    /**
     * @brief `checkbox`: a box that is checked or not, with a `checkbox-mark` child the default stylesheet shows while
     * checked. A click and Space while focused toggle it, which gives or takes `:checked` and sends Change (value 1
     * checked, 0 not).
     */
    class Checkbox : public Control
    {
      public:
        /** @brief Checks or unchecks it, with `:checked`; sends no Change. */
        void SetChecked(bool checked);
        /** @brief Whether it is checked. */
        [[nodiscard]] bool IsChecked() const noexcept
        {
            return m_checked;
        }
        /** @brief Its mark, the child the default stylesheet shows while it is checked. */
        [[nodiscard]] ElementId GetMark() const noexcept
        {
            return m_mark;
        }

        RouteResult OnInput(App& app, const RoutedInput& input) override;

      protected:
        void OnCreate() override;

      private:
        /** @brief Toggles it and sends Change. */
        void Toggle();

        bool m_checked = false;
        ElementId m_mark;
    };

    /** @brief Which way a slider runs: left to right, or bottom to top. */
    enum class Orientation : std::uint8_t
    {
        Horizontal,
        Vertical,
    };

    /**
     * @brief `slider`: a value from min to max in steps, shown by a `slider-fill` child up to the value and a
     * `slider-thumb` child at it, both placed by their inline style. A press on it sets the value where it lands and
     * drags it; Left and Down step it down and Right and Up step it up while focused; the wheel over it steps it while
     * focused, and passes on otherwise. It
     * sends Change (value the new value) at each change while dragged, from the keys and the wheel, and once more at
     * the drag's release with `integer` 1. Vertical, the class `vertical` is set on it and min is at the bottom.
     */
    class Slider : public Control
    {
      public:
        /** @brief Sets min and max (swapped when max is below min) and keeps the value within them. */
        void SetRange(float min, float max);
        /** @brief Sets the step the value snaps to from min; 0 for none (the keys and wheel then move a hundredth). */
        void SetStep(float step);
        /** @brief Sets the value, clamped to the range and snapped to the step; sends no Change. */
        void SetValue(float value);
        /** @brief Sets which way it runs. */
        void SetOrientation(Orientation orientation);

        /** @brief Its value, within its range and on its step. */
        [[nodiscard]] float GetValue() const noexcept
        {
            return m_value;
        }
        /** @brief Its range's low end. */
        [[nodiscard]] float GetMin() const noexcept
        {
            return m_min;
        }
        /** @brief Its range's high end. */
        [[nodiscard]] float GetMax() const noexcept
        {
            return m_max;
        }
        /** @brief Its step; 0 for none. */
        [[nodiscard]] float GetStep() const noexcept
        {
            return m_step;
        }
        /** @brief Which way it runs. */
        [[nodiscard]] Orientation GetOrientation() const noexcept
        {
            return m_orientation;
        }
        /** @brief Its fill child, from min to the value. */
        [[nodiscard]] ElementId GetFill() const noexcept
        {
            return m_fill;
        }
        /** @brief Its thumb child, at the value. */
        [[nodiscard]] ElementId GetThumb() const noexcept
        {
            return m_thumb;
        }

        RouteResult OnInput(App& app, const RoutedInput& input) override;

      protected:
        void OnCreate() override;

      private:
        /** @brief `value` clamped and snapped. */
        [[nodiscard]] float Fit(float value) const;
        /** @brief Sets the value to `value`, fitted, and sends Change when it moved or `final`. */
        void Move(float value, bool final);
        /** @brief The value at the pointer, from where it is over the padding box. */
        [[nodiscard]] float ValueAtPointer() const;
        /** @brief Places the fill and thumb at the value. */
        void Place();

        float m_min = 0.0f;
        float m_max = 1.0f;
        float m_step = 0.0f;
        float m_value = 0.0f;
        Orientation m_orientation = Orientation::Horizontal;
        bool m_dragging = false;
        ElementId m_fill;
        ElementId m_thumb;
    };

    /** @brief `progress`: a value from min to max shown by a `progress-fill` child sized to its fraction of the bar. */
    class ProgressBar : public Element
    {
      public:
        /** @brief Sets min and max (swapped when max is below min) and keeps the value within them. */
        void SetRange(float min, float max);
        /** @brief Sets the value, clamped to the range. */
        void SetValue(float value);

        /** @brief Its value, within its range. */
        [[nodiscard]] float GetValue() const noexcept
        {
            return m_value;
        }
        /** @brief How far the value is from min to max, 0 to 1 (0 for an empty range). */
        [[nodiscard]] float GetFraction() const noexcept;
        /** @brief Its fill child, whose inline width is the fraction. */
        [[nodiscard]] ElementId GetFill() const noexcept
        {
            return m_fill;
        }

      protected:
        void OnCreate() override;

      private:
        void Place();

        float m_min = 0.0f;
        float m_max = 1.0f;
        float m_value = 0.0f;
        ElementId m_fill;
    };

    /** @brief How an image control draws its image in its content box. */
    enum class ImageFit : std::uint8_t
    {
        /** @brief Stretched over the box. */
        Stretch,
        /** @brief Scaled evenly to fit inside the box, centred. */
        Contain,
        /** @brief Scaled evenly to cover the box, centred, the overhang cut off. */
        Cover,
    };

    /**
     * @brief `image`: draws an image asset in its content box by its fit, tinted by nothing. With no width or height
     * given, layout measures it at the image's size, one UI unit per pixel; with one side bounded, the other follows
     * the image's aspect ratio. Nothing is drawn or measured while the source is empty or not loaded.
     */
    class ImageView : public Element
    {
      public:
        /** @brief Sets the image, measuring it again. */
        void SetSource(AssetHandle<Image> image);
        /** @brief Sets the fit. */
        void SetFit(ImageFit fit);

        /** @brief Its image; empty for none. */
        [[nodiscard]] AssetHandle<Image> GetSource() const noexcept
        {
            return m_source;
        }
        /** @brief How it draws its image. */
        [[nodiscard]] ImageFit GetFit() const noexcept
        {
            return m_fit;
        }

        [[nodiscard]] bool HasMeasure() const override
        {
            return true;
        }
        [[nodiscard]] Math::Vec2 Measure(const MeasureConstraints& constraints) override;
        void Paint(Painter& painter, const ElementPaint& paint) override;

      private:
        /** @brief The image's size in pixels; zero when there is none to draw. */
        [[nodiscard]] Math::Vec2 ImageSize() const;

        AssetHandle<Image> m_source;
        ImageFit m_fit = ImageFit::Stretch;
    };

    /**
     * @brief `scroll`: a scroll view, `overflow: scroll` in the default stylesheet, whose children scroll vertically
     * and horizontally by its scroll offset (UIRoot::SetScrollOffset), with scrollbars drawn from its `::scrollbar` and
     * `::scrollbar-thumb` pseudo-elements. The wheel over it or anything in it scrolls it (`ScrollStep` units a notch);
     * a press on a thumb drags it, and one on a track jumps a page towards the pointer; a press on the content that
     * moves past `DragThreshold` drags the content, taking the press from what it landed on so that sends no click;
     * while it, or something in it that does not take the key, has the focus, the arrows scroll by a step, Page Up and
     * Page Down by a page, and Home and End to the ends.
     */
    class ScrollView : public Control
    {
      public:
        /** @brief UI units a wheel notch or an arrow key scrolls. */
        static constexpr float ScrollStep = 40.0f;
        /** @brief UI units a press on the content moves before it drags the content. */
        static constexpr float DragThreshold = 8.0f;

        /** @brief Its scroll offset: how far its content is scrolled right and down. */
        [[nodiscard]] Math::Vec2 GetOffset() const noexcept;
        /** @brief Scrolls to `offset`, clamped to its range. */
        void SetOffset(Math::Vec2 offset);

        RouteResult OnInput(App& app, const RoutedInput& input) override;

      private:
        /** @brief What a held press is doing. */
        enum class Drag : std::uint8_t
        {
            None,
            /** @brief Pressed on the content, not yet past the threshold. */
            Pending,
            Content,
            VerticalThumb,
            HorizontalThumb,
        };

        RouteResult Press(const MouseButtonEvent& event);
        void Motion();
        RouteResult KeyPressed(const KeyEvent& event);

        Drag m_drag = Drag::None;
        MouseButton m_button = MouseButton::Left;
        Math::Vec2 m_pressPointer{0.0f, 0.0f};
        Math::Vec2 m_pressOffset{0.0f, 0.0f};
    };
} // namespace Fenrir

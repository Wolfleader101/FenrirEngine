#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirText/TextSegments.hpp"
#include "FenrirUI/Element.hpp"
#include "FenrirUI/ElementId.hpp"

// FenrirUI's controls (spec: "FenrirUI" controls): built-in elements with behaviour of their own, each registered under
// its tag name by UIRoots and styled by the default stylesheet (DefaultStylesheet), which a game's sheets override. A
// control's value is read and set through its element: UIRoot::GetElement and a cast to its class.
namespace Fenrir
{
    struct Image;
    class Paragraph;

    namespace Detail
    {
        class ElementText;
    } // namespace Detail

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
     * checked. A click, and Space or Enter while focused, toggle it, which gives or takes `:checked` and sends Change
     * (value 1 checked, 0 not).
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
     * drags it; Left and Down step it down and Right and Up step it up while focused, and so do the D-pad's directions
     * along its axis (the root's gamepad navigation, UIRoot), passing at the end they step to; the wheel over it steps
     * it while focused, and passes on otherwise. It sends Change (value the new value) at each change while dragged,
     * from the keys and the wheel, and once more at the drag's release with `integer` 1. Vertical, the class `vertical`
     * is set on it and min is at the bottom.
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
     * Page Down by a page, and Home and End to the ends. Focused itself, the D-pad's directions (the root's gamepad
     * navigation, UIRoot) scroll it by a step while it can still scroll that way, and pass at the end of its range.
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
        RouteResult DirectionPressed(const GamepadButtonEvent& event);

        Drag m_drag = Drag::None;
        MouseButton m_button = MouseButton::Left;
        Math::Vec2 m_pressPointer{0.0f, 0.0f};
        Math::Vec2 m_pressOffset{0.0f, 0.0f};
    };

    /**
     * @brief `textentry`: a single line of UTF-8 text a player types, selects, cuts, copies and pastes in (spec:
     * "FenrirUI" controls). It holds a value, and a caret and an anchor as byte offsets into it on grapheme boundaries
     * (UAX#29); the selection is the text between them. While it has the focus:
     *
     * - text typed (FenrirInput's TextEvent) replaces the selection, or goes in at the caret, a codepoint at a time;
     * - Left and Right move the caret a grapheme, or with Ctrl to the start of the word before or the end of the word
     *   after (FenrirText's Words); with a selection and no Shift they go to its start or end; Home and End go to the
     *   ends; Shift with any of them moves only the caret, extending the selection;
     * - Backspace and Delete take the selection, or the grapheme (with Ctrl, the word; a password has none, so all)
     *   before or after the caret;
     * - Ctrl+A selects everything; Ctrl+C, Ctrl+X and Ctrl+V copy, cut and paste through FenrirWindow's clipboard (a
     *   password copies and cuts nothing);
     * - Enter sends Submit, and Escape takes the focus away.
     *
     * A press on it focuses it and puts the caret at the character under the pointer, a drag selects from there, and a
     * double click selects the word under it. Each edit sends Change; the value is read with GetValue. While focused,
     * the root claims text input with the caret's rectangle (GetTextCaret), so the window takes text and places the
     * operating system's input method at it.
     *
     * It paints its own text, in its text properties, on one line through its content box, scrolled across to keep
     * the caret in view; behind the selection its `::selection` style's background colour, over it that style's
     * colour; the caret, while it has the focus and no selection, a `CaretWidth` bar a line tall in its colour, shown
     * and hidden every `BlinkSeconds` of FenrirTime from the last edit or move; while the value is empty, its
     * placeholder in its `::placeholder` style's text properties. Layout measures its content a line tall.
     */
    class TextEntry : public Control
    {
      public:
        /** @brief The caret's width, in UI units. */
        static constexpr float CaretWidth = 1.0f;
        /** @brief How long the caret is shown, and then hidden, in each blink, in seconds. */
        static constexpr double BlinkSeconds = 0.5;
        /** @brief What a password shows for each of its graphemes: U+2022, a bullet, in UTF-8. */
        static constexpr std::string_view Bullet = "\xE2\x80\xA2";

        TextEntry();
        TextEntry(const TextEntry&) = delete;
        TextEntry& operator=(const TextEntry&) = delete;
        TextEntry(TextEntry&&) = delete;
        TextEntry& operator=(TextEntry&&) = delete;
        ~TextEntry() override;

        /**
         * @brief Makes `utf8` the value, with the caret and anchor at its end; sends no Change. Tabs and line breaks
         * become spaces and other control characters are dropped, as from a paste; the maximum length is not applied.
         */
        void SetValue(std::string_view utf8);
        /** @brief Its value, UTF-8. */
        [[nodiscard]] std::string_view GetValue() const noexcept
        {
            return m_value;
        }
        /** @brief Sets what it shows while its value is empty. */
        void SetPlaceholder(std::string_view utf8);
        /** @brief What it shows while its value is empty. */
        [[nodiscard]] std::string_view GetPlaceholder() const noexcept
        {
            return m_placeholder;
        }
        /** @brief Shows a bullet for each grapheme of the value instead of the value, and has no words. */
        void SetPassword(bool password);
        /** @brief Whether it shows bullets. */
        [[nodiscard]] bool IsPassword() const noexcept
        {
            return m_password;
        }
        /**
         * @brief Sets the most graphemes typing and pasting may make the value; 0 for no limit. What is typed past it
         * is dropped, whole graphemes at a time; a value already longer keeps its length, and an edit may shorten it
         * but not lengthen it.
         */
        void SetMaxLength(std::size_t graphemes);
        /** @brief The most graphemes typing and pasting may make the value; 0 for no limit. */
        [[nodiscard]] std::size_t GetMaxLength() const noexcept
        {
            return m_maxLength;
        }

        /** @brief Sets the anchor and the caret, each moved back to a grapheme boundary within the value. */
        void Select(std::uint32_t anchor, std::uint32_t caret);
        /** @brief Selects the whole value, the caret at its end. */
        void SelectAll();
        /** @brief The caret, a byte offset into the value on a grapheme boundary. */
        [[nodiscard]] std::uint32_t GetCaret() const noexcept
        {
            return m_caret;
        }
        /** @brief The selection's other end; the caret's offset when nothing is selected. */
        [[nodiscard]] std::uint32_t GetAnchor() const noexcept
        {
            return m_anchor;
        }
        /** @brief How far its text is scrolled left, in UI units, as of its last paint. */
        [[nodiscard]] float GetScroll() const noexcept
        {
            return m_scroll;
        }
        /**
         * @brief The paragraph it shows, the value or its bullets, laid out in the target's pixels as of its last
         * paint (a paragraph's units, as UIRoot::GetParagraph's are).
         */
        [[nodiscard]] const Paragraph& GetParagraph() const noexcept;

        RouteResult OnInput(App& app, const RoutedInput& input) override;
        [[nodiscard]] bool HasMeasure() const override
        {
            return true;
        }
        [[nodiscard]] Math::Vec2 Measure(const MeasureConstraints& constraints) override;
        void Paint(Painter& painter, const ElementPaint& paint) override;
        /** @brief Its caret as of its last paint, a line tall; before its first, its content box's left edge. */
        [[nodiscard]] std::optional<Rect2D> GetTextCaret() const override;

      protected:
        void OnCreate() override;

      private:
        [[nodiscard]] bool HasSelection() const noexcept
        {
            return m_caret != m_anchor;
        }
        /** @brief The largest grapheme boundary of the value at or before `index`. */
        [[nodiscard]] std::uint32_t Snap(std::uint32_t index) const;
        [[nodiscard]] std::uint32_t NextGrapheme(std::uint32_t index) const;
        [[nodiscard]] std::uint32_t PreviousGrapheme(std::uint32_t index) const;
        [[nodiscard]] std::uint32_t PreviousWordStart(std::uint32_t index) const;
        [[nodiscard]] std::uint32_t NextWordEnd(std::uint32_t index) const;
        /** @brief The word at `index`, or the run between words there. */
        [[nodiscard]] TextRange WordAt(std::uint32_t index) const;
        /** @brief Where value offset `index` is in the shown text. */
        [[nodiscard]] std::uint32_t ShownIndex(std::uint32_t index) const;
        /** @brief The value offset at shown offset `index`. */
        [[nodiscard]] std::uint32_t ValueIndex(std::uint32_t index) const;
        /** @brief The value offset under `point`, UI units from the root's top-left, as of the last paint. */
        [[nodiscard]] std::uint32_t IndexAt(Math::Vec2 point);

        /** @brief Moves the caret to `index`, and the anchor with it unless `extend`. */
        void MoveCaret(std::uint32_t index, bool extend);
        /** @brief Puts `text` over [begin, end) within the maximum length, the caret after it; Change if it changed. */
        void Replace(std::uint32_t begin, std::uint32_t end, std::string_view text);
        /** @brief Replaces the selection, or inserts at the caret. */
        void Insert(std::string_view text);
        /** @brief The value changed: its boundaries, words and shown text follow. */
        void ValueChanged();
        RouteResult KeyPressed(App& app, const KeyEvent& key);

        /** @brief Target pixels per UI unit: the root's scale times the target's content scale. */
        [[nodiscard]] float PixelsPerUnit() const;
        /** @brief Gives its paragraphs `style` and its placeholder's; true when a line's height may have changed. */
        bool ApplyStyle(const ResolvedStyle& style);
        /** @brief Lays its paragraphs out with the app's fonts; false without them. */
        bool LayOut();

        std::string m_value;
        std::string m_placeholder;
        bool m_password = false;
        std::size_t m_maxLength = 0;
        std::vector<std::uint32_t> m_graphemes{0};
        std::vector<TextRange> m_words;
        std::uint32_t m_caret = 0;
        std::uint32_t m_anchor = 0;
        bool m_dragging = false;

        /** @brief What it shows (the value or its bullets), its placeholder, and a space it measures a line by. */
        std::unique_ptr<Detail::ElementText> m_shown;
        std::unique_ptr<Detail::ElementText> m_placeholderText;
        std::unique_ptr<Detail::ElementText> m_line;

        float m_scroll = 0.0f;
        /** @brief Seconds of FenrirTime since the last edit or move, for the blink. */
        double m_blink = 0.0;
        /** @brief Where its text's top-left and its caret were last painted, in UI units; whether they have been. */
        Math::Vec2 m_origin{0.0f, 0.0f};
        Rect2D m_caretRect;
        bool m_painted = false;
    };
} // namespace Fenrir

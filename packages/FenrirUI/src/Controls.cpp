#include "FenrirUI/Controls.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "AttributeValues.hpp"
#include "BoxPainting.hpp"
#include "ElementText.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirPainter/Painter.hpp"
#include "FenrirText/FontLibrary.hpp"
#include "FenrirText/Paragraph.hpp"
#include "FenrirUI/UI.hpp"
#include "FenrirUI/UIRoot.hpp"
#include "FenrirWindow/Window.hpp"

namespace Fenrir
{
    namespace
    {
        /** @brief A UIEvent of `kind` at `element`, with the pointer where its root last saw it. */
        UIEvent MakeEvent(const Element& element, UIEventKind kind)
        {
            UIEvent event;
            event.target = element.GetId();
            event.kind = kind;
            event.position = element.GetRoot().GetPointer().value_or(Math::Vec2(0.0f));
            return event;
        }

        /** @brief Sends Click at `element`, as a press and release would. */
        void SendClick(const Element& element)
        {
            UIEvent click = MakeEvent(element, UIEventKind::Click);
            click.integer = 1;
            element.GetRoot().SendEvent(click);
        }

        /** @brief The key press `input` is, when it is one and not a release. */
        const KeyEvent* KeyPress(const RoutedInput& input)
        {
            const auto* key = std::get_if<KeyEvent>(&input);
            return key != nullptr && key->pressed ? key : nullptr;
        }

        /** @brief The `text` attribute, the element's text (UIRoot::SetText); Unknown for any other name. */
        AttributeResult SetTextAttribute(const Element& element, std::string_view name, std::string_view value)
        {
            if (name != "text")
            {
                return AttributeResult::Unknown;
            }
            element.GetRoot().SetText(element.GetId(), value);
            return AttributeResult::Set;
        }

        /**
         * @brief Gives a range control the `min`, `max` or `value` attribute `number`, then fits the value written to
         * the range it has now.
         */
        template <typename TControl>
        void SetRangeAttribute(TControl& control, Detail::AuthoredRange& authored, std::string_view name, float number)
        {
            if (name == "value")
            {
                authored.value = number;
            }
            else
            {
                (name == "min" ? authored.min : authored.max) = number;
                control.SetRange(authored.min.value_or(control.GetMin()), authored.max.value_or(control.GetMax()));
            }
            if (authored.value.has_value())
            {
                control.SetValue(*authored.value);
            }
        }

        bool Inside(const Rect2D& rect, Math::Vec2 point)
        {
            return point.x >= rect.x && point.y >= rect.y && point.x < rect.x + rect.width &&
                   point.y < rect.y + rect.height;
        }
    } // namespace

    AttributeResult Label::SetAttribute(std::string_view name, std::string_view value)
    {
        return SetTextAttribute(*this, name, value);
    }

    AttributeResult Control::SetAttribute(std::string_view name, std::string_view value)
    {
        if (name != "disabled")
        {
            return AttributeResult::Unknown;
        }
        const std::optional<bool> disabled = Detail::ParseBoolAttribute(value);
        if (!disabled.has_value())
        {
            return AttributeResult::Invalid;
        }
        SetDisabled(*disabled);
        return AttributeResult::Set;
    }

    void Control::SetDisabled(bool disabled)
    {
        m_disabled = disabled;
        UIRoot& root = GetRoot();
        root.SetPseudoClass(GetId(), PseudoClass::Disabled, disabled);
        root.SetFocusable(GetId(), !disabled);
    }

    void Control::OnCreate()
    {
        GetRoot().SetFocusable(GetId(), !m_disabled);
    }

    AttributeResult Button::SetAttribute(std::string_view name, std::string_view value)
    {
        const AttributeResult result = SetTextAttribute(*this, name, value);
        return result == AttributeResult::Unknown ? Control::SetAttribute(name, value) : result;
    }

    RouteResult Button::OnInput(App& app, const RoutedInput& input)
    {
        (void)app;
        const KeyEvent* key = KeyPress(input);
        if (key == nullptr || (key->key != Key::Enter && key->key != Key::KeypadEnter && key->key != Key::Space))
        {
            return RouteResult::Passed;
        }
        if (!key->repeat)
        {
            SendClick(*this);
        }
        return RouteResult::Consumed;
    }

    AttributeResult Checkbox::SetAttribute(std::string_view name, std::string_view value)
    {
        if (name != "checked")
        {
            return Control::SetAttribute(name, value);
        }
        const std::optional<bool> checked = Detail::ParseBoolAttribute(value);
        if (!checked.has_value())
        {
            return AttributeResult::Invalid;
        }
        SetChecked(*checked);
        return AttributeResult::Set;
    }

    void Checkbox::SetChecked(bool checked)
    {
        m_checked = checked;
        GetRoot().SetPseudoClass(GetId(), PseudoClass::Checked, checked);
    }

    void Checkbox::OnCreate()
    {
        Control::OnCreate();
        UIRoot& root = GetRoot();
        m_mark = root.Create(GetId(), "checkbox-mark");
        // The root's click (a press and release on it) toggles it, before the game's listeners see the click.
        root.AddListener(GetId(), UIEventKind::Click, [this](UIListenerContext& context) {
            if (context.GetEvent().target == GetId())
            {
                Toggle();
            }
        });
    }

    RouteResult Checkbox::OnInput(App& app, const RoutedInput& input)
    {
        (void)app;
        const KeyEvent* key = KeyPress(input);
        if (key == nullptr || (key->key != Key::Space && key->key != Key::Enter && key->key != Key::KeypadEnter))
        {
            return RouteResult::Passed;
        }
        if (!key->repeat)
        {
            SendClick(*this);
        }
        return RouteResult::Consumed;
    }

    void Checkbox::Toggle()
    {
        SetChecked(!m_checked);
        UIEvent change = MakeEvent(*this, UIEventKind::Change);
        change.value = m_checked ? 1.0f : 0.0f;
        GetRoot().SendEvent(change);
    }

    AttributeResult Slider::SetAttribute(std::string_view name, std::string_view value)
    {
        if (name == "orientation")
        {
            if (value != "horizontal" && value != "vertical")
            {
                return AttributeResult::Invalid;
            }
            SetOrientation(value == "vertical" ? Orientation::Vertical : Orientation::Horizontal);
            return AttributeResult::Set;
        }
        if (name != "min" && name != "max" && name != "step" && name != "value")
        {
            return Control::SetAttribute(name, value);
        }
        const std::optional<float> number = Detail::ParseNumberAttribute(value);
        if (!number.has_value())
        {
            return AttributeResult::Invalid;
        }
        if (name != "step")
        {
            SetRangeAttribute(*this, m_authored, name, *number);
            return AttributeResult::Set;
        }
        SetStep(*number);
        if (m_authored.value.has_value())
        {
            SetValue(*m_authored.value);
        }
        return AttributeResult::Set;
    }

    void Slider::SetRange(float min, float max)
    {
        m_min = std::min(min, max);
        m_max = std::max(min, max);
        m_value = Fit(m_value);
        Place();
    }

    void Slider::SetStep(float step)
    {
        m_step = std::max(step, 0.0f);
        m_value = Fit(m_value);
        Place();
    }

    void Slider::SetValue(float value)
    {
        m_value = Fit(value);
        Place();
    }

    void Slider::SetOrientation(Orientation orientation)
    {
        m_orientation = orientation;
        if (orientation == Orientation::Vertical)
        {
            GetRoot().AddClass(GetId(), "vertical");
        }
        else
        {
            GetRoot().RemoveClass(GetId(), "vertical");
        }
        Place();
    }

    void Slider::OnCreate()
    {
        Control::OnCreate();
        m_fill = GetRoot().Create(GetId(), "slider-fill");
        m_thumb = GetRoot().Create(GetId(), "slider-thumb");
        Place();
    }

    float Slider::Fit(float value) const
    {
        value = std::clamp(value, m_min, m_max);
        if (m_step > 0.0f)
        {
            value = std::min(m_max, m_min + std::round((value - m_min) / m_step) * m_step);
        }
        return value;
    }

    void Slider::Move(float value, bool final)
    {
        const float fitted = Fit(value);
        const bool moved = fitted != m_value;
        m_value = fitted;
        if (!moved && !final)
        {
            return;
        }
        Place();
        UIEvent change = MakeEvent(*this, UIEventKind::Change);
        change.value = m_value;
        change.integer = final ? 1 : 0;
        GetRoot().SendEvent(change);
    }

    float Slider::ValueAtPointer() const
    {
        const Rect2D box = GetRoot().GetBoxes(GetId()).padding;
        const Math::Vec2 pointer = GetRoot().GetPointer().value_or(Math::Vec2(0.0f));
        float fraction = 0.0f;
        if (m_orientation == Orientation::Horizontal)
        {
            fraction = box.width > 0.0f ? (pointer.x - box.x) / box.width : 0.0f;
        }
        else
        {
            fraction = box.height > 0.0f ? 1.0f - (pointer.y - box.y) / box.height : 0.0f;
        }
        return m_min + std::clamp(fraction, 0.0f, 1.0f) * (m_max - m_min);
    }

    void Slider::Place()
    {
        UIRoot& root = GetRoot();
        const float percent = m_max > m_min ? (m_value - m_min) / (m_max - m_min) * 100.0f : 0.0f;
        if (StyleDeclarations* fill = root.EditInlineStyle(m_fill))
        {
            fill->SetLeft(Length::Px(0.0f)).SetBottom(Length::Px(0.0f));
            if (m_orientation == Orientation::Horizontal)
            {
                fill->SetTop(Length::Px(0.0f)).SetWidth(Length::Percent(percent));
                fill->Unset(StyleProperty::Right).Unset(StyleProperty::Height);
            }
            else
            {
                fill->SetRight(Length::Px(0.0f)).SetHeight(Length::Percent(percent));
                fill->Unset(StyleProperty::Top).Unset(StyleProperty::Width);
            }
        }
        if (StyleDeclarations* thumb = root.EditInlineStyle(m_thumb))
        {
            // The default stylesheet's transform centres the thumb on this point.
            if (m_orientation == Orientation::Horizontal)
            {
                thumb->SetLeft(Length::Percent(percent)).SetTop(Length::Percent(50.0f));
            }
            else
            {
                thumb->SetLeft(Length::Percent(50.0f)).SetTop(Length::Percent(100.0f - percent));
            }
        }
    }

    RouteResult Slider::OnInput(App& app, const RoutedInput& input)
    {
        (void)app;
        if (const auto* button = std::get_if<MouseButtonEvent>(&input))
        {
            if (button->button != MouseButton::Left)
            {
                return RouteResult::Passed;
            }
            if (button->pressed)
            {
                // Taken here, so a scroll view round it does not drag its content with the thumb.
                (void)GetRoot().Focus(GetId());
                m_dragging = true;
                Move(ValueAtPointer(), false);
            }
            else if (m_dragging)
            {
                m_dragging = false;
                Move(button->cancelled ? m_value : ValueAtPointer(), true);
            }
            return RouteResult::Consumed;
        }
        if (std::holds_alternative<MouseMotionEvent>(input))
        {
            if (m_dragging)
            {
                Move(ValueAtPointer(), false);
            }
            return RouteResult::Passed;
        }
        const float step = m_step > 0.0f ? m_step : (m_max - m_min) / 100.0f;
        if (const auto* pad = std::get_if<GamepadButtonEvent>(&input); pad != nullptr && pad->pressed)
        {
            // Along its own axis only, so the focus leaves it across.
            const bool horizontal = m_orientation == Orientation::Horizontal;
            const GamepadButton up = horizontal ? GamepadButton::DPadRight : GamepadButton::DPadUp;
            const GamepadButton down = horizontal ? GamepadButton::DPadLeft : GamepadButton::DPadDown;
            // At the end it is stepping to, the direction passes, so a row of sliders can still be left that way.
            const bool rising = pad->button == up;
            if ((!rising && pad->button != down) || m_value == (rising ? m_max : m_min))
            {
                return RouteResult::Passed;
            }
            Move(m_value + (rising ? step : -step), false);
            return RouteResult::Consumed;
        }
        if (const auto* scroll = std::get_if<MouseScrollEvent>(&input))
        {
            // Unfocused, the wheel passes on, so a scroll view round it scrolls past it.
            if (GetRoot().GetFocused() != GetId())
            {
                return RouteResult::Passed;
            }
            Move(m_value + scroll->notches.y * step, false);
            return RouteResult::Consumed;
        }
        const KeyEvent* key = KeyPress(input);
        if (key == nullptr)
        {
            return RouteResult::Passed;
        }
        switch (key->key)
        {
        case Key::Left:
        case Key::Down:
            Move(m_value - step, false);
            return RouteResult::Consumed;
        case Key::Right:
        case Key::Up:
            Move(m_value + step, false);
            return RouteResult::Consumed;
        case Key::Home:
            Move(m_min, false);
            return RouteResult::Consumed;
        case Key::End:
            Move(m_max, false);
            return RouteResult::Consumed;
        default:
            return RouteResult::Passed;
        }
    }

    AttributeResult ProgressBar::SetAttribute(std::string_view name, std::string_view value)
    {
        if (name != "min" && name != "max" && name != "value")
        {
            return AttributeResult::Unknown;
        }
        const std::optional<float> number = Detail::ParseNumberAttribute(value);
        if (!number.has_value())
        {
            return AttributeResult::Invalid;
        }
        SetRangeAttribute(*this, m_authored, name, *number);
        return AttributeResult::Set;
    }

    void ProgressBar::SetRange(float min, float max)
    {
        m_min = std::min(min, max);
        m_max = std::max(min, max);
        m_value = std::clamp(m_value, m_min, m_max);
        Place();
    }

    void ProgressBar::SetValue(float value)
    {
        m_value = std::clamp(value, m_min, m_max);
        Place();
    }

    float ProgressBar::GetFraction() const noexcept
    {
        return m_max > m_min ? (m_value - m_min) / (m_max - m_min) : 0.0f;
    }

    void ProgressBar::OnCreate()
    {
        m_fill = GetRoot().Create(GetId(), "progress-fill");
        Place();
    }

    void ProgressBar::Place()
    {
        if (StyleDeclarations* fill = GetRoot().EditInlineStyle(m_fill))
        {
            fill->SetWidth(Length::Percent(GetFraction() * 100.0f));
        }
    }

    AttributeResult ImageView::SetAttribute(std::string_view name, std::string_view value)
    {
        if (name == "fit")
        {
            if (value == "stretch")
            {
                SetFit(ImageFit::Stretch);
            }
            else if (value == "contain")
            {
                SetFit(ImageFit::Contain);
            }
            else if (value == "cover")
            {
                SetFit(ImageFit::Cover);
            }
            else
            {
                return AttributeResult::Invalid;
            }
            return AttributeResult::Set;
        }
        if (name != "src")
        {
            return AttributeResult::Unknown;
        }
        AssetStore* store = GetRoot().GetApp().TryGetResource<AssetStore>();
        if (store == nullptr || value.empty())
        {
            return AttributeResult::Invalid;
        }
        const LoadResult<Image> loaded = store->Load<Image>(value, AssetOwner::App());
        if (!loaded.Ok())
        {
            return AttributeResult::Invalid;
        }
        SetSource(loaded.handle);
        return AttributeResult::Set;
    }

    void ImageView::SetSource(AssetHandle<Image> image)
    {
        m_source = image;
        GetRoot().InvalidateMeasure(GetId());
    }

    void ImageView::SetFit(ImageFit fit)
    {
        m_fit = fit;
    }

    Math::Vec2 ImageView::ImageSize() const
    {
        const AssetStore* store = GetRoot().GetApp().TryGetResource<AssetStore>();
        if (m_source.IsEmpty() || store == nullptr)
        {
            return Math::Vec2(0.0f);
        }
        const auto* data = store->TryGet(m_source);
        if (data == nullptr || data->width == 0 || data->height == 0)
        {
            return Math::Vec2(0.0f);
        }
        return {static_cast<float>(data->width), static_cast<float>(data->height)};
    }

    Math::Vec2 ImageView::Measure(const MeasureConstraints& constraints)
    {
        Math::Vec2 size = ImageSize();
        if (size.x <= 0.0f)
        {
            return size;
        }
        const float aspect = size.y / size.x;
        if (constraints.widthMode == MeasureMode::Exactly)
        {
            size.x = constraints.width;
            size.y = constraints.heightMode == MeasureMode::Exactly ? constraints.height : size.x * aspect;
            return size;
        }
        if (constraints.heightMode == MeasureMode::Exactly)
        {
            size.y = constraints.height;
            size.x = size.y / aspect;
            return size;
        }
        // Bounded, it shrinks to fit and keeps its shape.
        if (constraints.widthMode == MeasureMode::AtMost && size.x > constraints.width)
        {
            size = {constraints.width, constraints.width * aspect};
        }
        if (constraints.heightMode == MeasureMode::AtMost && size.y > constraints.height)
        {
            size = {constraints.height / aspect, constraints.height};
        }
        return size;
    }

    void ImageView::Paint(Painter& painter, const ElementPaint& paint)
    {
        const Math::Vec2 size = ImageSize();
        const Rect2D& box = paint.boxes.content;
        if (size.x <= 0.0f || box.width <= 0.0f || box.height <= 0.0f)
        {
            return;
        }
        ImageDraw draw;
        draw.image = m_source;
        draw.tint = Colour::White;
        draw.tint.a = paint.opacity;
        if (m_fit == ImageFit::Stretch)
        {
            painter.DrawImage(box, draw);
            return;
        }
        const float scale = m_fit == ImageFit::Cover ? std::max(box.width / size.x, box.height / size.y)
                                                     : std::min(box.width / size.x, box.height / size.y);
        const Math::Vec2 fitted = size * scale;
        const Rect2D rect{box.x + (box.width - fitted.x) / 2.0f, box.y + (box.height - fitted.y) / 2.0f, fitted.x,
                          fitted.y};
        if (m_fit == ImageFit::Cover)
        {
            // The box filled with the image laid over `rect`, rather than the image clipped to the box: the painter
            // evaluates only the innermost clip per pixel, so a clip of its own would take the place of an ancestor's
            // rounded overflow clip.
            ImagePattern pattern;
            pattern.image = m_source;
            pattern.sampler = draw.sampler;
            pattern.rect = rect;
            pattern.tint = draw.tint;
            painter.DrawRect(box, pattern);
            return;
        }
        painter.DrawImage(rect, draw);
    }

    Math::Vec2 ScrollView::GetOffset() const noexcept
    {
        return GetRoot().GetScrollOffset(GetId());
    }

    void ScrollView::SetOffset(Math::Vec2 offset)
    {
        GetRoot().SetScrollOffset(GetId(), offset);
    }

    RouteResult ScrollView::OnInput(App& app, const RoutedInput& input)
    {
        (void)app;
        if (const auto* button = std::get_if<MouseButtonEvent>(&input))
        {
            if (button->pressed)
            {
                return Press(*button);
            }
            if (m_drag != Drag::None && button->button == m_button)
            {
                const bool dragged = m_drag != Drag::Pending;
                m_drag = Drag::None;
                return dragged ? RouteResult::Consumed : RouteResult::Passed;
            }
            return RouteResult::Passed;
        }
        if (std::holds_alternative<MouseMotionEvent>(input))
        {
            Motion();
            return RouteResult::Passed;
        }
        if (const auto* scroll = std::get_if<MouseScrollEvent>(&input))
        {
            const Math::Vec2 range = GetRoot().GetScrollRange(GetId());
            if (range.x <= 0.0f && range.y <= 0.0f)
            {
                // Nothing to scroll: a scroll view round it may.
                return RouteResult::Passed;
            }
            // Notches away from the user scroll up; to the right, right.
            SetOffset(GetOffset() + Math::Vec2(scroll->notches.x, -scroll->notches.y) * ScrollStep);
            return RouteResult::Consumed;
        }
        if (const KeyEvent* key = KeyPress(input))
        {
            return KeyPressed(*key);
        }
        if (const auto* pad = std::get_if<GamepadButtonEvent>(&input); pad != nullptr && pad->pressed)
        {
            return DirectionPressed(*pad);
        }
        return RouteResult::Passed;
    }

    RouteResult ScrollView::DirectionPressed(const GamepadButtonEvent& event)
    {
        Math::Vec2 step{0.0f, 0.0f};
        switch (event.button)
        {
        case GamepadButton::DPadUp:
            step.y = -ScrollStep;
            break;
        case GamepadButton::DPadDown:
            step.y = ScrollStep;
            break;
        case GamepadButton::DPadLeft:
            step.x = -ScrollStep;
            break;
        case GamepadButton::DPadRight:
            step.x = ScrollStep;
            break;
        default:
            return RouteResult::Passed;
        }
        // At the end of its range the direction passes, so the focus moves on out of it.
        const Math::Vec2 offset = GetOffset();
        const Math::Vec2 range = GetRoot().GetScrollRange(GetId());
        const Math::Vec2 to(std::clamp(offset.x + step.x, 0.0f, std::max(range.x, 0.0f)),
                            std::clamp(offset.y + step.y, 0.0f, std::max(range.y, 0.0f)));
        if (to.x == offset.x && to.y == offset.y)
        {
            return RouteResult::Passed;
        }
        SetOffset(to);
        return RouteResult::Consumed;
    }

    RouteResult ScrollView::Press(const MouseButtonEvent& event)
    {
        if (event.button != MouseButton::Left)
        {
            return RouteResult::Passed;
        }
        // A new press starts over, whatever an earlier one whose release never came was doing.
        m_drag = Drag::None;
        UIRoot& root = GetRoot();
        const Math::Vec2 pointer = root.GetPointer().value_or(Math::Vec2(0.0f));
        const ScrollbarBoxes bars = root.GetScrollbars(GetId());
        m_button = event.button;
        m_pressPointer = pointer;
        m_pressOffset = GetOffset();
        const Rect2D page = root.GetBoxes(GetId()).padding;
        if (bars.vertical && Inside(bars.verticalTrack, pointer))
        {
            if (Inside(bars.verticalThumb, pointer))
            {
                m_drag = Drag::VerticalThumb;
            }
            else
            {
                SetOffset(GetOffset() +
                          Math::Vec2(0.0f, pointer.y < bars.verticalThumb.y ? -page.height : page.height));
            }
            return RouteResult::Consumed;
        }
        if (bars.horizontal && Inside(bars.horizontalTrack, pointer))
        {
            if (Inside(bars.horizontalThumb, pointer))
            {
                m_drag = Drag::HorizontalThumb;
            }
            else
            {
                SetOffset(GetOffset() +
                          Math::Vec2(pointer.x < bars.horizontalThumb.x ? -page.width : page.width, 0.0f));
            }
            return RouteResult::Consumed;
        }
        const Math::Vec2 range = root.GetScrollRange(GetId());
        if (range.x > 0.0f || range.y > 0.0f)
        {
            m_drag = Drag::Pending;
        }
        // What the press landed on still takes it, and the focus, until it moves far enough to drag.
        return RouteResult::Passed;
    }

    void ScrollView::Motion()
    {
        if (m_drag == Drag::None)
        {
            return;
        }
        UIRoot& root = GetRoot();
        const Math::Vec2 pointer = root.GetPointer().value_or(m_pressPointer);
        const Math::Vec2 moved = pointer - m_pressPointer;
        switch (m_drag)
        {
        case Drag::Pending:
            if (moved.x * moved.x + moved.y * moved.y <= DragThreshold * DragThreshold)
            {
                return;
            }
            // The press went away with the element it was on, its release with it: there is no drag.
            if (!root.TakePress(GetId()))
            {
                m_drag = Drag::None;
                return;
            }
            m_drag = Drag::Content;
            [[fallthrough]];
        case Drag::Content:
            SetOffset(m_pressOffset - moved);
            return;
        case Drag::VerticalThumb:
        case Drag::HorizontalThumb: {
            // The thumb's travel spans the range.
            const ScrollbarBoxes bars = root.GetScrollbars(GetId());
            const Math::Vec2 range = root.GetScrollRange(GetId());
            Math::Vec2 offset = m_pressOffset;
            if (m_drag == Drag::VerticalThumb && bars.travel.y > 0.0f)
            {
                offset.y += moved.y * range.y / bars.travel.y;
            }
            if (m_drag == Drag::HorizontalThumb && bars.travel.x > 0.0f)
            {
                offset.x += moved.x * range.x / bars.travel.x;
            }
            SetOffset(offset);
            return;
        }
        case Drag::None:
            return;
        }
    }

    RouteResult ScrollView::KeyPressed(const KeyEvent& event)
    {
        UIRoot& root = GetRoot();
        const Math::Vec2 range = root.GetScrollRange(GetId());
        const Rect2D page = root.GetBoxes(GetId()).padding;
        const Math::Vec2 offset = GetOffset();
        switch (event.key)
        {
        case Key::Up:
            SetOffset(offset - Math::Vec2(0.0f, ScrollStep));
            return RouteResult::Consumed;
        case Key::Down:
            SetOffset(offset + Math::Vec2(0.0f, ScrollStep));
            return RouteResult::Consumed;
        case Key::Left:
            SetOffset(offset - Math::Vec2(ScrollStep, 0.0f));
            return RouteResult::Consumed;
        case Key::Right:
            SetOffset(offset + Math::Vec2(ScrollStep, 0.0f));
            return RouteResult::Consumed;
        case Key::PageUp:
            SetOffset(offset - Math::Vec2(0.0f, page.height));
            return RouteResult::Consumed;
        case Key::PageDown:
            SetOffset(offset + Math::Vec2(0.0f, page.height));
            return RouteResult::Consumed;
        case Key::Home:
            SetOffset({offset.x, 0.0f});
            return RouteResult::Consumed;
        case Key::End:
            SetOffset({offset.x, range.y});
            return RouteResult::Consumed;
        default:
            return RouteResult::Passed;
        }
    }

    namespace
    {
        /** @brief `codepoint` as UTF-8, appended to `out`. */
        void AppendUtf8(std::string& out, char32_t codepoint)
        {
            const auto byte = [&](std::uint32_t value) { out.push_back(static_cast<char>(value)); };
            const auto value = static_cast<std::uint32_t>(codepoint);
            if (value < 0x80u)
            {
                byte(value);
            }
            else if (value < 0x800u)
            {
                byte(0xC0u | (value >> 6u));
                byte(0x80u | (value & 0x3Fu));
            }
            else if (value < 0x10000u)
            {
                byte(0xE0u | (value >> 12u));
                byte(0x80u | ((value >> 6u) & 0x3Fu));
                byte(0x80u | (value & 0x3Fu));
            }
            else
            {
                byte(0xF0u | (value >> 18u));
                byte(0x80u | ((value >> 12u) & 0x3Fu));
                byte(0x80u | ((value >> 6u) & 0x3Fu));
                byte(0x80u | (value & 0x3Fu));
            }
        }

        /**
         * @brief `utf8` made fit for one line: tabs and line breaks become spaces and the other C0 controls and DEL
         * are dropped. Multi-byte sequences pass as they are.
         */
        std::string OneLine(std::string_view utf8)
        {
            std::string line;
            line.reserve(utf8.size());
            for (const char c : utf8)
            {
                const auto byte = static_cast<unsigned char>(c);
                if (byte == '\t' || byte == '\n' || byte == '\r')
                {
                    line.push_back(' ');
                }
                else if (byte >= 0x20u && byte != 0x7Fu)
                {
                    line.push_back(c);
                }
            }
            return line;
        }

        /** @brief Whether a typed codepoint is text: not a C0 or C1 control or DEL, nor a surrogate. */
        bool IsTypedText(char32_t codepoint)
        {
            return codepoint >= 0x20 && !(codepoint >= 0x7F && codepoint <= 0x9F) &&
                   !(codepoint >= 0xD800 && codepoint <= 0xDFFF) && codepoint <= 0x10FFFF;
        }

        /** @brief `paragraph` from `point`, its runs in `colour` when given, faded by `alpha`. */
        void DrawParagraph(Painter& painter, Paragraph& paragraph, Math::Vec2 point, const Colour* colour, float alpha)
        {
            if (colour == nullptr && alpha >= 1.0f)
            {
                painter.DrawText(paragraph, point);
                return;
            }
            Detail::PaintRuns(painter, paragraph, point, colour, alpha);
        }

        /** @brief Where the caret before shown offset `index` is, in pixels from the paragraph's left. */
        float CaretOffset(const Paragraph& paragraph, std::uint32_t index)
        {
            const auto size = static_cast<std::uint32_t>(paragraph.Text().size());
            if (size == 0)
            {
                return 0.0f;
            }
            if (index < size)
            {
                const std::vector<Rect2D> rects = paragraph.RangeRects(index, paragraph.NextGrapheme(index));
                if (!rects.empty())
                {
                    return rects.front().x;
                }
            }
            const std::vector<Rect2D> rects = paragraph.RangeRects(paragraph.PreviousGrapheme(index), index);
            return rects.empty() ? paragraph.Size().x : rects.back().x + rects.back().width;
        }

        Rect2D ToUnits(const Rect2D& pixels, Math::Vec2 origin, float pixelsPerUnit)
        {
            return {origin.x + pixels.x / pixelsPerUnit, origin.y + pixels.y / pixelsPerUnit,
                    pixels.width / pixelsPerUnit, pixels.height / pixelsPerUnit};
        }
    } // namespace

    TextEntry::TextEntry()
        : m_shown(std::make_unique<Detail::ElementText>()), m_placeholderText(std::make_unique<Detail::ElementText>()),
          m_line(std::make_unique<Detail::ElementText>())
    {
        for (Detail::ElementText* text : {m_shown.get(), m_placeholderText.get(), m_line.get()})
        {
            text->SetSingleLine(true);
        }
        // A space keeps its line's height, which an empty paragraph has none of.
        m_line->SetText(" ");
    }

    TextEntry::~TextEntry() = default;

    void TextEntry::OnCreate()
    {
        Control::OnCreate();
        // The root's double click, after the press that put the caret under it.
        GetRoot().AddListener(GetId(), UIEventKind::DoubleClick, [this](UIListenerContext& context) {
            if (context.GetEvent().target == GetId())
            {
                const TextRange word = WordAt(m_caret);
                Select(word.begin, word.end);
            }
        });
        // A caret that takes the focus starts its blink shown.
        GetRoot().AddListener(GetId(), UIEventKind::Focus, [this](UIListenerContext& context) {
            if (context.GetEvent().target == GetId())
            {
                m_blink = 0.0;
            }
        });
    }

    AttributeResult TextEntry::SetAttribute(std::string_view name, std::string_view value)
    {
        if (name == "value")
        {
            SetValue(value);
        }
        else if (name == "placeholder")
        {
            SetPlaceholder(value);
        }
        else if (name == "password")
        {
            const std::optional<bool> password = Detail::ParseBoolAttribute(value);
            if (!password.has_value())
            {
                return AttributeResult::Invalid;
            }
            SetPassword(*password);
        }
        else if (name == "maxlength")
        {
            const std::optional<std::size_t> graphemes = Detail::ParseCountAttribute(value);
            if (!graphemes.has_value())
            {
                return AttributeResult::Invalid;
            }
            SetMaxLength(*graphemes);
        }
        else
        {
            return Control::SetAttribute(name, value);
        }
        return AttributeResult::Set;
    }

    void TextEntry::SetValue(std::string_view utf8)
    {
        m_value = OneLine(utf8);
        ValueChanged();
        const auto end = static_cast<std::uint32_t>(m_value.size());
        Select(end, end);
    }

    void TextEntry::SetPlaceholder(std::string_view utf8)
    {
        m_placeholder = OneLine(utf8);
        m_placeholderText->SetText(m_placeholder);
    }

    void TextEntry::SetPassword(bool password)
    {
        m_password = password;
        ValueChanged();
    }

    void TextEntry::SetMaxLength(std::size_t graphemes)
    {
        m_maxLength = graphemes;
    }

    void TextEntry::Select(std::uint32_t anchor, std::uint32_t caret)
    {
        m_anchor = Snap(anchor);
        m_caret = Snap(caret);
        m_blink = 0.0;
    }

    void TextEntry::SelectAll()
    {
        Select(0, static_cast<std::uint32_t>(m_value.size()));
    }

    const Paragraph& TextEntry::GetParagraph() const noexcept
    {
        return m_shown->GetParagraph();
    }

    std::uint32_t TextEntry::Snap(std::uint32_t index) const
    {
        const auto at = std::upper_bound(m_graphemes.begin(), m_graphemes.end(), index);
        return at == m_graphemes.begin() ? 0 : *(at - 1);
    }

    std::uint32_t TextEntry::NextGrapheme(std::uint32_t index) const
    {
        const auto at = std::upper_bound(m_graphemes.begin(), m_graphemes.end(), index);
        return at == m_graphemes.end() ? m_graphemes.back() : *at;
    }

    std::uint32_t TextEntry::PreviousGrapheme(std::uint32_t index) const
    {
        const auto at = std::lower_bound(m_graphemes.begin(), m_graphemes.end(), index);
        return at == m_graphemes.begin() ? 0 : *(at - 1);
    }

    std::uint32_t TextEntry::PreviousWordStart(std::uint32_t index) const
    {
        for (auto word = m_words.rbegin(); word != m_words.rend(); ++word)
        {
            if (word->begin < index)
            {
                return word->begin;
            }
        }
        return 0;
    }

    std::uint32_t TextEntry::NextWordEnd(std::uint32_t index) const
    {
        for (const TextRange& word : m_words)
        {
            if (word.end > index)
            {
                return word.end;
            }
        }
        return static_cast<std::uint32_t>(m_value.size());
    }

    TextRange TextEntry::WordAt(std::uint32_t index) const
    {
        if (m_password)
        {
            return {0, static_cast<std::uint32_t>(m_value.size())};
        }
        // A word that holds `index`, or ends at it, is the word there; else the run between the words either side.
        TextRange between{0, static_cast<std::uint32_t>(m_value.size())};
        for (const TextRange& word : m_words)
        {
            if (word.begin <= index && index <= word.end)
            {
                return word;
            }
            if (word.end < index)
            {
                between.begin = word.end;
            }
            else
            {
                between.end = word.begin;
                break;
            }
        }
        return between;
    }

    std::uint32_t TextEntry::ShownIndex(std::uint32_t index) const
    {
        if (!m_password)
        {
            return index;
        }
        const auto before = std::lower_bound(m_graphemes.begin(), m_graphemes.end(), index) - m_graphemes.begin();
        return static_cast<std::uint32_t>(before) * static_cast<std::uint32_t>(Bullet.size());
    }

    std::uint32_t TextEntry::ValueIndex(std::uint32_t index) const
    {
        if (!m_password)
        {
            return Snap(index);
        }
        const std::size_t grapheme = std::min<std::size_t>(index / Bullet.size(), m_graphemes.size() - 1);
        return m_graphemes[grapheme];
    }

    std::uint32_t TextEntry::IndexAt(Math::Vec2 point)
    {
        if (!m_painted || !LayOut())
        {
            return static_cast<std::uint32_t>(m_value.size());
        }
        const Math::Vec2 local = (point - m_origin) * PixelsPerUnit();
        return ValueIndex(m_shown->GetParagraph().CharacterAt(local));
    }

    void TextEntry::MoveCaret(std::uint32_t index, bool extend)
    {
        m_caret = Snap(index);
        if (!extend)
        {
            m_anchor = m_caret;
        }
        m_blink = 0.0;
    }

    void TextEntry::Replace(std::uint32_t begin, std::uint32_t end, std::string_view text)
    {
        std::string inserted = OneLine(text);
        std::string value = m_value.substr(0, begin) + inserted + m_value.substr(end);
        if (m_maxLength > 0)
        {
            // Drop the inserted text's last graphemes until the value fits, or is no longer than it was (a value set
            // longer than the limit may still be edited down). Start at the count the rest leaves room for; a
            // grapheme that joins the text beside it can let one more in, so the count is checked after.
            const auto graphemes = [](std::string_view text) { return GraphemeBoundaries(text).size() - 1; };
            const std::string rest = m_value.substr(0, begin) + m_value.substr(end);
            const std::size_t limit = std::max(m_maxLength, m_graphemes.size() - 1);
            const std::vector<std::uint32_t> pieces = GraphemeBoundaries(inserted);
            const std::size_t room = limit - std::min(limit, graphemes(rest));
            std::size_t keep = std::min(pieces.size() - 1, room + 1);
            inserted.resize(pieces[keep]);
            value = m_value.substr(0, begin) + inserted + m_value.substr(end);
            while (keep > 0 && graphemes(value) > limit)
            {
                --keep;
                inserted.resize(pieces[keep]);
                value = m_value.substr(0, begin) + inserted + m_value.substr(end);
            }
        }
        const bool changed = value != m_value;
        m_value = std::move(value);
        ValueChanged();
        const auto caret = static_cast<std::uint32_t>(begin + inserted.size());
        Select(caret, caret);
        if (changed)
        {
            GetRoot().SendEvent(MakeEvent(*this, UIEventKind::Change));
        }
    }

    void TextEntry::Insert(std::string_view text)
    {
        Replace(std::min(m_caret, m_anchor), std::max(m_caret, m_anchor), text);
    }

    void TextEntry::ValueChanged()
    {
        m_graphemes = GraphemeBoundaries(m_value);
        m_words = m_password ? std::vector<TextRange>{} : Words(m_value);
        if (m_password)
        {
            std::string bullets;
            for (std::size_t i = 1; i < m_graphemes.size(); ++i)
            {
                bullets += Bullet;
            }
            m_shown->SetText(bullets);
        }
        else
        {
            m_shown->SetText(m_value);
        }
        m_caret = Snap(m_caret);
        m_anchor = Snap(m_anchor);
        m_blink = 0.0;
    }

    RouteResult TextEntry::OnInput(App& app, const RoutedInput& input)
    {
        if (const auto* text = std::get_if<TextEvent>(&input))
        {
            if (IsTypedText(text->codepoint))
            {
                std::string utf8;
                AppendUtf8(utf8, text->codepoint);
                Insert(utf8);
            }
            return RouteResult::Consumed;
        }
        if (const KeyEvent* key = KeyPress(input))
        {
            return KeyPressed(app, *key);
        }
        if (const auto* button = std::get_if<MouseButtonEvent>(&input))
        {
            if (button->button != MouseButton::Left)
            {
                return RouteResult::Passed;
            }
            if (!button->pressed)
            {
                m_dragging = false;
                return RouteResult::Passed;
            }
            // Taken here, focusing itself, so an enclosing scroll view starts no content drag.
            (void)GetRoot().Focus(GetId());
            MoveCaret(IndexAt(GetRoot().GetPointer().value_or(m_origin)), false);
            m_dragging = true;
            return RouteResult::Consumed;
        }
        if (std::holds_alternative<MouseMotionEvent>(input) && m_dragging)
        {
            if (const std::optional<Math::Vec2> pointer = GetRoot().GetPointer())
            {
                MoveCaret(IndexAt(*pointer), true);
            }
            return RouteResult::Consumed;
        }
        return RouteResult::Passed;
    }

    RouteResult TextEntry::KeyPressed(App& app, const KeyEvent& key)
    {
        const bool ctrl = HasModifiers(key.modifiers, KeyModifiers::Ctrl);
        const bool shift = HasModifiers(key.modifiers, KeyModifiers::Shift);
        const std::uint32_t begin = std::min(m_caret, m_anchor);
        const std::uint32_t end = std::max(m_caret, m_anchor);
        const auto size = static_cast<std::uint32_t>(m_value.size());
        switch (key.key)
        {
        case Key::Left:
            if (HasSelection() && !shift)
            {
                MoveCaret(begin, false);
            }
            else
            {
                MoveCaret(ctrl ? PreviousWordStart(m_caret) : PreviousGrapheme(m_caret), shift);
            }
            return RouteResult::Consumed;
        case Key::Right:
            if (HasSelection() && !shift)
            {
                MoveCaret(end, false);
            }
            else
            {
                MoveCaret(ctrl ? NextWordEnd(m_caret) : NextGrapheme(m_caret), shift);
            }
            return RouteResult::Consumed;
        case Key::Home:
            MoveCaret(0, shift);
            return RouteResult::Consumed;
        case Key::End:
            MoveCaret(size, shift);
            return RouteResult::Consumed;
        case Key::Backspace:
            if (HasSelection())
            {
                Insert({});
            }
            else if (m_caret > 0)
            {
                Replace(ctrl ? PreviousWordStart(m_caret) : PreviousGrapheme(m_caret), m_caret, {});
            }
            return RouteResult::Consumed;
        case Key::Delete:
            if (HasSelection())
            {
                Insert({});
            }
            else if (m_caret < size)
            {
                Replace(m_caret, ctrl ? NextWordEnd(m_caret) : NextGrapheme(m_caret), {});
            }
            return RouteResult::Consumed;
        case Key::A:
            if (ctrl)
            {
                SelectAll();
            }
            return RouteResult::Consumed;
        case Key::C:
        case Key::X:
            if (ctrl && HasSelection() && !m_password)
            {
                (void)Window::SetClipboardText(app, std::string_view(m_value).substr(begin, end - begin));
                if (key.key == Key::X)
                {
                    Insert({});
                }
            }
            return RouteResult::Consumed;
        case Key::V:
            if (ctrl)
            {
                Insert(Window::ClipboardText(app));
            }
            return RouteResult::Consumed;
        case Key::Enter:
        case Key::KeypadEnter:
            if (!key.repeat)
            {
                GetRoot().SendEvent(MakeEvent(*this, UIEventKind::Submit));
            }
            return RouteResult::Consumed;
        case Key::Escape:
            GetRoot().Blur();
            return RouteResult::Consumed;
        default:
            // Tab moves the focus on, and Up, Down and the page keys reach an enclosing scroll view.
            return RouteResult::Passed;
        }
    }

    float TextEntry::PixelsPerUnit() const
    {
        const UIRoot& root = GetRoot();
        const float contentScale = root.GetTargetSize().contentScale;
        return root.GetScale() * (contentScale > 0.0f ? contentScale : 1.0f);
    }

    bool TextEntry::ApplyStyle(const ResolvedStyle& style)
    {
        const float pixelsPerUnit = PixelsPerUnit();
        const bool resized = m_line->SetStyle(style, pixelsPerUnit);
        (void)m_shown->SetStyle(style, pixelsPerUnit);
        const ResolvedStyle* placeholder = GetRoot().GetPseudoElementStyle(GetId(), PseudoElement::Placeholder);
        (void)m_placeholderText->SetStyle(placeholder != nullptr ? *placeholder : style, pixelsPerUnit);
        return resized;
    }

    bool TextEntry::LayOut()
    {
        FontLibrary* fonts = GetRoot().GetApp().GetResource<UIRoots>().GetFonts();
        if (fonts == nullptr)
        {
            return false;
        }
        for (Detail::ElementText* text : {m_shown.get(), m_placeholderText.get(), m_line.get()})
        {
            (void)text->LayOutForDrawing(*fonts);
        }
        return true;
    }

    Math::Vec2 TextEntry::Measure(const MeasureConstraints& constraints)
    {
        // Layout runs after the frame's styles are resolved: its own is this frame's.
        if (const ResolvedStyle* style = GetRoot().GetResolvedStyle(GetId()))
        {
            (void)ApplyStyle(*style);
        }
        const float line = m_line->Measure(GetRoot().GetApp().GetResource<UIRoots>().GetFonts(), {}).y;
        const float width = constraints.widthMode == MeasureMode::Exactly ? constraints.width : 0.0f;
        float height = line;
        if (constraints.heightMode == MeasureMode::Exactly)
        {
            height = constraints.height;
        }
        else if (constraints.heightMode == MeasureMode::AtMost)
        {
            height = std::min(line, constraints.height);
        }
        return {width, height};
    }

    void TextEntry::Paint(Painter& painter, const ElementPaint& paint)
    {
        if (ApplyStyle(paint.style))
        {
            // Its font changed after layout measured it: its line's height is measured again next frame.
            GetRoot().InvalidateMeasure(GetId());
        }
        if (!LayOut())
        {
            return;
        }
        UIRoot& root = GetRoot();
        m_blink += root.GetApp().GetTime().deltaTime;
        const float pixelsPerUnit = PixelsPerUnit();
        const Rect2D& content = paint.boxes.content;
        Paragraph& shown = m_shown->GetParagraph();
        const float lineHeight = m_line->GetParagraph().Size().y / pixelsPerUnit;
        const float caret = CaretOffset(shown, ShownIndex(m_caret)) / pixelsPerUnit;
        const float width = shown.Size().x / pixelsPerUnit;

        // Scrolled as little as keeps the caret in the content box, and no further than the text's end needs.
        const float view = std::max(content.width - CaretWidth, 0.0f);
        m_scroll = std::clamp(m_scroll, caret - view, caret);
        m_scroll = std::clamp(m_scroll, 0.0f, std::max(width - view, 0.0f));
        m_origin = {content.x - m_scroll, content.y + (content.height - lineHeight) / 2.0f};
        m_caretRect = {m_origin.x + caret, m_origin.y, CaretWidth, lineHeight};
        m_painted = true;

        const bool focused = root.GetFocused() == GetId();
        painter.Save();
        painter.ClipRect(content);
        if (m_value.empty())
        {
            DrawParagraph(painter, m_placeholderText->GetParagraph(), m_origin, nullptr, paint.opacity);
        }
        else
        {
            std::vector<Rect2D> selection;
            const ResolvedStyle* selected = root.GetPseudoElementStyle(GetId(), PseudoElement::Selection);
            if (focused && HasSelection() && selected != nullptr)
            {
                for (const Rect2D& rect :
                     shown.RangeRects(ShownIndex(std::min(m_caret, m_anchor)), ShownIndex(std::max(m_caret, m_anchor))))
                {
                    selection.push_back(ToUnits(rect, m_origin, pixelsPerUnit));
                    const Colour background = Detail::Faded(selected->backgroundColor, paint.opacity);
                    if (background.a > 0.0f)
                    {
                        painter.DrawRect(selection.back(), background);
                    }
                }
            }
            DrawParagraph(painter, shown, m_origin, nullptr, paint.opacity);
            // The selected text again over it, in the selection's colour, when that differs.
            if (!selection.empty() && !Detail::SameColour(selected->color, paint.style.color))
            {
                for (const Rect2D& rect : selection)
                {
                    painter.Save();
                    painter.ClipRect(rect);
                    DrawParagraph(painter, shown, m_origin, &selected->color, paint.opacity);
                    painter.Restore();
                }
            }
        }
        if (focused && !HasSelection() && std::fmod(m_blink, 2.0 * BlinkSeconds) < BlinkSeconds)
        {
            painter.DrawRect(m_caretRect, Detail::Faded(paint.style.color, paint.opacity));
        }
        painter.Restore();
    }

    std::optional<Rect2D> TextEntry::GetTextCaret() const
    {
        if (m_painted)
        {
            return m_caretRect;
        }
        const Rect2D content = GetRoot().GetBoxes(GetId()).content;
        return Rect2D{content.x, content.y, CaretWidth, content.height};
    }
} // namespace Fenrir

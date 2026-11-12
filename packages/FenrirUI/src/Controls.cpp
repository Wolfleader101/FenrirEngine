#include "FenrirUI/Controls.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <variant>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirPainter/Painter.hpp"
#include "FenrirUI/UIRoot.hpp"

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

        bool Inside(const Rect2D& rect, Math::Vec2 point)
        {
            return point.x >= rect.x && point.y >= rect.y && point.x < rect.x + rect.width &&
                   point.y < rect.y + rect.height;
        }
    } // namespace

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
        if (key == nullptr || key->key != Key::Space)
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
            painter.Save();
            painter.ClipRect(box);
            painter.DrawImage(rect, draw);
            painter.Restore();
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
        return RouteResult::Passed;
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
} // namespace Fenrir

#include "InputRouting.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>
#include <variant>

#include "ElementGeometry.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirUI/UI.hpp"
#include "FenrirUI/UIRoot.hpp"
#include "Scrolling.hpp"
#include "UIRootState.hpp"

namespace Fenrir::Detail
{
    namespace
    {
        /** @brief How long after a click a second one on the same element is a double click, in seconds. */
        constexpr double doubleClickSeconds = 0.5;
        /** @brief How far, in UI units, the pointer may move between the two clicks of a double click. */
        constexpr float doubleClickDistance = 4.0f;

        ElementId HitElement(const UIRootState& state, ElementId id, Math::Vec2 point)
        {
            const ElementSlot& slot = state.slots[id.index];
            const ResolvedStyle& style = slot.resolved;
            if (style.display == Display::None)
            {
                return {};
            }
            // Painting puts the element's box and children under its transform; undo it to test in the same space.
            if (!style.transform.empty())
            {
                point = ElementTransform(style, slot.boxes.border).Inverse().Apply(point);
            }
            // A scroll view's bars lie over its children.
            if (style.overflow == Overflow::Scroll && style.pointerEvents == PointerEvents::All)
            {
                const ScrollbarBoxes bars = ScrollbarsOf(slot);
                const auto inside = [&](const Rect2D& rect) {
                    return point.x >= rect.x && point.y >= rect.y && point.x < rect.x + rect.width &&
                           point.y < rect.y + rect.height;
                };
                if ((bars.vertical && inside(bars.verticalTrack)) || (bars.horizontal && inside(bars.horizontalTrack)))
                {
                    return id;
                }
            }
            const bool childrenReachable =
                style.overflow == Overflow::Visible || ContainsRounded(slot.boxes.padding, InnerRadiiOf(style), point);
            if (childrenReachable && !slot.children.empty())
            {
                // Painted by increasing z-index, equal ones in tree order: the last painted is on top, so tested first.
                std::vector<ElementId> children = slot.children;
                std::stable_sort(children.begin(), children.end(), [&](ElementId a, ElementId b) {
                    return state.slots[a.index].resolved.zIndex < state.slots[b.index].resolved.zIndex;
                });
                for (auto child = children.rbegin(); child != children.rend(); ++child)
                {
                    const ElementId hit = HitElement(state, *child, point);
                    if (hit.IsValid())
                    {
                        return hit;
                    }
                }
            }
            if (style.pointerEvents == PointerEvents::All && ContainsRounded(slot.boxes.border, RadiiOf(style), point))
            {
                return id;
            }
            return {};
        }

        /** @brief Whether `element` is `ancestor` or under it. */
        bool IsWithin(const UIRootState& state, ElementId element, ElementId ancestor)
        {
            for (const ElementSlot* slot = state.Find(element); slot != nullptr; slot = state.Find(slot->parent))
            {
                if (element == ancestor)
                {
                    return true;
                }
                element = slot->parent;
            }
            return false;
        }

        /** @brief `element` (when `withSelf`) and its ancestors, nearest first. */
        std::vector<ElementId> Chain(const UIRootState& state, ElementId element, bool withSelf)
        {
            std::vector<ElementId> chain;
            const ElementSlot* slot = state.Find(element);
            if (slot == nullptr)
            {
                return chain;
            }
            if (withSelf)
            {
                chain.push_back(element);
            }
            for (ElementId above = slot->parent; state.Find(above) != nullptr; above = state.Find(above)->parent)
            {
                chain.push_back(above);
            }
            return chain;
        }

        /** @brief Moves `pseudoClass` from the elements of `chain` to those of `next`, and keeps `next`. */
        void MoveChain(UIRoot& root, std::vector<ElementId>& chain, std::vector<ElementId> next,
                       PseudoClass pseudoClass)
        {
            for (const ElementId element : chain)
            {
                if (std::find(next.begin(), next.end(), element) == next.end())
                {
                    root.SetPseudoClass(element, pseudoClass, false);
                }
            }
            for (const ElementId element : next)
            {
                root.SetPseudoClass(element, pseudoClass, true);
            }
            chain = std::move(next);
        }

        ElementId FirstPressed(const UIRootInput& input)
        {
            for (const ElementId element : input.pressed)
            {
                if (element.IsValid())
                {
                    return element;
                }
            }
            return {};
        }

        void PublishClaims(UIRootState& state)
        {
            UIRootInput& input = state.input;
            const ElementSlot* focused = state.Find(input.focused);
            // Only while it navigates the focus or drives the cursor does the UI want the pad: a HUD root never claims
            // it.
            if (const bool gamepad = focused != nullptr || input.cursor.on;
                input.routerId.value != 0 && gamepad != input.gamepadPublished)
            {
                input.gamepadPublished = gamepad;
                Input::SetGamepadClaim(state.app, input.routerId, gamepad);
            }
            WindowInputClaims claims;
            claims.pointer = state.Find(input.hovered) != nullptr;
            claims.pointerCapture = state.Find(FirstPressed(input)) != nullptr;
            claims.keyboardFocus = focused != nullptr;
            // A focused element that takes text, a text entry, has the window take it, with the IME at its caret.
            if (const std::optional<Rect2D> caret =
                    focused != nullptr ? focused->element->GetTextCaret() : std::nullopt)
            {
                claims.textInput = true;
                claims.caret.position = Math::Vec2(caret->x, caret->y) * state.scale;
                claims.caret.size = Math::Vec2(caret->width, caret->height) * state.scale;
            }
            if (input.routerId.value == 0 || claims == input.published)
            {
                return;
            }
            input.published = claims;
            Input::SetInputClaims(state.app, input.routerId, state.config.window, claims);
        }

        /** @brief The nearest focusable element at or above `element`; nothing when none is. */
        ElementId FocusableAt(const UIRootState& state, ElementId element)
        {
            for (const ElementSlot* slot = state.Find(element); slot != nullptr; slot = state.Find(slot->parent))
            {
                if (slot->focusable)
                {
                    return element;
                }
                element = slot->parent;
            }
            return {};
        }

        /** @brief Every focusable element that is displayed, in tree order. */
        std::vector<ElementId> FocusOrder(const UIRootState& state)
        {
            std::vector<ElementId> order;
            const auto walk = [&](const auto& self, ElementId id) -> void {
                const ElementSlot& slot = state.slots[id.index];
                if (slot.resolved.display == Display::None)
                {
                    return;
                }
                if (slot.focusable)
                {
                    order.push_back(id);
                }
                for (const ElementId child : slot.children)
                {
                    self(self, child);
                }
            };
            walk(walk, state.rootElement);
            return order;
        }

        /** @brief A UIEvent of `kind` at `target`, with the pointer where it last was. */
        UIEvent MakeEvent(const UIRootState& state, UIEventKind kind, ElementId target)
        {
            UIEvent event;
            event.target = target;
            event.kind = kind;
            event.position = state.input.pointer.value_or(Math::Vec2(0.0f));
            return event;
        }

        /** @brief Offers the input to `element`, then to each ancestor in turn, until one takes it. */
        RouteResult ElementInput(UIRootState& state, ElementId element, const RoutedInput& input)
        {
            for (ElementSlot* slot = state.Find(element); slot != nullptr; slot = state.Find(element))
            {
                // Read first: the element may change the tree, and the slots with it.
                const ElementId parent = slot->parent;
                if (slot->element->OnInput(state.app, input) == RouteResult::Consumed)
                {
                    return RouteResult::Consumed;
                }
                element = parent;
            }
            return RouteResult::Passed;
        }

        /** @brief The D-pad button's direction as a step of one: x right, y down. */
        Math::Vec2 DirectionOf(GamepadButton button)
        {
            switch (button)
            {
            case GamepadButton::DPadUp:
                return {0.0f, -1.0f};
            case GamepadButton::DPadDown:
                return {0.0f, 1.0f};
            case GamepadButton::DPadLeft:
                return {-1.0f, 0.0f};
            case GamepadButton::DPadRight:
                return {1.0f, 0.0f};
            default:
                return {0.0f, 0.0f};
            }
        }

        /** @brief The way the stick leans past the navigation dead zone, its larger axis winning; none inside it. */
        std::optional<GamepadButton> StickDirection(Math::Vec2 stick)
        {
            const bool across = std::abs(stick.x) >= std::abs(stick.y);
            const float lean = across ? stick.x : stick.y;
            if (std::abs(lean) <= UIRoot::NavigationDeadZone)
            {
                return std::nullopt;
            }
            if (across)
            {
                return lean > 0.0f ? GamepadButton::DPadRight : GamepadButton::DPadLeft;
            }
            return lean > 0.0f ? GamepadButton::DPadDown : GamepadButton::DPadUp;
        }

        /** @brief A rectangle's extent on one axis. */
        struct Span
        {
            float start = 0.0f;
            float end = 0.0f;
        };

        /**
         * @brief The focusable element nearest `from` in `direction`, by UIRoot's "Input and focus" rule: past its far
         * edge, by distance along, gap across and overlap across. Nothing when none lies that way.
         */
        ElementId Nearest(const UIRootState& state, ElementId from, GamepadButton direction)
        {
            const Math::Vec2 step = DirectionOf(direction);
            const bool horizontal = step.x != 0.0f;
            const float sign = horizontal ? step.x : step.y;
            // Along the direction, measured so that further that way is larger; across it, as laid out.
            const auto along = [&](const Rect2D& box) {
                const float start = horizontal ? box.x : box.y;
                const float end = start + (horizontal ? box.width : box.height);
                return sign > 0.0f ? Span{start, end} : Span{-end, -start};
            };
            const auto across = [&](const Rect2D& box) {
                const float start = horizontal ? box.y : box.x;
                return Span{start, start + (horizontal ? box.height : box.width)};
            };
            const Rect2D& origin = state.Find(from)->boxes.border;
            const Span fromAlong = along(origin);
            const Span fromAcross = across(origin);

            const std::vector<ElementId> order = FocusOrder(state);
            // An element with focusable elements under it, such as a scroll view round a list, is passed over for them.
            const auto holdsFocusable = [&](ElementId element) {
                return std::any_of(order.begin(), order.end(), [&](ElementId other) {
                    return other != element && IsWithin(state, other, element);
                });
            };
            ElementId best;
            float bestScore = std::numeric_limits<float>::max();
            for (const ElementId candidate : order)
            {
                if (candidate == from)
                {
                    continue;
                }
                const Rect2D& box = state.slots[candidate.index].boxes.border;
                const Span toAlong = along(box);
                if ((toAlong.start + toAlong.end) * 0.5f <= fromAlong.end || holdsFocusable(candidate))
                {
                    continue;
                }
                const Span toAcross = across(box);
                const float distance = std::max(0.0f, toAlong.start - fromAlong.end);
                const float overlap =
                    std::max(0.0f, std::min(fromAcross.end, toAcross.end) - std::max(fromAcross.start, toAcross.start));
                const float gap =
                    std::max(0.0f, std::max(fromAcross.start, toAcross.start) - std::min(fromAcross.end, toAcross.end));
                const float score = distance + 2.0f * gap - overlap;
                if (score < bestScore)
                {
                    bestScore = score;
                    best = candidate;
                }
            }
            return best;
        }

        /** @brief How far a view showing `viewSize` from `viewStart` must scroll to show `size` from `start`. */
        float Uncover(float start, float size, float viewStart, float viewSize)
        {
            if (start < viewStart || size > viewSize)
            {
                return start - viewStart;
            }
            if (start + size > viewStart + viewSize)
            {
                return start + size - (viewStart + viewSize);
            }
            return 0.0f;
        }

        /** @brief Scrolls each scroll view above `element`, nearest first, just far enough to show its border box. */
        void Reveal(UIRoot& root, const UIRootState& state, ElementId element)
        {
            const ElementSlot* slot = state.Find(element);
            if (slot == nullptr)
            {
                return;
            }
            Rect2D box = slot->boxes.border;
            ElementId above = slot->parent;
            while (const ElementSlot* view = state.Find(above))
            {
                if (view->resolved.overflow == Overflow::Scroll)
                {
                    const Rect2D& inside = view->boxes.padding;
                    const Math::Vec2 before = root.GetScrollOffset(above);
                    const Math::Vec2 delta(Uncover(box.x, box.width, inside.x, inside.width),
                                           Uncover(box.y, box.height, inside.y, inside.height));
                    if (delta.x != 0.0f || delta.y != 0.0f)
                    {
                        root.SetScrollOffset(above, before + delta);
                        // The offset is clamped to the view's range; the box moves by what it really scrolled.
                        const Math::Vec2 moved = root.GetScrollOffset(above) - before;
                        box.x -= moved.x;
                        box.y -= moved.y;
                    }
                }
                above = view->parent;
            }
        }

        /** @brief Puts the virtual cursor's element at the pointer. */
        void PlaceCursor(UIRoot& root, const UIRootState& state)
        {
            const UIRootInput& input = state.input;
            if (state.Find(input.cursor.element) == nullptr || !input.pointer)
            {
                return;
            }
            if (StyleDeclarations* style = root.EditInlineStyle(input.cursor.element))
            {
                style->SetLeft(Length::Px(input.pointer->x)).SetTop(Length::Px(input.pointer->y));
            }
        }

        /** @brief Whether `element` or an element above it carries :disabled. */
        bool IsDisabled(const UIRootState& state, ElementId element)
        {
            constexpr auto disabled = static_cast<std::uint16_t>(PseudoClass::Disabled);
            for (const ElementSlot* slot = state.Find(element); slot != nullptr; slot = state.Find(slot->parent))
            {
                if ((slot->pseudoClasses & disabled) != 0)
                {
                    return true;
                }
            }
            return false;
        }
    } // namespace

    /** @brief A root's input router: FenrirInput gives it each input before the game. */
    class RootRouter final : public InputRouter, public std::enable_shared_from_this<RootRouter>
    {
      public:
        RootRouter(UIRoot& root, UIRootState& state) : m_root(&root), m_state(&state)
        {
        }

        /** @brief Cuts it from its root, which is going: it passes everything from here on. */
        void Detach() noexcept
        {
            m_root = nullptr;
            m_state = nullptr;
        }

        void BeginDispatch(App& app) override
        {
            if (m_state == nullptr)
            {
                return;
            }
            // A listener that destroys the root drops the root's and FenrirInput's hold on this router.
            const std::shared_ptr<RootRouter> keep = shared_from_this();
            m_state->input.clock += app.GetTime().deltaTime;
            Repeat();
            if (m_state != nullptr)
            {
                MoveCursor(app.GetTime().deltaTime);
            }
            // The layout may have moved under a still pointer since the last input.
            if (m_state != nullptr && m_state->input.pointer)
            {
                Hover(HitTest(*m_state, *m_state->input.pointer), false);
            }
            if (m_state != nullptr)
            {
                PublishClaims(*m_state);
            }
        }

        RouteResult Route(App& app, const RoutedInput& input) override
        {
            (void)app;
            if (m_state == nullptr)
            {
                return RouteResult::Passed;
            }
            // A listener that destroys the root drops the root's and FenrirInput's hold on this router mid-route.
            const std::shared_ptr<RootRouter> keep = shared_from_this();
            const RouteResult result = std::visit(
                [&](const auto& event) -> RouteResult {
                    using Event = std::decay_t<decltype(event)>;
                    if constexpr (std::is_same_v<Event, MouseMotionEvent>)
                    {
                        return Motion(event, input);
                    }
                    else if constexpr (std::is_same_v<Event, MouseButtonEvent>)
                    {
                        return Button(event, input);
                    }
                    else if constexpr (std::is_same_v<Event, MouseScrollEvent>)
                    {
                        return Scroll(event, input);
                    }
                    else if constexpr (std::is_same_v<Event, KeyEvent>)
                    {
                        return KeyInput(event, input);
                    }
                    else if constexpr (std::is_same_v<Event, TextEvent>)
                    {
                        return Text(event, input);
                    }
                    else if constexpr (std::is_same_v<Event, GamepadButtonEvent>)
                    {
                        return GamepadButtonInput(event);
                    }
                    else if constexpr (std::is_same_v<Event, GamepadAxisEvent>)
                    {
                        return GamepadAxisInput(event);
                    }
                    else
                    {
                        return RouteResult::Passed;
                    }
                },
                input);
            // A listener may have destroyed the root.
            if (m_state != nullptr)
            {
                PublishClaims(*m_state);
            }
            return result;
        }

      private:
        [[nodiscard]] bool Ours(WindowId window) const
        {
            return window == m_state->config.window;
        }

        void Send(const UIEvent& event)
        {
            SendUIEvent(*m_state, event);
        }

        void Send(UIEventKind kind, ElementId target)
        {
            Send(MakeEvent(*m_state, kind, target));
        }

        /** @brief Makes `hit` the hovered element, sending leave and enter, or a move over it when it stays. */
        void Hover(ElementId hit, bool moved)
        {
            UIRootInput& input = m_state->input;
            if (hit == input.hovered)
            {
                if (moved && m_state->Find(hit) != nullptr)
                {
                    Send(UIEventKind::PointerMove, hit);
                }
                return;
            }
            const ElementId left = input.hovered;
            input.hovered = hit;
            MoveChain(*m_root, input.hoverChain, Chain(*m_state, hit, true), PseudoClass::Hover);
            if (m_state->Find(left) != nullptr)
            {
                Send(UIEventKind::PointerLeave, left);
            }
            if (m_state != nullptr && m_state->Find(hit) != nullptr)
            {
                Send(UIEventKind::PointerEnter, hit);
            }
        }

        RouteResult Motion(const MouseMotionEvent& event, const RoutedInput& routed)
        {
            if (!Ours(event.window))
            {
                return RouteResult::Passed;
            }
            m_state->input.pointer = event.position / m_state->scale;
            PlaceCursor(*m_root, *m_state);
            // A pressed element follows the pointer wherever it goes, so a drag that leaves it keeps reaching it.
            const ElementId pressed = FirstPressed(m_state->input);
            (void)ElementInput(*m_state, pressed.IsValid() ? pressed : HitTest(*m_state, *m_state->input.pointer),
                               routed);
            if (m_state == nullptr)
            {
                return RouteResult::Passed;
            }
            Hover(HitTest(*m_state, *m_state->input.pointer), true);
            // Motion is no one's (ADR-0027): the game's Mouse keeps the pointer's place; the pointer claim says it is
            // over the UI.
            return RouteResult::Passed;
        }

        RouteResult Button(const MouseButtonEvent& event, const RoutedInput& routed)
        {
            UIRootInput& input = m_state->input;
            const auto slot = static_cast<std::size_t>(event.button);
            if (!event.pressed)
            {
                // Only a press this router consumed brings its release here.
                const ElementId pressed = input.pressed[slot];
                input.pressed[slot] = {};
                const bool taken = input.pressTaken;
                if (!FirstPressed(input).IsValid())
                {
                    input.pressTaken = false;
                }
                MoveChain(*m_root, input.activeChain, Chain(*m_state, FirstPressed(input), true), PseudoClass::Active);
                if (m_state->Find(pressed) == nullptr)
                {
                    return RouteResult::Consumed;
                }
                (void)ElementInput(*m_state, pressed, routed);
                UIEvent release = MakeEvent(*m_state, UIEventKind::PointerRelease, pressed);
                release.button = event.button;
                Send(release);
                if (!event.cancelled && !taken && m_state != nullptr && input.pointer &&
                    !IsDisabled(*m_state, pressed) && IsWithin(*m_state, HitTest(*m_state, *input.pointer), pressed))
                {
                    Click(pressed, event.button);
                }
                return RouteResult::Consumed;
            }
            if (!Ours(event.window))
            {
                return RouteResult::Passed;
            }
            const ElementId hit = input.pointer ? HitTest(*m_state, *input.pointer) : ElementId{};
            if (!hit.IsValid())
            {
                // A press outside the UI goes to the game, and takes the focus with it.
                Blur(*m_root, *m_state);
                return RouteResult::Passed;
            }
            if (IsDisabled(*m_state, hit))
            {
                // A disabled control is still the UI's, so the game does not see the press, but it does nothing.
                return RouteResult::Consumed;
            }
            // A click on this root is a click outside every other root on the window: they lose the focus.
            for (UIRoot* other : m_state->roots.GetRoots())
            {
                if (other != m_root && other->GetConfig().window == event.window)
                {
                    other->Blur();
                }
            }
            input.pressed[slot] = hit;
            MoveChain(*m_root, input.activeChain, Chain(*m_state, hit, true), PseudoClass::Active);
            const RouteResult taken = ElementInput(*m_state, hit, routed);
            UIEvent press = MakeEvent(*m_state, UIEventKind::PointerPress, hit);
            press.button = event.button;
            Send(press);
            // An element that took the press itself keeps the focus where it was.
            if (taken != RouteResult::Consumed && m_state != nullptr)
            {
                const ElementId focusable = FocusableAt(*m_state, hit);
                if (focusable.IsValid())
                {
                    (void)Focus(*m_root, *m_state, focusable);
                }
                else
                {
                    Blur(*m_root, *m_state);
                }
            }
            return RouteResult::Consumed;
        }

        void Click(ElementId element, MouseButton button)
        {
            UIRootInput& input = m_state->input;
            const Math::Vec2 position = input.pointer.value_or(Math::Vec2(0.0f));
            const auto& last = input.lastClick;
            const Math::Vec2 moved = position - last.position;
            const bool second = last.armed && last.element == element && last.button == button &&
                                input.clock - last.time <= doubleClickSeconds &&
                                moved.x * moved.x + moved.y * moved.y <= doubleClickDistance * doubleClickDistance;
            // A third click starts over rather than making a second double click.
            input.lastClick = {element, button, input.clock, position, !second};
            UIEvent click = MakeEvent(*m_state, UIEventKind::Click, element);
            click.button = button;
            click.integer = second ? 2 : 1;
            Send(click);
            if (second && m_state != nullptr)
            {
                click.kind = UIEventKind::DoubleClick;
                Send(click);
            }
        }

        RouteResult Scroll(const MouseScrollEvent& event, const RoutedInput& routed)
        {
            if (!Ours(event.window) || !m_state->input.pointer)
            {
                return RouteResult::Passed;
            }
            const ElementId hit = HitTest(*m_state, *m_state->input.pointer);
            if (!hit.IsValid())
            {
                return RouteResult::Passed;
            }
            // A disabled element and what is under it are passed over, to what holds them.
            ElementId above = hit;
            while (IsDisabled(*m_state, above))
            {
                above = m_state->Find(above)->parent;
            }
            if (above != hit)
            {
                (void)ElementInput(*m_state, above, routed);
                return RouteResult::Consumed;
            }
            (void)ElementInput(*m_state, hit, routed);
            UIEvent scroll = MakeEvent(*m_state, UIEventKind::Scroll, hit);
            scroll.value = event.notches.y;
            Send(scroll);
            return RouteResult::Consumed;
        }

        RouteResult KeyInput(const KeyEvent& event, const RoutedInput& routed)
        {
            const ElementId focused = m_state->input.focused;
            auto& presses = m_state->input.keyPresses;
            if (!event.pressed)
            {
                // Only a press this router consumed brings its release here, and it goes where the press went: Tab's
                // press moves the focus, and its release is not the new element's.
                const auto press = std::find_if(presses.begin(), presses.end(),
                                                [&](const auto& entry) { return entry.first == event.scancode; });
                const ElementId pressed = press != presses.end() ? press->second : ElementId{};
                if (press != presses.end())
                {
                    presses.erase(press);
                }
                if (m_state->Find(pressed) != nullptr)
                {
                    (void)ElementInput(*m_state, pressed, routed);
                    UIEvent release = MakeEvent(*m_state, UIEventKind::KeyRelease, pressed);
                    release.key = event.key;
                    release.integer = static_cast<std::int32_t>(event.modifiers);
                    Send(release);
                }
                return RouteResult::Consumed;
            }
            if (!Ours(event.window) || m_state->Find(focused) == nullptr)
            {
                return RouteResult::Passed;
            }
            if (!event.repeat)
            {
                presses.emplace_back(event.scancode, focused);
            }
            const RouteResult taken = ElementInput(*m_state, focused, routed);
            UIEvent press = MakeEvent(*m_state, UIEventKind::KeyPress, focused);
            press.key = event.key;
            press.value = event.repeat ? 1.0f : 0.0f;
            press.integer = static_cast<std::int32_t>(event.modifiers);
            Send(press);
            // Tab is the root's default action, unless the focused element took the key itself.
            if (taken != RouteResult::Consumed && event.key == Key::Tab && m_state != nullptr)
            {
                MoveFocus((event.modifiers & KeyModifiers::Shift) != KeyModifiers::None);
            }
            // While an element has the focus the root has the keyboard (ADR-0065).
            return RouteResult::Consumed;
        }

        RouteResult Text(const TextEvent& event, const RoutedInput& routed)
        {
            const ElementId focused = m_state->input.focused;
            if (!Ours(event.window) || m_state->Find(focused) == nullptr)
            {
                return RouteResult::Passed;
            }
            (void)ElementInput(*m_state, focused, routed);
            UIEvent text = MakeEvent(*m_state, UIEventKind::TextInput, focused);
            text.integer = static_cast<std::int32_t>(event.codepoint);
            Send(text);
            return RouteResult::Consumed;
        }

        /** @brief Tab or Shift+Tab: the next or previous focusable element in tree order, wrapping. */
        void MoveFocus(bool backwards)
        {
            const std::vector<ElementId> order = FocusOrder(*m_state);
            if (order.empty())
            {
                return;
            }
            const auto at = std::find(order.begin(), order.end(), m_state->input.focused);
            const std::size_t count = order.size();
            std::size_t next = 0;
            if (at == order.end())
            {
                next = backwards ? count - 1 : 0;
            }
            else
            {
                const auto index = static_cast<std::size_t>(at - order.begin());
                next = backwards ? (index + count - 1) % count : (index + 1) % count;
            }
            if (Focus(*m_root, *m_state, order[next]) && m_state != nullptr)
            {
                Reveal(*m_root, *m_state, order[next]);
            }
        }

        RouteResult GamepadButtonInput(const GamepadButtonEvent& event)
        {
            UIRootInput& input = m_state->input;
            if (!event.pressed)
            {
                // Only a press this router consumed brings its release here.
                if (input.held.direction == event.button && !input.held.fromStick && input.held.pad == event.gamepad)
                {
                    input.held.direction.reset();
                }
                if (event.button == GamepadButton::South && input.cursor.pressed)
                {
                    input.cursor.pressed = false;
                    const MouseButtonEvent release{m_state->config.window, MouseButton::Left, false, false};
                    (void)Button(release, release);
                }
                return RouteResult::Consumed;
            }
            const ElementId focused = input.focused;
            const bool hasFocus = m_state->Find(focused) != nullptr;
            if (event.button == GamepadButton::South)
            {
                if (input.cursor.on)
                {
                    // The mouse's held buttons keep their elements: A presses nothing while one is down.
                    if (FirstPressed(input).IsValid())
                    {
                        return RouteResult::Consumed;
                    }
                    // The cursor's press is the mouse's: it goes to the game when there is no UI under the cursor.
                    const MouseButtonEvent press{m_state->config.window, MouseButton::Left, true, false};
                    const RouteResult result = Button(press, press);
                    if (result == RouteResult::Consumed && m_state != nullptr)
                    {
                        m_state->input.cursor.pressed = true;
                    }
                    return result;
                }
                if (!hasFocus)
                {
                    return RouteResult::Passed;
                }
                const KeyEvent enter{m_state->config.window, Key::Enter, Scancode::Enter, KeyModifiers::None, true};
                (void)ElementInput(*m_state, focused, enter);
                return RouteResult::Consumed;
            }
            if (event.button == GamepadButton::East)
            {
                if (!hasFocus)
                {
                    return RouteResult::Passed;
                }
                Send(UIEventKind::Cancel, focused);
                return RouteResult::Consumed;
            }
            if (const Math::Vec2 step = DirectionOf(event.button);
                (step.x != 0.0f || step.y != 0.0f) && Navigate(event.gamepad, event.button))
            {
                Hold(event.gamepad, event.button, false);
                return RouteResult::Consumed;
            }
            return RouteResult::Passed;
        }

        RouteResult GamepadAxisInput(const GamepadAxisEvent& event)
        {
            UIRootInput& input = m_state->input;
            if (event.axis != GamepadAxis::LeftX && event.axis != GamepadAxis::LeftY)
            {
                return RouteResult::Passed;
            }
            (event.axis == GamepadAxis::LeftX ? input.stick.x : input.stick.y) = event.value;
            if (input.cursor.on)
            {
                // BeginDispatch moves the cursor by the stick each frame.
                return RouteResult::Consumed;
            }
            const std::optional<GamepadButton> direction = StickDirection(input.stick);
            if (direction != input.stickDirection)
            {
                input.stickDirection = direction;
                if (input.held.fromStick)
                {
                    input.held.direction.reset();
                }
                if (direction && Navigate(event.gamepad, *direction))
                {
                    Hold(event.gamepad, *direction, true);
                }
            }
            return m_state != nullptr && m_state->Find(m_state->input.focused) != nullptr ? RouteResult::Consumed
                                                                                          : RouteResult::Passed;
        }

        /**
         * @brief A gamepad direction: offered to the focused element, then the focus moves the nearest way; with no
         * focus anywhere, the first focusable element takes it. False, doing nothing, when this root has no focus to
         * move and does not take it.
         */
        bool Navigate(GamepadId pad, GamepadButton direction)
        {
            const ElementSlot* focused = m_state->Find(m_state->input.focused);
            if (focused == nullptr)
            {
                for (const UIRoot* other : m_state->roots.GetRoots())
                {
                    if (other->GetFocused().IsValid())
                    {
                        return false;
                    }
                }
                const std::vector<ElementId> order = FocusOrder(*m_state);
                if (order.empty())
                {
                    return false;
                }
                if (Focus(*m_root, *m_state, order.front()) && m_state != nullptr)
                {
                    Reveal(*m_root, *m_state, order.front());
                }
                return true;
            }
            // The focused element alone, not its ancestors: a scroll view round a list would take every direction.
            const RoutedInput offer = GamepadButtonEvent{pad, direction, true};
            if (focused->element->OnInput(m_state->app, offer) == RouteResult::Consumed || m_state == nullptr)
            {
                return true;
            }
            // The element may have given the focus away, or removed itself.
            if (m_state->Find(m_state->input.focused) == nullptr)
            {
                return true;
            }
            const ElementId next = Nearest(*m_state, m_state->input.focused, direction);
            if (next.IsValid() && Focus(*m_root, *m_state, next) && m_state != nullptr)
            {
                Reveal(*m_root, *m_state, next);
            }
            return true;
        }

        /** @brief Starts `direction` repeating while it is held. */
        void Hold(GamepadId pad, GamepadButton direction, bool fromStick)
        {
            if (m_state == nullptr)
            {
                return;
            }
            UIRootInput& input = m_state->input;
            input.held.direction = direction;
            input.held.pad = pad;
            input.held.fromStick = fromStick;
            input.held.next = input.clock + UIRoot::NavigationRepeatDelay;
        }

        /** @brief Moves the focus again for a direction held past its repeat time, once a frame at most. */
        void Repeat()
        {
            auto& held = m_state->input.held;
            if (!held.direction)
            {
                return;
            }
            if (m_state->Find(m_state->input.focused) == nullptr)
            {
                held.direction.reset();
                return;
            }
            if (m_state->input.clock < held.next)
            {
                return;
            }
            // A long frame moves once and does not leave a backlog of repeats.
            held.next = std::max(held.next, m_state->input.clock) + UIRoot::NavigationRepeatInterval;
            (void)Navigate(held.pad, *held.direction);
        }

        /** @brief Moves the virtual cursor by the stick over `seconds`, as mouse motion. */
        void MoveCursor(double seconds)
        {
            const UIRootInput& input = m_state->input;
            if (!input.cursor.on || !input.pointer || m_state->scale <= 0.0f)
            {
                return;
            }
            const float lean = std::sqrt(input.stick.x * input.stick.x + input.stick.y * input.stick.y);
            if (lean <= UIRoot::VirtualCursorDeadZone)
            {
                return;
            }
            // Past the dead zone the speed rises from nothing, so a light lean moves it slowly.
            const float strength =
                std::min(1.0f, (lean - UIRoot::VirtualCursorDeadZone) / (1.0f - UIRoot::VirtualCursorDeadZone));
            const Math::Vec2 step =
                input.stick * (strength / lean * m_state->config.virtualCursorSpeed * static_cast<float>(seconds));
            const Math::Vec2 bounds = m_state->size.logicalSize / m_state->scale;
            const Math::Vec2 from = *input.pointer;
            const Math::Vec2 to(std::clamp(from.x + step.x, 0.0f, bounds.x),
                                std::clamp(from.y + step.y, 0.0f, bounds.y));
            if (to.x == from.x && to.y == from.y)
            {
                return;
            }
            const MouseMotionEvent motion{m_state->config.window, to * m_state->scale, (to - from) * m_state->scale};
            (void)Motion(motion, motion);
        }

        UIRoot* m_root;
        UIRootState* m_state;
    };

    void StartInput(UIRoot& root, UIRootState& state)
    {
        UIRootInput& input = state.input;
        if (input.routerId.value != 0 || state.app.TryGetResource<InputFeed>() == nullptr)
        {
            return;
        }
        if (input.router == nullptr)
        {
            input.router = std::make_shared<RootRouter>(root, state);
        }
        input.routerId = Input::AddInputRouter(state.app, state.config.inputPriority, input.router);
    }

    void StopInput(UIRootState& state, bool detach)
    {
        UIRootInput& input = state.input;
        if (detach && input.router != nullptr)
        {
            input.router->Detach();
        }
        if (input.routerId.value != 0)
        {
            Input::RemoveInputRouter(state.app, input.routerId);
            input.routerId = {};
            // FenrirInput clears a removed router's claims.
            input.gamepadPublished = false;
        }
    }

    void FrameInput(UIRoot& root, UIRootState& state, Math::Vec2 uiSize)
    {
        UIRootInput& input = state.input;
        if (!input.cursor.on)
        {
            return;
        }
        if (state.Find(input.cursor.element) == nullptr)
        {
            input.cursor.element = root.Create(state.rootElement, "virtual-cursor");
            if (StyleDeclarations* style = root.EditInlineStyle(input.cursor.element))
            {
                style->SetPosition(Position::Absolute)
                    .SetZIndex(std::numeric_limits<std::int32_t>::max())
                    .SetPointerEvents(PointerEvents::None);
            }
        }
        if (!input.pointer)
        {
            input.pointer = uiSize * 0.5f;
        }
        PlaceCursor(root, state);
    }

    void SetVirtualCursor(UIRoot& root, UIRootState& state, bool on)
    {
        UIRootInput& input = state.input;
        if (input.cursor.on == on)
        {
            return;
        }
        input.cursor.on = on;
        input.stickDirection.reset();
        if (!on)
        {
            const ElementId cursor = input.cursor.element;
            input.cursor.element = {};
            if (state.Find(cursor) != nullptr)
            {
                (void)root.Remove(cursor, RemoveMode::Immediate);
            }
        }
        PublishClaims(state);
    }

    void RefreshInput(UIRoot& root, UIRootState& state)
    {
        UIRootInput& input = state.input;
        const auto forget = [&](ElementId& element) {
            if (state.Find(element) == nullptr)
            {
                element = {};
            }
        };
        forget(input.hovered);
        forget(input.focused);
        for (ElementId& pressed : input.pressed)
        {
            forget(pressed);
        }
        MoveChain(root, input.hoverChain, Chain(state, input.hovered, true), PseudoClass::Hover);
        MoveChain(root, input.activeChain, Chain(state, FirstPressed(input), true), PseudoClass::Active);
        MoveChain(root, input.focusChain, Chain(state, input.focused, false), PseudoClass::FocusWithin);
        PublishClaims(state);
    }

    ElementId HitTest(const UIRootState& state, Math::Vec2 position)
    {
        return HitElement(state, state.rootElement, position);
    }

    bool Focus(UIRoot& root, UIRootState& state, ElementId element)
    {
        const ElementSlot* slot = state.Find(element);
        if (slot == nullptr || !slot->focusable)
        {
            return false;
        }
        UIRootInput& input = state.input;
        if (input.focused == element)
        {
            return true;
        }
        const ElementId blurred = input.focused;
        input.focused = element;
        root.SetPseudoClass(blurred, PseudoClass::Focus, false);
        root.SetPseudoClass(element, PseudoClass::Focus, true);
        MoveChain(root, input.focusChain, Chain(state, element, false), PseudoClass::FocusWithin);
        PublishClaims(state);
        if (state.Find(blurred) != nullptr)
        {
            SendUIEvent(state, MakeEvent(state, UIEventKind::Blur, blurred));
        }
        if (state.Find(element) != nullptr)
        {
            SendUIEvent(state, MakeEvent(state, UIEventKind::Focus, element));
        }
        return true;
    }

    void Blur(UIRoot& root, UIRootState& state)
    {
        UIRootInput& input = state.input;
        const ElementId blurred = input.focused;
        if (!blurred.IsValid())
        {
            return;
        }
        input.focused = {};
        root.SetPseudoClass(blurred, PseudoClass::Focus, false);
        MoveChain(root, input.focusChain, {}, PseudoClass::FocusWithin);
        PublishClaims(state);
        if (state.Find(blurred) != nullptr)
        {
            SendUIEvent(state, MakeEvent(state, UIEventKind::Blur, blurred));
        }
    }

    bool TakePress(UIRoot& root, UIRootState& state, ElementId element)
    {
        UIRootInput& input = state.input;
        if (state.Find(element) == nullptr || !FirstPressed(input).IsValid())
        {
            return false;
        }
        for (ElementId& pressed : input.pressed)
        {
            if (pressed.IsValid())
            {
                pressed = element;
            }
        }
        input.pressTaken = true;
        MoveChain(root, input.activeChain, Chain(state, element, true), PseudoClass::Active);
        PublishClaims(state);
        return true;
    }

    void SendUIEvent(UIRootState& state, UIEvent event)
    {
        if (state.Find(event.target) == nullptr)
        {
            return;
        }
        event.root = state.input.id;
        state.app.SendEvent(event);
        // The path is fixed when the event is sent: an element a listener moves or removes does not change where it
        // bubbles, and removed elements are passed over.
        const std::vector<ElementId> path = Chain(state, event.target, true);
        for (const ElementId element : path)
        {
            const ElementSlot* slot = state.Find(element);
            if (slot == nullptr)
            {
                continue;
            }
            std::vector<std::shared_ptr<UIListener>> listeners;
            for (const ListenerEntry& entry : slot->listeners)
            {
                if (entry.kind == event.kind)
                {
                    listeners.push_back(entry.listener);
                }
            }
            UIListenerContext context(event, element);
            for (const std::shared_ptr<UIListener>& listener : listeners)
            {
                (*listener)(context);
            }
            if (context.IsStopped())
            {
                return;
            }
        }
    }
} // namespace Fenrir::Detail

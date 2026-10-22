// Input routers, control owners and input claims (ADR-0027): the part of FenrirInput's dispatch between the input feed
// and the game.

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "FenrirApp/App.hpp"
#include "InputDispatch.hpp"

namespace Fenrir
{
    WindowInputClaims InputClaims::For(WindowId window) const
    {
        const auto found =
            std::find_if(m_windows.begin(), m_windows.end(), [window](const Entry& e) { return e.window == window; });
        return found == m_windows.end() ? WindowInputClaims{} : found->claims;
    }

    std::vector<WindowId> InputClaims::Windows() const
    {
        std::vector<WindowId> windows;
        windows.reserve(m_windows.size());
        for (const Entry& entry : m_windows)
        {
            windows.push_back(entry.window);
        }
        return windows;
    }

    namespace Detail
    {
        namespace
        {
            using Device = OwnedControl::Device;

            InputRouting* FindRouting(App& app)
            {
                const InstalledInput* installed = app.TryGetResource<InstalledInput>();
                if (installed == nullptr)
                {
                    return nullptr;
                }
                const std::shared_ptr<InputInstallation> installation = installed->installation.lock();
                // The owners keep the installation alive; the pointer stays good while one does.
                return installation == nullptr ? nullptr : &installation->routing;
            }

            RouterEntry* FindRouter(InputRouting& routing, InputRouterId id)
            {
                for (std::vector<RouterEntry>* list : {&routing.active, &routing.starting})
                {
                    for (RouterEntry& entry : *list)
                    {
                        if (entry.id == id && !entry.removed)
                        {
                            return &entry;
                        }
                    }
                }
                return nullptr;
            }

            // The control a key names: its scancode, else its key code, else none (no state, so never owned).
            std::optional<std::uint32_t> KeyControl(const KeyEvent& event)
            {
                constexpr std::uint32_t byKeyCode = 1u << 16;
                if (event.scancode != Scancode::Unknown)
                {
                    return static_cast<std::uint32_t>(event.scancode);
                }
                if (event.key != Key::Unknown)
                {
                    return byKeyCode | static_cast<std::uint32_t>(event.key);
                }
                return std::nullopt;
            }

            OwnedControl Control(Device device, std::uint32_t code, GamepadId gamepad, WindowId window,
                                 const FedInput& press)
            {
                OwnedControl control;
                control.device = device;
                control.code = code;
                control.gamepad = gamepad;
                control.window = window;
                control.press = press;
                return control;
            }

            bool SameControl(const OwnedControl& a, const OwnedControl& b)
            {
                return a.device == b.device && a.code == b.code &&
                       (a.device != Device::Gamepad || a.gamepad == b.gamepad);
            }

            std::vector<OwnedControl>::iterator FindControl(InputRouting& routing, const OwnedControl& control)
            {
                return std::find_if(routing.controls.begin(), routing.controls.end(),
                                    [&control](const OwnedControl& held) { return SameControl(held, control); });
            }

            WindowInputClaims ClaimsIn(const RouterEntry& entry, WindowId window)
            {
                for (const auto& [claimed, claims] : entry.claims)
                {
                    if (claimed == window)
                    {
                        return claims;
                    }
                }
                return {};
            }

            // Drops the routers removed during a dispatch, once no call into them is in progress.
            void Compact(InputRouting& routing)
            {
                std::erase_if(routing.active, [](const RouterEntry& entry) { return entry.removed; });
                std::erase_if(routing.starting, [](const RouterEntry& entry) { return entry.removed; });
            }

            // Starts the routers added since the last frame's dispatch, each after every router of its priority or
            // higher, so equal priorities keep registration order.
            void Start(InputRouting& routing)
            {
                for (RouterEntry& entry : routing.starting)
                {
                    const auto after =
                        std::find_if(routing.active.begin(), routing.active.end(),
                                     [&entry](const RouterEntry& a) { return a.priority < entry.priority; });
                    routing.active.insert(after, std::move(entry));
                }
                routing.starting.clear();
            }

            // Clears `dispatching` however Run leaves.
            struct DispatchScope
            {
                InputRouting& routing;

                explicit DispatchScope(InputRouting& r) : routing(r)
                {
                    routing.dispatching = true;
                }
                DispatchScope(const DispatchScope&) = delete;
                DispatchScope& operator=(const DispatchScope&) = delete;
                ~DispatchScope()
                {
                    routing.dispatching = false;
                    Compact(routing);
                }
            };
        } // namespace

        void InputDispatch::Run(App& app)
        {
            InputRouting* routing = FindRouting(app);
            InputFeed* feed = app.TryGetResource<InputFeed>();
            if (routing == nullptr || feed == nullptr || routing->dispatching)
            {
                return;
            }
            const DispatchScope scope(*routing);

            const std::uint64_t frame = app.GetTime().frameCount;
            if (routing->begunFrame != frame)
            {
                routing->begunFrame = frame;
                Start(*routing);
                // By index: a router may add routers (they wait in `starting`) or remove them (they are flagged).
                for (std::size_t i = 0; i < routing->active.size(); ++i)
                {
                    if (!routing->active[i].removed)
                    {
                        const std::shared_ptr<InputRouter> router = routing->active[i].router;
                        router->BeginDispatch(app);
                    }
                }
            }

            // Taken first, so input fed while it is dispatched waits for the next dispatch.
            const std::vector<FedInput> fed = std::exchange(feed->m_queue, {});
            for (const FedInput& input : fed)
            {
                Route(app, *routing, input);
            }
        }

        void InputDispatch::Route(App& app, InputRouting& routing, const FedInput& input)
        {
            std::visit(
                [&](const auto& event) {
                    using Event = std::decay_t<decltype(event)>;
                    if constexpr (std::is_same_v<Event, KeyEvent>)
                    {
                        const std::optional<std::uint32_t> code = KeyControl(event);
                        if (!code.has_value())
                        {
                            if (!Offer(app, routing, input, event.window, false))
                            {
                                Pass(app, input);
                            }
                            return;
                        }
                        const OwnedControl control = Control(Device::Keyboard, *code, {}, event.window, input);
                        if (event.pressed && !event.repeat)
                        {
                            Press(app, routing, input, control, false);
                        }
                        else
                        {
                            Follow(app, routing, input, control, !event.pressed, false);
                        }
                    }
                    else if constexpr (std::is_same_v<Event, MouseButtonEvent>)
                    {
                        const OwnedControl control =
                            Control(Device::Mouse, static_cast<std::uint32_t>(event.button), {}, event.window, input);
                        if (event.pressed)
                        {
                            Press(app, routing, input, control, true);
                        }
                        else
                        {
                            Follow(app, routing, input, control, true, true);
                        }
                    }
                    else if constexpr (std::is_same_v<Event, GamepadButtonEvent>)
                    {
                        const OwnedControl control = Control(Device::Gamepad, static_cast<std::uint32_t>(event.button),
                                                             event.gamepad, {}, input);
                        if (event.pressed)
                        {
                            Press(app, routing, input, control, false);
                        }
                        else
                        {
                            Follow(app, routing, input, control, true, false);
                        }
                    }
                    else if constexpr (std::is_same_v<Event, MouseMotionEvent> ||
                                       std::is_same_v<Event, MouseScrollEvent>)
                    {
                        if (!Offer(app, routing, input, event.window, true))
                        {
                            Pass(app, input);
                        }
                    }
                    else if constexpr (std::is_same_v<Event, TextEvent>)
                    {
                        if (!Offer(app, routing, input, event.window, false))
                        {
                            Pass(app, input);
                        }
                    }
                    else if constexpr (std::is_same_v<Event, GamepadAxisEvent>)
                    {
                        if (!Offer(app, routing, input, {}, false))
                        {
                            Pass(app, input);
                        }
                    }
                    else if constexpr (std::is_same_v<Event, WindowFocusEvent>)
                    {
                        // Cancelled first, so the releases arrive before the focus loss itself.
                        if (!event.focused)
                        {
                            LoseFocus(app, routing, event.window);
                        }
                        Broadcast(app, routing, input);
                        Pass(app, input);
                    }
                    else if constexpr (std::is_same_v<Event, GamepadConnectionEvent>)
                    {
                        if (!event.connected)
                        {
                            // An unplugged pad holds nothing, for the game and routers alike.
                            std::erase_if(routing.controls, [&event](const OwnedControl& held) {
                                return held.device == Device::Gamepad && held.gamepad == event.gamepad;
                            });
                        }
                        Broadcast(app, routing, input);
                        Pass(app, input);
                    }
                },
                input);
        }

        std::optional<InputRouterId> InputDispatch::Offer(App& app, InputRouting& routing, const FedInput& input,
                                                          WindowId window, bool pointer)
        {
            const auto captures = [window](const RouterEntry& entry) {
                return !entry.removed && ClaimsIn(entry, window).pointerCapture;
            };
            const bool captured = pointer && std::any_of(routing.active.begin(), routing.active.end(), captures);

            // By index, and checked again before each call: a router may remove another, or itself.
            for (std::size_t i = 0; i < routing.active.size(); ++i)
            {
                const RouterEntry& entry = routing.active[i];
                if (entry.removed || (captured && !captures(entry)))
                {
                    continue;
                }
                const std::shared_ptr<InputRouter> router = entry.router;
                const InputRouterId id = entry.id;
                if (router->Route(app, input) == RouteResult::Consumed)
                {
                    return id;
                }
            }
            return std::nullopt;
        }

        void InputDispatch::Press(App& app, InputRouting& routing, const FedInput& input, OwnedControl control,
                                  bool pointer)
        {
            // A press while the control is still recorded (its release was lost, or it was cancelled and pressed
            // again) starts a new ownership.
            if (const auto held = FindControl(routing, control); held != routing.controls.end())
            {
                routing.controls.erase(held);
            }
            const std::optional<InputRouterId> consumer = Offer(app, routing, input, control.window, pointer);
            control.owner = consumer.value_or(InputRouterId{});
            // A router may have been removed while it consumed the press: the control is then over before it began.
            control.ended = consumer.has_value() && FindRouter(routing, *consumer) == nullptr;
            routing.controls.push_back(std::move(control));
            if (!consumer.has_value())
            {
                Pass(app, input);
            }
        }

        void InputDispatch::Follow(App& app, InputRouting& routing, const FedInput& input, const OwnedControl& control,
                                   bool release, bool pointer)
        {
            const auto held = FindControl(routing, control);
            if (held == routing.controls.end())
            {
                // No press was recorded (it came before the input was installed): dispatched like unowned input.
                if (!Offer(app, routing, input, control.window, pointer))
                {
                    Pass(app, input);
                }
                return;
            }
            const OwnedControl owned = *held;
            if (release)
            {
                routing.controls.erase(held);
            }
            if (owned.ended)
            {
                return;
            }
            if (owned.owner == InputRouterId{})
            {
                Pass(app, input);
            }
            else if (const RouterEntry* entry = FindRouter(routing, owned.owner))
            {
                const std::shared_ptr<InputRouter> router = entry->router;
                (void)router->Route(app, input);
            }
        }

        void InputDispatch::Cancel(App& app, InputRouting& routing, std::size_t index)
        {
            OwnedControl& control = routing.controls[index];
            control.ended = true;
            const InputRouterId owner = control.owner;
            FedInput release = control.press;
            std::visit(
                [](auto& event) {
                    using Event = std::decay_t<decltype(event)>;
                    if constexpr (std::is_same_v<Event, KeyEvent> || std::is_same_v<Event, MouseButtonEvent>)
                    {
                        event.pressed = false;
                        event.cancelled = true;
                        if constexpr (std::is_same_v<Event, KeyEvent>)
                        {
                            event.repeat = false;
                        }
                    }
                },
                release);

            if (owner == InputRouterId{})
            {
                Pass(app, release);
            }
            else if (const RouterEntry* entry = FindRouter(routing, owner))
            {
                const std::shared_ptr<InputRouter> router = entry->router;
                (void)router->Route(app, release);
            }
        }

        void InputDispatch::LoseFocus(App& app, InputRouting& routing, WindowId window)
        {
            // By index: an owner told of its cancelled release may change claims, which may cancel more, but nothing
            // here adds or erases a control.
            for (std::size_t i = 0; i < routing.controls.size(); ++i)
            {
                const OwnedControl& control = routing.controls[i];
                if (!control.ended && control.device != Device::Gamepad && control.window == window)
                {
                    Cancel(app, routing, i);
                }
            }
            for (std::vector<RouterEntry>* list : {&routing.active, &routing.starting})
            {
                for (RouterEntry& entry : *list)
                {
                    for (auto& [claimed, claims] : entry.claims)
                    {
                        if (claimed == window)
                        {
                            claims.pointerCapture = false;
                        }
                    }
                }
            }
            Publish(app, routing);
        }

        void InputDispatch::Broadcast(App& app, InputRouting& routing, const FedInput& input)
        {
            for (std::size_t i = 0; i < routing.active.size(); ++i)
            {
                if (!routing.active[i].removed)
                {
                    const std::shared_ptr<InputRouter> router = routing.active[i].router;
                    (void)router->Route(app, input);
                }
            }
        }

        void InputDispatch::Publish(App& app, const InputRouting& routing)
        {
            InputClaims* published = app.TryGetResource<InputClaims>();
            if (published == nullptr)
            {
                return;
            }
            // Every live router, highest priority first: the active ones are in that order, and a starting one was
            // registered after them all, so a stable sort keeps registration order among equals.
            std::vector<const RouterEntry*> routers;
            for (const std::vector<RouterEntry>* list : {&routing.active, &routing.starting})
            {
                for (const RouterEntry& entry : *list)
                {
                    if (!entry.removed)
                    {
                        routers.push_back(&entry);
                    }
                }
            }
            std::stable_sort(routers.begin(), routers.end(),
                             [](const RouterEntry* a, const RouterEntry* b) { return a->priority > b->priority; });

            published->m_windows.clear();
            published->m_gamepad = false;
            for (const RouterEntry* entry : routers)
            {
                published->m_gamepad = published->m_gamepad || entry->gamepadClaim;
                for (const auto& [window, claims] : entry->claims)
                {
                    auto found = std::find_if(published->m_windows.begin(), published->m_windows.end(),
                                              [window](const InputClaims::Entry& e) { return e.window == window; });
                    if (found == published->m_windows.end())
                    {
                        published->m_windows.push_back(InputClaims::Entry{window, {}});
                        found = std::prev(published->m_windows.end());
                    }
                    WindowInputClaims& sum = found->claims;
                    // The first textInput claimant is the highest-priority one: its caret is the window's.
                    if (claims.textInput && !sum.textInput)
                    {
                        sum.caret = claims.caret;
                    }
                    sum.pointer = sum.pointer || claims.pointer;
                    sum.pointerCapture = sum.pointerCapture || claims.pointerCapture;
                    sum.keyboardFocus = sum.keyboardFocus || claims.keyboardFocus;
                    sum.textInput = sum.textInput || claims.textInput;
                }
            }
            // A window nobody claims anything in is left out.
            std::erase_if(published->m_windows,
                          [](const InputClaims::Entry& e) { return e.claims == WindowInputClaims{}; });
        }

        InputRouterId InputDispatch::AddRouter(App& app, int priority, std::shared_ptr<InputRouter> router)
        {
            InputRouting* routing = FindRouting(app);
            if (routing == nullptr || router == nullptr)
            {
                return {};
            }
            RouterEntry entry;
            entry.id = InputRouterId{routing->nextId++};
            entry.priority = priority;
            entry.router = std::move(router);
            routing->starting.push_back(std::move(entry));
            return routing->starting.back().id;
        }

        void InputDispatch::RemoveRouter(App& app, InputRouterId id)
        {
            InputRouting* routing = FindRouting(app);
            RouterEntry* entry = routing == nullptr ? nullptr : FindRouter(*routing, id);
            if (entry == nullptr)
            {
                return;
            }
            entry->removed = true;
            entry->claims.clear();
            entry->gamepadClaim = false;
            // Its controls end silently; their physical releases go to no one.
            for (OwnedControl& control : routing->controls)
            {
                if (control.owner == id)
                {
                    control.ended = true;
                }
            }
            Publish(app, *routing);
            if (!routing->dispatching)
            {
                Compact(*routing);
            }
        }

        void InputDispatch::SetClaims(App& app, InputRouterId id, WindowId window, const WindowInputClaims& claims)
        {
            InputRouting* routing = FindRouting(app);
            RouterEntry* entry = routing == nullptr ? nullptr : FindRouter(*routing, id);
            if (entry == nullptr)
            {
                return;
            }
            const WindowInputClaims before = ClaimsIn(*entry, window);
            const auto found = std::find_if(entry->claims.begin(), entry->claims.end(),
                                            [window](const auto& claimed) { return claimed.first == window; });
            if (found == entry->claims.end())
            {
                entry->claims.emplace_back(window, claims);
            }
            else
            {
                found->second = claims;
            }
            Publish(app, *routing);

            const bool raisedFocus = claims.keyboardFocus && !before.keyboardFocus;
            const bool raisedCapture = claims.pointerCapture && !before.pointerCapture;
            for (std::size_t i = 0; i < routing->controls.size(); ++i)
            {
                const OwnedControl& control = routing->controls[i];
                const bool cancels = (raisedFocus && control.device == Device::Keyboard) ||
                                     (raisedCapture && control.device == Device::Mouse);
                if (cancels && !control.ended && control.owner == InputRouterId{} && control.window == window)
                {
                    Cancel(app, *routing, i);
                }
            }
        }

        void InputDispatch::SetGamepadClaim(App& app, InputRouterId id, bool claimed)
        {
            InputRouting* routing = FindRouting(app);
            RouterEntry* entry = routing == nullptr ? nullptr : FindRouter(*routing, id);
            if (entry != nullptr)
            {
                entry->gamepadClaim = claimed;
                Publish(app, *routing);
            }
        }
    } // namespace Detail
} // namespace Fenrir

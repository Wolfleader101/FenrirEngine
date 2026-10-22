#pragma once

#include <cstdint>
#include <variant>
#include <vector>

#include "FenrirInput/InputEvents.hpp"
#include "FenrirInput/InputTypes.hpp"
#include "FenrirMath/Math.hpp"

// Input routers, control owners and input claims (ADR-0027): a UI sees input before the game, consumes what it
// handles, owns the controls it takes and says what it is doing with input in each window.

namespace Fenrir
{
    class App;

    namespace Detail
    {
        class InputDispatch;
    } // namespace Detail

    /** @brief One input as a router sees it: the event it becomes if it reaches the game. */
    using RoutedInput = std::variant<KeyEvent, TextEvent, MouseButtonEvent, MouseMotionEvent, MouseScrollEvent,
                                     WindowFocusEvent, GamepadConnectionEvent, GamepadButtonEvent, GamepadAxisEvent>;

    /** @brief What an input router did with an input. */
    enum class RouteResult : std::uint8_t
    {
        /** @brief Not handled: it goes on to the next router, then the game. */
        Passed,
        /** @brief Handled: no lower router, resource or game event reader sees it. */
        Consumed,
    };

    /** @brief Names a registered input router; Input::AddInputRouter gives it. Zero names none. */
    struct InputRouterId
    {
        std::uint32_t value = 0;

        friend constexpr bool operator==(InputRouterId a, InputRouterId b) noexcept = default;
    };

    /**
     * @brief An input router (ADR-0027): a callback, such as a UI, that sees each input before the game and may consume
     * it. Register it with Input::AddInputRouter. Its calls come from FenrirInput's dispatch, a sequential PreUpdate
     * system, so on the thread that runs the App: a router may reach SDL's text input from them.
     *
     * What reaches Route:
     * - a press of a key, a mouse button or a gamepad button, unless a higher router consumed it. Consuming it makes
     *   this router the press's control owner: its repeats and its release then come to this router alone, whatever
     *   Route returns for them;
     * - motion, scroll, text and gamepad axes, which no one owns, each event on its own;
     * - focus changes and gamepad connections, which every router sees and which always reach the game too;
     * - a release marked `cancelled` for a control it owns, when its window loses the focus.
     */
    class InputRouter
    {
      public:
        InputRouter() = default;
        InputRouter(const InputRouter&) = delete;
        InputRouter& operator=(const InputRouter&) = delete;
        virtual ~InputRouter() = default;

        /**
         * @brief Called once a frame, before the frame's first input: on the frame's first dispatch, even when nothing
         * was fed. A UI can refresh its hit-test data or publish its input claims here.
         */
        virtual void BeginDispatch(App& app)
        {
            (void)app;
        }

        /** @brief Sees one input; returns Consumed to keep it from lower routers and the game. */
        virtual RouteResult Route(App& app, const RoutedInput& input) = 0;
    };

    /** @brief A rectangle in a window's logical coordinates, from its top-left corner (the mouse's units). */
    struct CaretRect
    {
        Math::Vec2 position{0.0f, 0.0f};
        Math::Vec2 size{0.0f, 0.0f};

        friend bool operator==(const CaretRect& a, const CaretRect& b) noexcept = default;
    };

    /**
     * @brief What a router is doing with input in one window (its input claims), or, in InputClaims, the union of
     * every router's. Claims describe the interaction; they never filter events.
     */
    struct WindowInputClaims
    {
        /** @brief The pointer is over the router's UI. */
        bool pointer = false;
        /**
         * @brief The router has captured the pointer (a drag that began on its UI): the window's pointer events go to
         * it first. Raising it cancels the game's held mouse buttons in the window.
         */
        bool pointerCapture = false;
        /** @brief The router has keyboard focus. Raising it cancels the game's held keys in the window. */
        bool keyboardFocus = false;
        /** @brief The router wants text: FenrirWindow keeps the window's text input on, with the caret below. */
        bool textInput = false;
        /** @brief Where text is being typed, for the IME: read only with `textInput`. */
        CaretRect caret;

        friend bool operator==(const WindowInputClaims& a, const WindowInputClaims& b) noexcept = default;
    };

    /**
     * @brief The union of the input claims every router publishes (ADR-0027): per window, each claim is raised if any
     * router raises it, and the caret is that of the highest-priority router claiming textInput; the gamepad claim is
     * app-wide. An app resource added by Input::Install, read by FenrirWindow and by game code; only FenrirInput
     * writes it.
     */
    class InputClaims
    {
      public:
        /** @brief The claims raised in `window`; none raised for a window no router claims anything in. */
        [[nodiscard]] WindowInputClaims For(WindowId window) const;

        /** @brief Whether any router claims the gamepads, for UI navigation. */
        [[nodiscard]] bool Gamepad() const noexcept
        {
            return m_gamepad;
        }

        /** @brief The windows any router claims something in. */
        [[nodiscard]] std::vector<WindowId> Windows() const;

      private:
        friend class Detail::InputDispatch;

        struct Entry
        {
            WindowId window;
            WindowInputClaims claims;
        };

        std::vector<Entry> m_windows;
        bool m_gamepad = false;
    };
} // namespace Fenrir

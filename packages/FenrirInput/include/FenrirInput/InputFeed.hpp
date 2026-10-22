#pragma once

#include <string_view>
#include <vector>

#include "FenrirInput/InputEvents.hpp"
#include "FenrirInput/InputRouter.hpp"
#include "FenrirInput/InputTypes.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir
{
    namespace Detail
    {
        class InputDispatch;

        /** @brief One input fed and not yet dispatched: the event it becomes. */
        using FedInput = RoutedInput;
    } // namespace Detail

    /**
     * @brief The input feed: FenrirInput's public entry point, through which raw input comes in (ADR-0017).
     * FenrirWindow passes on what its windowing library delivers, translated into FenrirInput's types; a test or a
     * headless app feeds input by hand. An app resource added by Input::Install.
     *
     * Each call queues one input. FenrirInput's dispatch, a sequential PreUpdate system, takes the queue in the order
     * it was fed: each input goes to the input routers, and what they pass updates Keyboard, Mouse and Gamepads and
     * is sent as its event. Input fed before the dispatch runs (between frames, or from an earlier sequential
     * PreUpdate system) is dispatched that frame. A feeder that runs after it, such as a window's pump, calls
     * Input::Dispatch once it has fed, so its input is dispatched that frame too; input fed later and not dispatched
     * waits for the next frame. Feed from the thread that runs the App, outside parallel systems.
     */
    class InputFeed
    {
      public:
        /** @brief `key` (at `scancode`) went down in `window`, or repeated while held when `repeat` is true. */
        void KeyDown(WindowId window, Key key, Scancode scancode, KeyModifiers modifiers = KeyModifiers::None,
                     bool repeat = false);
        /** @brief `key` (at `scancode`) went up in `window`. */
        void KeyUp(WindowId window, Key key, Scancode scancode, KeyModifiers modifiers = KeyModifiers::None);

        /** @brief Text was typed in `window`: one TextEvent per codepoint, in order. */
        void Text(WindowId window, std::u32string_view codepoints);

        /** @brief `button` went down in `window`. */
        void MouseButtonDown(WindowId window, MouseButton button);
        /** @brief `button` went up in `window`. */
        void MouseButtonUp(WindowId window, MouseButton button);
        /**
         * @brief The pointer moved to `position` in `window`'s logical coordinates (from its top-left corner), by
         * `motion` in the same units (raw motion while the cursor is captured).
         */
        void MouseMove(WindowId window, Math::Vec2 position, Math::Vec2 motion);
        /** @brief The wheel turned over `window` by `notches`: y positive away from the user, x to the right. */
        void Scroll(WindowId window, Math::Vec2 notches);

        /** @brief `window` gained the input focus. */
        void FocusGained(WindowId window);
        /** @brief `window` lost the input focus. */
        void FocusLost(WindowId window);

        /** @brief A gamepad was plugged in, under an id never given to another pad. */
        void GamepadConnected(GamepadId gamepad);
        /** @brief A gamepad was unplugged. */
        void GamepadDisconnected(GamepadId gamepad);
        /** @brief `button` went down on `gamepad`. */
        void GamepadButtonDown(GamepadId gamepad, GamepadButton button);
        /** @brief `button` went up on `gamepad`. */
        void GamepadButtonUp(GamepadId gamepad, GamepadButton button);
        /** @brief `axis` on `gamepad` moved to `value`: -1 to 1 for a stick, 0 to 1 for a trigger. */
        void GamepadAxisMoved(GamepadId gamepad, GamepadAxis axis, float value);

      private:
        friend class Detail::InputDispatch;

        std::vector<Detail::FedInput> m_queue;
    };
} // namespace Fenrir

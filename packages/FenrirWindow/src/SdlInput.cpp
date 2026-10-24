#include "SdlInput.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>

#include "FenrirMath/Math.hpp"
#include "WindowState.hpp"

namespace Fenrir::Detail
{
    namespace
    {
// The keys FenrirInput names (InputTypes.hpp), each with the name SDL gives it: SDL's key codes (SDLK_) and scancodes
// (SDL_SCANCODE_) use the same names.
// clang-format off
#define FENRIR_SDL_KEYS(ENTRY) \
    ENTRY(A, A) ENTRY(B, B) ENTRY(C, C) ENTRY(D, D) ENTRY(E, E) ENTRY(F, F) ENTRY(G, G) ENTRY(H, H) ENTRY(I, I) \
    ENTRY(J, J) ENTRY(K, K) ENTRY(L, L) ENTRY(M, M) ENTRY(N, N) ENTRY(O, O) ENTRY(P, P) ENTRY(Q, Q) ENTRY(R, R) \
    ENTRY(S, S) ENTRY(T, T) ENTRY(U, U) ENTRY(V, V) ENTRY(W, W) ENTRY(X, X) ENTRY(Y, Y) ENTRY(Z, Z) ENTRY(Num0, 0) \
    ENTRY(Num1, 1) ENTRY(Num2, 2) ENTRY(Num3, 3) ENTRY(Num4, 4) ENTRY(Num5, 5) ENTRY(Num6, 6) ENTRY(Num7, 7) \
    ENTRY(Num8, 8) ENTRY(Num9, 9) ENTRY(Escape, ESCAPE) ENTRY(Enter, RETURN) ENTRY(Tab, TAB) \
    ENTRY(Backspace, BACKSPACE) ENTRY(Space, SPACE) ENTRY(Minus, MINUS) ENTRY(Equals, EQUALS) \
    ENTRY(LeftBracket, LEFTBRACKET) ENTRY(RightBracket, RIGHTBRACKET) ENTRY(Backslash, BACKSLASH) \
    ENTRY(Semicolon, SEMICOLON) ENTRY(Apostrophe, APOSTROPHE) ENTRY(Grave, GRAVE) ENTRY(Comma, COMMA) \
    ENTRY(Period, PERIOD) ENTRY(Slash, SLASH) ENTRY(CapsLock, CAPSLOCK) ENTRY(F1, F1) ENTRY(F2, F2) ENTRY(F3, F3) \
    ENTRY(F4, F4) ENTRY(F5, F5) ENTRY(F6, F6) ENTRY(F7, F7) ENTRY(F8, F8) ENTRY(F9, F9) ENTRY(F10, F10) \
    ENTRY(F11, F11) ENTRY(F12, F12) ENTRY(F13, F13) ENTRY(F14, F14) ENTRY(F15, F15) ENTRY(F16, F16) ENTRY(F17, F17) \
    ENTRY(F18, F18) ENTRY(F19, F19) ENTRY(F20, F20) ENTRY(F21, F21) ENTRY(F22, F22) ENTRY(F23, F23) ENTRY(F24, F24) \
    ENTRY(PrintScreen, PRINTSCREEN) ENTRY(ScrollLock, SCROLLLOCK) ENTRY(Pause, PAUSE) ENTRY(Insert, INSERT) \
    ENTRY(Delete, DELETE) ENTRY(Home, HOME) ENTRY(End, END) ENTRY(PageUp, PAGEUP) ENTRY(PageDown, PAGEDOWN) \
    ENTRY(Left, LEFT) ENTRY(Right, RIGHT) ENTRY(Up, UP) ENTRY(Down, DOWN) ENTRY(NumLock, NUMLOCKCLEAR) \
    ENTRY(KeypadDivide, KP_DIVIDE) ENTRY(KeypadMultiply, KP_MULTIPLY) ENTRY(KeypadMinus, KP_MINUS) \
    ENTRY(KeypadPlus, KP_PLUS) ENTRY(KeypadEnter, KP_ENTER) ENTRY(KeypadPeriod, KP_PERIOD) ENTRY(Keypad0, KP_0) \
    ENTRY(Keypad1, KP_1) ENTRY(Keypad2, KP_2) ENTRY(Keypad3, KP_3) ENTRY(Keypad4, KP_4) ENTRY(Keypad5, KP_5) \
    ENTRY(Keypad6, KP_6) ENTRY(Keypad7, KP_7) ENTRY(Keypad8, KP_8) ENTRY(Keypad9, KP_9) ENTRY(Menu, APPLICATION) \
    ENTRY(LeftCtrl, LCTRL) ENTRY(LeftShift, LSHIFT) ENTRY(LeftAlt, LALT) ENTRY(LeftSuper, LGUI) \
    ENTRY(RightCtrl, RCTRL) ENTRY(RightShift, RSHIFT) ENTRY(RightAlt, RALT) ENTRY(RightSuper, RGUI)
        // clang-format on

        Key ToKey(SDL_Keycode code)
        {
            switch (code)
            {
#define FENRIR_SDL_KEY_CASE(fenrir, sdl) \
    case SDLK_##sdl:                     \
        return Key::fenrir;
                FENRIR_SDL_KEYS(FENRIR_SDL_KEY_CASE)
#undef FENRIR_SDL_KEY_CASE
            default:
                return Key::Unknown;
            }
        }

        Scancode ToScancode(SDL_Scancode code)
        {
            switch (code)
            {
#define FENRIR_SDL_SCANCODE_CASE(fenrir, sdl) \
    case SDL_SCANCODE_##sdl:                  \
        return Scancode::fenrir;
                FENRIR_SDL_KEYS(FENRIR_SDL_SCANCODE_CASE)
#undef FENRIR_SDL_SCANCODE_CASE
            default:
                return Scancode::Unknown;
            }
        }

#undef FENRIR_SDL_KEYS

        KeyModifiers ToModifiers(SDL_Keymod mod)
        {
            KeyModifiers modifiers = KeyModifiers::None;
            const auto add = [&modifiers, mod](SDL_Keymod sdl, KeyModifiers fenrir) {
                if ((mod & sdl) != 0)
                {
                    modifiers |= fenrir;
                }
            };
            add(SDL_KMOD_SHIFT, KeyModifiers::Shift);
            add(SDL_KMOD_CTRL, KeyModifiers::Ctrl);
            add(SDL_KMOD_ALT, KeyModifiers::Alt);
            add(SDL_KMOD_GUI, KeyModifiers::Super);
            add(SDL_KMOD_CAPS, KeyModifiers::CapsLock);
            add(SDL_KMOD_NUM, KeyModifiers::NumLock);
            return modifiers;
        }

        std::optional<MouseButton> ToMouseButton(Uint8 button)
        {
            switch (button)
            {
            case SDL_BUTTON_LEFT:
                return MouseButton::Left;
            case SDL_BUTTON_RIGHT:
                return MouseButton::Right;
            case SDL_BUTTON_MIDDLE:
                return MouseButton::Middle;
            case SDL_BUTTON_X1:
                return MouseButton::X1;
            case SDL_BUTTON_X2:
                return MouseButton::X2;
            default:
                return std::nullopt;
            }
        }

        // SDL's standard layout lists the buttons and axes in FenrirInput's order (InputTypes.hpp); SDL's extra
        // buttons past the touchpad have no name in FenrirInput.
        std::optional<GamepadButton> ToGamepadButton(Uint8 button)
        {
            if (button > SDL_GAMEPAD_BUTTON_TOUCHPAD)
            {
                return std::nullopt;
            }
            return static_cast<GamepadButton>(button);
        }

        std::optional<GamepadAxis> ToGamepadAxis(Uint8 axis)
        {
            if (axis >= SDL_GAMEPAD_AXIS_COUNT)
            {
                return std::nullopt;
            }
            return static_cast<GamepadAxis>(axis);
        }

        // SDL's axes run from -32768 to 32767, triggers from 0; FenrirInput's from -1 to 1, triggers from 0.
        float ToAxisValue(Sint16 value)
        {
            return std::clamp(static_cast<float>(value) / static_cast<float>(SDL_JOYSTICK_AXIS_MAX), -1.0f, 1.0f);
        }

        std::u32string DecodeUtf8(const char* text)
        {
            std::u32string codepoints;
            if (text == nullptr)
            {
                return codepoints;
            }
            std::size_t length = SDL_strlen(text);
            while (length > 0)
            {
                codepoints.push_back(static_cast<char32_t>(SDL_StepUTF8(&text, &length)));
            }
            return codepoints;
        }

        // The window an input came from, or none for a window FenrirWindow did not make (or no window at all).
        std::optional<WindowId> FromWindow(WindowState& state, SDL_WindowID sdlId)
        {
            if (const WindowRecord* record = state.FindSdl(sdlId))
            {
                return record->id;
            }
            return std::nullopt;
        }
    } // namespace

    bool FeedInput(WindowState& state, InputFeed& feed, const SDL_Event& event)
    {
        switch (event.type)
        {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            if (const std::optional<WindowId> window = FromWindow(state, event.key.windowID))
            {
                const Key key = ToKey(event.key.key);
                const Scancode scancode = ToScancode(event.key.scancode);
                const KeyModifiers modifiers = ToModifiers(event.key.mod);
                if (event.key.down)
                {
                    feed.KeyDown(*window, key, scancode, modifiers, event.key.repeat);
                }
                else
                {
                    feed.KeyUp(*window, key, scancode, modifiers);
                }
            }
            return true;
        case SDL_EVENT_TEXT_INPUT:
            if (const std::optional<WindowId> window = FromWindow(state, event.text.windowID))
            {
                feed.Text(*window, DecodeUtf8(event.text.text));
            }
            return true;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            const std::optional<WindowId> window = FromWindow(state, event.button.windowID);
            const std::optional<MouseButton> button = ToMouseButton(event.button.button);
            if (window && button)
            {
                if (event.button.down)
                {
                    feed.MouseButtonDown(*window, *button);
                }
                else
                {
                    feed.MouseButtonUp(*window, *button);
                }
            }
            return true;
        }
        case SDL_EVENT_MOUSE_MOTION:
            if (const std::optional<WindowId> window = FromWindow(state, event.motion.windowID))
            {
                feed.MouseMove(*window, Math::Vec2(event.motion.x, event.motion.y),
                               Math::Vec2(event.motion.xrel, event.motion.yrel));
            }
            return true;
        case SDL_EVENT_MOUSE_WHEEL:
            if (const std::optional<WindowId> window = FromWindow(state, event.wheel.windowID))
            {
                const float direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
                feed.Scroll(*window, Math::Vec2(event.wheel.x, event.wheel.y) * direction);
            }
            return true;
        case SDL_EVENT_GAMEPAD_ADDED:
            // SDL's joystick ids are never reused while SDL runs, as a GamepadId must not be.
            if (SDL_Gamepad* gamepad = SDL_OpenGamepad(event.gdevice.which))
            {
                state.gamepads.push_back(gamepad);
                feed.GamepadConnected(GamepadId{event.gdevice.which});
            }
            return true;
        case SDL_EVENT_GAMEPAD_REMOVED:
            if (SDL_Gamepad* gamepad = SDL_GetGamepadFromID(event.gdevice.which))
            {
                std::erase(state.gamepads, gamepad);
                SDL_CloseGamepad(gamepad);
                feed.GamepadDisconnected(GamepadId{event.gdevice.which});
            }
            return true;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
            if (const std::optional<GamepadButton> button = ToGamepadButton(event.gbutton.button))
            {
                if (event.gbutton.down)
                {
                    feed.GamepadButtonDown(GamepadId{event.gbutton.which}, *button);
                }
                else
                {
                    feed.GamepadButtonUp(GamepadId{event.gbutton.which}, *button);
                }
            }
            return true;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            if (const std::optional<GamepadAxis> axis = ToGamepadAxis(event.gaxis.axis))
            {
                feed.GamepadAxisMoved(GamepadId{event.gaxis.which}, *axis, ToAxisValue(event.gaxis.value));
            }
            return true;
        default:
            return false;
        }
    }

    void CloseGamepads(WindowState& state)
    {
        for (SDL_Gamepad* gamepad : state.gamepads)
        {
            SDL_CloseGamepad(gamepad);
        }
        state.gamepads.clear();
    }
} // namespace Fenrir::Detail

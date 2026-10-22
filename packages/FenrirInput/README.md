# FenrirInput

Keyboard, mouse and gamepad input for a Fenrir app: device-neutral types, the polled `Keyboard`, `Mouse` and `Gamepads` app resources, and an engine event for every input (ADR-0017). It links no third-party library and is always built. FenrirWindow feeds it from SDL3; a test or a headless app feeds it by hand through the same input feed.

```cpp
#include "FenrirInput/Input.hpp"

Fenrir::Input input = Fenrir::Input::Install(app); // keep it alive while the app runs; it cannot fail

app.AddSystem(Fenrir::SchedulePriority::Tick, [](Fenrir::App& app) {
    const auto& keyboard = app.GetResource<Fenrir::Keyboard>();
    if (keyboard.JustPressed(Fenrir::Scancode::Space)) { /* jump, once per press */ }
});
```

## Types

- `Key` is a **key code**: the key as the keyboard layout names it (`Key::Z` is the key that types Z). Use it for shortcuts named by letter.
- `Scancode` is where the key sits, whatever the layout, named as a US QWERTY keyboard prints it. Use it for keys placed by position, such as WASD.
- `KeyModifiers` (a bit set: `Shift`, `Ctrl`, `Alt`, `Super`, `CapsLock`, `NumLock`), `MouseButton`, `GamepadButton` and `GamepadAxis` in the standard layout (buttons named by position: `South` is A on an Xbox pad).
- `WindowId` names the window an input came from (FenrirWindow keys its windows by it); `GamepadId` names a pad, never reused after it is unplugged.

## The input feed

`InputFeed` is an app resource. Each call queues one input: `KeyDown` (with `repeat`), `KeyUp`, `Text` (codepoints), `MouseButtonDown`, `MouseButtonUp`, `MouseMove` (logical position and motion), `Scroll` (notches), `FocusGained`, `FocusLost`, `GamepadConnected`, `GamepadDisconnected`, `GamepadButtonDown`, `GamepadButtonUp` and `GamepadAxisMoved`.

FenrirInput's **dispatch** takes the queue in `PreUpdate`, in the order it was fed: it updates the resources and sends each input as its event. Input fed before the dispatch runs (between frames, or from a sequential `PreUpdate` system added before `Input::Install`, such as a window's pump) is dispatched that frame; input fed later waits a frame. Feed from the thread that runs the App, outside parallel systems.

**Main-thread rule.** The dispatch is a sequential system (`AddSequentialSystem`), so it runs on the thread that runs the App: input routers (ADR-0027) will be called from it, and they may reach SDL's text input, which must stay on the main thread.

## Polled state

`Keyboard`, `Mouse` and `Gamepads` are app resources. Any system may read them, in parallel or not. Read them from `BeforeTicks` on: in `PreUpdate`, parallel systems run before the dispatch.

- `IsDown`, `JustPressed` and `JustReleased`: for keys by key code or by scancode (tracked separately), mouse buttons, and gamepad buttons per pad.
- **Input edges follow the reading phase.** From `PreTick`, `Tick` or `PostTick`, "just pressed" and "just released" are the edges since the last tick; from any other phase, the edges since the last frame. A tap is never lost in a frame with no tick, nor seen twice in a frame with two. A tap whose press and release both came between two ticks reads in the next tick as just pressed and just released, with held false. Repeats are not presses. The edges are stamped with the frame and fixed step they belong to (`Time::frameCount` and `Time::tickCount`) and compared with the reader's phase (`App::CurrentPhase`), so no system clears them and the answer does not depend on install order.
- `Keyboard::Modifiers()` (of the latest key) and `Keyboard::FocusedWindow()`.
- `Mouse::Position()` is in the focused window's logical coordinates (floats, from its top-left); `Motion()` is in the same units (raw while the cursor is captured) and `Scroll()` in notches (y positive away from the user). Motion and scroll are summed since the last tick or frame, by the reading phase, as edges are. Only the focused window's pointer input moves them, or any window's while none has the focus.
- `Gamepads::Connected()`, `IsConnected`, and `Axis` (sticks -1 to 1, positive right and down; triggers 0 to 1). An unplugged pad reads nothing held.

## Events

Every input is also an engine event, read through `EventReader<T>` once per reader, in the order fed (ADR-0012): `KeyEvent` (key, scancode, modifiers, pressed, repeat, window), `TextEvent` (one codepoint, window), `MouseButtonEvent`, `MouseMotionEvent`, `MouseScrollEvent`, `WindowFocusEvent`, `GamepadConnectionEvent`, `GamepadButtonEvent` and `GamepadAxisEvent`. Each is plain data.

## Owners

`Input::Install(App&)` returns a move-only owner. A second `Install` while one lives shares the same input, so FenrirWindow can install it whether or not the game did. When the last owner is destroyed the four resources are removed and the dispatch does nothing. Destroy it before the App.

## Not here yet

Input routers, control owners and input claims (ADR-0027) come next: they will sit in the dispatch between the feed and the resources. Input mapping (actions and bindings), rumble, IME composition, touch and pen are out of scope.

## Tests

`FenrirInputTests` (with `-DFENRIR_BUILD_TESTS=ON`) feed input with no window and drive the App one frame at a time.

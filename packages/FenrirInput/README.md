# FenrirInput

Keyboard, mouse and gamepad input for a Fenrir app: device-neutral types, the polled `Keyboard`, `Mouse` and `Gamepads` app resources, an engine event for every input (ADR-0017), and input routers through which a UI sees input before the game (ADR-0027). It links no third-party library and is always built. FenrirWindow feeds it from SDL3; a test or a headless app feeds it by hand through the same input feed.

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

FenrirInput's **dispatch** takes the queue in `PreUpdate`, in the order it was fed: each input goes to the input routers, and what they pass updates the resources and is sent as its event. Input fed before the dispatch runs (between frames, or from a sequential `PreUpdate` system added before `Input::Install`) is dispatched that frame. A feeder that runs after it calls `Input::Dispatch(app)` once it has fed: that dispatches everything pending. With nothing pending it dispatches nothing, though a frame's first call still gives the routers their `BeginDispatch`. FenrirWindow's pump does this, so input from a window reaches the game in the same frame whichever of the two packages was installed first. Input fed later and not dispatched waits a frame. Feed from the thread that runs the App, outside parallel systems.

**Main-thread rule.** The dispatch is a sequential system (`AddSequentialSystem`), so it runs on the thread that runs the App: input routers are called from it, and they may reach SDL's text input, which must stay on the main thread.

## Polled state

`Keyboard`, `Mouse` and `Gamepads` are app resources. Any system may read them, in parallel or not. Read them from `BeforeTicks` on: in `PreUpdate`, parallel systems run before the dispatch.

- `IsDown`, `JustPressed` and `JustReleased`: for keys by key code or by scancode (tracked separately), mouse buttons, and gamepad buttons per pad.
- **Input edges follow the reading phase.** From `PreTick`, `Tick` or `PostTick`, "just pressed" and "just released" are the edges since the last tick; from any other phase, the edges since the last frame. A tap is never lost in a frame with no tick, nor seen twice in a frame with two. A tap whose press and release both came between two ticks reads in the next tick as just pressed and just released, with held false. Repeats are not presses. The edges are stamped with the frame and fixed step they belong to (`Time::frameCount` and `Time::tickCount`) and compared with the reader's phase (`App::CurrentPhase`), so no system clears them and the answer does not depend on install order.
- `Keyboard::Modifiers()` (of the latest key) and `Keyboard::FocusedWindow()`.
- `Mouse::Position()` is in the focused window's logical coordinates (floats, from its top-left); `Motion()` is in the same units (raw while the cursor is captured) and `Scroll()` in notches (y positive away from the user). Motion and scroll are summed since the last tick or frame, by the reading phase, as edges are. Only the focused window's pointer input moves them, or any window's while none has the focus.
- `Gamepads::Connected()`, `IsConnected`, and `Axis` (sticks -1 to 1, positive right and down; triggers 0 to 1). An unplugged pad reads nothing held.

## Events

Every input that reaches the game is also an engine event, read through `EventReader<T>` once per reader, in the order fed (ADR-0012): `KeyEvent` (key, scancode, modifiers, pressed, repeat, cancelled, window), `TextEvent` (one codepoint, window), `MouseButtonEvent` (with cancelled), `MouseMotionEvent`, `MouseScrollEvent`, `WindowFocusEvent`, `GamepadConnectionEvent`, `GamepadButtonEvent` and `GamepadAxisEvent`. Each is plain data.

## Owners

`Input::Install(App&)` returns a move-only owner. A second `Install` while one lives shares the same input, so FenrirWindow can install it whether or not the game did. When the last owner is destroyed the five resources (`Keyboard`, `Mouse`, `Gamepads`, `InputFeed`, `InputClaims`) are removed, the routers are dropped and the dispatch does nothing. Destroy it before the App.

## Input routers

An **input router** (`FenrirInput/InputRouter.hpp`, ADR-0027) sees each input before the game and may consume it. Nothing a router consumes reaches `Keyboard`, `Mouse`, `Gamepads` or a game event reader, tick edges included.

```cpp
class Panel : public Fenrir::InputRouter
{
    void BeginDispatch(Fenrir::App& app) override { /* refresh hit-test data, publish claims */ }
    Fenrir::RouteResult Route(Fenrir::App& app, const Fenrir::RoutedInput& input) override
    {
        return OverPanel(input) ? Fenrir::RouteResult::Consumed : Fenrir::RouteResult::Passed;
    }
};

Fenrir::InputRouterId panel = Fenrir::Input::AddInputRouter(app, 100, std::make_shared<Panel>());
```

- **Registration.** `Input::AddInputRouter(app, priority, router)` returns an id; `Input::RemoveInputRouter(app, id)` removes it. Routers run highest priority first, equal priorities in registration order. A router starts at the next frame's dispatch, so one added during a dispatch sees input from the next frame on. One removed stops at once, even mid-dispatch.
- **Dispatch.** Each router gets `BeginDispatch` once a frame, on the frame's first dispatch, however many times `Input::Dispatch` runs. Then the inputs come in arrival order, each to the routers in turn until one returns `Consumed`; if none does, it passes to the game.
- **Control owners.** A key, mouse-button or gamepad-button press belongs to whoever took it: the router that consumed it, or the game. Its repeats and its release go straight to that owner, without dispatch, whatever the owner's `Route` returns. Keys are owned by scancode (by key code when the scancode is unknown). Motion, scroll, text and gamepad axes are not owned: each event is dispatched. Focus changes and gamepad connections go to every router and always reach the game.
- **Pointer capture.** While a router claims `pointerCapture` for a window, that window's pointer input (buttons, motion, scroll) goes to it first (to each holder, highest priority first), and what it passes goes on to the game, skipping the other routers.
- **Input claims.** `Input::SetInputClaims(app, id, window, claims)` publishes a router's `WindowInputClaims` for a window: `pointer`, `pointerCapture`, `keyboardFocus`, and `textInput` with a `caret` rectangle in logical units. `Input::SetGamepadClaim(app, id, claimed)` is the app-wide `gamepad` claim. The `InputClaims` app resource holds the union per window (`For(window)`, `Gamepad()`), with the caret of the highest-priority `textInput` claimant. Claims never filter events. FenrirWindow keeps SDL's text input on, with the caret, in each window where a router claims `textInput`.
- **Cancellation.** A router raising `keyboardFocus` in a window cancels the game's held keys there; raising `pointerCapture` cancels its held mouse buttons there. The game gets a release with `cancelled` set at once, and the later physical release (and any repeat) goes to no one. When a window loses the focus, every owned control in it is cancelled, the game's and the routers' alike, before the focus event itself, and every router's `pointerCapture` in that window is dropped. A router removed while it owns controls has them ended silently, and its claims cleared.
- **Threads.** These calls, like the feed, are made from the thread that runs the App, outside parallel systems. A router's calls come from the sequential dispatch, so a router may reach SDL.

**IME composition** is reserved, not built. It will slot in as: a composition event (the preedit string with its cursor and selection) added to the feed and to `RoutedInput`; the committed text as the existing `TextEvent`; the IME's candidate window placed from the `textInput` claim's caret, which FenrirWindow already passes to SDL; and the composition cancelled when the window's focus or the claim goes.

## Scripting

With `FENRIR_ENABLE_SCRIPTING` on, the `FenrirInputScripting` library gives C# input (ADR-0029): add `Fenrir::Input::ScriptingExtension()` to `ScriptingConfig::extensions` and call `fenrir_deploy_input_scripting(<target>)` beside `fenrir_deploy_scripting`. The `Fenrir.Input` contract (`Managed/Fenrir.Input`) has:

- the types mirrored with C++'s values and underlying types: `Key`, `Scancode`, `KeyModifiers`, `MouseButton`, `GamepadButton`, `GamepadAxis` (each with C++'s `Count`, a size and not an input), and `WindowId` and `GamepadId` (one `uint` each; `Fenrir.Window` uses this `WindowId`);
- `Keyboard`, `Mouse` and `Gamepads` as static facades whose every answer comes from C++, so the input edges follow the reading phase exactly as in C++: a C# `Tick` system reads the edges since the last tick, any other C# code those since the last frame. `Gamepads.Connected(Span<GamepadId>)` fills the span and returns how many pads are connected;
- every input event as a native event read through `EventReader<T>`, sharing its queue with C++ readers (`TextEvent.Codepoint` is a `uint`);
- `InputClaims` as a read-only resource, `Resources.GetReadOnly<InputClaims>()`: `For(window)`, `Gamepad`, and up to `InputClaims.MaxWindows` (8) windows. C# reads a plain-data copy of the C++ resource, refreshed each time C# gets it. Routers stay C++.

The extension needs no `Input` owner, so scripts always load. With no input installed, every function does nothing and returns `false` or zero (and `InputClaims` is absent); so do the gamepad functions for a pad that is not connected. A debug build warns once per native call site, kept by the extension's state. `FenrirInputScriptingTests` checks C# against C++ through FenrirScripting's harness.

The binding is generated (ADR-0043): the events, ids and enums are written once, in this package's headers (`api/FenrirInput.api.json`), the `InputClaims` copy once, in `Scripting/include/FenrirInputScripting/ScriptInputClaims.hpp` (`Scripting/api/FenrirInputScripting.api.json`), and each native function once, as its definition in `Scripting/src/InputScripting.cpp`. `Scripting/api/FenrirInput.binding.toml` generates from them the contract's mirrors and `InputNative` (`Managed/Fenrir.Input/Generated`), the Iris mappings (`Scripting/include/FenrirInputScripting/Generated`, which a binding using this one includes; `InputMappings.hpp` keeps the ids' `Iris::WrapperOf`), the extension's lists and the defaults test. The contract's own files hold the facades and the ids' and `InputClaims`'s behaviour; `InputClaims` is registered by hand, since it is refreshed whenever C# gets it. After changing a shared type or a native, run `fenrir-describe-api` and then `fenrir-generate-bindings`.

## Not here yet

Input mapping (actions and bindings), rumble, IME composition, touch and pen are out of scope.

## Tests

`FenrirInputTests` (with `-DFENRIR_BUILD_TESTS=ON`) feed input with no window and drive the App one frame at a time.

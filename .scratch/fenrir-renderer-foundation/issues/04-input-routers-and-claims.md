# 04: Input routers, control owners and input claims

**What to build:** A UI can see input before the game, consume it, own the controls it takes and claim the pointer, keyboard focus and text input, so clicking a panel never fires a gun (spec: "FenrirInput" (input routers, input claims, cancellation), "FenrirWindow" (text input per claim, focus for cancellation), "Testing Decisions" seam 3; ADR-0027, ADR-0017). No UI uses it yet; ticket 20's ImGui overlay is the first.

- **Order in `PreUpdate`:** feed → routers → whatever passes updates `Keyboard`, `Mouse` and `Gamepads` and becomes game events, tick edges included. Nothing a router consumes reaches game state or a game reader.
- **Registration:** `AddInputRouter(priority, router)` returns an id; `RemoveInputRouter(id)` removes it. Highest priority first, ties in registration order. A router added during dispatch starts next frame; one removed stops at once.
- **Dispatch:** `BeginDispatch` before the frame's first event; events in arrival order until one router returns `Consumed`.
- **Control owners:** a key, mouse-button or gamepad-button press belongs to whoever took it (a router or the game), and its repeats and release go straight to that owner. Motion, scroll and text are not owned; each event is dispatched. While a router holds `pointerCapture` for a window, that window's pointer events reach it first.
- **Input claims:** `pointer`, `pointerCapture`, `keyboardFocus` and `textInput` (with a caret rectangle) per window, plus an app-wide `gamepad` claim, published by routers. The `InputClaims` app resource holds the union per window. Claims never filter events.
- **Cancellation:** raising `keyboardFocus` or `pointerCapture` cancels the game-owned controls of that device in that window with a `cancelled` release, and the later physical release goes to no one. Focus loss cancels every owned control in the window and drops its capture. A removed router's controls end silently and its claims clear.
- **FenrirWindow:** SDL text input is on, with the caret rectangle, in each window where a router claims `textInput`, else following `textInputByDefault`.
- IME composition stays reserved and unbuilt; the README says where it will slot in (a composition event, commit as the text event, the caret from the claim, cancellation).
- The routers run inside FenrirInput's sequential dispatch system (they may touch SDL text input).

**Blocked by:** 03 (FenrirWindow over SDL3: the main window, its services and the input feed).

**Touches:** `packages/FenrirInput/`, `packages/FenrirWindow/src/`

**Status:** ready-for-agent

- [ ] `FenrirInputTests` routers cases: priority and stable tie order; a consumed event reaches no lower router, no resource and no game reader; a router added during dispatch starts next frame and one removed stops at once.
- [ ] Ownership: a press taken by a router sends its repeats and release only to it; a game-owned press still releases to the game after a router appears; motion, scroll and text dispatch per event; pointer capture routes a window's pointer events to its holder first.
- [ ] Cancellation: raising `keyboardFocus` cancels held game keys with a `cancelled` release and the later physical release reaches no one; focus loss cancels every owned control and drops capture; removing an owning router ends its controls silently and clears its claims.
- [ ] `InputClaims` holds the per-window union; two windows' claims and owners stay separate.
- [ ] A FenrirWindow check that text input turns on for a `textInput` claim and off when it clears (skipped without a display).

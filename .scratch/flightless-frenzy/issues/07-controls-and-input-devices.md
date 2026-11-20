# What does the player play with?

Type: grilling
Status: resolved
Blocked by: 01, 05

## Question

Which input devices does the game support, and what are the bindings? Keyboard and mouse as shipped (WASD, space, mouse look); a gamepad or not; whether bindings can be changed by the player; mouse sensitivity and inverted look; how the cursor behaves between play and menus.

The inventory says what FenrirInput offers; the movement prototype says which actions exist. Anything missing (gamepad support, an action layer, cursor capture) is an engine gap.

## Answer

Grilled 2026-11-20.

- **Devices:** keyboard with mouse, and one gamepad (the first connected). Both are live at the same time; there is no setting to choose between them.
- **The bindings**, all of them:

  | Action | Keyboard and mouse | Gamepad |
  | --- | --- | --- |
  | Run | WASD or arrow keys | left stick |
  | Look | mouse | right stick |
  | Jump (hold for height) | Space | South (A) |
  | Extra jump | Space again in the air | South again |
  | Glide | hold Space in the air once the extra jump is spent | hold South |
  | Pause, and back out of a menu | Escape | Start, and East (B) in menus |

  One button does everything in the air. There is no sprint, attack or interact button. Bindings cannot be changed by the player.
- **Merging the devices:** one plain C# type turns both devices into a small set of values (a run direction, a look amount, jump pressed and held, pause pressed), under unit test. Every other system reads only that. The engine's action layer stays deferred; this game does not ask for it.
- **Look settings:** one sensitivity slider for the mouse, one look-speed slider for the stick, and one invert-Y toggle that applies to both. This refines "camera sensitivity and invert-Y" in "What are the HUD and the menus?".
- **Dead zones:** the game writes none of its own. Gamepad play waits for the engine's [Stick and trigger dead zones](../../fenrir-input-dead-zones/spec.md) to be built; the first playable slice is keyboard and mouse only. That spec is needed before the game's gamepad ticket ([No input actions or bindings](../../engine-gaps/issues/09-input-actions.md)).
- **Rumble:** the engine has none. Raised as [No gamepad rumble](../../engine-gaps/issues/37-gamepad-rumble.md), wanted and not blocking.
- **On-screen prompts:** where the HUD or a menu names a control, the wording follows the device used last. Text only, no button pictures.
- **The cursor** is as "What are the HUD and the menus?" settled: hidden and captured in play, released in any menu.

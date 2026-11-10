# 18: UIDemo: the pause menu

**What to build:** a pause menu in UIDemo that a player drives with mouse, keyboard and gamepad, animated in and out (spec: "UIDemo").

- **The menu root** on the window overlay above the HUD, opened and closed with Escape: a frosted panel (`backdrop-filter: blur`) with `:intro`/`:outro` transitions; Resume, Settings and Quit buttons; a settings panel with sliders (volume, field of view, applied to the demo), checkboxes (invert look, show HUD), a text entry for a player name shown on the HUD, and a scrolling list of entries; hover, focus and press transitions; gamepad navigation with the focus ring and the virtual cursor on a toggle; the root takes pointer input so the camera stops turning while it is open; the menu's stylesheet hot-reloads.
- Menu events read by a demo system through `EventReader<UIEvent>` in C++.

**Blocked by:** 12, 13, 14, 17

**Touches:** `examples/UIDemo/`

**Status:** ready-for-agent

- [ ] Headless smoke test: opening the menu through the test feed's Escape makes the root take pointer input; a direction and A through the gamepad feed click Resume and close it.

Hand check: the menu blurs the room behind it, animates in and out, every control works with the mouse, Tab and the controller, the name typed shows on the HUD, and the sliders change the demo.

- [ ] `check_tickets.py` and clang-format pass.

# What are the HUD and the menus?

Type: grilling
Status: resolved
Blocked by: 01, 04, 06

## Question

What is on screen, built on FenrirUI (Razor panels if the game is in C#)?

- **HUD:** the timer, friends freed out of the total, score, active power-ups, a pointer to the exit once it opens or none.
- **Screens:** title, pause, results for a win and a loss, and an options screen (which options, given the game has no sound).
- How menus are driven (mouse, keyboard, gamepad if supported) and what the game does behind a menu.
- The UI's style, to match "What does the game look like?" once that is settled.

Anything FenrirUI cannot do that these need is an engine gap.

## Answer

Grilled 2026-11-19. FenrirUI covers all of this; no new engine gap came out of it.

- **The HUD** during play, and nothing else:
  - the timer as MM:SS, top centre; it turns red and pulses in the last 30 seconds;
  - a time penalty shown as "-5s" dropping off the timer when it happens;
  - friends freed out of the total;
  - the score;
  - the active power-up, with a bar that drains as it runs out.

  No health, lives, minimap or coin count. Coins found out of the total appear on the results screen.
- **Finding the exit:** when the last friend is freed the HUD shows "All friends free! Head for the exit", then a marker that points to the exit: over it when it is on screen, at the screen's edge when it is not. Cages get no markers; finding the friends is the game.
- **The screens:**

  | Screen | Shows | Buttons |
  | --- | --- | --- |
  | Title | best score and best time | Play, Options, Quit |
  | Pause | | Resume, Restart, Options, Title, Quit |
  | Won | the score in its parts, time left, friends and coins out of the total, "new best" if it is one | Restart, Title |
  | Lost | "Out of time", friends freed out of the total | Restart, Title |
  | Options | see below | Back |

  Options is reached from Title and Pause.
- **Options:** camera sensitivity and invert-Y only. A fullscreen toggle is added when the engine can do it ([No fullscreen or display settings](../../engine-gaps/issues/12-fullscreen-and-display-settings.md)). They are saved in the same file as the records. There is no key rebinding.
- **Driving the menus:** the mouse, Tab and a gamepad today, and the arrow keys once [Arrow keys do not move focus in FenrirUI](../../engine-gaps/issues/10-arrow-key-focus-in-ui.md) is fixed. Escape, and Start on a gamepad, opens the pause menu and steps back out of any menu. The mouse cursor is hidden and captured during play and released in any menu. Whether the game supports a gamepad at all is for "Controls and input devices".
- **Behind a menu:** the pause menu freezes the level and shows it blurred behind the panel; this needs [pause and time scale](../../engine-gaps/issues/05-pause-and-time-scale.md). Won and Lost do the same, with Kiki no longer controllable. The title screen is a plain styled backdrop; a live fly-over of the level is left in the map's fog.
- **How it is built:** each screen is one Razor panel. The HUD is on a root at the back; menus are on a root in front that takes input first. The game keeps its own small record of which screen is open and which to return to; it does not ask the engine for a menu stack. The exit marker uses the renderer's world-to-screen call, which C# already has.
- **Style:** the game ships its own display font, chosen in "Where do assets come from?". Colours and shapes follow "What does the game look like?".

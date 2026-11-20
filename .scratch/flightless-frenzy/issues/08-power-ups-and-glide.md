# How do the power-ups work?

Type: grilling
Status: resolved
Blocked by: 02, 05

## Question

The game has four timed pickups: speed, slowed timer, extra jump and glide (settled while charting; no ability cards, no selection). For each: its strength and duration, whether picking up a second one stacks, refreshes or is refused, whether pickups come back after a while, how an active one is shown on Kiki and on the HUD, and how the player is warned it is ending.

Start from the old game's values and the movement prototype's numbers.

## Answer

Grilled 2026-11-20. No engine gap came out of it.

- **What each one does:**

  | Power-up | Effect while active |
  | --- | --- |
  | Speed | run at 1.5 × |
  | Slowed timer | the level timer runs at half speed |
  | Extra jump | one more jump in the air, each time Kiki leaves the ground |
  | Glide | hold jump in the air to sink slowly |

  The movement numbers are those of "How does Kiki move?". Picking one up scores nothing.
- **How long:** 15 seconds each to start with. Each kind's duration is its own value, tuned once the level exists.
- **Holding more than one:** different kinds run at the same time, each on its own clock. Picking up a kind that is already active refills it to full and adds nothing. The HUD shows one draining bar per active kind, which refines the single bar in "What are the HUD and the menus?".
- **Pickups come back:** a pickup reappears in its place 5 seconds after its effect ends, and is shown faintly while it is away. So a kind can never be stacked on itself.
- **The rule this gives the level:** a friend who can only be reached with a power-up always has that pickup beside the route.
- **What ends one early:** nothing but the run ending. A power-up survives a fall and an enemy hit.
- **Showing it:** its bar on the HUD, and Kiki's rim light in its colour. Both blink in the last 3 seconds as the warning. The rim colour waits on the engine's [per-entity material values](../../engine-gaps/issues/15-per-entity-material-colour.md); the bar works from the start.
- **The glide's model:** the 2022 art has none. A new one is made in the same style (a feather) and listed as new work beside Darcy Wilson's credit. Until then the double-jump model in a different colour stands in. Who makes it is decided with the Kiki rigging ticket.

The rules here (durations, refill, the return delay, the half-speed timer) are plain C# types under unit test.

# Does the engine offer navigation?

Type: grilling
Status: resolved
Blocked by: none
Settles: [No navigation or path-finding](08-navigation.md)

## Question

Nothing in the engine finds a path across level geometry. Flightless Frenzy's enemies patrol placed points and chase in a straight line, checking ledges and walls with ray casts, so the game does not need it. By the engine/game line, what the world's walkable space is may be the game's.

- Does the engine offer a navigation building block (a navigation mesh built from given geometry and path queries, for example Recast and Detour behind a seam), now, later, or never?
- If later, where is it recorded (a map's fog, an engine issue left open), and what would make it come back?

Outcome: `wontfix` with the reason, deferred with the reason, or a pointer to where it is planned.

## Answer

Grilled 2026-11-19, round 1 of charting [Engine gaps for Flightless Frenzy](../map.md); the recommendation was accepted.

**Deferred.** A navigation building block (a navigation mesh built from geometry the game gives, with path queries, for example Recast and Detour behind a seam) sits on the engine's side of the line: what is walkable is the game's choice, expressed by what it feeds in. No client needs it: Flightless Frenzy's enemies follow patrol points and chase in a straight line with ray casts. It comes back when a game asks for path-finding across a level.

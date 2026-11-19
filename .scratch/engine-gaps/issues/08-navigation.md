# No navigation or path-finding

Status: needs-triage
Blocked by: none

## What is missing

Nothing in the engine finds a path across level geometry: no navigation mesh, no path queries, no steering. Physics queries (ray, shape cast, overlap) are the only way to ask about the world.

## Who needs it

Flightless Frenzy ([map](../../flightless-frenzy/map.md)): enemies that patrol and that chase Kiki when near, on a level made of platforms and ledges. "What are the enemies?" decides how clever they must be.

## Does anything plan it?

No.

## Does it block the game?

No. Enemies can follow patrol points placed in the level and chase in a straight line, checking for a ledge or a wall with ray casts. By the engine/game line, what the world's walkable space is may be the game's to decide; triage should say whether the engine offers a building block here at all.

## Comments

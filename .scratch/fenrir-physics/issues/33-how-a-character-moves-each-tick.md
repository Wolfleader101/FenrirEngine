# How does a character move each tick?

Type: grilling
Status: open
Blocked by: 30, 32

## Question

Where does moving a character sit in the foundation's per-tick sequence ("How does physics step?": `PostTick` drains watches, diffs, applies forces, steps, writes back, sends events, makes debug lines), and what does a move do? Builds on [What does Jolt offer for characters?](30-what-jolt-offers-for-characters.md) and [How is a character body modelled in the ECS?](32-character-bodies-in-the-ecs.md).

- **Order:** characters moved before or after the world step, and what each order means for standing on bodies the step just moved.
- **Gravity:** applied by physics or by game code through the velocity.
- **Stairs and floors:** stepping up and sticking to the floor going down, on by default or opt-in.
- **Platforms:** inheriting the ground's velocity.
- **Interpolation:** whether characters interpolate like dynamic bodies (`RigidBody::interpolate`).
- **Teleports and edits** detected by the diff.
- **Determinism:** the order characters are moved in, and multithreaded stepping.
- **Inactive scenes:** the world pauses; confirm nothing new.

Facts from "What does Jolt offer for characters?": a character update cannot run during `PhysicsSystem::Update` or a step listener; Jolt's samples update before the step, its tests after. The host adds gravity and `GetGroundVelocity`. `ExtendedUpdate` does stairs (0.4 m) and stick-to-floor (0.5 m). Characters may update concurrently with separate temp allocators, but update order between characters changes results, and `CharacterID`s come from a process-global counter.

From "How is a character body modelled in the ECS?" (ADR-0014): a character moves once per tick in `PostTick` by `CharacterBody::velocity`, which game code writes in `Tick`; this ticket places that move before or after the step. Physics writes `velocity` back with the parts into what it hit removed, so gravity can live in `velocity` as game code's own addition; ground and platform velocity are never written into it, so riding a platform must come from somewhere else (added privately, or a separate field). Position only is written back, through `SetWorldTransform`, in world space even under a parent. A world pose differing from physics's copy is a teleport (no sweep, contacts and ground state refreshed, `velocity` kept); whether interpolation snaps follows `RigidBody::interpolate`'s rule, and whether `CharacterBody` has its own `interpolate` is this ticket's. Up is world +Y, so `ExtendedUpdate`'s Y-axis defaults hold. Slope, step and floor settings become live fields on `CharacterBody`; Jolt's construction-only settings stay private. A disabled or inactive character does not move.

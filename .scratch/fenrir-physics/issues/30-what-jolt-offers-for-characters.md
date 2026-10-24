# What does Jolt offer for characters?

Type: research
Status: resolved
Blocked by: none

## Question

What do JoltPhysics v5.6.0's character controllers offer to, and require of, FenrirPhysics, in the areas where those facts constrain the character spec? Answer from Jolt's own source, docs and samples (the submodule at `packages/FenrirPhysics/libs/JoltPhysics`), pinned to v5.6.0:

- **The two characters:** `CharacterVirtual` (not a body; collide-and-slide) and `Character` (a rigid body kept upright); what each is for, what Jolt's docs recommend, and whether `CharacterVirtual`'s optional inner body is needed for other bodies, triggers and queries to see it.
- **Settings:** max slope angle, max strength (pushing), mass, padding, penetration recovery, predictive contact distance, the up vector, supporting volume; which can change on a live character.
- **Moving:** `Update` vs `ExtendedUpdate` (stick to floor, walk stairs), what each needs per call (gravity, filters, temp allocator), and how velocity is meant to be set by the host.
- **Ground state:** `EGroundState` values, the ground normal, position, velocity and body, and how they are kept up to date.
- **Contacts:** `CharacterContactListener` (validate, added, persisted, removed, solve), which thread it runs on, what it may do, and how pushing dynamic bodies and being pushed work.
- **Platforms:** how ground velocity from a moving or kinematic body reaches the character.
- **Character against character:** `CharacterVsCharacterCollision` and its cost.
- **Shape changes:** `SetShape` with its penetration check (crouch, stand up).
- **Filters:** the broad-phase, object-layer, body and shape filters a character update and its queries take, and how they map onto FenrirPhysics's private object layers.
- **Threading and timing:** whether a character update may run during or beside `PhysicsSystem::Update`, on job threads, and in what order relative to the step.
- **Determinism:** what the result depends on (update order, contact order).
- **Motion-test queries:** Jolt's `CastShape`, `CollideShape` and penetration queries a "sweep a shape and report a safe position" query could be built on.

Put the findings on a `research/jolt-characters` branch, in `.scratch/fenrir-physics/research/jolt-characters.md`, and link them here.

## Answer

Findings: branch `research/jolt-characters`, `.scratch/fenrir-physics/research/jolt-characters.md` (commit 2953281), read against Jolt v5.6.0 (`e77f175`), every claim cited to file and line.

- **Two characters:**
  - `Character` is a dynamic rigid body locked to translation, with `PostSimulation` after each step; Jolt recommends it for cheap AI.
  - `CharacterVirtual` is collide-and-slide and not a body; Jolt recommends it for players.
  - Without its optional kinematic **inner body**, a `CharacterVirtual` is invisible to rigid bodies, queries and FenrirPhysics triggers (sensors see only bodies).
- **Settings:**
  - live: up, max slope (default 50°), supporting volume, mass (70 kg), max strength (100 N), recovery speed, max hits, shape offset, shape;
  - construction-only: padding (0.02), predictive distance (0.1), iterations, tolerances, back-face mode.
- **Moving:**
  - `Update` and `ExtendedUpdate` take dt, gravity, four query filters and a temp allocator;
  - **the host applies gravity and adds ground velocity**; `Update` never changes velocity, so a jump into a ceiling keeps its upward velocity until the host clears it;
  - `ExtendedUpdate` adds steep-slope cancelling, stick-to-floor (0.5 m down) and stair walking (0.4 m up).
- **Ground state:**
  - four values: OnGround, OnSteepGround, NotSupported, InAir;
  - normal and velocity averaged over contacts;
  - cached, refreshed only by an update, `RefreshContacts`, stairs, stick-to-floor or a checked `SetShape`.
- **Contacts:**
  - `CharacterContactListener` callbacks (validate, added, persisted, removed, solve, adjust velocity) run synchronously on the updating thread; sensors get an add only;
  - pushing is an immediate `AddImpulse`, clamped by max strength and never downward;
  - the character is pushed only inside its own update.
- **Platforms:** body velocity enters as contact-plane velocity and as `GetGroundVelocity`, which the host adds. FenrirPhysics's per-tick `MoveKinematic` makes a platform's velocity visible at once. Jolt documents hovering above a descending elevator.
- **Character against character:** an app-supplied interface. The shipped one is brute force (O(N²) per tick), not thread safe, and treats the other character as kinematic.
- **Shape changes:** `SetShape(shape, maxPenetration)` refuses a shape that penetrates deeper than the limit at the current position; the inner body's shape is updated separately.
- **Filters:** four query filters are passed per call; the character has no layer of its own. FenrirPhysics's private layer mapping can supply them, and triggers are reached only if the trigger broad-phase tree is included.
- **Threading:**
  - not during `PhysicsSystem::Update` or in a step listener;
  - always uses locking interfaces;
  - characters may update concurrently with separate temp allocators;
  - Jolt's samples update characters before the step, its unit tests after.
- **Determinism:**
  - contacts are sorted, but only below `mMaxNumHits`;
  - `CharacterID` comes from a process-global counter drawn when settings are constructed;
  - the inner body's ID can be pinned;
  - update order between characters matters;
  - character state is outside `SaveState`;
  - removed-contact callbacks fire in hash-map order.
- **Motion tests:**
  - `CastShape` gives a fraction, normal and initial overlaps;
  - `CollideShape` gives a penetration axis and depth;
  - Jolt's padding back-off for a safe position is private (`ValidateMovement`);
  - FenrirPhysics's current `ShapeCast`/`Overlap` report no depth or safe fraction.

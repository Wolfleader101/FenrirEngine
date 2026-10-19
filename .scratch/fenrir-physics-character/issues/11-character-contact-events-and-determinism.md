# 11: Character contact events, ForEachContact and determinism

**What to build:** The existing contact events name characters as bodies, a character's contacts can be listed for a wall jump, and characters stay bit-identical across runs and thread counts (spec: "FenrirPhysics: contact and trigger events", "ground state" (`ForEachContact`), "the move in `PostTick`" (determinism, step 6); ADR-0012).

- **Contact events:** `ContactBegin`/`ContactEnd`, the character's entity as both body and collider, opt-in through `ContactEvents` on either side. No new events.
  - With static and kinematic bodies and other characters, from moves only: begins and ends from comparing each move's end-of-move contacts with the previous move's; `relativeSpeed` is the closing speed along the normal at the start of the move.
  - With dynamic bodies, the union of move contacts and the inner body's step contacts (from the body contact listener on job threads, buffered and merged on the main thread): touching while either has the pair, `ContactBegin` from whichever sees it first with its `relativeSpeed`, `ContactEnd` when neither has it.
  - One pair per entity pair, merged into step 6's sort by (entity A, entity B, collider A, collider B); character against character is one pair ordered by entity.
  - A character disabled, made inactive or losing `CharacterBody` ends each reported contact on the next step, as a removed body does; likewise when the other side drops `ContactEvents`.
- **`PhysicsWorld::ForEachContact(character)`** lists end-of-move contacts with point and normal; the doc comment says they come from before the step.
- **Wording:** where `ContactBegin`, `ContactEnd`, `TriggerEnter`, `TriggerExit`, `Contact` and `ContactEvents` say "rigid body entity", they now say "body entity (a rigid body or a character body)" in C++ (C# in ticket 14).
- **Determinism test:** three characters bumping into each other and four boxes, with a lift and jumps, bit-identical over 300 ticks in two runs, single- and multi-threaded.
- **DemoApp:** the ball from ticket 08 has `ContactEvents`, so its bounce's `ContactBegin` naming the character is logged.

**Blocked by:** 06 (Disabled and inactive characters); 08 (Pushing, weight and kinematic shoves); 09 (Characters among bodies).

**Status:** ready-for-agent

- [ ] Tests: walking onto and off a static floor gives one `ContactBegin` and one `ContactEnd`; a ball bouncing off a standing character gives exactly one `ContactBegin`, then one `ContactEnd`.
- [ ] Tests: character and body contacts arrive in one sorted stream, one pair per entity pair; two characters touching give one pair ordered by entity.
- [ ] Tests: disabling, making inactive and removing a character each end its contacts on the next step; dropping `ContactEvents` on the other side ends them too.
- [ ] Tests: `ForEachContact` lists a wall's point and normal for a character against it.
- [ ] The determinism scene is bit-identical over 300 ticks in two runs, single- and multi-threaded.
- [ ] B's ball logs its `ContactBegin` naming the character (checked by running).

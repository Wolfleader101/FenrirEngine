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

**Status:** resolved (built test-first on branch `claude/character-contacts-determinism-dcef05`; tests in `Tests/CharacterContacts.cpp`, tags `[characters][events]`). Where the build settled something the ticket left open:

- **What a move touches:** the end-of-move contacts Jolt marks as collided (`mHadCollision`, not discarded, not a sensor), one per collider touched, the first in Jolt's sorted order. Jolt marks a contact collided once it is within its collision tolerance and not moving away, so a character standing still keeps its floor, and a wall it walks along without pressing into does not flicker.
- **Where the union lives:** the backend flags each touch whose body is a character's (`Touch::isCharacter1/2`, set when the pair begins, so an end after the character is gone still knows) and returns each move's contacts in `CharacterMoved::contacts`. `PhysicsWorld::Impl::TrackCharacterContacts` keeps the character pairs touching after each step (`m_characterTouches`): each character's move contacts, then its body's step contacts, keyed and oriented as body contacts are; the body path skips flagged touches. A pair both sources begin at once takes the move's point, normal and `relativeSpeed`, since the move ran first. The begins and ends join the body ones before the one sort.
- **Kinematic bodies never touch a character's body in the step** (Jolt does not collide kinematic with kinematic or static), so with them and other characters the moves alone count, as the spec says, with no filtering needed.
- **`ForEachContact` on a body** also lists the characters touching it, as the events tell it (a wall lists the character leaning on it); on a character only its move's contacts, so a ball met only in the step is listed for the ball and not for the character. The doc comment says so.
- **Normals:** a character meeting a floor's rounded edge first begins with the edge's normal (y ≈ 0.99), as a body would.
- **DemoApp:** B's ball has `ContactEvents`; the scene reads `ContactBegin` each frame and logs each one naming the ball ("B: the ball began touching the character at 5.6 m/s"), the ground included.

- [x] Tests: walking onto and off a static floor gives one `ContactBegin` and one `ContactEnd`; a ball bouncing off a standing character gives exactly one `ContactBegin`, then one `ContactEnd`.
- [x] Tests: character and body contacts arrive in one sorted stream, one pair per entity pair; two characters touching give one pair ordered by entity.
- [x] Tests: disabling, making inactive and removing a character each end its contacts on the next step; dropping `ContactEvents` on the other side ends them too.
- [x] Tests: `ForEachContact` lists a wall's point and normal for a character against it.
- [x] The determinism scene is bit-identical over 300 ticks in two runs, single- and multi-threaded.
- [ ] B's ball logs its `ContactBegin` naming the character (checked by running).

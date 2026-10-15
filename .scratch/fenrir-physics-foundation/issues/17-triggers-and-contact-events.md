# 17: Triggers and contact events

**What to build:** Game code learns about collisions as engine events (spec: "FenrirPhysics: contact and trigger events"; glossary: trigger, contact). `Trigger` and `ContactEvents` are components with `bool enabled = true`. Enabled triggers report `TriggerEnter`/`TriggerExit { trigger, otherBody, otherCollider }`; contacts on pairs where either body has enabled `ContactEvents` report `ContactBegin { bodyA, bodyB, colliderA, colliderB, point, normal, relativeSpeed }` and `ContactEnd`. Every trigger is its own backend body (a kinematic sensor kept active) with its own filter, following its parent. Sleep is hidden. Events are buffered during the step, sorted by (entity A, entity B, collider A, collider B) and sent once per step. Removing a body or trigger (or disabling it) sends the matching end. `PhysicsWorld::ForEachContact(entity, fn)` answers "touching now".

**Blocked by:** 07 (Event readers in C++); 15 (Physics materials and collision layers).

**Status:** ready-for-agent

- [ ] Tests (reading through `EventReader<T>`): a box landing on the ground with `ContactEvents` gives one `ContactBegin` with the right entities, an upward normal and a plausible closing speed; lifting it gives one `ContactEnd`; without `ContactEvents` on either body, no events.
- [ ] A body entering and leaving a trigger gives one enter and one exit; a trigger child of a moving body follows it and uses its own filter.
- [ ] A body resting on the ground or inside a trigger that falls asleep produces no end or exit.
- [ ] Destroying a body in contact, removing its component, or disabling the trigger sends the matching end or exit on the next step.
- [ ] Event order is identical across runs; `ForEachContact` lists current contacts.

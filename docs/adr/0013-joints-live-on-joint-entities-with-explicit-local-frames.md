---
status: accepted
---

# Joints live on joint entities, with explicit local frames

A joint is a `Joint` component on a **joint entity** that names the two bodies it joins, not a component on one body naming the other. Its anchor on each body is an explicit frame local to that body, never captured from the scene when the joint is created. Both follow from ADR-0011's rules: an entity holds at most one component of each type, and anything switched during play should be a field edit. A survey of Unity, Godot, Avian, bevy_rapier, Unreal and O3DE is on branch `research/engine-joint-models`. The facts about Jolt are on `research/jolt-constraints`.

## The model

```cpp
struct Joint
{
    Entity bodyA;              // a body entity
    Entity bodyB;              // a body entity, or empty: the world
    JointFrame frameA, frameB; // { position, rotation }
    JointKind kind;            // tagged union: HingeJoint, SliderJoint, ...
    bool enabled = true;
};
```

- **Any entity may be a joint entity,** including one of the bodies it joins (a door holding its own hinge to the world). A body takes part in any number of joints, each on its own entity: a chain of ten links is ten joint entities.
- **Physics never reads the joint entity's `Transform` for the joint.** A joint entity may sit anywhere in the hierarchy for organisation.
- **The bodies are body entities** (a `RigidBody` entity, or a standalone static collider's), never a collider child of a compound body. An empty `bodyB` is the world, and that side is fixed by the API, never swapped privately.
- **The kind is a tagged union,** as `CollisionShape` is in `Collider`, so changing kind is an assignment.
- **Frames are local to each body entity's origin,** the way game code sees the body (`frameB` is in world space when `bodyB` is the world).
  - Their positions follow the body's world scale, so an anchor on a crate's corner stays on the corner.
  - Physics converts the frames to the backend's centre-of-mass frames privately, and reports centre-of-mass changes (a compound body rebuilt) to the backend so anchors do not drift.
- **The rest pose is defined by the frames coinciding:** a hinge's angle is 0 when `frameA` and `frameB` coincide in world space. Limits and targets are measured from that pose, so recreating the backend constraint never shifts the zero.
- **Authoring help is explicit:** `FrameFromWorld(bodyA, bodyB, anchor)` computes both local frames from one world pose when game code calls it.
- **The ECS owns the configuration;** physics watches `Joint` for adds and removes and diffs the rest each tick (ADR-0011).

## Considered options

- **A component on one body naming the other** (classic Unity's `connectedBody`, O3DE, bevy_rapier's `ImpulseJoint`). Rejected: one component per type allows one joint per body, so further joints need child entities or fixed slots. bevy_rapier's users call this "not idiomatic to ECS" and ask to move to joint entities (bevy_rapier issue #561). The model is natural only for trees.
- **One component type per kind** (`HingeJoint`, `SliderJoint`; Avian, Unity, Godot). Rejected for the reason ADR-0011 gives for colliders: changing kind becomes a structural change, and an entity could hold two contradictory kinds.
- **The joint entity's own `Transform` as the frame** (Godot, Unreal). Rejected: frames captured from a node's pose at creation or enable time are behind both engines' most common complaints (rest poses drifting, results depending on timing).
- **One world-space frame captured at creation** (Godot, O3DE's lead side, Unity's auto-configure). Rejected for the same reason, and because a world snapshot re-bases the zero angle whenever the constraint is recreated. The `FrameFromWorld` helper keeps the convenience as an explicit call.
- **Ignoring scale in frames** (Godot orthonormalises). Rejected: an anchor would leave a scaled body's surface, while its collider scales.
- **A static body entity for the world end** (Avian, bevy_rapier). Rejected: fixing a door or a chain to the world is the common case, and it should not need a dummy entity.

## Consequences

- Physics keeps a private index from each body to its joints. The backend does not remove a constraint when its body goes, and a constraint outliving its body is undefined, so physics removes a joint's constraint before either body's whenever a body is destroyed or rebuilt.
- Editing a frame, a body or the kind recreates the backend constraint, which is cheap but loses one step of warm starting. Limits, motors and springs are live edits (their fields are a later decision).
- `Joint` is the second component holding a tagged union, which reopens whether the deferred `[TaggedUnion]` source generator is now due.

# How do other engines model joints?

Type: research
Status: resolved
Blocked by: none

## Question

How do other engines put joints in front of game code? Survey Unity (`Joint` and its kinds, `connectedBody`, `ArticulationBody`), Godot (`Joint3D`, `node_a`/`node_b`, `Generic6DOFJoint3D`), Bevy's Avian (joints as entities naming `entity1`/`entity2`), bevy_rapier (`ImpulseJoint` on the child naming its parent, `MultibodyJoint`), Unreal (`PhysicsConstraintComponent`, constraint profiles) and O3DE, from their docs and source. For each:

- **Where a joint lives:** a component on one of the bodies, its own entity or node, or a resource; how a body with several joints is expressed.
- **Kinds:** one type with a kind field, or one type per kind; which kinds exist.
- **Anchors:** how the joint frame on each body is given (local frames, world point, auto-computed), and how it reacts to scaling or reparenting.
- **Joining to the world:** how a joint with one free end is expressed.
- **Limits, motors and springs:** how they are configured, and which can change at runtime.
- **Breaking:** thresholds, what happens to the joint (destroyed, disabled), and how game code hears about it.
- **Reading state:** current angle or position, applied force.
- **Connected bodies colliding:** the default, and how it is switched.
- **Scripting access:** how the joint is exposed to the engine's scripting language.
- **Leaks and complaints:** known pain points from issues and forums (frame confusion, joint explosions, edit-and-rebuild costs).

Put the findings on a `research/engine-joint-models` branch and link them here.

## Answer

Findings: branch `research/engine-joint-models`, `.scratch/fenrir-physics/research/engine-joint-models.md` (commit 7783334). One section per engine, a comparison table, and the options for FenrirPhysics with no choice made. Sources: Unity reference source 7000.0.0a10, Godot `4d10419` and godot-jolt `7f22589`, Avian `b620b67`, bevy_rapier v0.37.0 (rapier `475caa3`), O3DE `74006f5`; Unreal from the UE 5.8 docs only.

- **Where a joint lives:**
  - its own entity or node naming both bodies: Avian (`body1`/`body2`), Godot (`node_a`/`node_b`), Unreal, and Unity's ECS package (a joint entity with a body-pair component);
  - a component on one body naming the other: classic Unity (`connectedBody`), O3DE (follower names lead), bevy_rapier (`ImpulseJoint` on the child names its parent).
- **Several joints on one body:**
  - Avian allows one joint per entity (since 0.4), so each joint is its own entity;
  - bevy_rapier needs child entities, and its issue #561 calls that "not idiomatic to ECS" and asks to move to Avian's model;
  - Unity and O3DE stack components; Godot and Unreal add more nodes or components.
- **Kinds:**
  - kind is a field edit: bevy_rapier (an enum over a generic six-DOF joint), Unreal (one six-DOF type, the kind is settings);
  - everyone else has one type per kind.
- **Breaking:**
  - Unity: `breakForce`/`breakTorque`; the joint is destroyed and `OnJointBreak` does not say which joint broke. Unity 2D later added the joint and a choice of Destroy, Disable or callback only;
  - Unreal: opt-in thresholds; the component stays, with `IsBroken()` and `OnConstraintBroken`;
  - O3DE: thresholds with no notification;
  - Godot: none;
  - Avian and bevy_rapier: no thresholds by design. They report force or impulse and give a disabled marker, and game code breaks the joint.
- **Connected bodies colliding:**
  - off by default: Godot, O3DE;
  - on by default: Avian (`JointCollisionDisabled` opts out), Rapier (`contacts_enabled`);
  - Unity and Unreal: a per-joint flag, default not stated in the docs read;
  - everywhere it covers only the directly joined pair. Bugs: a Godot exception outliving its joint (#115015), Avian's marker panic (#1076).
- **Joining to the world:** an empty body field (Unity, Godot, O3DE), or a static or fixed body entity (Avian, bevy_rapier). Godot on Jolt flips which side is the world, which inverts limits.
- **State:** an angle or position only for one-free-axis joints, or swing and twist (Unreal). Force is a stored component in Avian and bevy_rapier, a getter elsewhere.
- **Common complaints:**
  - frames captured at creation or enable time drift (Unity re-derives the rest pose on re-enable);
  - runtime scale is ignored or mishandled;
  - the joint frame ignores the visual transform (bevy_rapier);
  - chain and limit instability;
  - order-sensitive runtime creation;
  - no scripting access (O3DE).

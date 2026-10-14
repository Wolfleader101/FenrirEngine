# How do other engines hide their physics backend?

Type: research
Status: resolved
Blocked by: none

## Question

How do existing engines put their physics library behind an API of their own, and where does that API leak the library underneath? Survey, from primary sources (source code and official docs):

- **Godot:** `PhysicsServer3D` and how Jolt was added as an alternative to Godot Physics (the godot-jolt extension, then the built-in module). What had to change, and which behaviour differences leaked through.
- **O3DE:** the `AzPhysics` interfaces over PhysX (and any second backend).
- **Bevy ecosystem:** Avian and `bevy_rapier` — how ECS components map to bodies and colliders, who owns state, and how they sync transforms and hierarchy.
- **Unreal:** the Chaos/PhysX abstraction (`FPhysicsInterface` or its successor), briefly.

For each: the seam's shape (runtime interface vs build-time selection), the vocabulary it owns (body, shape, collider, material, layer), how it handles IDs and lifetimes, how contact events cross threads, how queries look, and what it found impossible to hide. Close with the patterns that recur and the leaks that recur.

Write findings to a Markdown file on the `research/physics-backend-seams` branch, citing each claim's source, and link it here.

## Answer

Findings: `.scratch/fenrir-physics/research/physics-backend-seams.md` on branch `research/physics-backend-seams` (commit `afc6571`), each claim cited to pinned source or official docs. Unreal rests on its API reference only (source needs an Epic account).

- **Seam shape varies:** Godot and O3DE use runtime interfaces (Godot picks a backend by name from a project setting; O3DE in practice has one, PhysX). Unreal selects at build time through typedefs. bevy_rapier re-exports Rapier; Avian has no body backend, only a collider trait.
- **IDs:** every engine owns an opaque handle plus a reverse map to its own object, and defers freeing until after the step.
- **Transforms:** every physics API is world-space; an adapter converts to and from the local hierarchy and suppresses or detects its own write-back to avoid echoes. Hierarchies hurt everywhere: O3DE marks a transform path unsafe, Avian flattens nested bodies, bevy_rapier walks parents to the nearest body.
- **Contacts:** always buffered on worker threads and delivered on one thread after the step; reporting is usually opt-in per object.
- **Recurring leaks:** backend tuning becomes a second settings surface; behaviour differences (joint limits, margins, impulses, kinematic contacts) have to be documented; generic code ends up naming the backend; native pointers become reachable; threading and locking follow the backend's shape; godot-jolt cannot promise determinism although Jolt can.

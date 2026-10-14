# How do other engines hide their physics backend?

Type: research
Status: open
Blocked by: none

## Question

How do existing engines put their physics library behind an API of their own, and where does that API leak the library underneath? Survey, from primary sources (source code and official docs):

- **Godot:** `PhysicsServer3D` and how Jolt was added as an alternative to Godot Physics (the godot-jolt extension, then the built-in module). What had to change, and which behaviour differences leaked through.
- **O3DE:** the `AzPhysics` interfaces over PhysX (and any second backend).
- **Bevy ecosystem:** Avian and `bevy_rapier` — how ECS components map to bodies and colliders, who owns state, and how they sync transforms and hierarchy.
- **Unreal:** the Chaos/PhysX abstraction (`FPhysicsInterface` or its successor), briefly.

For each: the seam's shape (runtime interface vs build-time selection), the vocabulary it owns (body, shape, collider, material, layer), how it handles IDs and lifetimes, how contact events cross threads, how queries look, and what it found impossible to hide. Close with the patterns that recur and the leaks that recur.

Write findings to a Markdown file on the `research/physics-backend-seams` branch, citing each claim's source, and link it here.

# What is the physics world's lifecycle?

Type: grilling
Status: resolved
Blocked by: 03

## Question

How do physics worlds relate to scenes and the app?

- One physics world per scene, or one per app that only the active scene uses?
- When is a world created and destroyed: on scene creation, on activation, lazily on the first body?
- What happens to an inactive scene's world when the active scene switches (kept and paused, or torn down and rebuilt)?
- Where does a world live so that systems and native functions reach it: in the scene, in the app, in a FenrirPhysics resource?
- What per-world settings exist (gravity, step settings) and how an app sets them.

## Answer

- **One physics world per scene.** A world is created the first time its scene runs with any physics component; it is not stepped while its scene is inactive, and resumes where it was when the scene is active again; it is destroyed with its scene. Rejected: one world per app rebuilt on every scene switch. The ECS holds velocities (ADR-0011), but contact and sleep state would be lost, so resting contacts would re-fire begin and trigger-enter events after each switch, and sleeping bodies would wake.
- **The world is a scene resource.** FenrirScene gains a neutral, typed per-scene slot for packages' per-scene state (`scene.GetResource<T>()` / `AddResource`, Bevy-like), destroyed with the scene. A world's lifetime follows its scene by construction, with no scene ids or scene notifications. Rejected: a map from scene to world in the `Physics` owner, which needs stable scene ids and scene-destroyed notifications.
- **Reached through its scene.** `PhysicsWorld::Of(scene)` gives the world; per-simulation operations are on it (`AddForce(entity, ...)`, queries, `SetGravity`). C# native functions reach it through the call context's App and its active scene. The `Physics` owner from `Physics::Install` holds only process-wide state: the backend's global setup, the installed systems, the default settings.
- **Settings:** `PhysicsConfig` at `Install` gives every new world's defaults: gravity, capacities (max bodies, body pairs, contacts), worker-thread use. Gravity can change on a world at any time. Capacities are fixed once a world exists (the backend fixes them at creation), so a scene that needs different capacities sets them before its first physics tick. Step settings belong to "How does physics step?".
- **Shutdown order** (follows from the above): the backend's process-wide state must outlive every world, so shutting down the `Physics` owner first removes the world from every scene (`App::ForEachScene`).
- **FenrirApp fixes, prerequisites in the foundation spec:**
  - `App` stores scenes by value in a `std::vector`, so `CreateScene` can move every scene, leaving each `Entity`'s `EntityList*` (and any world's reference to its scene) dangling. Scenes move to stable addresses (`std::unique_ptr<Scene>` or a stable container). This is a live bug today, independent of physics.
  - `DestroyScene` erases by index without adjusting `activeSceneIndex`, so destroying a scene before the active one silently changes the active scene. It keeps the active scene correct, and destroying the active scene is refused or defined.

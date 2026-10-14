# What is the physics world's lifecycle?

Type: grilling
Status: open
Blocked by: 03

## Question

How do physics worlds relate to scenes and the app?

- One physics world per scene, or one per app that only the active scene uses?
- When is a world created and destroyed: on scene creation, on activation, lazily on the first body?
- What happens to an inactive scene's world when the active scene switches (kept and paused, or torn down and rebuilt)?
- Where does a world live so that systems and native functions reach it: in the scene, in the app, in a FenrirPhysics resource?
- What per-world settings exist (gravity, step settings) and how an app sets them.

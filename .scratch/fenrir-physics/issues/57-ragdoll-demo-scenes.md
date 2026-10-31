# What do the ragdoll demo scenes show?

Type: grilling
Status: open
Blocked by: 55, 56

## Question

What do the PhysicsDemo's ragdoll scenes show, one built in C++ and one built in C#, added to the scaffold's scene list after the vehicle scenes (the renderer map's "How are the demo apps split, and what does each show?" replaced the legacy DemoApp's F-key families with one ordered list per demo app, so the map's "F7" note no longer applies), given the spike from [Spike: a character that falls as a ragdoll and gets up, through the proposed model](55-spike-ragdoll-falling-and-getting-up.md) and the C# surface from [What does C# get for ragdolls?](56-ragdolls-in-csharp.md)?

- **The model:** which skinned character (the AnimationDemo's Quaternius human with its clips, or CesiumMan: both are downloaded demo assets under ADR-0052, and the PhysicsDemo would start fetching them), and what the PhysicsDemo gains in dependencies (FenrirSkeleton, FenrirAnimation, the renderer's skinned meshes, which it has not needed).
- **Ragdolls (C++):** the spike's scene rewritten: a walking character whose ragdoll follows as hit boxes; keys to drop it (with a shove), make an arm limp, power it, get it up, and shoot a ray that logs the bone hit; a second ragdoll to pile onto; a character body it ignores; boxes; readouts in names (mode per part, awake); a follow camera; P autopilot for smoke runs.
- **Ragdolls (C#):** built only from C# through the extension: a derived ragdoll on the same character, a drop and get-up cycle, a partial ragdoll, a collision-group demonstration (two groups), and whatever the C# ticket lists.
- **Debug lines:** a new `DebugDraw::Ragdolls` category (body-to-bone offsets, the part each body follows, motor targets) or nothing beyond `Joints` and `Colliders`; `PhysicsDebugCategories` in C# too.
- **Hand-check list** for the README, in the format of the other families' scenes, and the CTest scene-log checks (`cmake/CheckSceneLog.cmake`) each scene must log.

No ADR.

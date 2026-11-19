# What do the ragdoll demo scenes show?

Type: grilling
Status: resolved
Blocked by: 55, 56

## Question

What do the PhysicsDemo's ragdoll scenes show, one built in C++ and one built in C#, added to the scaffold's scene list after the vehicle scenes (the renderer map's "How are the demo apps split, and what does each show?" replaced the legacy DemoApp's F-key families with one ordered list per demo app, so the map's "F7" note no longer applies), given the spike from [Spike: a character that falls as a ragdoll and gets up, through the proposed model](55-spike-ragdoll-falling-and-getting-up.md) and the C# surface from [What does C# get for ragdolls?](56-ragdolls-in-csharp.md)?

- **The model:** which skinned character (the AnimationDemo's Quaternius human with its clips, or CesiumMan: both are downloaded demo assets under ADR-0052, and the PhysicsDemo would start fetching them), and what the PhysicsDemo gains in dependencies (FenrirSkeleton, FenrirAnimation, the renderer's skinned meshes, which it has not needed).
- **Ragdolls (C++):** the spike's scene rewritten: a walking character whose ragdoll follows as hit boxes; keys to drop it (with a shove), make an arm limp, power it, get it up, and shoot a ray that logs the bone hit; a second ragdoll to pile onto; a character body it ignores; boxes; readouts in names (mode per part, awake); a follow camera; P autopilot for smoke runs.
- **Ragdolls (C#):** built only from C# through the extension: a derived ragdoll on the same character, a drop and get-up cycle, a partial ragdoll, a collision-group demonstration (two groups), and whatever the C# ticket lists.
- **Debug lines:** a new `DebugDraw::Ragdolls` category (body-to-bone offsets, the part each body follows, motor targets) or nothing beyond `Joints` and `Colliders`; `PhysicsDebugCategories` in C# too.
- **Hand-check list** for the README, in the format of the other families' scenes, and the CTest scene-log checks (`cmake/CheckSceneLog.cmake`) each scene must log.

No ADR.

## Answer

No ADR, no new glossary terms.

- **The model and dependencies:** the Quaternius human and its clips (CesiumMan has one clip and no idle), fetched under ADR-0052's option; the PhysicsDemo links FenrirSkeleton, FenrirAnimation and FenrirRagdoll. The ragdoll scenes are skipped with the scaffold's "skipped" line when the assets are not fetched, so CI without assets passes.
- **Powered pose source:** game code pauses or swaps the clip when it powers the ragdoll (the demo plays the idle clip); `Powered` drives towards the base pose and `Ragdoll` gets no second pose source. A "hold this pose" source is listed as later work.
- **Ragdolls (C++):** the spike's scene rewritten: the walking human on a `CharacterBody` (root bone pinned, or an in-place clip), its ragdoll following as hit boxes; Space drops it with a shove, A toggles a limp arm with blend-back, K powers it (switching to the idle clip) with 1 and 2 stepping strength, G gets up with the weight ramp and the character re-enabled, H casts a ray from the camera and logs the bone hit, 3 drops the bystander into the lane, B drops a crate on the head, L logs state; two collision groups; readouts in names (mode, strength, awake, hips weight); the scaffold's follow camera; P autopilots the keys for the smoke run; a `CheckSceneLog.cmake` entry.
- **Ragdolls (C#):** built only from C# through `Fenrir.Ragdoll`: the same human described and built from C# (`DescribeHumanoid`, `Build`), a drop and get-up cycle, a partial ragdoll on one arm, a powered ragdoll with strength stepping, two ragdolls in different groups colliding with each other and ignoring their own characters, a bone-name hit log from a raycast; readouts in names; P autopilot; its own `CheckSceneLog.cmake` entry.
- **Debug lines:** a new `DebugDraw::Ragdolls` category in `PhysicsDebugCategories` (C++ and C#), drawn by FenrirRagdoll into the scene's `DebugDraw`: each body's bone frame and its offset line to the body, coloured by mode (following grey, simulating orange, powered green), and powered motor targets as small frames. Joints and colliders keep their categories.
- **Hand-check list:** the README gets the ragdoll scenes in the other families' format; the list is ticket 55's "React to it" items plus the spike's two findings (root-motion slide, powered legs stepping) as regressions to watch.

# 04: Scene requests in the app

Spec: [C# reach: parenting, scenes, the app, C# resources and mesh positions](../spec.md).

**What to build:** The app gains scene requests: create, activate and destroy a scene by name, queued and applied at the end of the frame, after every phase, in the order made, so no system sees a scene change mid-frame. C++'s immediate calls stay as they are. A request to destroy the active scene is refused with a logged error at its turn, so a restart (create, activate, destroy the old one) succeeds in one frame because the activate lands first. Creating a name that exists, or activating or destroying one that does not, is a logged error.

**Blocked by:** None (can start immediately)
**Touches:** `GLOSSARY.md`, `packages/FenrirApp/`

**Status:** resolved

Runs off the base, beside the scripting stack; 05 builds on it.

- [x] FenrirApp tests: requests made during a frame take effect after its last phase, in order.
- [x] A restart made of create, activate and destroy in one frame leaves only the new scene, active.
- [x] Destroying the active scene by request is refused with an error; bad names are errors.
- [x] The immediate C++ calls behave as before.
- [x] GLOSSARY.md gains *Scene request*.

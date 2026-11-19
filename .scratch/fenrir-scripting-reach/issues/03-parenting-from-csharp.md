# 03: Parenting from C#

Spec: [C# reach: parenting, scenes, the app, C# resources and mesh positions](../spec.md).

**What to build:** A C# script re-parents entities: `Entity.Parent` reads an entity's parent (or no entity), and setting it re-parents keeping the child's world transform, or unparents when set to no entity; `Entity.SetParent(other, keepWorldTransform)` chooses to keep the local transform instead. The change is deferred on the call context and applied when the system ends, in the order recorded, beside inserts and removes; `Relationship` stays read-only in C#. Re-parenting under one's own descendant, or under an entity in another scene, is refused with a logged error. Entities spawning a model made can be re-parented like any other. ScriptingDemo's showcase gains a row: a C# entity parented under a box, following it as it bobs.

**Blocked by:** None (can start immediately)
**Touches:** `packages/FenrirScripting/src/`, `packages/FenrirScripting/api/`, `packages/FenrirScripting/Managed/Fenrir.Scripting/`, `packages/FenrirScripting/Tests/`, `examples/ScriptingDemo/`

**Status:** ready-for-agent

Stacked on 02.

- [ ] Tests through the script fixture: the world transform kept by default; the local transform kept on request; unparenting keeps the world transform; the change not visible inside the system that made it and visible after.
- [ ] Tests: re-parenting under a descendant and under another scene's entity are refused with an error; a spawned model's entity re-parents.
- [ ] A test pins what happens to a destroyed parent's children, matching C++.
- [ ] ScriptingDemo shows the parented entity following the box (a hand check), and its smoke run passes.
- [ ] The API description and generated bindings are current.

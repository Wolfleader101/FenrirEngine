# 27: ScriptingDemo: the scripting showcase ported

**What to build:** The legacy DemoApp's scripting showcase runs unchanged as ScriptingDemo's one scene, "Showcase", on the demo scaffold, with its backpacks drawn by `MeshRenderer` (spec: "Demo apps" (ScriptingDemo, assets); ticket 23).

- **`examples/ScriptingDemo/`** through `fenrir_add_demo_app`, built only with scripting on, with its C# scripts project referencing `Fenrir.Demo`.
- **"Showcase"** is the legacy scripting demo unchanged: `Spin`, `Bob`, Sparks, the C++→C# key event, the C#→C++ `Burst`, the C#-only `Expired`, hot reload, the `R` and `Space` keys and its README table. Only what the scaffold forces changes: scene setup through the scaffold, keys through `Keyboard`/`Fenrir.Input`, the backpacks as `SpawnModel` or `MeshRenderer` entities instead of `DemoMesh`, and the camera through the free-fly camera.
- Its README carries the legacy table and the hand checks. The legacy `.glsl` shaders are not carried over.
- Smoke tests registered per backend.

**Blocked by:** 25 (The Fenrir.Renderer scripting extension and C# demo scenes).

**Touches:** `examples/ScriptingDemo/`, `examples/CMakeLists.txt`

**Status:** resolved
Hand check: ScriptingDemo's Showcase behaves as the legacy scripting demo did, each row of the table in `examples/ScriptingDemo/README.md` checked (its "Hand checks" list): the backpacks spin, Backpack 2 bobs with its name counting the phase, R reverses the spin, Space bursts five sparks that burn out, and hot reload, editing `scripts/Spin.cs` while the demo runs and building `ScriptingDemoDeployScripts`, keeps the spin angles.

- [x] `FENRIR_DEMO_SMOKE` with `FENRIR_DEMO_KEYS=1` runs "Showcase" on null and GL with no errors and the legacy log lines (the key event, `Burst`, `Expired`).
- [x] Every behaviour in the legacy README table is present (checked against the legacy scripts).
- [ ] By hand: the backpacks spin and bob, sparks burst on `Space`, `R` and hot reload work as in the legacy demo (checked by hand).

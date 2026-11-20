# 01: Rename gamepadNavigation to directionalNavigation

Spec: [Arrow keys move UI focus](../spec.md).

**What to build:** `UIRootConfig::gamepadNavigation` is renamed `directionalNavigation` everywhere: FenrirUI, its API description and C# binding, the demo UI kit (which keeps it off), FenrirUI's README and ADR-0065's wording, to match the glossary's Directional navigation. Behaviour is unchanged: it still covers the d-pad and the left stick until the next ticket adds the arrow keys.

**Blocked by:** None (can start immediately)
**Touches:** `GLOSSARY.md`, `packages/FenrirUI/include/FenrirUI/`, `packages/FenrirUI/src/`, `packages/FenrirUI/Tests/`, `packages/FenrirUI/api/`, `packages/FenrirUI/Scripting/`, `packages/FenrirUI/Managed/`, `examples/DemoScaffold/`, `packages/FenrirUI/README.md`, `docs/adr/0065-*.md`

**Status:** ready-for-agent

Bottom of the stack 01, 02, 03.

- [ ] No use of `gamepadNavigation` remains in the repo.
- [ ] FenrirUI's tests and the demo apps' smoke runs pass unchanged.
- [ ] The API description and generated bindings are current.
- [ ] GLOSSARY.md's *Gamepad navigation* becomes *Directional navigation* (from the D-pad, the left stick, the arrow keys or a direction the game sends; *Avoid:* gamepad navigation, its old name), and *Focusable* says a direction rather than a gamepad direction.

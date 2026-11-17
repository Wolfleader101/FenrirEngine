# 08: AnimationDemo on the kit

**What to build:** AnimationDemo's screen-text readouts become readouts in the demo panel, in its C++ scenes and, through Razor, its C# twins. Every scene gets key rows, and the first scene and Blending get tutorials.

**Spec:** `.scratch/demo-ui/spec.md`, "Per-demo moves" (AnimationDemo). The inventory is `.scratch/demo-ui/research/inventory.md`, "AnimationDemo".

- **C++ scenes:** every `draw->ScreenText` readout becomes a scene section of readouts with the same values:
  - Playback, Skeleton, Blending, Bone attachments, Crowd and Going limp.
  - Skin diagnostics' static line becomes a `Text` row.
  - Crowd may use `frameStats` for its frame line.
  - The clip-finished message stays a message.
- **C# twins:** Playback (C#) and Bone attachments (C#) mount Razor panels (`razorPanel`) of `Readout`s in their slots, replacing `DebugDraw.ScreenText` in `scripts/Playback.cs` and `scripts/BoneAttachments.cs`.
- **Keys:** the key help of every scene becomes key rows, the C# twins' included (`CSharpScenes.cpp`).
- **Tutorials:**
  - The first scene, three to five steps: the scene list, F1, the free-fly camera, the scene's keys.
  - Blending: the crossfade, the upper-body layer and the lean, each step ending when its key is pressed.
- **World text labels stay.**
- **README:** a "The demo panel" section listing each scene's readouts; rewritten hand checks.

**Blocked by:** 05

**Touches:** `examples/AnimationDemo/`

**Status:** resolved

- [x] No `ScreenText` remains in `examples/AnimationDemo/` (C++ or C#).
- [x] `AnimationDemo-Smoke-null` passes with scripting off and on. With scripting off, the C# twins are not listed, as today.
- [ ] Hand checks, on OpenGL:
  - every scene's readouts change as its keys are pressed
  - the C# twins' Razor readouts do the same
  - the tutorials show once and step
- [ ] Clang and MSVC x64 debug build without new warnings.

# 01: Rules test executable, change buffer cases and the probe

**What to build:** A maintainer can check the change buffer's rules without starting .NET (spec: `spec.md`, "Seam 1" and "The public `Scripting` interface").

FenrirScripting gets a second test executable that starts no runtime and needs no fixture assembly or environment variable. It reuses the headless App helper and reaches the change buffer through the package's private source directory, which is the only internal it is given. The change buffer's interface does not change: it is tested as it stands, with native and dynamic component registrations and a real entity list.

The rules that are checked today only by driving C# move there with their expected values unchanged. `ApplyChanges`, `DiscardChanges` and their result type leave the public `Scripting` interface for the test probe, since only tests called them.

**Blocked by:** None (can start immediately).

**Status:** resolved

- [x] A new test executable is built with the other FenrirScripting tests, runs in CTest in declaration order, starts no .NET runtime, and needs no fixture path or environment variable.
- [x] Cases at the change buffer pin: changes apply in the order recorded; a change naming an entity an earlier change destroyed is skipped with a warning and never reaches a reused slot; destroying twice is harmless and not reported; a created entity is usable at once and gets Transform, Name and Relationship only at playback; discarding drops every change and destroys entities created and not yet played back.
- [x] Cases pin each refusal code (unknown component, read-only, required, entity not alive, scripting not running, unknown event), that an event is put in its queue at playback and not before, that an event whose queue has gone is skipped with a warning, and that an add copies its value bytes when it is recorded.
- [x] The C#-driven cases for these rules are removed, or cut down to the part that needs C# (recording from C#, the messages built from the codes). No rule is left untested between steps, and no behaviour is covered twice.
- [x] `Scripting` no longer declares `ApplyChanges`, `DiscardChanges` or the change summary type. The probe provides them and the remaining tests use the probe. `FindComponent` stays public.
- [x] The package README and the doc comments no longer describe those operations as something an app calls.
- [x] The full CTest suite passes and the DemoApp builds.

## Comments

Done. Notes:

- **Refusal code `NotActive`.** The ticket listed "scripting not running" among the buffer's refusal codes. The buffer never returns it: it is the built-in extension's "not running" value for the four change functions, so it is covered by construction and by the existing not-live case, which still calls playback and discard after `Shutdown` through the probe.
- **Moved or removed C# cases.** "destroy then add on the same entity leaves it destroyed" and "discarding changes leaves no entities behind" were buffer rules only; they are removed with the fixture methods that only they used. "changes apply in the order they were made" is cut to what needs C# (calls from C# reach the buffer in order, a native component takes its value from C#) and renamed. "reads see live state" and "get and structural changes fail clearly" stay: they are about what C# sees.
- **Seam checked.** A mutation of the buffer (a discard that no longer destroys created entities) fails its case; the other mutation tried (dropping the redundant alive check before an add) is equivalent, because `EntityList::Add` refuses a dead entity itself.
- **Ticket 03 note.** `Scripting::ApplyChanges`, `DiscardChanges` and `ChangeSummary` still exist, now private and used only by the probe, because the probe is a friend of `Scripting` and reaches the state through them (the same way it reaches `ReadDiagnostics`).

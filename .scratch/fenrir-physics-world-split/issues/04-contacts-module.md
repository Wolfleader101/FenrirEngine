# 04: Contacts module: contact and trigger events

**What to build:** contact and trigger events and `ForEachContact` move into one private module, Contacts, and joints send their own `JointBroken` events right after it (spec: "FenrirPhysics: the modules" (Contacts, Joints), "the stages of `Step`" (events)).

- **`Contacts`** (`src/Contacts.hpp`, `src/Contacts.cpp`, in `Fenrir::Detail`) owns `ContactKey`, `OverlapKey`, `Oriented`, the reported contacts and overlaps, the character touches, `ReportsContacts`, today's `SendEvents` and `TrackCharacterContacts`, and `ForEachContact`. It reads the backend's touch changes and touches, Bodies' const view (`AttachedBodyOf`, `ResolveBody`), and each character's end-of-move contacts, which character code exposes read-only.
- **Order kept:** begins and ends from bodies and characters in one list, sorted as today; then enters and exits; sent begins, ends, enters, exits.
- **`JointBroken`** is sent by joint code from its own list, sorted by joint entity, right after Contacts sends, so the send order is unchanged.
- Contact debug lines draw through `DebugCanvas` from Contacts or from the backend's touches, as today.
- Behaviour, warnings and order unchanged; FenrirPhysics's and the scripting tests pass unedited, the event and character contact tests included.

**Blocked by:** 03 (Poses module).

**Touches:** `packages/FenrirPhysics/src/` (new `Contacts`; `WorldImpl.hpp`, `PhysicsWorld.cpp`, `Characters.cpp`, `JointBreaking.cpp`, `DebugLines.cpp`), `packages/FenrirPhysics/CMakeLists.txt`.

**Status:** ready-for-agent

- [ ] The reported contacts, overlaps and character touches are members of Contacts only.
- [ ] `FenrirPhysicsTests` and the physics scripting tests pass unedited on `clang-x64-debug`.
- [ ] Smoke runs (`FENRIR_DEMO_KEYS=1`) with no errors: `"Physics (C#)"`, `"Joints (C++)"`, `"Joints (C#)"`, `"Character (C++)"`, `"Character (C#)"`, `"Vehicles (C++)"`.

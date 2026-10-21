# 04: Contacts module: contact and trigger events

**What to build:** contact and trigger events and `ForEachContact` move into one private module, Contacts, and joints send their own `JointBroken` events right after it (spec: "FenrirPhysics: the modules" (Contacts, Joints), "the stages of `Step`" (events)).

- **`Contacts`** (`src/Contacts.hpp`, `src/Contacts.cpp`, in `Fenrir::Detail`) owns `ContactKey`, `OverlapKey`, `Oriented`, the reported contacts and overlaps, the character touches, `ReportsContacts`, today's `SendEvents` and `TrackCharacterContacts`, and `ForEachContact`. It reads the backend's touch changes and touches, Bodies' const view (`AttachedBodyOf`, `ResolveBody`), and each character's end-of-move contacts, which character code exposes read-only.
- **Order kept:** begins and ends from bodies and characters in one list, sorted as today; then enters and exits; sent begins, ends, enters, exits.
- **`JointBroken`** is sent by joint code from its own list, sorted by joint entity, right after Contacts sends, so the send order is unchanged.
- Contact debug lines draw through `DebugCanvas` from Contacts or from the backend's touches, as today.
- Behaviour, warnings and order unchanged; FenrirPhysics's and the scripting tests pass unedited, the event and character contact tests included.

**Blocked by:** 03 (Poses module).

**Touches:** `packages/FenrirPhysics/src/` (new `Contacts`; `WorldImpl.hpp`, `PhysicsWorld.cpp`, `Characters.cpp`, `JointBreaking.cpp`, `DebugLines.cpp`), `packages/FenrirPhysics/CMakeLists.txt`.

**Status:** resolved

- [x] The reported contacts, overlaps and character touches are members of Contacts only.
- [x] `FenrirPhysicsTests` and the physics scripting tests pass unedited on `clang-x64-debug`.
- [x] Smoke runs (`FENRIR_DEMO_KEYS=1`) with no errors: `"Physics (C#)"`, `"Joints (C++)"`, `"Joints (C#)"`, `"Character (C++)"`, `"Character (C#)"`, `"Vehicles (C++)"`.

**Resolved by:** PR #PRNUM (branch claude/physics-world-split-04-contacts), into claude/physics-world-split-spec. Contacts is `src/Contacts.hpp`/`.cpp` in `Fenrir::Detail`, holding the reported contacts, overlaps and character touches, `ContactKey`, `OverlapKey`, `Oriented` and `ReportsContacts`. It reads the backend and Bodies (`AttachedBodyOf`, `ResolveBody`) through const references. Until Characters is a module (ticket 05), the characters' end-of-move contacts reach it as arguments from the world: `Step` passes `Send` each character's contacts in entity order (`CharacterMoveContacts`), and `ForEachContact` passes the asked entity's (`CharacterMoveContactsOf`, null for an entity with no backend character). Both are in `Characters.cpp`, so ticket 05 moves them into Characters as its read-only view. `JointBroken` is sent by `SendBroken` in `JointBreaking.cpp`, right after `Send`; both sort through `src/SortAndSend.hpp`. `Contacts::Draw` draws the contact lines, in the same place among the categories. `clang-x64-debug` passes 81/81 tests, and `scripts/demo-smoke.sh` passed all nine physics scenes.

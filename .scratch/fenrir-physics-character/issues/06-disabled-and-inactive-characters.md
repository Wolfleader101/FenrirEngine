# 06: Disabled and inactive characters

**What to build:** A cutscene can switch a character off with a field, and every misuse (a character beside a `RigidBody`, with no `Collider`, with a mesh, with a solid child collider, named by a joint) is visible with one warning and never crashes (spec: "the `CharacterBody` component" (inactive, no compounds, disabled), "the move in `PostTick`" (inactive scenes), "other bodies" (joints)).

- **Disabled** (`enabled = false`): not moved, invisible to bodies, queries and triggers, `velocity` not written back, `Transform` owned by game code, `state` reset to the defaults, carried velocity cleared. Re-enabling refreshes contacts and ground state as a teleport does and snaps interpolation.
- **Inactive:** beside a `RigidBody` (the `RigidBody` works), with no `Collider`, or with a `MeshShape`. The component is kept, there is no backend character or inner body, one warning is logged on each transition from valid to invalid, and it comes back by itself once valid. It does not move, nothing sees it, nothing is written back, and `state` is reset.
- **No compounds:** a solid collider on a child entity with no `RigidBody` of its own is not part of the character, gets no body and logs one warning; a child `Trigger` works as beneath any body. A character is never part of a parent `RigidBody`'s compound.
- **Joints:** a `Joint` naming a character entity is an inactive joint under the joints spec's rule (component kept, one warning, back once valid).
- **Ground never stale:** ground fields are cleared when disabled or inactive; a ground entity destroyed before game code reads it is detected by its version.
- **Paused worlds:** a paused world moves no character and keeps `velocity`, carried velocity and `state`; on resume characters carry on with no refresh.
- **DemoApp:** 4 toggles `enabled`, M swaps to a mesh collider and back.

**Blocked by:** 05 (Teleports, rotation, parents and interpolation): re-enabling refreshes as a teleport.

**Status:** resolved

Built test-first on branch `claude/character-inactive-df55ab`; the DemoApp hand check is the user's. Where the build settled something the ticket left open:

- Disabled and inactive are one path: the drain destroys the backend character (and so its inner body) of one that is disabled or no longer valid, and makes it again, in the same entity-order batch as new characters, once it is enabled and valid. Re-enabling is therefore a new character: contacts and ground come from that tick's move where it stands, and interpolation snaps as for a new character, with no separate teleport. Carried velocity (ticket 07) lives with the backend character, so it is cleared with it.
- The valid-to-invalid check runs every tick against what the entity has now, so a field edit (a `Collider` changed to a `MeshShape`, or a hull whose mesh is released) makes a character inactive as a structural change does. The warning names the entity and the reason. Ticket 10's checked reshape no longer needs to skip meshes: one never reaches it.
- `state` is reset in step 5 for each `CharacterBody` with no backend character, every tick, so game code's edits to it are overwritten there too; nothing else is written.
- A solid collider is a character's child when walking up from it finds a character (a `CharacterBody` with no `RigidBody`) before a body host. A `RigidBody` of its own, or one between, makes it a body or part of that body's compound as before. A child `Trigger` is its own body, and the character counts as the body it is attached to, so the character never sets off its own child trigger.
- A joint naming a character says so in its inactive warning ("is a character body, which a joint cannot join").
- `groundBody` and `groundCollider` are versioned handles, so a ground destroyed before game code reads them is already not alive; and a paused (inactive) scene's characters keep everything, both with no new code (tested).
- DemoApp: 4 toggles `enabled` and M swaps the `Collider` to a box-sized triangle mesh and back. The scene's game code stops driving the character while it is off or inactive, so it carries on where it stands rather than falling with gravity piled up. The readings show "disabled" and "inactive (mesh)".

- [x] Tests: a disabled character is unmoved, unseen by a ray and a trigger, its `velocity` untouched and its `state` reset; re-enabling refreshes `state` that tick and snaps interpolation.
- [x] Tests: inactive beside a `RigidBody`, with no `Collider` and with a mesh, each with exactly one warning per transition, and active again once fixed; a solid child collider warned and ignored; a trigger child entered by bodies.
- [x] Tests: a joint naming a character is inactive with its warning; `groundBody` naming a destroyed platform reads as not alive; a paused world changes nothing.
- [ ] 4 off lets boxes pass through and resumes where it stands; M gives one warning, the character inactive, and back (checked by hand).

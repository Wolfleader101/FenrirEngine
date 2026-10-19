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

**Status:** ready-for-agent

- [ ] Tests: a disabled character is unmoved, unseen by a ray and a trigger, its `velocity` untouched and its `state` reset; re-enabling refreshes `state` that tick and snaps interpolation.
- [ ] Tests: inactive beside a `RigidBody`, with no `Collider` and with a mesh, each with exactly one warning per transition, and active again once fixed; a solid child collider warned and ignored; a trigger child entered by bodies.
- [ ] Tests: a joint naming a character is inactive with its warning; `groundBody` naming a destroyed platform reads as not alive; a paused world changes nothing.
- [ ] 4 off lets boxes pass through and resumes where it stands; M gives one warning, the character inactive, and back (checked by hand).

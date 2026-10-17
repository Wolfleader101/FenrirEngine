# 11: Teleports, shape and scale changes

**What to build:** Moving or reshaping a jointed body never tears or snaps its joints (spec: "FenrirPhysics: edge cases", teleports and shapes and scale).

- **Teleports:** writing one end's `Transform` does not move the other end; the joint pulls them back together. Physics privately resets warm starting on the teleported body's joints. Kinematic moves are not teleports; reparenting a body entity is one. Promised: a teleport never breaks a joint and adds no velocity to its bodies (the backend corrects the gap by position, so neither `GetJointState` nor breaking sees it). Not promised: how fast the bodies are pulled back. Documented: move a whole jointed group in the same tick; there is no group-teleport helper.
- **Shapes:** a shape or centre-of-mass change on a body that keeps its backend body (a `Collider` edit, a compound child added, removed or moved) keeps the constraint; physics tells the backend privately, so anchors stay put relative to the body's origin.
- **Scale:** a scale change recreates the constraint like a frame edit. A rebuilt body has its joints' constraints removed before it and created after it.
- The Joints (C++) scene adds keys 4 teleport the door alone, 5 teleport the door and its frame, 6 scale link 5, 7 add or remove a lump on link 6, 8 reshape link 6.

**Blocked by:** 09 (Breaking and JointBroken).

**Status:** ready-for-agent

- [ ] Tests: a lone teleport is pulled back; it never breaks a joint, even with a low threshold, and adds no velocity to the joint's bodies; warm starting is reset.
- [ ] Tests: a `Collider` edit and a compound child added, removed or moved keep anchors put relative to the body's origin; a scale change keeps anchors on the scaled body; a body rebuild keeps its joints.
- [ ] 4 pulls the door back without a jolt, 5 lines up at once, and 6, 7 and 8 keep anchors put (checked by hand).
- [ ] `FENRIR_DEMO_SCENE` with `FENRIR_DEMO_KEYS=1` runs the Joints (C++) scene through every key registered so far with no errors, in Debug and Release.

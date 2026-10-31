---
status: accepted
---

# Ragdolls are ordinary bodies and joints mapped to bones by name, glued by FenrirRagdoll

ADR-0028 and ADR-0038 have **ragdoll glue** write simulated body poses into a skeleton as world `PoseOverride` targets and read the base pose for powered ragdolls' motor targets; ADR-0040 has glue move bone-following kinematic bodies each tick from the simulation pose. Both leave the bone-to-body mapping, the bodies, the joints and where the glue lives to the physics map's ragdoll spec. FenrirPhysics depends only on the core (ADR-0010's map notes), FenrirSkeleton depends on no physics, and neither may change for ragdolls. Jolt offers a `Ragdoll` class, but it is created from its own `RagdollSettings` and cannot wrap bodies and constraints that already exist.

## The model

- **A new package, FenrirRagdoll,** depends on FenrirPhysics, FenrirSkeleton and the core, and is built only when physics is on. It owns the ragdoll components, the builder that spawns a ragdoll from a description, the glue systems, and its own scripting extension (ADR-0009) with a `Fenrir.Ragdoll` contract, built only with scripting on. FenrirPhysics gains nothing for ragdolls beyond general collision groups (`CollisionGroup`), and FenrirSkeleton nothing.
- **Bodies and joints are ordinary.** Each ragdoll body is a `RigidBody` plus `Collider` entity; each joint between a body and its parent body is an ordinary `Joint` entity (ADR-0013), a hinge or a swing-twist. Nothing is a private Jolt `Ragdoll`. Events, queries, materials, breaking, debug lines and the C# physics surface apply to them unchanged.
- **The mapping is one component per body entity:**

  ```cpp
  struct RagdollBody
  {
      Entity ragdoll;    // the skeleton entity carrying the Ragdoll
      BoneName bone;     // the bone this body follows and drives, by name
      Transform offset;  // the body's pose relative to its bone
  };
  ```

  The ragdoll link is explicit, never "my parent". The bone is named, as `BoneAttachment`'s is (ADR-0037), so one description serves every instance of a skeleton, and the glue resolves it to an index per instance through `Skeletons::FindBone`. A body with no `RigidBody`, or a name no bone has, is skipped with one warning.
- **The offset is stored once and used both ways.** The builder computes it from the rest pose, `offset = bone⁻¹ × body`; game code may edit it; the glue never recalibrates it. Following writes the body's `Transform` as `bone × offset` (ADR-0040); simulating writes the bone's world target as `body × offset⁻¹` (ADR-0038). A rest-pose change in the skeleton asset leaves offsets stale until the ragdoll is rebuilt, which is documented.
- **`Ragdoll` sits on the skeleton entity** (the `SkeletonPose` entity), so a skeleton has at most one ragdoll. It holds the collision group its bodies share, the root body entity and the ragdoll's state; its mode fields are the switching decision's ("How does a ragdoll switch between following and simulating, and how is it powered?"). While the entity has no ready `SkeletonPose` the ragdoll is inactive: it writes nothing and warns once.
- **A ragdoll's bodies are its hit boxes.** While following they are kinematic, so a raycast or a contact event names a body entity and game code reads its `RagdollBody::bone`. A character that never falls keeps a `Ragdoll` in following mode; there is no separate hit-box component.
- **Lifetime.** The bodies live in the skeleton's scene. Destroying the skeleton entity or removing `Ragdoll` leaves the body entities, which are game code's to destroy (the builder offers a destroy helper). A body destroyed while simulating stops being written, and its bone keeps its last override (ADR-0038).

## Considered options

- **An optional module inside FenrirPhysics,** compiled when FenrirSkeleton is present. Rejected: physics would depend on a non-core package, against the map's rule and ADR-0010's intent.
- **App code, with a reference copy in the PhysicsDemo.** Rejected: every app would rewrite the same glue, and C# would get nothing.
- **Jolt's `Ragdoll` behind the seam.** Rejected: a second, private body-and-joint path for what `Joint` already does; its helpers (set pose, drive to pose, reset warm starts) are loops over bodies and motors the glue writes itself.
- **A mapping table on the `Ragdoll` component.** Rejected: components hold no growable arrays and stay blittable for C#, the reason vehicle wheels are entities (ADR-0015).
- **A separate hit-box component.** Rejected: one mechanism, and a ragdoll that never falls costs nothing.
- **A ragdoll entity of its own naming the skeleton.** Rejected: the skeleton entity is already what glue, attachments and C# address, and one ragdoll per skeleton is a fact.

## Consequences

- A new package and a new scripting contract; the demo scaffold installs it where physics is installed.
- The description a ragdoll is built from, its default bodies, joints, motors and strengths, and the modes that switch bodies between kinematic and dynamic are the ragdoll spec's next decisions, within this model.
- Vocabulary (`GLOSSARY.md`): *ragdoll*, *ragdoll body*, *ragdoll glue*; avoid *physical bone*, *physics asset*, *rig* and Jolt's *part*.
- Listed, not built: offsets recalibrated across skeleton rest-pose changes; several ragdolls on one skeleton.

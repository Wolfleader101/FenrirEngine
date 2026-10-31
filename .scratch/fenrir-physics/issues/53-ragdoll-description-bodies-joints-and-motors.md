# From what description is a ragdoll built, and with which bodies, joints, motors and strengths?

Type: grilling
Status: claimed
Blocked by: 52

## Question

Given the model from [How is a ragdoll modelled in the ECS, and where does the glue live?](52-ragdolls-in-the-ecs.md), what does game code hand over to get a ragdoll, and what does it get? Findings: [What does Jolt offer for ragdolls?](49-what-jolt-offers-for-ragdolls.md) (the samples' human: bodies, shapes, constraint kinds, limits, motor settings) and [How do other engines build and drive ragdolls?](50-how-other-engines-build-ragdolls.md) (Unity's wizard, Unreal's Physics Asset, Godot's physical skeleton).

- **The description:** a plain-data list of parts, each naming a bone, a parent part, a `CollisionShape`, a mass, a body-to-bone offset and a joint kind with limits (`RagdollDescription`, hand-authored in code or data), or a derivation from a skeleton (`DescribeRagdoll(SkeletonData, HumanoidBones)`: capsules along each listed bone to its child, sized by bone length, with default limits per part), or both: a derivation that produces a description game code can edit before building. Whether a description is an asset (FenrirAssets, shared between instances) or a value.
- **Derivation from a skeleton:** the input it needs beyond the skeleton (which bones are the hips, spine, head and limbs: a humanoid bone map by name, since bone names differ between Mixamo, Quaternius, UE and glTF sample rigs), how it sizes shapes (bone length and a radius guess), how it spreads mass (total mass by part fractions), and what it refuses (a skeleton with no named root, a cycle in the parts).
- **The builder:** a function that spawns body, collider and joint entities from a description under a parent, writes the mapping components and returns them (so game code can edit any entity afterwards); whether it is in the glue package or physics; whether it also sets the collision group ("How do general collision groups work?") and the initial motion type.
- **Bodies:** the usual set (hips, spine, chest, head, upper and lower arms, hands, upper and lower legs, feet) and whether hands and feet are in the default; capsules by default, boxes allowed; mass fractions; whether a body may span several bones.
- **Joints:** hinges for elbows and knees, swing-twist for the rest (as the joints spec chose), joined parent to child at the child bone's origin, frames from the rest pose through `FrameFromWorld`; default limits per part (the Jolt sample's or Unity's numbers as a start); `collideConnected = false` as joints default; friction as the resting stiffness.
- **Motors and strengths:** every joint's motor off by default; a powered ragdoll turns them to `Position` mode with targets from the base pose ("How does a ragdoll switch between following and simulating, and how is it powered?" decides when); how strength is expressed to game code (one `strength` scalar per ragdoll or per part that the glue turns into `maxTorque` and the motor spring's frequency and damping, as Unreal's profiles do, or the raw `JointMotor`/`SwingTwistMotor` fields the joints spec already exposes), and whether a description carries per-part strengths.
- **Tuning that stays private:** anything from Jolt's `Stabilize` or constraint priorities worth doing automatically for a ragdoll's joints (the joints spec refused solver overrides as tuning).
- **Vocabulary:** *ragdoll description*, *part*, *humanoid bone map*, *strength*; words to avoid.

Probably no ADR (a description format is a spec detail), unless the description becomes an asset kind.

# What Jolt offers for constraints

Research for [20: What does Jolt offer for constraints?](../issues/20-what-jolt-offers-for-constraints.md).

**Pinned source:** JoltPhysics **v5.6.0**, commit `e77f175595e64cb44218cc9d9d56fc365ad0e36a` (tagged 2026-07-11), the submodule at `packages/FenrirPhysics/libs/JoltPhysics`. Every link points at that tag. Sources are Jolt's own headers and `.cpp` files, `Docs/Architecture.md`, `Samples/Tests/Constraints/` and `Samples/Utils/RagdollLoader.cpp`.

Link shorthand: `Architecture.md` is [`Docs/Architecture.md`](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md); Markdown line links use `?plain=1`. `Constraints/` is `Jolt/Physics/Constraints/`.

---

## 1. Kinds

Every constraint is a `Constraint`. A two-body constraint is a `TwoBodyConstraint`, made from a `TwoBodyConstraintSettings` by `Create(Body &inBody1, Body &inBody2)`. Body 1 is "usually considered the parent, Body2 the child" ([TwoBodyConstraint.h L20-L26](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/TwoBodyConstraint.h#L20-L26)). The sub-type enum lists Fixed, Point, Hinge, Slider, Distance, Cone, SwingTwist, SixDOF, Path, Vehicle, RackAndPinion, Gear, Pulley and four `User` slots ([Constraint.h L32-L54](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/Constraint.h#L32-L54)). `VehicleConstraint` is a one-body `Constraint` plus `PhysicsStepListener`, not a `TwoBodyConstraint` ([VehicleConstraint.h L65](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Vehicle/VehicleConstraint.h#L65)). The doc's list of kinds is [Architecture.md L469-L483](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md?plain=1#L469-L483).

**Settings common to every kind** (`ConstraintSettings`): `mEnabled`, `mConstraintPriority`, `mNumVelocityStepsOverride`, `mNumPositionStepsOverride`, `mDrawConstraintSize`, `mUserData` (a `uint64`) ([Constraint.h L77-L94](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/Constraint.h#L77-L94)). Every two-body kind except Gear and Rack-and-pinion also has `mSpace` (section 4).

| Kind | DOF removed | Frame per body | Kind-specific settings | Source |
|---|---|---|---|---|
| **Fixed** (weld) | all 6 | point, X axis, Y axis | `mAutoDetectPoint` (world space only: weld in the current relative pose) | [FixedConstraint.h L25-L39](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/FixedConstraint.h#L25-L39) |
| **Point** (ball) | 3 translation | point | none | [PointConstraint.h L24-L32](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/PointConstraint.h#L24-L32) |
| **Distance** (stick/rope) | 1 (keeps two points within [min, max]) | point | `mMinDistance`, `mMaxDistance` (negative means "current distance", world space only), `mLimitsSpringSettings` | [DistanceConstraint.h L25-L40](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/DistanceConstraint.h#L25-L40) |
| **Hinge** (revolute) | 5 | point, hinge axis, normal axis (angle 0 where normals align) | `mLimitsMin` in [-pi, 0], `mLimitsMax` in [0, pi], `mLimitsSpringSettings`, `mMaxFrictionTorque`, `mMotorSettings` | [HingeConstraint.h L27-L57](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/HingeConstraint.h#L27-L57) |
| **Slider** (prismatic) | 5 | point, slider axis, normal axis | `mAutoDetectPoint`, `mLimitsMin` <= 0 <= `mLimitsMax`, `mLimitsSpringSettings`, `mMaxFrictionForce`, `mMotorSettings` | [SliderConstraint.h L28-L59](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/SliderConstraint.h#L28-L59) |
| **Cone** | 3 translation + swing beyond a cone; twist free | point, twist axis | `mHalfConeAngle` | [ConeConstraint.h L26-L37](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/ConeConstraint.h#L26-L37) |
| **Swing-twist** | 3 translation + limited swing and twist | point, twist axis, plane axis | `mSwingType` (Cone or Pyramid), `mNormalHalfConeAngle`, `mPlaneHalfConeAngle`, `mTwistMinAngle`, `mTwistMaxAngle`, `mMaxFrictionTorque`, `mSwingMotorSettings`, `mTwistMotorSettings` | [SwingTwistConstraint.h L31-L60](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/SwingTwistConstraint.h#L31-L60); swing types [SwingTwistConstraintPart.h L14-L18](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/ConstraintPart/SwingTwistConstraintPart.h#L14-L18) |
| **Six-DOF** | chosen per axis | point, X axis, Y axis | per axis (TX, TY, TZ, RX, RY, RZ): `mLimitMin`/`mLimitMax` (min > max fixes, ±FLT_MAX frees), `mMaxFriction`, `mMotorSettings`; `mLimitsSpringSettings` for the 3 translation axes only; `mSwingType` | [SixDOFConstraint.h L23-L97](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/SixDOFConstraint.h#L23-L97) |
| **Path** | body 2 follows a spline fixed to body 1 | path position and rotation relative to body 1 | `mPath` (a ref-counted `PathConstraintPath`, e.g. Hermite), `mPathFraction`, `mMaxFrictionForce`, `mPositionMotorSettings`, `mRotationConstraintType` (Free, around tangent/normal/binormal, to path, fully) | [PathConstraint.h L20-L71](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/PathConstraint.h#L20-L71); [PathConstraintPathHermite.h L27](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/PathConstraintPathHermite.h#L27) |
| **Gear** | 1 (couples two rotations) | hinge axis only | `mRatio` (`SetRatio(teeth1, teeth2)`); meant to be used with two hinges, optionally passed by `SetConstraints` to fix drift | [GearConstraint.h L24-L51, L79-L80](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/GearConstraint.h#L24-L51) |
| **Rack-and-pinion** | 1 (couples rotation of body 1 to translation of body 2) | hinge axis (pinion), slider axis (rack) | `mRatio` (`SetRatio(teethRack, rackLength, teethPinion)`); used with a hinge and a slider | [RackAndPinionConstraint.h L22-L53, L81-L82](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/RackAndPinionConstraint.h#L22-L53) |
| **Pulley** | 1 (Length1 + ratio × Length2 within [min, max]) | body point; plus two fixed **world** points | `mFixedPoint1`/`mFixedPoint2` (always world space), `mRatio`, `mMinLength`, `mMaxLength` (-1 means current) | [PulleyConstraint.h L12-L51](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/PulleyConstraint.h#L12-L51) |

Construction-time asserts worth knowing: a hinge or slider with `min == max` and no limit spring asserts "Better use a fixed constraint" ([HingeConstraint.cpp L83](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/HingeConstraint.cpp#L83); [SliderConstraint.cpp L159](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/SliderConstraint.cpp#L159)). Hinge limits must straddle 0 and lie in [-pi, pi] ([HingeConstraint.cpp L156-L163](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/HingeConstraint.cpp#L156-L163)); slider limits must straddle 0 ([SliderConstraint.cpp L183-L186](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/SliderConstraint.cpp#L183-L186)). These are `JPH_ASSERT`s, so release builds do not check them.

**Meant for ragdolls:**
- **Swing-twist** is "a specialized constraint for humanoid ragdolls" ([SwingTwistConstraint.h L67](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/SwingTwistConstraint.h#L67)). The sample ragdoll loader builds every joint as swing-twist by default ([RagdollLoader.cpp L41, L104, L286](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Utils/RagdollLoader.cpp#L41)).
- **Hinge** is the other kind `Ragdoll` knows. `DriveToPoseUsingMotors` handles only swing-twist and hinge and asserts "Constraint type not implemented!" for anything else ([Ragdoll.cpp L663-L677](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Ragdoll/Ragdoll.cpp#L663-L677)).
- `RagdollSettings::Part::mToParent` accepts any `TwoBodyConstraintSettings` ([Ragdoll.h L95](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Ragdoll/Ragdoll.h#L95)). The loader can override the type to fixed, point, hinge, slider or cone for testing ([RagdollLoader.cpp L45-L104](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Utils/RagdollLoader.cpp#L45-L104)).
- `Ragdoll` is a thin owner of bodies and constraints. Its other helpers are covered under the matching headings: `Stabilize`, priorities (section 10), `DisableParentChildCollisions` (section 7), and adding and removing everything as a batch (section 8) ([Ragdoll.h L26-L62, L148-L151](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Ragdoll/Ragdoll.h#L26-L62)).

## 2. Limits, motors and springs

**`SpringSettings`** has three modes, `ESpringMode::FrequencyAndDamping` (Hz and damping ratio; the default), `StiffnessAndDamping` (N/m or N·m/rad) and `MassNormalizedStiffnessAndDamping` ("acceleration mode", mass-independent). A frequency or stiffness <= 0 means a hard constraint ([SpringSettings.h L15-L84](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/SpringSettings.h#L15-L84)). Valid frequencies are (0, 0.5 × simulation rate]: at 60 Hz, 20 is stiff and 2 is soft ([Architecture.md L545](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md?plain=1#L545)). Zero damping still loses energy, because of the symplectic Euler integrator ([SpringSettings.h L82-L83](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/SpringSettings.h#L82-L83)).

**`MotorSettings`** holds a `SpringSettings` (used only for position driving, default 2 Hz with damping 1) and asymmetric force limits (N, for linear motors) and torque limits (N·m, for angular motors), each defaulting to ±FLT_MAX ([MotorSettings.h L39-L80](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/MotorSettings.h#L39-L80)). With unlimited force, a velocity motor reaches its target in one step ([Architecture.md L507-L513](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md?plain=1#L507-L513)).

**`EMotorState`** has **four** states, not three: `Off`, `Velocity`, `Position`, `PositionAndVelocity` (spring toward the target position plus damping toward the target velocity) ([MotorSettings.h L16-L22](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/MotorSettings.h#L16-L22); [Architecture.md L501-L505, which still says "three states" while listing four](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md?plain=1#L501-L505)). Turning a motor on asserts `MotorSettings::IsValid()` (non-negative frequency and damping, min <= max) ([HingeConstraint.h L119](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/HingeConstraint.h#L119)).

**Soft limits** are a `SpringSettings` named `mLimitsSpringSettings`. A frequency > 0 lets the constraint pass its limit and get pulled back.

Which kinds have which:

| Kind | Limits | Soft limits | Motor | Friction |
|---|---|---|---|---|
| Fixed, Point | n/a | n/a | no | no |
| Distance | min/max distance | yes | no | no |
| Hinge | angle | yes | angular (1) | torque |
| Slider | position | yes | linear (1) | force |
| Cone | half-angle | **no** | no | no |
| Swing-twist | swing cone/pyramid + twist range | **no** | swing + twist (angular) | torque |
| Six-DOF | per axis | **translation axes only** ("soft rotation limits are not currently supported") | per axis, all 6 | per axis |
| Path | path ends (unless looping) | no | position along path (linear) | force |
| Gear, Rack-and-pinion | n/a | n/a | no | no |
| Pulley | min/max length | no | no | no |

Sources: the settings rows in section 1; Six-DOF soft limits [SixDOFConstraint.h L81-L83](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/SixDOFConstraint.h#L81-L83). Six-DOF's translation motors work in body 1's constraint space, its rotation motors in **body 2's** ([SixDOFConstraint.h L169-L181](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/SixDOFConstraint.h#L169-L181)). Hinge and slider clamp a position target into the limits ([HingeConstraint.h L123](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/HingeConstraint.h#L123); [SliderConstraint.h L111](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/SliderConstraint.h#L111)).

## 3. Runtime edits

All configuration is copied into the constraint at `Create`, and the settings object is no longer needed afterwards ([SwingTwistConstraint.h L16](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/SwingTwistConstraint.h#L16)). What can change on a live constraint is exactly what has a setter:

- **Every kind** (`Constraint`): `SetEnabled`, `SetConstraintPriority`, `SetNumVelocityStepsOverride`/`SetNumPositionStepsOverride`, `SetUserData` ([Constraint.h L136-L160](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/Constraint.h#L136-L160)). Also `NotifyShapeChanged` and `ResetWarmStart` (sections 4 and 5).
- **Point:** `SetPoint1`, `SetPoint2`, in either space. It is the only kind whose anchors can move ([PointConstraint.h L63-L67](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/PointConstraint.h#L63-L67)).
- **Distance:** `SetDistance`, limit spring ([DistanceConstraint.h L75-L83](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/DistanceConstraint.h#L75-L83)).
- **Hinge:** limits, limit spring, friction, motor settings (by reference), motor state, targets ([HingeConstraint.h L111-L140](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/HingeConstraint.h#L111-L140)). **Slider:** the same set ([SliderConstraint.h L99-L123](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/SliderConstraint.h#L99-L123)).
- **Cone:** `SetHalfConeAngle` ([ConeConstraint.h L98](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/ConeConstraint.h#L98)).
- **Swing-twist:** the four limit angles, friction, both motors, targets ([SwingTwistConstraint.h L104-L145](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/SwingTwistConstraint.h#L104-L145)); **not** the swing type.
- **Six-DOF:** translation and rotation limits (which also re-derive which axes are fixed or free, [SixDOFConstraint.cpp L121-L238](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/SixDOFConstraint.cpp#L121-L238)), translation limit springs, friction, motors, targets ([SixDOFConstraint.h L136-L193](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/SixDOFConstraint.h#L136-L193)); **not** the swing type.
- **Path:** `SetPath(path, fraction)`, friction, motor ([PathConstraint.h L107-L130](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/PathConstraint.h#L107-L130)); **not** the rotation constraint type or the path's placement on body 1.
- **Pulley:** `SetLength` ([PulleyConstraint.h L86-L89](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/PulleyConstraint.h#L86-L89)); **not** the ratio or the fixed points.
- **Gear, Rack-and-pinion:** only `SetConstraints`; **not** the ratio ([GearConstraint.h L79-L80](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/GearConstraint.h#L79-L80)).

**Changing any of these needs the constraint destroyed and recreated:** the kind, either body (there is no `SetBody`), the frames and axes (except Point's anchors), the space, the swing type, the path rotation type, and the gear, rack or pulley ratio.

Changing a motor's state deactivates that motor's parts, so the new mode is not warm-started with the old mode's impulse ([SixDOFConstraint.cpp L310-L324](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/SixDOFConstraint.cpp#L310-L324); [SwingTwistConstraint.cpp L159-L184](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/SwingTwistConstraint.cpp#L159-L184)).

**Cost of recreating:**
- *Allocation:* one heap object from `Create`, which converts frames to local space ([HingeConstraint.cpp L77-L119](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/HingeConstraint.cpp#L77-L119)).
- *Add and remove:* `AddConstraint` is a mutex-guarded `push_back`, and `RemoveConstraint` a swap-with-last-and-pop ([ConstraintManager.cpp L17-L61](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/ConstraintManager.cpp#L17-L61)). Both are O(1). There is no constraint capacity at `Init`, and no broad-phase work.
- *Warm starting:* the real cost is that a new constraint starts with zero accumulated impulse. Jolt itself warns that skipping warm start makes constraints "much 'softer'", so a chain "might sag" ([Constraint.h L168-L172](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/Constraint.h#L168-L172)).
- *Ordering:* remove-and-add changes the solve order (section 10).
- *Frames:* recreating from current world-space poses re-bases the frames, and so the zero angle (section 4). `GetConstraintSettings()` round-trips a live constraint back to settings in `LocalToBodyCOM` space, but not its bodies ([HingeConstraint.cpp L422-L439](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/HingeConstraint.cpp#L422-L439); [Constraint.h L206-L207](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/Constraint.h#L206-L207)).

## 4. Frames

**`EConstraintSpace`** ([Constraint.h L56-L61](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/Constraint.h#L56-L61)):
- **`WorldSpace`** (the default on every kind). Points and axes are world-space values, converted at `Create` using each body's *current* inverse centre-of-mass transform. The relative rotation at creation becomes the zero angle ([HingeConstraint.cpp L89-L104](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/HingeConstraint.cpp#L89-L104)).
- **`LocalToBodyCOM`**. Values are relative to each body's **centre of mass**, not its origin: "you need to subtract Shape::GetCenterOfMass() from positions!".

There is no "local to body origin" option. A Jolt body's position is its origin, but constraints store everything relative to its COM.

**Anchors are a point plus axes.** Each kind's frame is a point plus one or two axes (primary and secondary, which must be perpendicular), as listed in section 1. `GetConstraintToBody1Matrix`/`GetConstraintToBody2Matrix` return the full frame relative to each body's COM, with the primary axis in column 0 ([TwoBodyConstraint.h L47-L51](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/TwoBodyConstraint.h#L47-L51)). Point, Distance and Pulley report an arbitrary rotation there ([PointConstraint.h L77](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/PointConstraint.h#L77)). Two exceptions to "both in `mSpace`":
- Path's start is given relative to body 1's **world transform (origin)**, and Jolt subtracts the COM itself ([PathConstraint.cpp L71-L72](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/PathConstraint.cpp#L71-L72)).
- Pulley's fixed points are always world space ([PulleyConstraint.h L35-L42](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/PulleyConstraint.h#L35-L42)).

`SliderConstraintSettings::SetSliderAxis` asserts world space ([SliderConstraint.cpp L43](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/SliderConstraint.cpp#L43)).

**When a body's shape or COM changes after creation, the constraint is not told.**
- `BodyInterface::SetShape` and `NotifyShapeChanged` keep the body's world position fixed by moving its COM position ([Body.cpp L117-L143](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Body/Body.cpp#L117-L143)). Neither touches constraints ([BodyInterface.cpp L298-L355](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Body/BodyInterface.cpp#L298-L355)).
- "Bodies don't know which constraints are connected to them so the user is responsible for notifying the relevant constraints": call `Constraint::NotifyShapeChanged(bodyID, newCOM - oldCOM)` ([Constraint.h L162-L166](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/Constraint.h#L162-L166)).
- The implementations shift only the stored local anchor positions; rotations are untouched ([HingeConstraint.cpp L121-L127](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/HingeConstraint.cpp#L121-L127); [PathConstraint.cpp L77-L83](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/PathConstraint.cpp#L77-L83)). Gear and Rack-and-pinion do nothing, having no anchor point ([GearConstraint.h L62](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/GearConstraint.h#L62)).
- Jolt's own sample rebuilds a compound's sub-shapes and then calls `BodyInterface::NotifyShapeChanged` followed by `Constraint::NotifyShapeChanged` on the constraints ([ConstraintVsCOMChangeTest.cpp L101-L128](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Constraints/ConstraintVsCOMChangeTest.cpp#L101-L128)).
- Without the notify call, each anchor silently ends up off by the COM delta.

## 5. Reading state

**Current position or angle.** Only some kinds expose it:
- **Hinge:** `GetCurrentAngle()`, the angle from the rest position, from `Quat::GetRotationAngle`, so wrapped to (-pi, pi], with no turn count ([HingeConstraint.h L107-L108](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/HingeConstraint.h#L107-L108); [HingeConstraint.cpp L129-L135](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/HingeConstraint.cpp#L129-L135)).
- **Slider:** `GetCurrentPosition()` ([SliderConstraint.h L95-L96](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/SliderConstraint.h#L95-L96)).
- **Pulley:** `GetCurrentLength()` ([PulleyConstraint.h L91-L92](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/PulleyConstraint.h#L91-L92)).
- **Path:** `GetPathFraction()` ([PathConstraint.h L113-L114](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/PathConstraint.h#L113-L114)).
- **Swing-twist and Six-DOF:** `GetRotationInConstraintSpace()`, a quaternion ([SwingTwistConstraint.h L147-L149](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/SwingTwistConstraint.h#L147-L149); [SixDOFConstraint.h L162-L163](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/SixDOFConstraint.h#L162-L163)).
- Distance, Cone, Fixed, Point, Gear and Rack-and-pinion have no reading. Six-DOF has none for translation. The host computes these from body poses and `GetConstraintToBody1/2Matrix`.

**Total lambda.** Every kind has `GetTotalLambda*` accessors: "the linear/angular impulse applied to satisfy the constraint" in the last update. Each constraint part has its own: position, rotation, limits and motor are separate, with different shapes (`Vec3`, `Vector<2>`, `float`).
- Hinge: `GetTotalLambdaPosition` (Vec3), `GetTotalLambdaRotation` (Vector<2>), `GetTotalLambdaRotationLimits`, `GetTotalLambdaMotor` ([HingeConstraint.h L142-L146](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/HingeConstraint.h#L142-L146)).
- Fixed: position and rotation, both Vec3 ([FixedConstraint.h L75-L77](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/FixedConstraint.h#L75-L77)).
- Swing-twist: position, twist, swing Y, swing Z, motor ([SwingTwistConstraint.h L151-L156](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/SwingTwistConstraint.h#L151-L156)).
- Six-DOF: position, rotation, motor translation, motor rotation ([SixDOFConstraint.h L195-L199](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/SixDOFConstraint.h#L195-L199)).
- Distance, Pulley, Gear and Rack-and-pinion each have a single `float`.

What a lambda means:
- **Lambda is an impulse** (N·s or N·m·s), accumulated over the velocity iterations of a collision step, warm start included ([AxisConstraintPart.h L261, L301-L304](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/ConstraintPart/AxisConstraintPart.h#L261); [PointConstraintPart.h L147-L160](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/ConstraintPart/PointConstraintPart.h#L147-L160)). A force threshold means dividing by the collision step's dt.
- Lambda is not reset when a constraint goes inactive (sleeping or disabled), so it can be stale. It is reset only when a part is deactivated (`Deactivate`, e.g. via `ResetWarmStart`) ([PointConstraintPart.h L129-L133](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/ConstraintPart/PointConstraintPart.h#L129-L133)).

**Breaking.** Confirmed: Jolt has no built-in breaking.
- The documented recipe: "After every simulation step, check the total 'lambda' applied on each constraint and disable the constraint if the value goes over a certain threshold" with `SetEnabled(false)` ([Architecture.md L555-L557](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md?plain=1#L555-L557); [Constraint.h L149-L153](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/Constraint.h#L149-L153)).
- A disabled constraint "will not affect the simulation in any way anymore" but "does incur some processing overhead". Removing it instead "may be more costly if you want to disable the constraint for a short while".
- No sample or unit test at v5.6.0 implements breaking.

## 6. The world as one end

- **`Body::sFixedToWorld`** is a process-wide static dummy body: static motion type, an empty shape, at the origin with identity rotation, invalid ID and invalid object layer ([Body.h L354-L355](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Body/Body.h#L354-L355); [Body.cpp L22-L35](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Body/Body.cpp#L22-L35)). It is the documented way to pin to the world "instead of creating a static body" ([Architecture.md L485](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md?plain=1#L485)).
- Because it sits at the origin, its "local" frame equals world space. Samples use it as body 1 for hinges and sliders ([GearConstraintTest.cpp L96](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Constraints/GearConstraintTest.cpp#L96); [RackAndPinionConstraintTest.cpp L96-L106](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Constraints/RackAndPinionConstraintTest.cpp#L96-L106); [FixedConstraintTest.cpp L101](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Constraints/FixedConstraintTest.cpp#L101)).
- `BodyInterface::CreateConstraint(settings, id1, id2)` locks both bodies and substitutes `sFixedToWorld` for any invalid ID ([BodyInterface.cpp L269-L281](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Body/BodyInterface.cpp#L269-L281)).
- **Static, kinematic and dynamic ends:**
  - A two-body constraint is active only if it is enabled, at least one body is active (awake), and at least one body is dynamic ([TwoBodyConstraint.h L38](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/TwoBodyConstraint.h#L38)).
  - Static-static, static-kinematic and kinematic-kinematic constraints are allowed to exist but are never solved.
  - A kinematic or static end is not put into the dynamic body's island, since "a constraint cannot affect the velocity of a kinematic body" ([TwoBodyConstraint.cpp L44-L56](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/TwoBodyConstraint.cpp#L44-L56)).
  - A real static body works like `sFixedToWorld`, but its actual transform is used, both at creation and every step.
  - The constraint reads each body's live motion type every step, so a body switching between dynamic and kinematic needs no constraint change. Islands are rebuilt from scratch each step ([Architecture.md L940](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md?plain=1#L940)).

## 7. Ignoring collision between connected bodies

**Constraints do not disable collision themselves.** Nothing in `Constraints/` has a "collide connected" flag. The mechanism is the body's **`CollisionGroup`**, judged by a **`GroupFilter`**.
- Each body holds exactly one `CollisionGroup`: a ref-counted `GroupFilter` pointer, a 32-bit `GroupID` and a 32-bit `SubGroupID`. It is set at creation or with `Body::SetCollisionGroup`/`BodyInterface::SetCollisionGroup` ([CollisionGroup.h L15-L34](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Collision/CollisionGroup.h#L15-L34); [Body.h L130-L132](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Body/Body.h#L130-L132); [BodyInterface.h L298](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Body/BodyInterface.h#L298)).
- `CollisionGroup::CanCollide` asks the first body's filter, or the second's if the first has none. If neither has one, the pair collides ([CollisionGroup.h L82-L89](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Collision/CollisionGroup.h#L82-L89)).
- `GroupFilter` is an abstract class with one virtual `CanCollide(group1, group2)`, so a host can write its own ([GroupFilter.h L16-L26](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Collision/GroupFilter.h#L16-L26)).

**`GroupFilterTable`** is the provided filter: a triangular bit table over N sub-groups, everything colliding by default, with `DisableCollision(a, b)`/`EnableCollision(a, b)` ([GroupFilterTable.h L12-L93](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Collision/GroupFilterTable.h#L12-L93)). Its rules ([GroupFilterTable.h L96-L116](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Collision/GroupFilterTable.h#L96-L116)):
- An invalid group always collides, and different `GroupID`s collide.
- **The same `GroupID` with different filter pointers never collides.**
- The same sub-group never collides; otherwise the table decides.
- The table costs N(N-1)/2 bits.

Jolt's own chain sample gives each segment sub-group i and disables (i, i+1) ([ConstraintVsCOMChangeTest.cpp L32-L51](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Constraints/ConstraintVsCOMChangeTest.cpp#L32-L51)). `RagdollSettings::DisableParentChildCollisions` does the same per skeleton joint. It asks for a GroupID "unique throughout the PhysicsSystem" per ragdoll ([Ragdoll.h L35-L47](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Ragdoll/Ragdoll.h#L35-L47); [Ragdoll.cpp L225-L276](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Ragdoll/Ragdoll.cpp#L225-L276)).

**How it combines with object-layer filtering:**
- Filters run in order: broad-phase layer, then object-layer pair, then group filter (simulation only), then shape filter, then the contact listener ([Architecture.md L646-L655](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md?plain=1#L646-L655)).
- In code the pair test is `ObjectLayerPairFilter::ShouldCollide && Body::sFindCollidingPairsCanCollide` ([QuadTree.cpp L1476-L1477](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Collision/BroadPhase/QuadTree.cpp#L1476-L1477)), and the latter ends with the group check ([Body.inl L74-L76](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Body/Body.inl#L74-L76)). So **it is a logical AND: a group filter can only remove pairs that the layers allow, never add one.**
- Continuous collision (linear cast) also respects groups ([PhysicsSystem.cpp L1992](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/PhysicsSystem.cpp#L1992)).
- **Queries do not use the group filter.** They use `BodyFilter` instead ([Architecture.md L652-L653](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md?plain=1#L652-L653)).
- The filter is called during broad-phase pair finding inside `Update`, on job threads. It must be read-only and thread-safe then.

## 8. Lifetime and threading

**Lifetime:**
- `Constraint` is ref-counted (`RefTarget`), and `Create` returns a raw pointer at refcount 0.
- The manager stores `Array<Ref<Constraint>>`, so `AddConstraint` takes a reference and `RemoveConstraint` drops it ([ConstraintManager.h L21, L96](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/ConstraintManager.h#L21)). A host that keeps no `Ref<>` loses the constraint on removal.
- Adding one constraint twice asserts ([ConstraintManager.cpp L27-L28](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/ConstraintManager.cpp#L27-L28)).
- Batch `AddConstraints`/`RemoveConstraints` exist ([PhysicsSystem.h L127-L140](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/PhysicsSystem.h#L127-L140)).

**When a constrained body is removed:**
- A constraint holds **raw `Body *` pointers**, not IDs or references ([TwoBodyConstraint.h L60-L62](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/TwoBodyConstraint.h#L60-L62)).
- "Bodies do not keep track of the constraints that are connected to them. This means that you're responsible for removing any constraints attached to a body before removing the body" ([Architecture.md L487](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md?plain=1#L487)).
- Jolt does not detect the mistake. If the other end is awake and dynamic, island building tries to wake the removed body, which only a debug assert catches: "Use BodyInterface::AddBody to add the body first!" ([TwoBodyConstraint.cpp L37-L42](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/TwoBodyConstraint.cpp#L37-L42); [BodyManager.cpp L506](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Body/BodyManager.cpp#L506)). After `DestroyBody` the pointer dangles.
- `Ragdoll` shows the order: add the bodies, then the constraints; remove the constraints, then the bodies ([Ragdoll.cpp L505-L533](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Ragdoll/Ragdoll.cpp#L505-L533)).

**Threading:**
- Adding and removing constraints is thread-safe under the manager's mutex, but the constraints themselves have no protection against concurrent access. They "can be freely modified except during the physics simulation step" ([Architecture.md L489](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md?plain=1#L489); [ConstraintManager.cpp L17-L38](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/ConstraintManager.cpp#L17-L38)).
- During `Update` the constraint list is locked ([PhysicsSystem.cpp L333-L338](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/PhysicsSystem.cpp#L333-L338)), and nothing may touch constraints ([Architecture.md L159](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md?plain=1#L159)).
- The one exception is `PhysicsStepListener::OnStep`, which "can read/write bodies and constraints but not add/remove them" ([PhysicsStepListener.h L29](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/PhysicsStepListener.h#L29)).
- Each step allocates `numConstraints × sizeof(Constraint*)` from the temp allocator, plus island-builder space for the active ones. Temp-allocator sizing grows with the constraint count ([PhysicsSystem.cpp L338](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/PhysicsSystem.cpp#L338); [IslandBuilder.cpp L67](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/IslandBuilder.cpp#L67)).

## 9. Sleep and waking

- **Constraints join islands.** An island is the set of dynamic bodies linked by contacts or constraints, and it sleeps as a whole once every body in it is at rest ([Architecture.md L417-L421](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md?plain=1#L417-L421)). Only dynamic-dynamic constraints link bodies ([TwoBodyConstraint.cpp L44-L47](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/TwoBodyConstraint.cpp#L44-L47)).
- **A constraint wakes its bodies only through an awake end.** If either body is active, island building activates the other dynamic body ([TwoBodyConstraint.cpp L34-L42](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/TwoBodyConstraint.cpp#L34-L42); [Architecture.md L942](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md?plain=1#L942)). An awake kinematic end therefore wakes the dynamic end.
- **What keeps a jointed island asleep:** if both ends are asleep, or one is asleep and the other static, the constraint is inactive (section 6) and nothing wakes them.
- **Edits do not wake.** Setters only store values (e.g. [HingeConstraint.h L119-L123](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/HingeConstraint.h#L119-L123)), and nothing under `Constraints/` calls an activation function apart from island building. That covers motor targets, limits, `SetEnabled` and `AddConstraint`.
  - The host must wake the bodies, with `BodyInterface::ActivateConstraint(constraint)`, which activates both bodies ([BodyInterface.h L154-L156](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Body/BodyInterface.h#L154-L156); [BodyInterface.cpp L283-L287](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Body/BodyInterface.cpp#L283-L287)).
  - Jolt's powered-hinge sample sidesteps this with `SetAllowSleeping(false)` on the driven body ([PoweredHingeConstraintTest.cpp L42](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Constraints/PoweredHingeConstraintTest.cpp#L42)).
  - A position motor that reaches its target lets the island sleep. A later target change does nothing until something wakes it.

## 10. Determinism and priority

- Same-binary determinism needs bodies and constraints "added/removed/modified in exactly the same order" ([Architecture.md L789](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md?plain=1#L789)).
- **Solve order within an island** is a sort by `(priority ascending, index in the manager's list)` ([ConstraintManager.cpp L103-L116](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/ConstraintManager.cpp#L103-L116)).
  - The index comes from add order, and **removal swaps the last constraint into the hole** ([ConstraintManager.cpp L49-L59](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/ConstraintManager.cpp#L49-L59)). Removing one constraint therefore reorders another.
  - Ascending order means a higher priority is solved later, so it wins conflicts. Hence "higher numbers are more likely to be solved correctly" ([Constraint.h L80-L82](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/Constraint.h#L80-L82)).
- **Order-independence:**
  - Add order does not matter if every constraint in the system has a unique `mConstraintPriority` ([Architecture.md L850](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md?plain=1#L850)).
  - But `PhysicsSystem::SaveState` relies on list order, so constraints must then be saved individually ([Architecture.md L851](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md?plain=1#L851)).
  - `SetConstraintPriority` is live ([Constraint.h L138-L139](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/Constraint.h#L138-L139)).
- **Priority as a tuning tool:** `RagdollSettings::CalculateConstraintPriorities` gives constraints nearer the root a higher priority than those near the leaves ([Ragdoll.h L30-L33](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Ragdoll/Ragdoll.h#L30-L33); [Ragdoll.cpp L205-L222](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Ragdoll/Ragdoll.cpp#L205-L222)). The `ConstraintPriorityTest` sample builds two identical chains, one with reversed priorities, to show the difference ([ConstraintPriorityTest.cpp L26-L55](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Constraints/ConstraintPriorityTest.cpp#L26-L55)).

## 11. Solver settings

- **Per-constraint overrides:** `mNumVelocityStepsOverride`/`mNumPositionStepsOverride` take values from 0 to 255; 0 means the world default (`PhysicsSettings::mNumVelocitySteps = 10`, `mNumPositionSteps = 2`) ([Constraint.h L84-L88, L123-L124](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/Constraint.h#L84-L88); [PhysicsSettings.h L84-L87](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/PhysicsSettings.h#L84-L87)).
  - The island runs the **max** over all its constraints, contacts and bodies. The default also joins the max if any member has no override ([CalculateSolverSteps.h L19-L40](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/CalculateSolverSteps.h#L19-L40)).
  - An override therefore raises iterations for the **whole island**, and cannot lower them below the world default while any member uses the default.
  - Bodies carry the same pair of overrides ([BodyCreationSettings.h L114-L115](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Body/BodyCreationSettings.h#L114-L115)).
  - Jolt's sample uses 64 for a stiff welded structure ([FixedConstraintTest.cpp L131-L142](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Samples/Tests/Constraints/FixedConstraintTest.cpp#L131-L142)).
- **Other solver-shaped knobs** live in `PhysicsSettings`, world-wide (Baumgarte, warm start on or off; [PhysicsSettings.h L45, L111](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/PhysicsSettings.h#L45)), and in `Ragdoll::Stabilize` (mass-ratio and inertia clamping, [Ragdoll.cpp L51-L56](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Ragdoll/Ragdoll.cpp#L51-L56)). `ResetWarmStart` is a per-constraint knob for teleports ([Constraint.h L168-L172](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Jolt/Physics/Constraints/Constraint.h#L168-L172)).
- **Against ADR-0010:** ADR-0010 refuses "backend tuning settings" and allows "step settings" ([0010 L22](../../../docs/adr/0010-fenrirphysics-hides-its-backend-at-compile-time.md)). Facts that bear on which side these fall:
  - Iteration counts are a property of Jolt's sequential-impulse solver (Box2D, Bullet and PhysX have the same idea; Rapier's multibody/impulse joints differ).
  - Their effect is island-wide, not local to the joint.
  - Priority is Jolt-specific ordering.
  - `mDrawConstraintSize` only matters with Jolt's debug renderer, which FenrirPhysics does not use.

  Deciding is left to the spec.

## 12. Articulations

Jolt has **no reduced-coordinate articulations**: no Featherstone, no multibody. A search of `Jolt/` and `Docs/` for "featherstone", "articulat", "multibody", "multi-body" and "reduced coordinate" finds nothing. Every joint is a maximal-coordinate `Constraint` solved by the sequential-impulse solver with warm starting ([Architecture.md L731](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md?plain=1#L731)). Its answers to chain stability are the iteration overrides, priorities, `Stabilize` and large-island splitting. A `Ragdoll` is a skeleton-indexed bundle of ordinary bodies and constraints, not an articulation. `SoftBody` uses its own particle constraints and cannot take ordinary constraints ([Architecture.md L462](https://github.com/jrouwe/JoltPhysics/blob/v5.6.0/Docs/Architecture.md?plain=1#L462)).

---

## What this means for FenrirPhysics

Facts the joints decisions will rest on. These are not the decisions themselves.

**ECS modelling** (ticket 22):
- A Jolt constraint names two `Body` objects by raw pointer and cannot change its bodies. A component naming "the other entity" maps to destroy-and-recreate whenever that link changes.
- A missing other end has a ready mapping: `sFixedToWorld`, or an invalid `BodyID` through `BodyInterface::CreateConstraint`.
- Jolt never removes a constraint for you, and a constraint outliving its body is undefined. When a body goes (entity destroyed, `RigidBody` removed, body recreated for a motion-type or shape change), physics must first remove every constraint touching it. Bodies do not know their constraints, so FenrirPhysics must keep that index itself.
- Order matters: bodies are added before their constraints and removed after them.
- The constraint list has no capacity limit, but temp-allocator use per step grows with it.
- `mUserData` (`uint64`) can carry an entity, as body user data does.
- Frames are per body: a point plus one or two axes. The space is world (snapshotted at creation, which also fixes the zero angle) or local to the **COM**, never to the body origin. FenrirPhysics's `Transform` origin is not the COM for offset or compound shapes, so local anchors given against the entity's origin need converting by the shape's COM.
- Any shape or COM change on a jointed body (a compound rebuilt as collider child entities come and go, a `SetShape`) needs `Constraint::NotifyShapeChanged(delta COM)` on each of its constraints, or the anchors drift by the delta.

**Kinds** (ticket 23):
- There are 12 two-body kinds, with very uneven features (see the tables in sections 1 and 2): soft limits on Distance, Hinge, Slider and Six-DOF translation only; motors on Hinge, Slider, Swing-twist, Six-DOF and Path only.
- Current-value readings exist only for Hinge (wrapped angle), Slider, Pulley, Path and the rotation of Swing-twist and Six-DOF. Others must be computed from body poses.
- Gear and Rack-and-pinion assume companion hinges or sliders. Pulley's fixed points are world-only. Path needs a ref-counted path object.
- The ragdoll spec will lean on **Swing-twist and Hinge**: those are the only kinds `Ragdoll` drives with motors. Fixed, Point, Cone and Slider are the loader's alternatives.
- Motors have four states, including `PositionAndVelocity`.
- Springs have three modes, with frequency-and-damping the default and documented as stable up to half the tick rate.
- Hinge and slider limits must straddle 0, and degenerate limits assert.

**Runtime edits** (tickets 22 and 24):
- Live: enable, priority, iteration overrides, user data, limits, limit springs, friction, motor settings, state and targets, Point's anchors, distance, pulley length and path.
- Recreate only: bodies, frames and axes, space, swing type, path rotation type, ratios.
- Recreating is cheap in CPU but drops warm-start impulses (a softer frame), changes solve order, and re-bases world-space frames (the zero angle) if they are re-snapshotted.

**Breaking** (ticket 24):
- There is no built-in breaking. The documented way is: after each step, compare `GetTotalLambda*` to a threshold, then `SetEnabled(false)` or remove.
- Lambda is a per-collision-step **impulse**, split by part (position, rotation, limit, motor), with per-kind shapes and no single "total force" accessor. A force threshold means dividing by the step dt and choosing which parts count.
- Lambda goes stale while the constraint is asleep or disabled.
- Reading happens after `Update` on the host thread. Nothing may be read or changed during a step, except in a step listener.

**No-collide** (ticket 25):
- Constraints do not stop their bodies colliding. The tool is one `CollisionGroup` per body (filter, `GroupID`, `SubGroupID`), ANDed after object-layer filtering. It can only remove pairs, and it applies to simulation and continuous collision but **not to queries**.
- `GroupFilterTable` handles N sub-groups within one `GroupID` (N(N-1)/2 bits). A custom `GroupFilter` subclass can implement any rule, such as a set of jointed pairs, but runs on job threads during the step.
- Because a body has one group, a body jointed into two separate groups cannot be expressed by `GroupFilterTable` alone.
- Same `GroupID` with different filter objects means "never collide", a trap if groups are allocated carelessly.
- This sits beside, and is invisible to, the Godot-style layer and mask (`CollisionFilter`) that ADR-0011 and ticket 07 already map onto object layers.

**Edge cases** (ticket 26):
- A world end via `sFixedToWorld`.
- Static or kinematic ends are allowed. Constraints with no dynamic end, or with no awake end, are silently never solved. A kinematic end is never pushed.
- Motion-type changes need no constraint change.
- Body removal and body recreation require removing constraints first.
- COM changes require `NotifyShapeChanged`. Teleports may want `ResetWarmStart`.
- Sleep: jointed dynamic bodies sleep as one island. **No constraint edit wakes anything**, including motor targets, enable and add. FenrirPhysics must call `ActivateConstraint` (or wake the bodies) when a joint changes, or the edit has no effect until something else wakes the island.

**Determinism:**
- The solve order is `(priority, list index)`. The index depends on add order and is disturbed by swap-removal.
- The current foundation already promises ordered API calls, which keeps this deterministic as long as FenrirPhysics adds and removes constraints in a stable (e.g. entity-sorted) order.
- Unique priorities would make add order irrelevant, at the cost of `SaveState`.

**ADR-0010 and solver settings:** the per-joint iteration overrides (island-wide max, Jolt-solver-shaped) and the priority (Jolt ordering) are the candidates for "backend tuning". Neither is needed for correctness. **Articulations:** Jolt has none, so multibody joints cannot be offered through this backend.

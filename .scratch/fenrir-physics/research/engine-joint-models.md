# How other engines model joints

Research for [21-how-other-engines-model-joints](../issues/21-how-other-engines-model-joints.md). This file reports how Unity, Godot, Avian, bevy_rapier, Unreal and O3DE put joints in front of game code, and what users complain about. It does not choose anything for FenrirPhysics.

Sources are engine source on GitHub, pinned to the commit read, plus official docs and the engines' own issue trackers. Line numbers refer to those commits:

- Unity C# reference source, `Unity 7000.0.0a10` @ [`6c1fc79`](https://github.com/Unity-Technologies/UnityCsReference/tree/6c1fc7919aa64a2698e06922c4516eb1daa91b75) (the managed bindings only; the native side is closed). Unity docs are the 6000.2 Scripting Reference and Manual.
- Godot `master` @ [`4d10419`](https://github.com/godotengine/godot/tree/4d10419cacd6d3009976e8f9fd3abd970fd58018), and the godot-jolt extension @ [`7f22589`](https://github.com/godot-jolt/godot-jolt/tree/7f225894706bb923e9ab9e9c73152b71880c5a83).
- Avian `main` (0.8.0-dev) @ [`b620b67`](https://github.com/avianphysics/avian/tree/b620b67121def1a88a9eb272763aa24e7d594ccc).
- bevy_rapier v0.37.0 (10 October 2026), now kept in the Rapier repo, `master` @ [`475caa3`](https://github.com/dimforge/rapier/tree/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier).
- Unreal Engine 5.8: the source needs an Epic account, so this file cites only the official C++, Python and Blueprint API reference and manual on dev.epicgames.com.
- O3DE `development` @ [`74006f5`](https://github.com/o3de/o3de/tree/74006f539d581ea2ae8d21d4ef629d7a610a3e01) (PhysX gem), plus docs.o3de.org.

Every link is a full URL.

---

## Unity

Unity has two models: the classic `Joint` components over PhysX, and `ArticulationBody` for reduced-coordinate chains. Its ECS package, Unity Physics, has a third, closer to Avian's; it is summarised at the end of this section.

### Where a joint lives

- **A component on the moving body, naming the other.** `Joint` is a `Component` ([Joint.bindings.cs L17](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Physics/ScriptBindings/Joint.bindings.cs#L17)). Every concrete kind is `[RequireComponent(typeof(Rigidbody))]` ([HingeJoint.bindings.cs L15](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Physics/ScriptBindings/HingeJoint.bindings.cs#L15), and likewise `FixedJoint`, `SpringJoint`, `CharacterJoint`, `ConfigurableJoint`). The other end is `connectedBody` (a `Rigidbody`) or `connectedArticulationBody`; if both are set, `connectedBody` wins ([Joint.bindings.cs L21–L37](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Physics/ScriptBindings/Joint.bindings.cs#L21-L37)).
- **Several joints per body:** a GameObject can hold several joint components. The Hinge Joint manual nevertheless says "A single Hinge Joint should be applied to a GameObject", and builds chains by putting one joint on each link with the next link as the connected body ([Hinge Joint component reference](https://docs.unity3d.com/6000.2/Documentation/Manual/class-HingeJoint.html)).

### Kinds

One class per kind, all deriving from `Joint`: `FixedJoint`, `HingeJoint`, `SpringJoint`, `CharacterJoint` (a limited ball socket for ragdolls) and `ConfigurableJoint` (six degrees of freedom) ([Joints section of the Manual](https://docs.unity3d.com/6000.2/Documentation/Manual/joints-section.html)). `ConfigurableJoint` sets each axis with `xMotion` … `angularZMotion` (locked, limited, free), plus soft limits, drives and targets ([ConfigurableJoint.bindings.cs L60–L112](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Physics/ScriptBindings/ConfigurableJoint.bindings.cs#L60-L112)). Changing kind means removing one component and adding another.

### Anchors

- `anchor` and `axis` are in the local space of the joint's own body. `connectedAnchor` is in the connected body's local space, or world space when there is none ([Joint.bindings.cs L38–L46](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Physics/ScriptBindings/Joint.bindings.cs#L38-L46)).
- **Auto-computed by default.** `autoConfigureConnectedAnchor` (default on) derives `connectedAnchor` from the world position of `anchor` ([L47–L49](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Physics/ScriptBindings/Joint.bindings.cs#L47-L49)). The newer `connectedOrientation` and `autoConfigureConnectedOrientation` do the same for rotation. Their own docs warn that the orientation is recomputed every time the joint is enabled, so a joint re-enabled away from rest "adopts that distorted pose as its new zero", which shifts its limits and drive targets ([L50–L86](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Physics/ScriptBindings/Joint.bindings.cs#L50-L86)).
- **Scale is not followed at runtime.** Unity's issue tracker records that joints "do not respect scaling at runtime", with a workaround script that recomputes the anchor ([configurable joints are not scaled…](https://issuetracker.unity.com/issues/6030/configurable-joints-are-not-scaled-when-the-gameobjects-containing-the-joints-are-scaled), [joint anchor position remains the same when dynamically scaled](https://issuetracker-mig.prd.it.unity3d.com/issues/the-joint-anchor-position-remains-the-same-when-the-gameobject-is-dynamically-scaled)).

### Joining to the world

Leave `connectedBody` empty: the joint then connects "to a fixed point in world space" ([Joint.bindings.cs L22](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Physics/ScriptBindings/Joint.bindings.cs#L22); [Hinge Joint manual](https://docs.unity3d.com/6000.2/Documentation/Manual/class-HingeJoint.html)).

### Limits, motors and springs

Each kind carries value-type settings structs (`JointLimits`, `JointMotor`, `JointSpring`, `SoftJointLimit`, `JointDrive`) with on/off booleans beside them. On `HingeJoint` these are `useLimits`, `useMotor` and `useSpring`, and enabling the motor overrides the spring ([HingeJoint.bindings.cs L62–L154](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Physics/ScriptBindings/HingeJoint.bindings.cs#L62-L154)). All are read-write properties, so all can change during play. Because they are structs, script code copies the struct, edits it, and assigns it back.

### Breaking

- `breakForce` and `breakTorque` on the base class; `Mathf.Infinity` makes a joint unbreakable. Torque can break a joint only on axes that are locked or limited ([Joint.bindings.cs L87–L96](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Physics/ScriptBindings/Joint.bindings.cs#L87-L96)).
- **The joint is destroyed.** `MonoBehaviour.OnJointBreak(float breakForce)` is sent to scripts on the joint's GameObject, and "After OnJointBreak the joint will automatically be removed from the game object and deleted" ([OnJointBreak](https://docs.unity3d.com/6000.2/Documentation/ScriptReference/MonoBehaviour.OnJointBreak.html)). The callback runs from `Physics.Simulate` ([Physics.bindings.cs L194](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Physics/ScriptBindings/Physics.bindings.cs#L194)).
- **The callback does not say which joint broke.** It receives only the force. Users with two joints on one body resort to a helper component per joint ([I need to know which joint has broken](https://discussions.unity.com/t/i-need-to-know-which-joint-has-broken/675389)). Unity's 2D joints fixed both problems: `OnJointBreak2D(Joint2D brokenJoint)` passes the joint ([OnJointBreak2D](https://docs.unity3d.com/6000.2/Documentation/ScriptReference/MonoBehaviour.OnJointBreak2D.html)), and `Joint2D.breakAction` chooses `Ignore`, `CallbackOnly`, `Disable` or `Destroy` ([JointBreakAction2D](https://docs.unity3d.com/6000.2/Documentation/ScriptReference/JointBreakAction2D.html)).

### Reading state

`currentForce` and `currentTorque` give the solver's last force and torque, relative to the connected body or in world space ([Joint.bindings.cs L159–L185](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Physics/ScriptBindings/Joint.bindings.cs#L159-L185)). Only `HingeJoint` exposes `angle` (degrees, zero at the start of simulation) and `velocity` ([HingeJoint.bindings.cs L155–L159](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Physics/ScriptBindings/HingeJoint.bindings.cs#L155-L159)). A tracked bug reports that `angle` changes when the parent rotates ([the joint.angle value changes when the parent changes rotation](https://issuetracker.unity3d.com/issues/the-joint-dot-angle-value-changes-when-the-parent-changes-rotation)).

### Connected bodies colliding

`enableCollision`, a per-joint bool ([Joint.bindings.cs L97–L98](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Physics/ScriptBindings/Joint.bindings.cs#L97-L98)). Neither the bindings nor the [Scripting Reference page](https://docs.unity3d.com/6000.2/Documentation/ScriptReference/Joint-enableCollision.html) states the default.

### ArticulationBody

- **The joint lives on the child body.** `ArticulationBody` is a body and its joint to the parent in one component. `jointType` (fixed, prismatic, revolute, spherical) says how it connects to the nearest `ArticulationBody` up the transform hierarchy. The parent anchor is given in the parent articulation body's space, "not in the space of the parent transform" ([Articulations.bindings.cs L14–L25, L276–L314](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Physics/ScriptBindings/Articulations.bindings.cs#L276-L314)). So each body has exactly one joint, and the shape is a tree.
- Each axis takes an `ArticulationDofLock` (locked, limited, free) and an `ArticulationDrive`. `jointPosition`, `jointVelocity`, `jointForce` and `driveForce` are read back in reduced coordinates ([L317–L363, L799–L813](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Physics/ScriptBindings/Articulations.bindings.cs#L799-L813)).
- The bindings have no break threshold and no per-joint collision switch.

### Unity Physics (the DOTS ECS package)

Unity's ECS physics package takes the opposite route to its classic joints. A joint is its own entity carrying `PhysicsConstrainedBodyPair(entityA, entityB, enableCollision)` ([PhysicsConstrainedBodyPair](https://docs.unity3d.com/Packages/com.unity.physics@1.4/api/Unity.Physics.PhysicsConstrainedBodyPair.html)). A complex joint such as a ragdoll joint is several joint entities, linked by a `PhysicsJointCompanion` buffer "to facilitate group destruction, deactivation" ([PhysicsJointCompanion](https://docs.unity3d.com/Packages/com.unity.physics@1.4/api/Unity.Physics.PhysicsJointCompanion.html)). There is no built-in breaking. A constraint's `MaxImpulse` raises an `ImpulseEvent` naming the `JointEntity`, and game code decides what to do ([Constraint](https://docs.unity3d.com/Packages/com.unity.physics@1.4/api/Unity.Physics.Constraint.html), [ImpulseEvent](https://docs.unity3d.com/Packages/com.unity.physics@1.4/api/Unity.Physics.ImpulseEvent.html)).

### Scripting access

The joints are the C# API: every property above is a C# property on a `Component`. `OnJointBreak` is a message method on `MonoBehaviour`.

### Complaints

- The rest frame is re-derived whenever a joint is enabled, so limits drift (documented by Unity itself, above).
- Anchors do not follow runtime scale, and auto-configured anchors break when shapes change ([HingeJoint2D anchor after sprite change](https://discussions.unity.com/t/how-to-fix-problem-with-hingejoint2d-anchor-after-change-sprite/221927)).
- The break callback cannot tell joints apart.
- Mass ratios cause stretch. Unity's own `massScale` docs describe ragdoll limbs looking "rubbery or separated" and ship a script that normalises mass ([Joint.bindings.cs L110–L155](https://github.com/Unity-Technologies/UnityCsReference/blob/6c1fc7919aa64a2698e06922c4516eb1daa91b75/Modules/Physics/ScriptBindings/Joint.bindings.cs#L110-L155)).

---

## Godot

### Where a joint lives

- **Its own node, naming both bodies.** A `Joint3D` is a `Node3D` with two `NodePath` properties, `node_a` and `node_b`, each typed to `PhysicsBody3D` ([joint_3d.cpp L231–L235](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/scene/3d/physics/joints/joint_3d.cpp#L231-L235)). The node owns a server joint RID, created in its constructor and freed in its destructor ([L238–L246](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/scene/3d/physics/joints/joint_3d.cpp#L238-L246)).
- **Several joints per body:** any number of joint nodes can name the same body. The joint node can sit anywhere in the tree, because it finds the bodies by path.
- **Validation is a warning, not an error.** If a path does not resolve to a `PhysicsBody3D`, or both paths name the same body, the node shows a configuration warning and clears the server joint ([L73–L98](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/scene/3d/physics/joints/joint_3d.cpp#L73-L98)).

### Kinds

One node class per kind: `PinJoint3D`, `HingeJoint3D`, `SliderJoint3D`, `ConeTwistJoint3D`, `Generic6DOFJoint3D` ([scene/3d/physics/joints/](https://github.com/godotengine/godot/tree/4d10419cacd6d3009976e8f9fd3abd970fd58018/scene/3d/physics/joints)). Each maps to a `joint_make_*` call on `PhysicsServer3D`, which turns one joint RID into a kind ([physics_server_3d.h L325–L376](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/servers/physics_3d/physics_server_3d.h#L325-L376)). So the server has one joint object with a kind, while the scene has one class per kind. `Generic6DOFJoint3D` stores a parameter array and a flag array per axis: linear and angular limits, springs, motors ([generic_6dof_joint_3d.h L39–L85](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/scene/3d/physics/joints/generic_6dof_joint_3d.h#L39-L85)).

### Anchors

- **The joint node's global transform is the joint frame,** captured once when the joint is configured. `HingeJoint3D::_configure_joint` multiplies the node's global transform by the inverse of each body's global transform, orthonormalises the result (dropping scale), and passes both local frames to the server ([hinge_joint_3d.cpp L100–L115](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/scene/3d/physics/joints/hinge_joint_3d.cpp#L100-L115)).
- Configuration runs on entering the tree and when `node_a`, `node_b` or `exclude_nodes_from_collision` change. `Joint3D::_notification` handles only `POST_ENTER_TREE` and `EXIT_TREE` ([joint_3d.cpp L172–L188](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/scene/3d/physics/joints/joint_3d.cpp#L172-L188)), so moving the joint node afterwards does not move the joint. Reparenting means leaving and re-entering the tree, which rebuilds the joint from the poses at that moment. A 2D issue reports that this rebuild introduces an offset ([#105230](https://github.com/godotengine/godot/issues/105230)).

### Joining to the world

Leave one path empty. With only `node_b` set, the base class configures the joint with that body alone ([joint_3d.cpp L102–L106](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/scene/3d/physics/joints/joint_3d.cpp#L102-L106)). **This leaks the backend:** Jolt treats the single body as `node_b`, with the world as `node_a`, which inverts limits compared with Godot Physics. A project setting, Joints > World Node, restores the old behaviour ([Using Jolt Physics, "Joint properties"](https://docs.godotengine.org/en/latest/tutorials/physics/using_jolt_physics.html); [jolt_joint_3d.cpp L128](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/modules/jolt_physics/joints/jolt_joint_3d.cpp#L128)). Users still hit it as "inverted limits" ([#116246](https://github.com/godotengine/godot/issues/116246)).

### Limits, motors and springs

These are plain `real_t` parameters and `bool` flags per kind, such as `PARAM_LIMIT_UPPER` and `FLAG_ENABLE_MOTOR` ([hinge_joint_3d.cpp L116–L137](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/scene/3d/physics/joints/hinge_joint_3d.cpp#L116-L137)). A parameter change is forwarded straight to the server (`hinge_joint_set_param`), so it applies during play without rebuilding the joint. Under Jolt, several soft-limit parameters (bias, softness, relaxation, limit damping) are unsupported and warn if changed from their defaults ([Using Jolt Physics](https://docs.godotengine.org/en/latest/tutorials/physics/using_jolt_physics.html)).

### Breaking

**Not built in.** Neither `Joint3D` nor `PhysicsServer3D` has a threshold or a signal. The godot-jolt extension's substitute nodes (`JoltHingeJoint3D` and the rest) added `enabled`, `get_applied_force()` and `get_applied_torque()`, so that game code could break joints itself ([jolt_joint_3d.cpp L6–L44](https://github.com/godot-jolt/godot-jolt/blob/7f225894706bb923e9ab9e9c73152b71880c5a83/src/joints/jolt_joint_3d.cpp#L6-L44), [jolt_hinge_joint_3d.cpp L42–L43](https://github.com/godot-jolt/godot-jolt/blob/7f225894706bb923e9ab9e9c73152b71880c5a83/src/joints/jolt_hinge_joint_3d.cpp#L42-L43), [v0.5.0 release notes](https://github.com/godot-jolt/godot-jolt/releases/tag/v0.5.0-stable)). Those nodes "have not been included in the Jolt module" ([Using Jolt Physics](https://docs.godotengine.org/en/latest/tutorials/physics/using_jolt_physics.html)).

### Reading state

The built-in joint nodes expose no current angle, position or applied force. Only the extension's nodes had applied force and torque.

### Connected bodies colliding

`exclude_nodes_from_collision`, **default true** ([joint_3d.h L47](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/scene/3d/physics/joints/joint_3d.h#L47)). It is applied as a collision exception between the two bodies, added and removed in both directions as the joint is configured and torn down ([joint_3d.cpp L56–L60, L124](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/scene/3d/physics/joints/joint_3d.cpp#L56-L124)). Toggling it tears the joint down and rebuilds it ([L190–L200](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/scene/3d/physics/joints/joint_3d.cpp#L190-L200)). At the server level the exception outlives the joint unless it is undone by hand ([#115015](https://github.com/godotengine/godot/issues/115015)).

### Scripting access

Every property is reflected through `ClassDB`, so GDScript and C# see `node_a`, `node_b`, the parameters and `get_rid()` ([joint_3d.cpp L216–L236](https://github.com/godotengine/godot/blob/4d10419cacd6d3009976e8f9fd3abd970fd58018/scene/3d/physics/joints/joint_3d.cpp#L216-L236)). Scripts can also drive `PhysicsServer3D` directly with RIDs.

### Complaints

- Assigning `node_a` and `node_b` from a script in `_ready` did not connect the bodies ([#77075](https://github.com/godotengine/godot/issues/77075)). Timing issues come from the frame being captured when the joint is configured.
- The editor gizmo and the runtime limits disagree under Jolt ([#111219](https://github.com/godotengine/godot/issues/111219), [#116246](https://github.com/godotengine/godot/issues/116246)).
- A frozen body joined to an unfrozen one can keep the unfrozen one stuck ([#89859](https://github.com/godotengine/godot/issues/89859)).
- Dragging an object with a joint to a moving `StaticBody3D` misbehaves. The maintainer's answer was that static bodies have no velocity, so moving them confuses joints, and a kinematic body is needed instead ([#116462](https://github.com/godotengine/godot/issues/116462)).

---

## Avian (Bevy)

### Where a joint lives

- **Its own entity, naming both bodies.** "Each joint is spawned as its own entity, providing the `Entity` identifiers of the bodies it should constrain", for example `commands.spawn(FixedJoint::new(body1, body2))` ([joints/mod.rs L37–L54](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/src/dynamics/joints/mod.rs#L37-L54)). The fields are `body1` and `body2`, renamed from `entity1` and `entity2` in 0.4 ([0.3-to-0.4 migration guide L103–L107](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/migration-guides/0.3-to-0.4.md#L103-L107)).
- **One joint per entity, any number per body.** Since 0.4, "Entities can now only hold up to one type of joint component". Adding a second joint kind removes the first, with a warning in debug builds. "A rigid body can still be attached to other bodies by multiple joints, but each joint must be on its own entity" ([migration guide L118–L120](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/migration-guides/0.3-to-0.4.md#L118-L120); enforced by the `on_add_joint` hook, [joint_graph/plugin.rs L191–L221](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/src/dynamics/joints/joint_graph/plugin.rs#L191-L221)).
- A `JointGraph` resource records which bodies each joint connects. A system rebuilds a joint's graph edge when its `body1` or `body2` changes ([plugin.rs L295–L330](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/src/dynamics/joints/joint_graph/plugin.rs#L295-L330)). Re-pointing a joint is therefore a field edit.

### Kinds

One component type per kind: `FixedJoint`, `DistanceJoint`, `PrismaticJoint`, `RevoluteJoint` and, in 3D, `SphericalJoint` ([mod.rs L24–L35](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/src/dynamics/joints/mod.rs#L24-L35)). There is no generic six-degrees-of-freedom joint. Issue [#199](https://github.com/avianphysics/avian/issues/199) discussed one as a way to share limits and motors across kinds. Changing kind is a component swap, a structural change.

### Anchors

Each body gets a `JointFrame` of a `JointAnchor` and a `JointBasis`. Each is either `Local(...)` to the body, or `FromGlobal(...)` and "automatically converted to local frames during the next simulation step" ([mod.rs L59–L77](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/src/dynamics/joints/mod.rs#L59-L77), [L783–L1000](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/src/dynamics/joints/mod.rs#L783-L1000)). By default the frames are the body transforms. Storing frames locally "allows the initial configuration to be preserved even when the bodies are moved". The docs say nothing about scale.

### Joining to the world

There is no world sentinel. Join the body to an entity with `RigidBody::Static`, as Avian's own joint tests do ([tests.rs L45](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/src/dynamics/joints/tests.rs#L45)).

### Limits, motors and springs

- **Fields on the kind's component.** `RevoluteJoint` has `hinge_axis`, `angle_limit: Option<AngleLimit>`, per-part compliance (`point_compliance`, `align_compliance`, `limit_compliance`) and `motor: AngularMotor` ([revolute.rs L48–L74](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/src/dynamics/joints/revolute.rs#L48-L74)). The motor has `enabled`, a target velocity and position, a maximum torque, and a `MotorModel` ([motor.rs L125–L135](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/src/dynamics/joints/motor.rs#L125-L135)). Motors arrived in 0.6, on revolute and prismatic joints only ([0.5-to-0.6 guide L53–L56](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/migration-guides/0.5-to-0.6.md#L53-L56)). Spherical motors are still open ([#934](https://github.com/avianphysics/avian/issues/934)).
- **Springs are compliance,** the inverse of stiffness, on each part.
- **Damping is a separate optional component,** `JointDamping` ([mod.rs L109–L136, L612](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/src/dynamics/joints/mod.rs#L109-L136)).
- Everything is a plain public field, so everything can change during play.

### Breaking

**Not built in, by decision.** `JointForces` (opt-in, added by the user) reports force, torque and motor force. `JointDisabled` (a marker) stops the joint being solved without removing it, and removing the marker re-enables it. The docs show a user system that inserts `JointDisabled` when `force().length()` passes a threshold, noting "Alternatively, you could simply remove the joint component or despawn the entity" ([mod.rs L138–L207](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/src/dynamics/joints/mod.rs#L138-L207)). The maintainer closed the breaking request [#199](https://github.com/avianphysics/avian/issues/199) with these two components, saying a built-in threshold "would add complexity, and people can have different requirements for what breaking means". There is no event; game code that breaks the joint already knows.

### Reading state

`JointForces` gives force and torque ([mod.rs L665–L700](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/src/dynamics/joints/mod.rs#L665-L700)). The joint kinds have no current-angle or current-position accessor.

### Connected bodies colliding

**They collide by default.** "By default, the attached bodies can still collide with each other. This behavior can be disabled with the `JointCollisionDisabled` component", a marker on the joint entity ([mod.rs L56–L57](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/src/dynamics/joints/mod.rs#L56-L57), [L530–L580](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/src/dynamics/joints/mod.rs#L530-L580)). Its hooks set a flag on the joint-graph edge, and the narrow phase skips that body pair. Switching it is a structural change (adding or removing a marker). A recent bug: adding the marker while the bodies touch leaves a stale contact in the sleeping island, and a later contact change panics ([#1076](https://github.com/avianphysics/avian/issues/1076)).

### Scripting access

Rust only. The joint components derive `Reflect` and optional `serde`, so Bevy's reflection and scene serialisation can see them.

### Complaints

- **Stability:** angle limits break under gravity in chains ([#553](https://github.com/avianphysics/avian/issues/553)); light bodies with limits are unstable ([#886](https://github.com/avianphysics/avian/issues/886)); a joint to a body with several child colliders explodes ([#258](https://github.com/avianphysics/avian/issues/258)); a distance joint created beyond its limit makes the bodies swap places ([#875](https://github.com/avianphysics/avian/issues/875)).
- **Hierarchy:** rotating a parent entity of jointed bodies changes their joint behaviour ([#148](https://github.com/avianphysics/avian/issues/148)).
- **Missing:** articulations ([#466](https://github.com/avianphysics/avian/issues/466)) and replacing the XPBD joint solver ([#440](https://github.com/avianphysics/avian/issues/440)).

---

## bevy_rapier

### Where a joint lives

- **A component on the second body (or a child of it), naming the first.** "The first end-point of the joint is the rigid-body attached to `ImpulseJoint::parent`. The second endpoint … is the rigid-body attached to the entity (or the parent of the entity) containing this `ImpulseJoint` component" ([joint.rs L90–L115](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/dynamics/joint.rs#L90-L115)). The plugin finds the second body by walking `ChildOf` up from the joint's entity to the first entity that has a body ([systems/joint.rs L34–L46](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/plugin/systems/joint.rs#L34-L46)).
- **Several joints per body use child entities:** "To attach multiple impulse joints to the same rigid-body, multiple joints can be added in the children of the entity containing that rigid-body (this is similar to the technique used to attach multiple colliders…)" ([joint.rs L97–L100](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/dynamics/joint.rs#L97-L100)).
- **Users want joints as entities.** Open issue [#561](https://github.com/dimforge/bevy_rapier/issues/561) calls the model "not idiomatic to ECS": it is unclear which body should hold the joint, two joints on one entity are impossible, and the joint data is split between two places. It proposes Avian's model.
- **Re-pointing is a field edit.** Since 0.37, changing `parent`, or reparenting the joint's entity, re-attaches the joint and keeps the solver's warm-start impulses. If a body does not exist yet, the joint is detached until it does ([systems/joint.rs L323–L396](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/plugin/systems/joint.rs#L323-L396); [CHANGELOG L166–L168](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/CHANGELOG.md#L166-L168)).

### Kinds

- **One component type, with the kind in a field.** `ImpulseJoint { parent, data: TypedJoint }`. `TypedJoint` is an enum of `FixedJoint`, `GenericJoint`, `PrismaticJoint`, `RevoluteJoint`, `RopeJoint`, `SphericalJoint` (3D), `SpringJoint` and `PinSlotJoint` (2D) ([joint.rs L25–L46](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/dynamics/joint.rs#L25-L46)).
- **Every kind is a thin wrapper over one six-degrees-of-freedom `GenericJoint`,** reachable through `as_ref()` and `as_mut()` ([L48–L80](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/dynamics/joint.rs#L48-L80)). `GenericJoint` has `locked_axes: JointAxesMask`, plus limits and motors per `JointAxis` ([generic_joint.rs L37–L290](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/dynamics/generic_joint.rs#L37-L290)).
- So changing kind is a field edit: assign a different `TypedJoint`.

### Anchors

`local_frame1` and `local_frame2` (anchor and basis), or just `local_anchor1` and `local_anchor2`, each in its body's local space ([generic_joint.rs L124–L170](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/dynamics/generic_joint.rs#L124-L170)). There is no auto-computation from a world point. Users who rotate a body's `Transform` are surprised that the joint frame ignores it ([#457](https://github.com/dimforge/bevy_rapier/issues/457), [#569](https://github.com/dimforge/bevy_rapier/issues/569)).

### Joining to the world

There is no world sentinel: `parent` must name an entity with a body, usually `RigidBody::Fixed`. `init_joints` creates the Rapier joint only when both bodies resolve ([systems/joint.rs L274–L286](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/plugin/systems/joint.rs#L274-L286)).

### Limits, motors and springs

- On `GenericJoint`: `set_limits(axis, [min, max])`; `set_motor_velocity`, `set_motor_position` and `set_motor_max_force` per axis, with a `MotorModel`; and `set_softness`, for spring coefficients on the locked axes ([generic_joint.rs L201–L290](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/dynamics/generic_joint.rs#L201-L290)). `SpringJoint` is a separate kind.
- **Edits apply during play.** A change to `ImpulseJoint` is copied into Rapier with warm-start impulses kept, and it wakes the bodies ([systems/joint.rs L381–L395](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/plugin/systems/joint.rs#L381-L395)).

### Breaking

**Not built in.** Since 0.37 there are two pieces ([CHANGELOG L47–L55](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/CHANGELOG.md#L47-L55)):

- `ImpulseJointImpulses`, added automatically and updated each step, holds the linear, angular, per-axis limit and per-axis motor impulses. Its doc suggests "Comparing these impulses with a threshold can be used to implement breakable joints (by removing the `ImpulseJoint` once the threshold is exceeded)" ([joint.rs L141–L160](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/dynamics/joint.rs#L141-L160)).
- `ImpulseJointDisabled`, a marker that keeps the joint attached but unsolved ([L127–L133](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/dynamics/joint.rs#L127-L133)). There is also `GenericJoint::set_enabled`, a field.

There is no break event.

### Reading state

- Impulses come from `ImpulseJointImpulses`, above.
- An impulse joint's current angle is computed on request, not stored: `impulse_revolute_joint_angle(entity)` on the context ([context/mod.rs L1583–L1588](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/plugin/context/mod.rs#L1583-L1588)). Before that helper existed, users could not read the angle ([#496](https://github.com/dimforge/bevy_rapier/issues/496)).
- Multibody joints write coordinates, velocities and relative rotation into a read-only `MultibodyJointState` component after each step ([multibody.rs L203–L221](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/dynamics/multibody.rs#L203-L221)).

### Connected bodies colliding

**They collide by default.** `contacts_enabled` is a field on every joint, and Rapier initialises it to `true` ([rapier generic_joint.rs L309, L328](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/src/dynamics/joint/generic_joint.rs#L309-L328); [bevy_rapier generic_joint.rs L172–L178](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/dynamics/generic_joint.rs#L172-L178)). Until 0.37 only `SpringJointBuilder` exposed it ([#624](https://github.com/dimforge/bevy_rapier/issues/624)). For multibodies, `MultibodySelfContactsDisabled` turns off contacts between all links of the multibody, not only neighbours ([CHANGELOG L56–L64](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/CHANGELOG.md#L56-L64)).

### Multibody joints

`MultibodyJoint { parent, data }` sits on the child body's entity. Reduced-coordinate chains "cannot form closed loops"; a loop-closing joint is ignored with an error ([joint.rs L183–L208](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/dynamics/joint.rs#L183-L208)). Multibody joints cannot be disabled ([L130](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/dynamics/joint.rs#L130)). Damping, friction, armature, springs and couplings are optional per-axis components.

### Scripting access

Rust only. `ImpulseJoint` and `MultibodyJoint` do not derive `Reflect`, but the new marker and state components do.

### Complaints

- The joint-on-a-body model itself ([#561](https://github.com/dimforge/bevy_rapier/issues/561), and [#12](https://github.com/dimforge/bevy_rapier/issues/12) on building joints inside a spawn hierarchy).
- Joint frames that ignore the visual transform ([#457](https://github.com/dimforge/bevy_rapier/issues/457)).
- No way to read the angle ([#496](https://github.com/dimforge/bevy_rapier/issues/496)).
- Panics on despawning jointed entities ([#196](https://github.com/dimforge/bevy_rapier/issues/196), [#645](https://github.com/dimforge/bevy_rapier/issues/645)).
- Despawned joints that stayed in the simulation ([#71](https://github.com/dimforge/bevy_rapier/issues/71)).

---

## Unreal Engine

### Where a joint lives

- **Its own scene component (or actor), naming both bodies.** `UPhysicsConstraintComponent : public USceneComponent` names its bodies by actor and component name (`ConstraintActor1/2`, `ComponentName1/2`), by direct pointer (`OverrideComponent1/2`), or with `SetConstrainedComponents(Component1, BoneName1, Component2, BoneName2)`, which "Will update frames based on current position" ([UPhysicsConstraintComponent](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UPhysicsConstraintComponent)).
  - If an actor is unset, the component searches its own owner. If a name is unset, it uses that actor's root component ([Python API, `component_name1`](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/PhysicsConstraintComponent)).
  - A level can also hold a standalone Physics Constraint Actor ([Constraints User Guide](https://dev.epicgames.com/documentation/en-us/unreal-engine/constraints-user-guide-in-unreal-engine)).
- **Several joints per body:** any number of constraint components or actors can name the same component. For skeletal meshes, the Physics Asset holds an array of `FConstraintInstance`s between named bones ([FConstraintInstance](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/FConstraintInstance)).

### Kinds

**One type; the kind is settings.** "The engine has a few default joint types (ball-and-socket, hinge, prismatic) but they only differ in their settings" ([Constraints User Guide](https://dev.epicgames.com/documentation/en-us/unreal-engine/constraints-user-guide-in-unreal-engine)). Every constraint is a six-degrees-of-freedom joint. `XMotion`, `YMotion` and `ZMotion`, and `Swing1Motion`, `Swing2Motion` and `TwistMotion`, are each Free, Limited or Locked ([Physics Constraint Reference](https://dev.epicgames.com/documentation/en-us/unreal-engine/physics-constraint-reference-in-unreal-engine)). Changing kind is a field edit, available at runtime as `SetLinearXLimit`, `SetAngularTwistLimit` and so on.

### Anchors

- `FConstraintInstance` stores a frame on each body: `Pos1`, `PriAxis1` and `SecAxis1` in Body1's reference frame ("usually the 'child' body"), and the same with 2 for the parent ([FConstraintInstance](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/FConstraintInstance)).
- The component's own placement supplies them. The guide says to position the constraint component where the joint should be ([Physics Constraint Component User Guide](https://dev.epicgames.com/documentation/unreal-engine/physics-constraint-component-user-guide-in-unreal-engine)). `UpdateConstraintFrames()` recomputes them, and `SetConstraintReferenceFrame/Position/Orientation` set them directly, taking effect immediately if the constraint is active ([UPhysicsConstraintComponent](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UPhysicsConstraintComponent)).
- **Scale:** the component passes a scale into the instance (`GetConstraintScale`, `LastKnownScale`), and `bScaleLinearLimits` scales linear limits by "the absolute min of the 3D scale of the owning component" ([FConstraintInstance](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/FConstraintInstance), [Physics Constraint Reference](https://dev.epicgames.com/documentation/en-us/unreal-engine/physics-constraint-reference-in-unreal-engine)).

### Joining to the world

The docs section is titled "Constraining physics objects together and to the world" ([Physics Constraints](https://dev.epicgames.com/documentation/en-us/unreal-engine/physics-constraints-in-unreal-engine)). None of the pages read says how to set up a world end; they only require physics on at least one of the two actors. I did not verify the mechanism.

### Limits, motors and springs

- Settings are grouped in `FConstraintProfileProperties`: `LinearLimit`, `ConeLimit`, `TwistLimit`, `LinearDrive`, `AngularDrive`, projection, shock propagation, plasticity and breaking ([FConstraintProfileProperties](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/FConstraintProfileProperties)).
- Limits can be hard or soft, a spring with stiffness, damping and restitution. Drives are position and velocity targets with strengths and a force limit, and the angular drive has SLERP or Twist-and-Swing modes ([Physics Constraint Reference](https://dev.epicgames.com/documentation/en-us/unreal-engine/physics-constraint-reference-in-unreal-engine)).
- All have runtime setters on the component.
- **Constraint profiles** are named copies of `FConstraintProfileProperties` in a Physics Asset. The struct exists to be "easily swapped at runtime … when going from ragdoll to standup", and `SetConstraintProfile(JointName, ProfileName)` on a skeletal mesh component switches one constraint's profile ([FConstraintProfileProperties](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/FConstraintProfileProperties); [Set Constraint Profile](https://dev.epicgames.com/documentation/unreal-engine/BlueprintAPI/Physics/SetConstraintProfile)). A profile is a bundle of settings swapped as one; the frames stay outside it.

### Breaking

- **Opt-in per direction, with thresholds:** `bLinearBreakable` with `LinearBreakThreshold` ("Force needed to break the distance constraint"), and `bAngularBreakable` with `AngularBreakThreshold`, a torque ([FConstraintProfileProperties](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/FConstraintProfileProperties)).
- **The joint is terminated; the component stays.** A broken constraint reports `IsBroken()`, and `GetConstraintForce` returns zero "if the constraint is not initialized or broken" ([UPhysicsConstraintComponent](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UPhysicsConstraintComponent)). Game code hears about it through `OnConstraintBroken`, a Blueprint-assignable delegate routed from the instance's broken callback. `BreakConstraint()` breaks one by hand.
- **Plasticity** is a softer cousin of breaking: past `LinearPlasticityThreshold` or `AngularPlasticityThreshold`, the drive target resets to the current pose, and `OnPlasticDeformation` fires.

### Reading state

`GetCurrentSwing1()`, `GetCurrentSwing2()` and `GetCurrentTwist()` give angles, and `GetConstraintForce(OutLinear, OutAngular)` gives the last force ([UPhysicsConstraintComponent](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UPhysicsConstraintComponent)). There is no linear position read-back in this API.

### Connected bodies colliding

`bDisableCollision` in the profile ("Disable collision between bodies joined by this constraint"), and `SetDisableCollision` at runtime ([FConstraintProfileProperties](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/FConstraintProfileProperties)). The pages read do not state its default. Because the flag sits in the profile, a profile swap can change it.

### Scripting access

Blueprint (`BlueprintCallable` functions and `BlueprintAssignable` delegates) and Python (`unreal.PhysicsConstraintComponent`) ([Python API](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/PhysicsConstraintComponent)). `GetPhysicsConstraintRef()` exposes the backend handle to C++ ([FConstraintInstance](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/FConstraintInstance)).

### Complaints

- **Spawning at runtime is order-sensitive:** `SetConstrainedComponents` fails or crashes unless it is called after `BeginPlay` and component registration ([Blueprint "set constrained components" failing](https://forums.unrealengine.com/t/blueprint-set-constrained-components-failing/304263); [Set Constrained Components not working in Construction script](https://forums.unrealengine.com/t/set-constrained-components-not-working-in-construction-script/325624); [Attempting to create a joint between objects that are both static](https://forums.unrealengine.com/t/attempting-to-create-a-joint-between-objects-that-are-both-static-no-joint-created/407751)).
- **Name-based binding fails silently** when a component name does not resolve. The guide's only check is whether a bounding box appears in the editor ([Constraints User Guide](https://dev.epicgames.com/documentation/en-us/unreal-engine/constraints-user-guide-in-unreal-engine)).
- **Chains go soft,** and the documented fix is projection, "a teleport for fixing larger errors". The Python docs warn that if the tolerances matter, "it likely indicates a setup stability issue" ([Python API, `set_projection_params`](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/PhysicsConstraintComponent)).

---

## O3DE (PhysX gem)

### Where a joint lives

- **A component on the follower body, naming the lead.** `JointComponentConfiguration` holds `m_leadEntity`, `m_followerEntity` (the entity the component sits on) and `m_localTransformFromFollower`, "Joint's location and orientation in the frame … of the follower entity" ([JointComponent.h L24–L36](https://github.com/o3de/o3de/blob/74006f539d581ea2ae8d21d4ef629d7a610a3e01/Gems/PhysX/Core/Code/Source/JointComponent.h#L24-L36)).
- The editor joint components require a dynamic rigid body on the follower, and are incompatible with non-uniform scale ([EditorHingeJointComponent.cpp L54–L68](https://github.com/o3de/o3de/blob/74006f539d581ea2ae8d21d4ef629d7a610a3e01/Gems/PhysX/Core/Code/Source/EditorHingeJointComponent.cpp#L54-L68)).
- **Several joints per body:** the joint request bus is addressed by `EntityComponentIdPair`, so each joint component on one entity is reachable on its own ([PhysXJointRequestsBus.h L23–L24](https://github.com/o3de/o3de/blob/74006f539d581ea2ae8d21d4ef629d7a610a3e01/Gems/PhysX/Core/Code/Include/PhysX/Joint/PhysXJointRequestsBus.h#L23-L24), [HingeJointComponent.cpp L94](https://github.com/o3de/o3de/blob/74006f539d581ea2ae8d21d4ef629d7a610a3e01/Gems/PhysX/Core/Code/Source/HingeJointComponent.cpp#L94)).
- **The joint waits for its bodies.** It listens on both bodies' notification buses and creates the native joint once both are enabled. It destroys the native joint when either body is disabled ([JointComponent.cpp L93–L129](https://github.com/o3de/o3de/blob/74006f539d581ea2ae8d21d4ef629d7a610a3e01/Gems/PhysX/Core/Code/Source/JointComponent.cpp#L93-L129)).

### Kinds

One component per kind: Ball, Fixed, Hinge, Prismatic and D6 (`BallJointComponent`, …, `D6JointComponent`, in [Gems/PhysX/Core/Code/Source](https://github.com/o3de/o3de/tree/74006f539d581ea2ae8d21d4ef629d7a610a3e01/Gems/PhysX/Core/Code/Source)). A spring joint is an open request ([#14224](https://github.com/o3de/o3de/issues/14224)). Underneath, the backend-neutral `AzPhysics::JointConfiguration` is a polymorphic base (RTTI) with parent and child local frames ([JointConfiguration.h L69–L72](https://github.com/o3de/o3de/blob/74006f539d581ea2ae8d21d4ef629d7a610a3e01/Code/Framework/AzFramework/AzFramework/Physics/Configuration/JointConfiguration.h#L69-L72)). It also has a string-keyed `GetPropertyValue` and `SetPropertyValue` for backend-specific extras ([L57](https://github.com/o3de/o3de/blob/74006f539d581ea2ae8d21d4ef629d7a610a3e01/Code/Framework/AzFramework/AzFramework/Physics/Configuration/JointConfiguration.h#L57)).

### Anchors

- **The joint's frame is local to the follower.** The lead's local frame is computed when the joint is created, from the joint's world transform ([JointComponent.cpp L256–L311](https://github.com/o3de/o3de/blob/74006f539d581ea2ae8d21d4ef629d7a610a3e01/Gems/PhysX/Core/Code/Source/JointComponent.cpp#L256-L311)).
- The editor has `m_fixJointLocation`: "When moving entity, the joint location and rotation will be recalculated to stay the same" ([EditorJointConfiguration.h L147–L150](https://github.com/o3de/o3de/blob/74006f539d581ea2ae8d21d4ef629d7a610a3e01/Gems/PhysX/Core/Code/Editor/EditorJointConfiguration.h#L147-L150)).
- Users ask for both frames to be exposed, as PhysX allows ([#9294](https://github.com/o3de/o3de/issues/9294)), and report that the local position is measured in the parent's scale ([#13741](https://github.com/o3de/o3de/issues/13741)).

### Joining to the world

Leave the lead entity empty: "If joint has no lead entity, it is a constraint on a global frame" ([JointComponent.cpp L109–L110](https://github.com/o3de/o3de/blob/74006f539d581ea2ae8d21d4ef629d7a610a3e01/Gems/PhysX/Core/Code/Source/JointComponent.cpp#L109-L110)). A lead entity without a body silently becomes the same thing: the code warns "Joint treated as constraint on global position" ([L266–L277](https://github.com/o3de/o3de/blob/74006f539d581ea2ae8d21d4ef629d7a610a3e01/Gems/PhysX/Core/Code/Source/JointComponent.cpp#L266-L277)).

### Limits, motors and springs

- `JointLimitProperties` (`m_isLimited`, `m_isSoftLimit`, damping, stiffness, two limit values, tolerance) and `JointMotorProperties`, held by value on the component ([PhysXJointConfiguration.h L67–L97](https://github.com/o3de/o3de/blob/74006f539d581ea2ae8d21d4ef629d7a610a3e01/Gems/PhysX/Core/Code/Include/PhysX/Joint/Configuration/PhysXJointConfiguration.h#L67-L97)).
- At runtime, `JointRequestBus` offers `SetVelocity`, `SetMaximumForce` and per-axis variants for the motor. It reads `GetLimits`, but has no limit setters ([PhysXJointRequestsBus.h L19–L92](https://github.com/o3de/o3de/blob/74006f539d581ea2ae8d21d4ef629d7a610a3e01/Gems/PhysX/Core/Code/Include/PhysX/Joint/PhysXJointRequestsBus.h#L19-L92)).

### Breaking

- `JointGenericProperties` has a `Breakable` flag with `m_forceMax` and `m_torqueMax` (defaults 1.0 when enabled); the comment says breakability can be switched during play ([PhysXJointConfiguration.h L41–L64](https://github.com/o3de/o3de/blob/74006f539d581ea2ae8d21d4ef629d7a610a3e01/Gems/PhysX/Core/Code/Include/PhysX/Joint/Configuration/PhysXJointConfiguration.h#L41-L64)). This goes straight to PhysX's `setBreakForce` ([PhysXJointUtils.cpp L121–L124](https://github.com/o3de/o3de/blob/74006f539d581ea2ae8d21d4ef629d7a610a3e01/Gems/PhysX/Core/Code/Source/Joint/PhysXJointUtils.cpp#L121-L124)).
- **Game code is not told.** The PhysX gem source has no handler for PhysX's constraint-break callback (a search for `onConstraintBreak` under `Gems/PhysX` at this commit finds nothing). The component stays, and the native joint simply stops acting.

### Reading state

`GetPosition()` returns the twist angle (hinge) or travel (prismatic), and `GetVelocity()` the rate along the free axis ([PhysXJointRequestsBus.h L26–L35](https://github.com/o3de/o3de/blob/74006f539d581ea2ae8d21d4ef629d7a610a3e01/Gems/PhysX/Core/Code/Include/PhysX/Joint/PhysXJointRequestsBus.h#L26-L35)). There is no applied-force read-back. `GetNativeJointPointer()` exposes the PhysX joint as an escape hatch ([L81–L89](https://github.com/o3de/o3de/blob/74006f539d581ea2ae8d21d4ef629d7a610a3e01/Gems/PhysX/Core/Code/Include/PhysX/Joint/PhysXJointRequestsBus.h#L81-L89)).

### Connected bodies colliding

**They do not collide by default.** The `SelfCollide` flag (the editor's "Lead-Follower Collide") maps to PhysX `eCOLLISION_ENABLED`, and the editor defaults it to false ([PhysXJointUtils.cpp L117–L119](https://github.com/o3de/o3de/blob/74006f539d581ea2ae8d21d4ef629d7a610a3e01/Gems/PhysX/Core/Code/Source/Joint/PhysXJointUtils.cpp#L117-L119); [EditorJointConfiguration.h L135–L139](https://github.com/o3de/o3de/blob/74006f539d581ea2ae8d21d4ef629d7a610a3e01/Gems/PhysX/Core/Code/Editor/EditorJointConfiguration.h#L135-L139); [Hinge Joint component docs](https://docs.o3de.org/docs/user-guide/components/reference/physx/hinge-joint/)).

### Scripting access

`JointRequestBus` is a C++ EBus. In the joint component files read, nothing is reflected to the `BehaviorContext`. An open request says no PhysX joint is exposed to Script Canvas, "including the Lead Entity setter" ([#15068](https://github.com/o3de/o3de/issues/15068)).

### Complaints

- Only one joint frame can be authored ([#9294](https://github.com/o3de/o3de/issues/9294)).
- Joint frame units follow parent scale ([#13741](https://github.com/o3de/o3de/issues/13741)).
- No scripting ([#15068](https://github.com/o3de/o3de/issues/15068)).
- Joints on entities with articulation links "fail silently" ([#18350](https://github.com/o3de/o3de/issues/18350)).
- The lead-entity picker offers unsuitable entities ([#6693](https://github.com/o3de/o3de/issues/6693)).

---

## Comparison

| | Unity (`Joint`) | Godot (`Joint3D`) | Avian | bevy_rapier (`ImpulseJoint`) | Unreal (`UPhysicsConstraintComponent`) | O3DE (PhysX) |
|---|---|---|---|---|---|---|
| **Where a joint lives** | Component on the moving body, `connectedBody` names the other | Own node, `node_a` and `node_b` paths | Own entity, `body1` and `body2` | Component on body 2 (or its child), `parent` names body 1 | Own scene component or actor, naming two components or bones | Component on the follower, `m_leadEntity` names the lead |
| **Several joints on one body** | Several components on the GameObject | Several nodes naming it | Several joint entities (one joint per entity) | Child entities, one joint each | Several components or actors | Several components, addressed by entity and component id |
| **Kinds** | One class per kind; `ConfigurableJoint` is 6-DOF | One node class per kind over one server joint with a kind; `Generic6DOFJoint3D` | One component per kind (fixed, distance, prismatic, revolute, spherical); no 6-DOF | One component, kind as an enum over a 6-DOF `GenericJoint` | One 6-DOF type; the kind is settings | One component per kind (ball, fixed, hinge, prismatic, D6) |
| **Changing kind** | Structural (swap component) | Structural (swap node) | Structural (swap component) | Field edit | Field edit | Structural |
| **Anchors** | Local `anchor`; `connectedAnchor` auto-computed by default; rest orientation re-derived on enable | Joint node's global transform, captured at configure, scale dropped | Local frame per body, or global converted to local next step | Local frame per body, no auto | Frame per body, taken from the component's placement; limits can scale | Follower-local frame; lead frame computed at creation |
| **Joining to the world** | `connectedBody` empty | One path empty (Jolt flips which side is the world) | Static body entity | Fixed body entity | Not stated in the docs read | Lead entity empty |
| **Limits, motors, springs** | Structs plus `use*` bools per kind, all settable | Float params and bool flags per kind, settable | Fields on the kind (`Option<Limit>`, motor with `enabled`, compliance); `JointDamping` component | Per-axis limits and motors, softness, all field edits | Profile struct: per-axis motion, soft limits, drives; runtime setters; named profiles swapped as a set | Limit and motor structs; runtime motor velocity and force only |
| **Breaking** | `breakForce` and `breakTorque`; joint destroyed; `OnJointBreak(float)` with no joint identity | None | None built in: `JointForces` plus `JointDisabled` marker, user system | None built in: `ImpulseJointImpulses` plus `ImpulseJointDisabled` | `bLinearBreakable` and `bAngularBreakable` with thresholds; constraint terminated, component kept; `OnConstraintBroken` | `Breakable` with force and torque max; PhysX breaks it; no notification |
| **Reading state** | `currentForce` and `currentTorque`; hinge `angle` and `velocity` | None (extension had applied force) | `JointForces` (opt-in) | Impulses component; revolute angle on request; multibody state component | Swing and twist angles, constraint force | Position and velocity along the free axis |
| **Bodies collide by default?** | Per-joint `enableCollision`; default not stated in the docs | No (`exclude_nodes_from_collision` true) | Yes (`JointCollisionDisabled` marker) | Yes (`contacts_enabled` true) | Per-profile `bDisableCollision`; default not stated in the docs | No (`SelfCollide` false) |
| **Scripting** | C# properties; `MonoBehaviour` message | GDScript and C# properties; server RIDs | Rust; `Reflect` | Rust | Blueprint and Python | C++ EBus only; no Script Canvas |
| **Common complaints** | Rest frame drift on re-enable; scale ignored; break callback anonymous; mass-ratio stretch | Frame captured once; Jolt world-side inversion; gizmo mismatch; collision exception lingers at server level | Chain and limit stability; parent transform effects; marker-induced panic | Joint-on-body model; frames ignore `Transform`; no angle; despawn panics | Runtime creation order; silent name binding; projection teleports | One frame only; scale; no scripting; silent failures |

The reduced-coordinate models are separate in every engine that has one: Unity `ArticulationBody`, Rapier `MultibodyJoint` and O3DE articulation links. Each lives on the child body, forms a tree, and has no breaking.

---

## What this means for FenrirPhysics

FenrirPhysics keeps plain-data components that physics diffs each tick, and an entity holds at most one component of each type (ADR-0011). Anything switched during play should be a field edit. Against those rules, the surveyed engines leave these options for tickets 22–26.

**Where a joint lives**

- **A. Its own entity naming both bodies** (Avian, Unity Physics, Godot's node, Unreal's component).
  - Several joints per body means several joint entities, which fits the one-of-each-type rule without a hierarchy trick.
  - It opens a sub-choice: whether the joint entity also sits in the transform hierarchy (Godot and Unreal use its transform as the frame), or is a free entity with explicit frames (Avian).
  - bevy_rapier's users are asking to move to this model ([#561](https://github.com/dimforge/bevy_rapier/issues/561)).
- **B. A component on one body naming the other** (Unity classic, O3DE, bevy_rapier).
  - The one-of-each-type rule then allows one joint per body. Further joints need child entities (bevy_rapier's answer), or a fixed number of slots.
  - It is natural only for trees, which is the shape articulations use.

**Kinds**

- **One `Joint` component with a tagged union of kind settings,** like `Collider` holding `CollisionShape` (bevy_rapier's `TypedJoint`, Unreal's single 6-DOF type). Changing kind is a field edit.
- **One component type per kind** (Avian, Unity, Godot, O3DE). Changing kind is a structural change, and a body cannot be "a hinge or a slider" under one type.
- Either way, a 6-DOF kind is the one ragdolls lean on: Unity `ConfigurableJoint` and `CharacterJoint`, Godot `Generic6DOFJoint3D`, Unreal everywhere, Rapier `GenericJoint`.

**Anchors**

- Explicit local frames per body (Avian, bevy_rapier, Unreal's stored frames).
- A world frame captured once at creation and converted to locals (Godot, O3DE's lead side, Avian's `FromGlobal`, Unity's auto-configure).
- The joint entity's own `Transform` as the frame (Godot, Unreal).

Capturing at creation or enable time is behind Unity's rest-drift and Godot's timing complaints. Explicit locals are behind bevy_rapier's "frame ignores my Transform" complaints. Scale: every engine either drops it (Godot orthonormalises) or offers a narrow opt-in (Unreal's `bScaleLinearLimits`). None follows runtime scale fully.

**Joining to the world**

- An empty or null body field (Unity, Godot, O3DE, and Unity Physics's pair).
- A static body entity (Avian, bevy_rapier).

Godot shows that "which side is the world" must be fixed by the API, or a backend swap inverts limits.

**Limits, motors and springs**

Every engine keeps them as fields with on/off switches (`use*`, `Option`, `enabled`, Free/Limited/Locked), all editable during play. Unreal also bundles them into named profiles swapped as a set. The choice is mainly per-kind fields versus per-axis arrays (Rapier, Unreal, Unity's `ConfigurableJoint`).

**Breaking.** Three shapes:

- **Thresholds in the joint, and physics destroys it** (Unity 3D). This is a structural change made by physics, the callback is anonymous, and Unity's own 2D API later added a reference and a choice of action.
- **Thresholds in the joint, and physics marks it broken while the component stays,** with an event naming the joint (Unreal's `IsBroken` and `OnConstraintBroken`; O3DE does the marking but sends no event).
- **No thresholds:** physics reports force each step, and game code disables the joint with a field or marker (Avian, bevy_rapier, Unity Physics's impulse events, godot-jolt). Avian's maintainer chose this on purpose.

A field-based `enabled` (Rapier's `GenericJoint::set_enabled`) fits the field-edit rule better than a marker component (`JointDisabled`, `ImpulseJointDisabled`).

**Reading state**

- Written back into a component each step (Avian `JointForces`, bevy_rapier `ImpulseJointImpulses` and `MultibodyJointState`).
- Asked for on demand (Unity properties, Unreal getters, Rapier's `impulse_revolute_joint_angle`, O3DE's bus).

Angle or position is offered only for single-free-axis kinds, or as swing and twist (Unreal).

**Connected bodies colliding.** Both defaults exist:

- Off: Godot, O3DE, and Unreal's flag on every constraint.
- On: Avian and Rapier.

The switch is a per-joint field everywhere except Avian, where it is a marker component, which is a structural change and caused a panic ([#1076](https://github.com/avianphysics/avian/issues/1076)). Every engine applies it to the directly joined pair only. Excluding a whole chain is a separate mechanism: Rapier's `MultibodySelfContactsDisabled`, Unreal's Physics Asset collision table. Godot shows that the exclusion must be undone when the joint goes away ([#115015](https://github.com/godotengine/godot/issues/115015)).

**Scripting.** The C# side mirrors whatever the components are. The complaint to avoid is O3DE's, where joints exist but scripts cannot reach them ([#15068](https://github.com/o3de/o3de/issues/15068)).

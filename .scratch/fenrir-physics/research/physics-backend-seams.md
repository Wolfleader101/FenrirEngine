# How other engines hide their physics backend

Research for [02-how-other-engines-hide-their-backend](../issues/02-how-other-engines-hide-their-backend.md). This file reports what other engines do and where their seams leak. It does not choose anything for FenrirPhysics.

Sources are engine source on GitHub, pinned to the commit read, and official docs. Line numbers refer to those commits:

- Godot `master` @ [`5ec712e`](https://github.com/godotengine/godot/tree/5ec712e220b7deac64d61dc9f968e72edb910039) (Godot links below use the prefix `G/` for `https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/`)
- godot-jolt `master` @ [`7f22589`](https://github.com/godot-jolt/godot-jolt/tree/7f225894706bb923e9ab9e9c73152b71880c5a83)
- O3DE `development` @ [`e580a19`](https://github.com/o3de/o3de/tree/e580a196d550bcddac5893d7e3bb3bdd1503e925)
- bevy_rapier, now kept in the Rapier repo ([old repo's README](https://github.com/dimforge/bevy_rapier/blob/master/README.md)), `master` @ [`475caa3`](https://github.com/dimforge/rapier/tree/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier)
- Avian `main` @ [`b620b67`](https://github.com/avianphysics/avian/tree/b620b67121def1a88a9eb272763aa24e7d594ccc)
- Unreal Engine: the source on GitHub needs an Epic account, so this file cites only the official API reference on dev.epicgames.com.

Every link in this file is a full URL. The `G/` shorthand above is for reading only.

---

## Godot: `PhysicsServer3D`, with Godot Physics and Jolt behind it

### Shape of the seam: a runtime interface, picked by a project setting

- `PhysicsServer3D` is an abstract class. Every operation on it is a pure virtual function, from `sphere_shape_create() = 0` to `step(real_t) = 0` ([physics_server_3d.h L51–L82, L382–L397](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/servers/physics_3d/physics_server_3d.h#L51-L397)).
- Each backend registers a factory callable by name with `PhysicsServer3DManager::register_server`, and one of them can be marked the default with `set_default_server` ([physics_server_3d_manager.h L78–L84](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/servers/physics_3d/physics_server_3d_manager.h#L78-L84)). At startup the manager creates the server named by the `physics/3d/physics_engine` project setting. If that fails it falls back to the default, and then to a `PhysicsServer3DDummy` ([physics_server_3d_manager.cpp L41, L54–L73](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/servers/physics_3d/physics_server_3d_manager.cpp#L54-L73)). Changing the setting requires a restart ([L89](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/servers/physics_3d/physics_server_3d_manager.cpp#L89)).
- Both built-in backends are modules that register themselves:
  - Godot Physics registers itself and sets itself as the default ([godot_physics_3d/register_types.cpp L57–L58](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/modules/godot_physics_3d/register_types.cpp#L57-L58)).
  - The Jolt module registers `JoltPhysicsServer3D`, wrapped in `PhysicsServer3DWrapMT` ([jolt_physics/register_types.cpp L42–L61](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/modules/jolt_physics/register_types.cpp#L42-L61)).
  - Its `config.py` builds the Jolt module unless 3D physics is disabled ([config.py](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/modules/jolt_physics/config.py)).
  - The selection itself happens at runtime.
- **A third-party backend plugs in through the same seam.** `PhysicsServer3DExtension` subclasses `PhysicsServer3D` so that a GDExtension can implement it ([physics_server_3d_extension.h L203](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/servers/physics_3d/physics_server_3d_extension.h#L203)). The godot-jolt extension did this: it subclasses `PhysicsServer3DExtension` ([jolt_physics_server_3d.hpp L11](https://github.com/godot-jolt/godot-jolt/blob/7f225894706bb923e9ab9e9c73152b71880c5a83/src/servers/jolt_physics_server_3d.hpp#L11)) and calls `PhysicsServer3DManager::register_server` ([register_types.cpp L17–L34](https://github.com/godot-jolt/godot-jolt/blob/7f225894706bb923e9ab9e9c73152b71880c5a83/src/register_types.cpp#L17-L34)).
- The official docs call the built-in module "largely a straight port" of the extension. Jolt shipped as a built-in alternative in Godot 4.4, and new projects now use it by default. The extension is in maintenance mode ([Using Jolt Physics, introduction and "differences to the Godot Jolt extension"](https://docs.godotengine.org/en/latest/tutorials/physics/using_jolt_physics.html)).

### Vocabulary

The server owns **space** (a world), **body**, **area** (a trigger volume with gravity and damping overrides), **soft body**, **shape** and **joint**. It also owns the enums `BodyMode`, `BodyParameter`, `BodyState`, `ShapeType`, `SpaceParameter` and `AreaParameter` ([physics_server_3d.h L73–L117, L161–L209, L404–L409](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/servers/physics_3d/physics_server_3d.h#L73-L209)).

- **Shapes are shared resources.** A body takes a list of shapes, each with a local transform: `body_add_shape(body, shape, transform, disabled)` ([L169–L171](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/servers/physics_3d/physics_server_3d.h#L169-L171)).
- **There are no material objects.** Friction and bounce are body parameters, set through `body_set_param(body, BodyParameter, Variant)` ([L202](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/servers/physics_3d/physics_server_3d.h#L202)).
- **Layers are two 32-bit masks per body**, set with `body_set_collision_layer` and `body_set_collision_mask` ([L188–L191](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/servers/physics_3d/physics_server_3d.h#L188-L191)). Jolt has its own scheme of object layers and broad-phase layers. The module maps each distinct pair of layer and mask to one Jolt `ObjectLayer`, allocated on first use and capped at 2^13 combinations ([jolt_layers.cpp L195–L215](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/modules/jolt_physics/spaces/jolt_layers.cpp#L195-L215)).
- **State and parameters cross the seam as `Variant`.** `shape_set_data(RID, Variant)` and `body_set_state(RID, BodyState, Variant)` are two examples ([L84, L209](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/servers/physics_3d/physics_server_3d.h#L84-L209)). The interface stays narrow, but every backend has to decode each payload itself.

### IDs and lifetimes

- **Every object is an opaque `RID`.** An RID is a 64-bit id ([rid.h L40](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/core/templates/rid.h#L40)) that carries a random validator, which the owner checks on lookup ([rid_owner.h L91–L162](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/core/templates/rid_owner.h#L91-L162)).
- Objects are created by `*_create()` and released by a single `free_rid(RID)` ([physics_server_3d.h L382](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/servers/physics_3d/physics_server_3d.h#L382)).
- The Jolt backend keeps one thread-safe `RID_PtrOwner<JoltX, true>` per object kind ([jolt_physics_server_3d.h L50–L55](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/modules/jolt_physics/jolt_physics_server_3d.h#L50-L55)). Its `free_rid` tries each owner in turn ([jolt_physics_server_3d.cpp L1604–L1620](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/modules/jolt_physics/jolt_physics_server_3d.cpp#L1604-L1620)).
- A wrapper object such as `JoltObject3D` stores the RID and the scene object's `ObjectID` ([jolt_object_3d.h L63–L64](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/modules/jolt_physics/objects/jolt_object_3d.h#L63-L64)), and also wraps the `JPH::Body` ([L121](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/modules/jolt_physics/objects/jolt_object_3d.h#L121)). The way back from Jolt to the wrapper is the Jolt body's user-data pointer ([jolt_contact_listener_3d.cpp L97–L98](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/modules/jolt_physics/spaces/jolt_contact_listener_3d.cpp#L97-L98)).
- The way back from the server to the scene is `body_attach_object_instance_id(RID, ObjectID)` ([physics_server_3d.h L182–L183](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/servers/physics_3d/physics_server_3d.h#L182-L183)). Query results return both the `rid` and the `collider_id` ([physics_server_3d_types.h L56–L63](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/servers/physics_3d/physics_server_3d_types.h#L56-L63)).
- The scene node owns the RID: `CollisionObject3D` frees it on destruction ([collision_object_3d.cpp L782](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/scene/3d/physics/collision_object_3d.cpp#L782)).

### Who owns state, and how transforms sync

- The server owns the simulated state. Nodes push world-space transforms in:
  - When a node enters the world or its transform changes, it calls `body_set_state(rid, BODY_STATE_TRANSFORM, get_global_transform())` ([collision_object_3d.cpp L65–L105](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/scene/3d/physics/collision_object_3d.cpp#L65-L105)).
- Nodes pull results out through a per-body callback:
  - `RigidBody3D` registers `body_set_state_sync_callback` ([rigid_body_3d.cpp L835](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/scene/3d/physics/rigid_body_3d.cpp#L835)).
  - In that callback the node writes the server's transform back with `set_global_transform`. It suppresses its own transform notification while doing so, so the write does not echo back to the server ([L150–L169](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/scene/3d/physics/rigid_body_3d.cpp#L150-L169)).
- The server API is entirely in world space. The scene layer converts to and from the local transforms of the node hierarchy, through the scene graph's own global-transform handling.

### Stepping, and how contact events cross threads

- **Order within a frame.** The main loop calls `sync()` and `flush_queries()`, then the game's `physics_process`, then `end_sync()` and `step()` ([main.cpp L5017–L5062](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/main/main.cpp#L5017-L5062)). Callbacks therefore fire from `flush_queries`, before user code runs, and they report the step taken in the previous frame.
- **Contacts inside the step.** Jolt calls its `ContactListener` callbacks from Jolt's job threads. The Godot module:
  - writes each contact manifold into a thread-local buffer (`ThreadLocals`) ([jolt_contact_listener_3d.h L84–L104](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/modules/jolt_physics/spaces/jolt_contact_listener_3d.h#L84-L104));
  - after `PhysicsSystem::Update`, merges those buffers under a mutex in `post_step`, keeping only the deepest manifold per shape pair ([jolt_contact_listener_3d.cpp L412–L448, L581–L587](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/modules/jolt_physics/spaces/jolt_contact_listener_3d.cpp#L412-L587));
  - attaches the merged contacts to each body;
  - queues each affected body or area into a mutex-guarded list ([jolt_space_3d.cpp L485–L508](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/modules/jolt_physics/spaces/jolt_space_3d.cpp#L485-L508)).

  `flush_queries` then drains the list on the main thread ([jolt_space_3d.cpp L221–L232](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/modules/jolt_physics/spaces/jolt_space_3d.cpp#L221-L232), [jolt_physics_server_3d.cpp L1665–L1680](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/modules/jolt_physics/jolt_physics_server_3d.cpp#L1665-L1680)).
- **Area overlaps** go through the same path, as enter and exit lists of `SubShapeIDPair`s ([jolt_contact_listener_3d.h L88–L90](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/modules/jolt_physics/spaces/jolt_contact_listener_3d.h#L88-L90)). Callables registered with `area_set_monitor_callback` deliver them ([physics_server_3d.h L152](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/servers/physics_3d/physics_server_3d.h#L152)).
- **Threading of the whole server.** `PhysicsServer3DWrapMT` decorates any server:
  - It forwards calls into a command queue when the caller is not the server thread ([physics_server_3d_wrap_mt.h L38–L40, L68–L80](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/servers/physics_3d/physics_server_3d_wrap_mt.h#L38-L80)).
  - `*_create` is not queued. It "returns directly, ensure thread safety yourself" ([server_wrap_mt_common.h L69–L73](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/servers/server_wrap_mt_common.h#L69-L73)), which is why the Jolt RID owners are thread-safe.

### Queries

- Queries go through `PhysicsDirectSpaceState3D`, obtained with `space_get_direct_state(space)`. That getter "only works on physics process, errors and returns null otherwise" ([physics_server_3d.h L104–L105](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/servers/physics_3d/physics_server_3d.h#L104-L105)).
- Queries are plain structs:
  - `RayParameters` holds from, to, an exclude set of RIDs, a collision mask, and flags for bodies and areas ([physics_server_3d_types.h L40–L53](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/servers/physics_3d/physics_server_3d_types.h#L40-L53)).
  - `RayResult` holds position, normal, rid, collider_id, shape index and face_index ([L56–L63](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/servers/physics_3d/physics_server_3d_types.h#L56-L63)).
  - Shape queries return either result objects or arrays ([physics_direct_space_state_3d.h L51–L67](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/servers/physics_3d/direct_states/physics_direct_space_state_3d.h#L51-L67)).
- `body_test_motion` is a dedicated query for character movement ([physics_server_3d.h L258](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/servers/physics_3d/physics_server_3d.h#L258)).

### What it could not hide (Jolt behind `PhysicsServer3D`)

- **The generic layer names the backends.** `PhysicsServer3DManager` hard-codes `GODOT_PHYSICS_3D_NAME` and `JOLT_PHYSICS_NAME` ([physics_server_3d_manager.h L73–L74](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/servers/physics_3d/physics_server_3d_manager.h#L73-L74)). `Shape3D::_validate_property` checks which backend is active, to hide `custom_solver_bias` under Jolt and `margin` under Godot Physics ([shape_3d.cpp L149–L158](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/scene/resources/3d/shape_3d.cpp#L149-L158)). The interface carries properties that only one backend uses.
- **The backend's tuning surfaces as its own settings.** The module registers about 30 settings under `physics/jolt_physics_3d/…`, covering:
  - the solver's velocity and position steps, Baumgarte factor, penetration slop and speculative contact distance;
  - edge removal;
  - limits for max bodies, body pairs, contact constraints and temporary memory.

  See [jolt_project_settings.cpp L37–L121](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/modules/jolt_physics/jolt_project_settings.cpp#L37-L121). Jolt's fixed-capacity buffers reach users as runtime warnings that point at these settings ([jolt_space_3d.cpp L187–L220](https://github.com/godotengine/godot/blob/5ec712e220b7deac64d61dc9f968e72edb910039/modules/jolt_physics/spaces/jolt_space_3d.cpp#L187-L220)).
- **Behaviour differs in ways the docs must list** ([Using Jolt Physics, "Notable differences to Godot Physics"](https://docs.godotengine.org/en/latest/tutorials/physics/using_jolt_physics.html)):
  - Joint soft limits are unsupported.
  - Single-body joints attach the body as `node_b`, not `node_a`, with a `joints/world_node` setting to restore the old behaviour.
  - Collision margins mean something different: Jolt shrinks the shape and then adds a shell.
  - Baumgarte stabilization behaves differently.
  - Ghost-collision handling differs.
  - Jolt allocates a fixed temporary buffer.
  - `face_index` is −1 unless opted into, at a memory cost.
  - Kinematic bodies do not report contacts with static and kinematic bodies unless opted into.
  - Contact impulses are estimates.
  - The world-boundary plane is smaller.
- **The extension found the same gaps.** godot-jolt lists:
  - no determinism guarantee, "even though Jolt itself offers deterministic simulations";
  - not thread-safe;
  - shape scale actually used;
  - joints that only fit Jolt's model through its own substitute joint nodes;
  - costly shape edits after a body enters the tree.

  See [README, "What about determinism?", "What's not supported?", "What else is different?"](https://github.com/godot-jolt/godot-jolt/blob/7f225894706bb923e9ab9e9c73152b71880c5a83/README.md#L47-L78). The substitute joint nodes (`JoltHingeJoint3D` and others) are new API, registered by the extension itself ([register_types.cpp L40–L43](https://github.com/godot-jolt/godot-jolt/blob/7f225894706bb923e9ab9e9c73152b71880c5a83/src/register_types.cpp#L40-L43)). They were not carried into the built-in module ([docs](https://docs.godotengine.org/en/latest/tutorials/physics/using_jolt_physics.html)).

---

## O3DE: the AzPhysics interfaces over PhysX

### Shape of the seam: runtime interfaces, with one backend gem

- The `AzPhysics::SystemInterface` and `AzPhysics::SceneInterface` interfaces live in AzFramework. Their operations are pure virtual functions ([PhysicsSystem.h L25–L135](https://github.com/o3de/o3de/blob/e580a196d550bcddac5893d7e3bb3bdd1503e925/Code/Framework/AzFramework/AzFramework/Physics/PhysicsSystem.h#L25-L135), [PhysicsScene.h L25–L254](https://github.com/o3de/o3de/blob/e580a196d550bcddac5893d7e3bb3bdd1503e925/Code/Framework/AzFramework/AzFramework/Physics/PhysicsScene.h#L25-L254)).
- The backend registers itself as the implementation: `class PhysXSystem : public AZ::Interface<AzPhysics::SystemInterface>::Registrar` ([PhysXSystem.h L40–L41](https://github.com/o3de/o3de/blob/e580a196d550bcddac5893d7e3bb3bdd1503e925/Gems/PhysX/Core/Code/Source/System/PhysXSystem.h#L40-L41)). Callers fetch it with `AZ::Interface<AzPhysics::SystemInterface>::Get()` ([PhysicsSystem.h L56](https://github.com/o3de/o3de/blob/e580a196d550bcddac5893d7e3bb3bdd1503e925/Code/Framework/AzFramework/AzFramework/Physics/PhysicsSystem.h#L56)).
- Selection happens at project level, by which gem is enabled. The only backend in the tree is the `PhysX` gem, built against PhysX 5 (`Gems/PhysX/Core/PhysX5`). The repository holds no second implementation of `SystemInterface` (from the repository tree at the pinned commit).

### Vocabulary

**Scene**, **SimulatedBody** (the base of `RigidBody` and `StaticRigidBody`), **Shape**, **Material**, **CollisionLayer** and **CollisionGroup**, and **Joint**.

- Bodies are created from configuration objects: `AddSimulatedBody(SceneHandle, const SimulatedBodyConfiguration*)` ([PhysicsScene.h L71](https://github.com/o3de/o3de/blob/e580a196d550bcddac5893d7e3bb3bdd1503e925/Code/Framework/AzFramework/AzFramework/Physics/PhysicsScene.h#L71)).
- There are 64 named collision layers, combined into groups as 64-bit masks ([CollisionLayers.h L27–L68](https://github.com/o3de/o3de/blob/e580a196d550bcddac5893d7e3bb3bdd1503e925/Code/Framework/AzFramework/AzFramework/Physics/Collision/CollisionLayers.h#L27-L68)).
- Layers and groups are set per shape, not per body ([Shape.h L102–L105](https://github.com/o3de/o3de/blob/e580a196d550bcddac5893d7e3bb3bdd1503e925/Code/Framework/AzFramework/AzFramework/Physics/Shape.h#L102-L105)).
- **Materials are bags of string-keyed properties:** `GetProperty(string_view)` and `SetProperty(string_view, MaterialPropertyValue)` ([PhysicsMaterial.h L19–L29](https://github.com/o3de/o3de/blob/e580a196d550bcddac5893d7e3bb3bdd1503e925/Code/Framework/AzFramework/AzFramework/Physics/Material/PhysicsMaterial.h#L19-L29)). A backend's `MaterialManager` creates them ([PhysicsMaterialManager.h L24–L56](https://github.com/o3de/o3de/blob/e580a196d550bcddac5893d7e3bb3bdd1503e925/Code/Framework/AzFramework/AzFramework/Physics/Material/PhysicsMaterialManager.h#L24-L56)).

### IDs and lifetimes

- **Handles are tuples, not pointers.** `SceneHandle = tuple<Crc32 of scene name, s8 index>`, and `SimulatedBodyHandle` and `JointHandle` are `tuple<Crc32, s32 index>` ([PhysicsTypes.h L50–L75](https://github.com/o3de/o3de/blob/e580a196d550bcddac5893d7e3bb3bdd1503e925/Code/Framework/AzFramework/AzFramework/Physics/Common/PhysicsTypes.h#L50-L75)).
- In PhysX, each body slot also stores a CRC for that body. A lookup checks the handle's CRC against the stored one, which makes the CRC act like a generation check:
  - Freed slot indices go back onto a free queue for reuse ([PhysXScene.cpp L733–L746, L785–L788](https://github.com/o3de/o3de/blob/e580a196d550bcddac5893d7e3bb3bdd1503e925/Gems/PhysX/Core/Code/Source/Scene/PhysXScene.cpp#L733-L788)).
  - Removing a body clears the slot and pushes the body onto `m_deferredDeletions` ([L811–L824](https://github.com/o3de/o3de/blob/e580a196d550bcddac5893d7e3bb3bdd1503e925/Gems/PhysX/Core/Code/Source/Scene/PhysXScene.cpp#L811-L824)).
  - Deferred deletions are destroyed after the step's results are fetched ([L650–L651](https://github.com/o3de/o3de/blob/e580a196d550bcddac5893d7e3bb3bdd1503e925/Gems/PhysX/Core/Code/Source/Scene/PhysXScene.cpp#L650-L651)).
- `SceneInterface::GetSimulatedBodyFromHandle` returns a raw `SimulatedBody*`, so callers can still hold a pointer ([PhysicsScene.h L84](https://github.com/o3de/o3de/blob/e580a196d550bcddac5893d7e3bb3bdd1503e925/Code/Framework/AzFramework/AzFramework/Physics/PhysicsScene.h#L84)).
- Every body knows its `AZ::EntityId` ([PhysicsSimulatedBody.h L132](https://github.com/o3de/o3de/blob/e580a196d550bcddac5893d7e3bb3bdd1503e925/Code/Framework/AzFramework/AzFramework/Physics/Common/PhysicsSimulatedBody.h#L132)). The system can look up a body from an entity with `FindAttachedBodyHandleFromEntityId` ([PhysicsSystem.h L118](https://github.com/o3de/o3de/blob/e580a196d550bcddac5893d7e3bb3bdd1503e925/Code/Framework/AzFramework/AzFramework/Physics/PhysicsSystem.h#L118)).

### Who owns state, and how transforms sync

- The backend owns body state. The entity component mirrors it:
  - `RigidBodyComponent` adds its body on activation, removes it on deactivation ([RigidBodyComponent.cpp L348–L387](https://github.com/o3de/o3de/blob/e580a196d550bcddac5893d7e3bb3bdd1503e925/Gems/PhysX/Core/Code/Source/RigidBodyComponent.cpp#L348-L387)), and subscribes to the scene's simulation-finish event.
  - In `PostPhysicsTick` it reads the body's world pose and writes `SetWorldTM`, either directly or through a forward-time interpolator ([L245–L300](https://github.com/o3de/o3de/blob/e580a196d550bcddac5893d7e3bb3bdd1503e925/Gems/PhysX/Core/Code/Source/RigidBodyComponent.cpp#L245-L300)).
  - User transform changes go the other way, through `OnTransformChanged`, which calls `body->SetTransform(world)` ([L304–L320](https://github.com/o3de/o3de/blob/e580a196d550bcddac5893d7e3bb3bdd1503e925/Gems/PhysX/Core/Code/Source/RigidBodyComponent.cpp#L304-L320)).
- **Hierarchy problem, as stated in the code:** "OnTransformChanged is not safe at the moment due to TransformComponent design flaw. It is called when the parent entity is activated after the children causing rigid body to move through the level instantly." ([L306–L308](https://github.com/o3de/o3de/blob/e580a196d550bcddac5893d7e3bb3bdd1503e925/Gems/PhysX/Core/Code/Source/RigidBodyComponent.cpp#L306-L308)).
- **Stepping.** `SystemInterface::Simulate(deltaTime)` is documented as `StartSimulation` (spawns jobs) followed by `FinishSimulation` (blocks and swaps buffers) for each scene ([PhysicsSystem.h L60–L72](https://github.com/o3de/o3de/blob/e580a196d550bcddac5893d7e3bb3bdd1503e925/Code/Framework/AzFramework/AzFramework/Physics/PhysicsSystem.h#L60-L72)). The fixed-timestep accumulator lives inside the backend, in `PhysXSystem::Simulate` ([PhysXSystem.cpp L178–L260](https://github.com/o3de/o3de/blob/e580a196d550bcddac5893d7e3bb3bdd1503e925/Gems/PhysX/Core/Code/Source/System/PhysXSystem.cpp#L178-L260)).

### Contact events across threads

- The PhysX `onContact` and `onTrigger` callbacks only push events onto `m_queuedCollisionEvents` and `m_queuedTriggerEvents` ([PhysXSceneSimulationEventCallback.cpp L49–L163](https://github.com/o3de/o3de/blob/e580a196d550bcddac5893d7e3bb3bdd1503e925/Gems/PhysX/Core/Code/Source/Scene/PhysXSceneSimulationEventCallback.cpp#L49-L163)).
- `FinishSimulation` runs on the thread that called it:
  1. `checkResults(true)` waits for the step without taking the scene's write lock, because of a PhysX deadlock with contact-modify callbacks (cited in a code comment).
  2. `fetchResults` runs under the lock.
  3. Active-body handles are collected.
  4. `FlushQueuedEvents()` signals the trigger events and then the collision events.
  5. Deferred deletions are cleared.
  6. `OnSceneSimulationFinish` is signalled.

  See [PhysXScene.cpp L578–L668](https://github.com/o3de/o3de/blob/e580a196d550bcddac5893d7e3bb3bdd1503e925/Gems/PhysX/Core/Code/Source/Scene/PhysXScene.cpp#L578-L668).
- Users subscribe per scene, with `RegisterSceneCollisionEventHandler` and `RegisterSceneTriggersEventHandler` ([PhysicsScene.h L244–L249](https://github.com/o3de/o3de/blob/e580a196d550bcddac5893d7e3bb3bdd1503e925/Code/Framework/AzFramework/AzFramework/Physics/PhysicsScene.h#L244-L249)).
- The event structs carry handles, plus raw `SimulatedBody*` and `Physics::Shape*` pointers, plus a vector of contacts ([CollisionEvents.h L34–L96](https://github.com/o3de/o3de/blob/e580a196d550bcddac5893d7e3bb3bdd1503e925/Code/Framework/AzFramework/AzFramework/Physics/Collision/CollisionEvents.h#L34-L96)).

### Queries

- `QueryScene(SceneHandle, const SceneQueryRequest*)` is polymorphic over request types (raycast, shapecast, overlap). It also has a batch form ([PhysicsScene.h L134–L147](https://github.com/o3de/o3de/blob/e580a196d550bcddac5893d7e3bb3bdd1503e925/Code/Framework/AzFramework/AzFramework/Physics/PhysicsScene.h#L134-L147)).
- Requests carry a `CollisionGroup`, a static/dynamic query type, a filter callback, and whether to report multiple hits ([PhysicsSceneQueries.h L157–L247](https://github.com/o3de/o3de/blob/e580a196d550bcddac5893d7e3bb3bdd1503e925/Code/Framework/AzFramework/AzFramework/Physics/Common/PhysicsSceneQueries.h#L157-L247)).
- A hit returns a `ResultFlags` mask saying which fields are valid, plus the body handle, `EntityId`, `Physics::Shape*`, material id, position and normal ([L275–L300](https://github.com/o3de/o3de/blob/e580a196d550bcddac5893d7e3bb3bdd1503e925/Code/Framework/AzFramework/AzFramework/Physics/Common/PhysicsSceneQueries.h#L275-L300)).

### What it could not hide

- **Native-pointer escape hatches sit in the abstract interface itself.** They are:
  - `Scene::GetNativePointer()` ([PhysicsScene.h L410](https://github.com/o3de/o3de/blob/e580a196d550bcddac5893d7e3bb3bdd1503e925/Code/Framework/AzFramework/AzFramework/Physics/PhysicsScene.h#L410));
  - `SimulatedBody::GetNativeType()` and `GetNativePointer()` ([PhysicsSimulatedBody.h L124–L125](https://github.com/o3de/o3de/blob/e580a196d550bcddac5893d7e3bb3bdd1503e925/Code/Framework/AzFramework/AzFramework/Physics/Common/PhysicsSimulatedBody.h#L124-L125));
  - `Shape::GetNativePointer()` ([Shape.h L118–L119](https://github.com/o3de/o3de/blob/e580a196d550bcddac5893d7e3bb3bdd1503e925/Code/Framework/AzFramework/AzFramework/Physics/Shape.h#L118-L119)).

  The backend casts them back, for example `static_cast<physx::PxActor*>(body.GetNativePointer())` ([PhysXScene.cpp L1094, L1122](https://github.com/o3de/o3de/blob/e580a196d550bcddac5893d7e3bb3bdd1503e925/Gems/PhysX/Core/Code/Source/Scene/PhysXScene.cpp#L1094-L1122)).
- **The entity components belong to the backend gem.** `RigidBodyComponent`, `BaseColliderComponent`, `BoxColliderComponent` and `StaticRigidBodyComponent` live in `Gems/PhysX/Core/Code/Source/` (repository tree at the pinned commit). Scripting shows them under the category `"PhysX"` ([RigidBodyComponent.cpp L68](https://github.com/o3de/o3de/blob/e580a196d550bcddac5893d7e3bb3bdd1503e925/Gems/PhysX/Core/Code/Source/RigidBodyComponent.cpp#L68)). A second backend would need its own components, so scenes saved with these would reference PhysX components.
- **Backend-only settings are serialized into the component.** The rigid-body component serializes a `"PhysXSpecificConfiguration"` field ([RigidBodyComponent.cpp L59](https://github.com/o3de/o3de/blob/e580a196d550bcddac5893d7e3bb3bdd1503e925/Gems/PhysX/Core/Code/Source/RigidBodyComponent.cpp#L59)). The header describes it as "Properties specific to PhysX which might not have exact equivalents in other physics engines" ([RigidBodyComponent.h L165](https://github.com/o3de/o3de/blob/e580a196d550bcddac5893d7e3bb3bdd1503e925/Gems/PhysX/Core/Code/Source/RigidBodyComponent.h#L165)).
- **Threading and locking follow PhysX's model.** The interface splits stepping into `StartSimulation` and `FinishSimulation`, matching PhysX's `simulate()` and `fetchResults()`, and the backend's lock workaround is visible in the code ([PhysXScene.cpp L558–L606](https://github.com/o3de/o3de/blob/e580a196d550bcddac5893d7e3bb3bdd1503e925/Gems/PhysX/Core/Code/Source/Scene/PhysXScene.cpp#L558-L606)).

---

## Bevy ecosystem

The two libraries sit at opposite ends. bevy_rapier wraps a separate physics world. Avian is a physics engine written in Bevy's ECS, with no separate world to hide.

### bevy_rapier: ECS components that mirror a Rapier world

**Shape of the seam.** It is a build-time choice of crate (`bevy_rapier2d` or `bevy_rapier3d`), and it re-exports the backend publicly: `pub extern crate rapier3d as rapier; pub use rapier::parry;` ([lib.rs L20–L23](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/lib.rs#L20-L23)). Rapier is not hidden. The plugin is a sync layer over it.

**Components and vocabulary.**

- `RigidBody` is an enum ([dynamics/rigid_body.rs L41](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/dynamics/rigid_body.rs#L41)). Small components alongside it include `Velocity`, `ExternalForce`, `ExternalImpulse`, `GravityScale`, `Ccd`, `Sleeping`, `Damping`, `LockedAxes` and `TransformInterpolation` ([L96–L687](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/dynamics/rigid_body.rs#L96-L687)).
- `Collider` is a component around `pub raw: SharedShape`, a Parry type ([geometry/collider.rs L106–L114](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/geometry/collider.rs#L106-L114)). `Sensor`, `Friction`, `Restitution`, `CollisionGroups`, `SolverGroups`, `ActiveEvents` and `ActiveHooks` are separate components ([L177–L640](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/geometry/collider.rs#L177-L640)).

**IDs and lifetimes.**

- When a body or collider is created, the plugin inserts `RapierRigidBodyHandle(pub RigidBodyHandle)` or `RapierColliderHandle(pub ColliderHandle)` as components ([rigid_body.rs L13](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/dynamics/rigid_body.rs#L13), [collider.rs L27](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/geometry/collider.rs#L27), [systems/collider.rs L571](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/plugin/systems/collider.rs#L571)).
- The context resources keep reverse maps, `entity2body` and `entity2collider` ([context/mod.rs L95, L1259](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/plugin/context/mod.rs#L1252-L1266)). Each Rapier collider stores its entity bits in `user_data` ([systems/collider.rs L507](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/plugin/systems/collider.rs#L507)).
- Despawning is caught through `RemovedComponents<RapierRigidBodyHandle>` and related queries, and the matching Rapier objects are then removed ([systems/remove.rs L32–L99](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/plugin/systems/remove.rs#L32-L99)).

**Who owns state.** Rapier's `RigidBodySet` and `ColliderSet` own it ([context/mod.rs L91–L93, L1252–L1254](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/plugin/context/mod.rs#L1252-L1254)). Each step runs three system sets, `SyncBackend`, then `StepSimulation`, then `Writeback`, in `PostUpdate` by default or in `FixedUpdate` if configured ([plugin.rs L111–L263](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/plugin/plugin.rs#L111-L263)):

- **`SyncBackend`** runs Bevy's transform propagation itself so that `GlobalTransform` is current. It then creates new bodies and colliders and applies user changes ([L127–L180](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/plugin/plugin.rs#L127-L180)).
- **Detecting user edits to transforms.** The plugin remembers the `GlobalTransform` it last wrote (`last_body_transform_set`). Any other value is treated as a user edit that must be pushed into Rapier ([systems/rigid_body.rs L225–L240](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/plugin/systems/rigid_body.rs#L225-L240)).
- **`Writeback`** turns each body's world pose back into a local `Transform`, as `parent_global.inverse() * world_pose`. It also predicts the next `GlobalTransform`, so that rounding does not trip the change detection. The code comment explains why it reads the parent's `GlobalTransform` rather than deriving it ([systems/rigid_body.rs L545–L600](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/plugin/systems/rigid_body.rs#L545-L600)).
- **Scale.** "Rapier's `RigidBody` doesn't know its own scale as it is encoded directly within its collider", so scale is copied from the Bevy transform ([L545–L548](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/plugin/systems/rigid_body.rs#L545-L548)).
- **Hierarchy for colliders.** `collider_offset` walks `ChildOf` up from a collider entity to the nearest entity with a rigid body. It multiplies the local `Transform`s on the way to get the collider's offset relative to that body ([systems/collider.rs L343–L377](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/plugin/systems/collider.rs#L343-L377)). The collider is then inserted with that body as its parent ([L544–L548](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/plugin/systems/collider.rs#L544-L548)).

**Contact events across threads.**

- `EventQueue` implements Rapier's `EventHandler` trait. It maps collider handles to entities through `user_data`, or through a `deleted_colliders` map for colliders removed during the step, and pushes into `RwLock<Vec<CollisionEvent>>` ([pipeline/events.rs L279–L327](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/pipeline/events.rs#L279-L327)).
- The public event is a Bevy `Message`: `CollisionEvent::Started(Entity, Entity, CollisionEventFlags)` or `Stopped(...)` ([L33–L39](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/pipeline/events.rs#L33-L39)). Contact geometry is not in the event. Users read it from the narrow phase while the pair still exists ([L25–L32](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/pipeline/events.rs#L25-L32)).
- Events are opt-in per collider through the `ActiveEvents` flag ([L22–L23](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/pipeline/events.rs#L22-L23)).

**Queries.** `RapierQueryPipeline::cast_ray(origin, dir, max_toi, solid) -> Option<(Entity, Real)>` translates the backend's collider handle back to an `Entity` ([context/mod.rs L828–L843](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/plugin/context/mod.rs#L828-L843)). `intersect_shape`, `cast_shape` and `project_point` work the same way ([L852–L1083](https://github.com/dimforge/rapier/blob/475caa3ff4646fd9a796b67d14116256f4962bc7/bindings/bevy_rapier/src/plugin/context/mod.rs#L852-L1083)).

**Leaks.** Most of the API is the backend:

- `Collider.raw` is a Parry `SharedShape`.
- `CollisionEventFlags` is a Rapier type.
- The handle components wrap Rapier handles.
- The crate re-exports `rapier` and `parry` wholesale.

### Avian: no backend, the ECS is the physics engine

**Shape of the seam.**

- Avian is "an ECS-driven 2D and 3D physics engine". It "uses the ECS for both the internals and the public API", which "removes the need for a separate physics world" ([lib.rs L3, L280–L282](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/src/lib.rs#L280-L282)). It has no body-side seam.
- **The one seam it does have is for colliders.** `AnyCollider` is a trait that "generalizes over colliders" ([collision/collider/mod.rs L124–L126](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/src/collision/collider/mod.rs#L124-L126)). It requires an AABB and contact manifolds ([L210–L331](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/src/collision/collider/mod.rs#L210-L331)). A custom type plugs in through `ColliderBackendPlugin<C>` ([backend.rs L1–L68](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/src/collision/collider/backend.rs#L1-L68)).
- The default collider is Parry-backed and chosen at build time by Cargo feature: `default-collider`, `parry-f32` or `parry-f64` ([crates/avian3d/Cargo.toml L15–L41](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/crates/avian3d/Cargo.toml#L15-L41)).

**Components.**

- `RigidBody` is an enum. Through `#[require(...)]` it pulls in its state components, such as `LinearVelocity`, `AngularVelocity` and mass properties ([dynamics/rigid_body/mod.rs L265–L285, L410](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/src/dynamics/rigid_body/mod.rs#L265-L285)).
- The simulation's pose lives in its own components, `Position` and `Rotation`, separate from Bevy's `Transform` ([physics_transform/transform.rs L44, L156](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/src/physics_transform/transform.rs#L44-L156)).

**IDs and lifetimes.** The `Entity` is the id, and no handles exist. Colliders link to bodies through `ColliderOf { body: Entity }`, an ECS relationship whose target side is `RigidBodyColliders(Vec<Entity>)` ([collider_hierarchy/mod.rs L1–L2, L53–L56, L208–L212](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/src/collision/collider/collider_hierarchy/mod.rs#L1-L212)). By default a collider attaches to the nearest rigid-body ancestor in the `ChildOf` hierarchy, or `ColliderOf` can be inserted explicitly ([L30–L44](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/src/collision/collider/collider_hierarchy/mod.rs#L30-L44)). Bevy does not allow a relationship that points at its own entity, so Avian implements this relationship by hand ([L67–L68](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/src/collision/collider/collider_hierarchy/mod.rs#L67-L68)).

**Transforms and hierarchy.**

- `PhysicsTransformPlugin` syncs in both directions:
  - `transform_to_position` copies `GlobalTransform` changes into `Position` and `Rotation`, ignoring changes under 0.01 mm ([physics_transform/mod.rs L184–L204](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/src/physics_transform/mod.rs#L184-L204)).
  - `position_to_transform` writes back a local `Transform`, reparented to the parent's pose ([L336–L370](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/src/physics_transform/mod.rs#L336-L370)).
  - Each direction can be switched off in `PhysicsTransformConfig` ([L132–L163](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/src/physics_transform/mod.rs#L132-L163)).
- **Nested rigid bodies are flattened.** "The engine treats nested rigid bodies as a flat structure… moving the parent will not affect the child". A `FixedJoint` is the suggested way to attach them rigidly ([L42–L49](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/src/physics_transform/mod.rs#L42-L49)).
- The simulation runs in its own `PhysicsSchedule`, run from `FixedPostUpdate` by default ([schedule/mod.rs L35–L54](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/src/schedule/mod.rs#L35-L54)).

**Contact events across threads.**

- The narrow phase updates contacts in parallel, into `NarrowPhaseThreadLocals` (a `ThreadLocal<RefCell<…>>` of bit vectors) ([narrow_phase/system_param.rs L92–L107](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/src/collision/narrow_phase/system_param.rs#L92-L107)).
- The system then writes `CollisionStart` and `CollisionEnd` through `MessageWriter`s ([narrow_phase/mod.rs L249–L275](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/src/collision/narrow_phase/mod.rs#L249-L275)).
- A later system also triggers them as observer `Event`s on the entity ([L284–L352](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/src/collision/narrow_phase/mod.rs#L284-L352)). Observer events fire only for entities with `CollisionEventsEnabled` ([collision_events.rs L35–L65](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/src/collision/collision_events.rs#L35-L65)).
- An event carries the two collider entities and their bodies ([L171–L178](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/src/collision/collision_events.rs#L171-L178)).

**Layers and queries.**

- `CollisionLayers { memberships: LayerMask, filters: LayerMask }` over a `u32`, with a `PhysicsLayer` derive for named layers ([layers.rs L9, L86, L362–L368](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/src/collision/collider/layers.rs#L362-L368)).
- The `SpatialQuery` system parameter has `cast_ray(...) -> Option<RayHitData>` ([spatial_query/system_param.rs L60, L124–L131](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/src/spatial_query/system_param.rs#L124-L131)), where `RayHitData` holds entity, distance and normal ([ray_caster.rs L403–L412](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/src/spatial_query/ray_caster.rs#L403-L412)).

**Leaks.** The only backend left is the collision library, and it shows through:

- `Collider` wraps a Parry `SharedShape`.
- `Collider::from(SharedShape)` is public, and the docs point users at `Collider::shape()` to get the Parry shape ([collider/parry/mod.rs L353–L382](https://github.com/avianphysics/avian/blob/b620b67121def1a88a9eb272763aa24e7d594ccc/src/collision/collider/parry/mod.rs#L353-L382)).

---

## Unreal Engine: `FPhysicsInterface` over Chaos (brief)

**Shape of the seam: build-time typedefs.** `PhysicsInterfaceDeclaresCore.h` maps generic names to one backend's concrete types ([PhysicsCore module API, Typedefs table](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/PhysicsCore?lang=en-US)):

| Generic name | Chaos type |
|---|---|
| `FPhysicsInterface` | `FPhysInterface_Chaos` |
| `FPhysicsActorHandle` | `Chaos::FSingleParticlePhysicsProxy*` |
| `FPhysicsShapeHandle` | `FPhysicsShapeReference_Chaos` |
| `FPhysicsGeometry` | `Chaos::FImplicitObject` |
| `FPhysicsMaterial` | `Chaos::FChaosPhysicsMaterial` |
| `FPhysScene` | `FPhysScene_Chaos` |
| `FHitRaycast` | `ChaosInterface::FRaycastHit` |

No vtable is involved. The engine compiles against whichever type the typedef names. The 4.27 API reference lists a sibling `FPhysicsInterface_PhysX` with the same bases ([4.27 API path](https://docs.unrealengine.com/4.27/en-US/API/Runtime/Engine/Physics/FPhysicsInterface_PhysX/index.html); this page returned HTTP 403 when fetched, so this rests on the search-index listing only). That suggests the typedef once selected between PhysX and Chaos at build time.

**Vocabulary and API style.**

- `FPhysInterface_Chaos` derives from `FGenericPhysicsInterface` and `FChaosEngineInterface` ([FPhysInterface_Chaos](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/FPhysInterface_Chaos)). It is a set of static functions over handles:
  - `ExecuteRead` and `ExecuteWrite`, which run a lambda against an actor handle under the scene lock;
  - geometry, shape and material functions;
  - `LineTrace_Geom`, `Sweep_Geom` and `Overlap_Geom`.
- The base `FChaosEngineInterface` (PhysicsCore, `Chaos/ChaosEngineInterface.h`) adds:
  - `CreateActor`, `ReleaseActor`, `AddActorToSolver` and `RemoveActorFromSolver`;
  - `AttachShape` and `DetachShape`;
  - `CreateConstraint`;
  - `CreateMaterial`;
  - many `*_AssumesLocked` body accessors, such as `GetGlobalPose_AssumesLocked` and `AddImpulse_AssumesLocked`.

  See [FChaosEngineInterface](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/PhysicsCore/FChaosEngineInterface). The `_AssumesLocked` suffix puts the locking contract in the function name.
- Layers are engine-level, not backend-level. Each object has an Object Type (channel), and each channel gets a Block, Overlap or Ignore response. Hit events and overlap events are separate opt-ins ([Collision overview](https://dev.epicgames.com/documentation/unreal-engine/collision-in-unreal-engine---overview)).

**Threads and events.**

- `FPhysScene_Chaos` keeps `PhysicsProxyToComponentMap` and `ComponentToPhysicsProxyMap`, with `GetOwningComponent`, to map backend proxies back to components. It queues collision notifies in `PendingCollisionNotifies`, with per-component `RegisterForCollisionEvents`, and has async-physics entry points (`RegisterAsyncPhysicsTickComponent`, `EnqueueAsyncPhysicsCommand`) ([FPhysScene_Chaos](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/FPhysScene_Chaos)).
- `FChaosMarshallingManager` moves data from the game thread to the physics thread, using timestamps ([FChaosMarshallingManager](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Chaos/FChaosMarshallingManager)).
- Proxies expose `_External` (game-thread) methods and `PushToPhysicsState` and `PullFromPhysicsState` ([FClusterUnionPhysicsProxy](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Chaos/FClusterUnionPhysicsProxy)).

**What it could not hide.**

- The "generic" handle types are Chaos types by typedef, so every user of them is compiled against Chaos headers.
- The interface classes are named after the backend (`FPhysInterface_Chaos`, `FPhysScene_Chaos`).
- Some functions in the interface take `Chaos::FPhysicsObject*` directly ([FChaosEngineInterface](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/PhysicsCore/FChaosEngineInterface)).

---

## Side by side

| | Godot | O3DE | bevy_rapier | Avian | Unreal |
|---|---|---|---|---|---|
| Seam | Runtime vtable (`PhysicsServer3D`) + named factory registry, picked by project setting | Runtime vtable (`AZ::Interface<SystemInterface>`), picked by enabled gem | None; build-time crate, backend re-exported | None for bodies; trait-based collider backend (`AnyCollider`), Cargo feature | Build-time typedefs |
| Id | `RID` (64-bit, validator) | `tuple<Crc32, index>` with CRC check on lookup | Backend handle stored as a component; entity↔handle maps | `Entity` | Pointer typedef (`FSingleParticlePhysicsProxy*`) |
| Who owns body state | Server | Backend scene | Rapier sets | ECS components | Chaos proxies, mirrored to game thread |
| Space | World (server API); scene converts | World; component converts | World; plugin converts via parent `GlobalTransform` | `Position`/`Rotation` world; plugin converts | World (`FTransform`) |
| Contacts leave step via | Thread-local buffers → merged post-step → flushed to callbacks at `flush_queries` | Callback queues → flushed in `FinishSimulation` → scene events | `RwLock<Vec>` in `EventHandler` → Bevy `Message`s | Thread-local bits → `Message`s + observer `Event`s | Pending notifies on `FPhysScene_Chaos` |
| Queries | `PhysicsDirectSpaceState3D` (physics-process only), param/result structs | `QueryScene(request*)`, polymorphic requests, flag-gated hit fields | System param returning `Entity` | System param returning `Entity` | `*_Geom` statics; engine-level traces |

## Recurring patterns

1. **An opaque handle owned by the physics layer, plus a reverse map to the engine's object.**
   - Godot pairs `RID` with `ObjectID`.
   - O3DE pairs `SimulatedBodyHandle` with `EntityId`.
   - bevy_rapier keeps `entity2body` and `entity2collider` alongside entity bits in `user_data`.
   - Unreal keeps `PhysicsProxyToComponentMap`.

   Where the backend has a user-data slot, the wrappers store their own pointer or id in it to get back from a backend callback: Godot with Jolt, O3DE with PhysX (`Utils::GetUserData`), and bevy_rapier with Rapier.
2. **Free one id, check it on lookup.** Godot has one `free_rid` for every object kind. O3DE's lookups check a CRC alongside the index. Destruction is deferred until after a step: O3DE has `m_deferredDeletions`, and bevy_rapier handles removals in its sync phase and keeps a `deleted_colliders` map so that events in flight can still name the entity.
3. **The physics API is world-space, and an adapter converts to the local hierarchy.** Every engine here converts on the engine side:
   - Godot uses `get_global_transform` and `set_global_transform`.
   - O3DE uses `GetWorldTM` and `SetWorldTM`.
   - bevy_rapier computes `parent_global.inverse() * pose`.
   - Avian uses `reparented_to`.

   Two techniques recur. The adapter suppresses or detects its own write-back so it does not echo: Godot ignores the transform notification, bevy_rapier keeps `last_body_transform_set`, and Avian has a 0.01 mm threshold. Optionally, it interpolates between fixed steps: O3DE's `TransformForwardTimeInterpolator`, bevy_rapier's `TransformInterpolation`.
4. **Each step runs in three phases: push changes in, step, write results out.**
   - bevy_rapier names them `SyncBackend`, `StepSimulation` and `Writeback`.
   - O3DE splits the step into `StartSimulation` and `FinishSimulation`, then sends a simulation-finish event that components use for write-back.
   - Godot's order is `sync`/`flush_queries`, then `physics_process`, then `step`.
5. **Contacts produced on worker threads are buffered, then delivered on one thread after the step.**
   - Godot has per-thread manifold buffers merged under a mutex.
   - O3DE queues events in callbacks and flushes them in `FinishSimulation`.
   - bevy_rapier collects into a `RwLock<Vec>`.
   - Avian uses thread-local bit vectors and then writes `Message`s.

   No engine here delivers events to user code from inside the backend's step.
6. **Event reporting is opt-in per object.** Godot has `body_set_contacts_reported_depth_threshold` and monitor callbacks, bevy_rapier has `ActiveEvents`, Avian has `CollisionEventsEnabled`, and Unreal has "Simulation Generates Hit Events" and "Generate Overlap Events".
7. **Layers are owned by the engine as bitmasks, and the backend maps them.**
   - Godot uses a 32-bit layer and mask, mapped to Jolt object layers.
   - O3DE has 64 named layers and groups.
   - bevy_rapier and Avian use 32-bit memberships and filters.
   - Unreal uses channels with Block, Overlap or Ignore responses.
8. **Queries are request and result structs, and results name the engine object.**
   - Godot returns `RayResult`, with `rid` and `collider_id`.
   - O3DE returns `SceneQueryHit`, with a handle and an `EntityId`, and flags saying which fields are valid.
   - The Bevy crates return the `Entity`.
9. **Materials are either per-body parameters or a property bag.** Godot makes friction and bounce body parameters. O3DE uses string-keyed properties, so that a backend can add its own. Unreal typedefs the material to the backend's type.
10. **Shapes are shared resources added to a body with a local offset.** Godot uses `body_add_shape` with a transform. O3DE attaches shapes to a body. Rapier inserts colliders with a parent. Avian links colliders to bodies through `ColliderOf`, with the offset taken from the hierarchy.

## Recurring leaks

1. **Backend tuning becomes a second settings surface.** Examples:
   - Godot's ~30 `physics/jolt_physics_3d/*` settings, which include fixed buffer capacities that show up as runtime warnings.
   - O3DE's serialized `PhysXSpecificConfiguration`.
   - godot-jolt's own settings namespace, renamed when it moved into the engine.
2. **Behaviour differences reach users even when the interface is the same.** Godot's docs have to list them: joint soft limits, which body counts as the world in single-body joints, what margins mean, contact impulses, kinematic contact reporting, ray-cast `face_index`, Baumgarte stabilization and world-boundary size. godot-jolt had to add Jolt-shaped joint nodes to expose features the generic joints could not carry.
3. **Generic code names a specific backend.** Godot's `Shape3D` hides properties based on which backend is active. Godot's manager hard-codes backend names. O3DE's components are categorised "PhysX" and live in the PhysX gem. Unreal's interface types carry `_Chaos` in their names.
4. **Native escape hatches.** O3DE's `GetNativePointer()` and `GetNativeType()` sit on the abstract `Scene`, `SimulatedBody` and `Shape` interfaces. bevy_rapier exposes `Collider.raw` and re-exports `rapier` and `parry`. Avian exposes `Collider::shape()`, which returns a Parry `SharedShape`. Unreal's handle typedefs are Chaos pointers.
5. **Threading and locking follow the backend.**
   - O3DE's start/finish split mirrors PhysX's `simulate()` and `fetchResults()`, with a deadlock workaround around PhysX scene locks.
   - Unreal's function names carry `_AssumesLocked`.
   - Godot's `space_get_direct_state` is only valid during physics processing.
   - Under `WrapMT`, Godot's `*_create` calls bypass the command queue, so each backend's RID owners must be thread-safe.
   - godot-jolt was not thread-safe, and the built-in module's thread support is marked experimental.
6. **Determinism depends on the wrapper as well as the backend.** godot-jolt states it cannot guarantee determinism although Jolt can. The extension's README does not give the cause.
7. **Scale and shape edits.**
   - Rapier bodies do not know their own scale, so bevy_rapier copies it from `Transform`.
   - godot-jolt uses scale where Godot Physics ignored it.
   - godot-jolt warns that editing shapes after a body enters the tree is costly, because Jolt rebuilds compound shapes.
8. **Parent–child transform ordering in the hierarchy.**
   - O3DE's `OnTransformChanged` is marked unsafe because a parent can activate after its children and teleport a body.
   - Avian flattens nested rigid bodies, and moving a parent does not carry them along.
   - bevy_rapier reads the parent's `GlobalTransform` so that its change detection survives rounding.

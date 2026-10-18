# Glossary

The terms this repository uses, and the words to avoid for them. When a name in code, an issue or a test describes one of these concepts, use the term defined here.

## Iris (.NET hosting)

Iris is the engine-independent C++ library in `packages/Iris` that hosts .NET. These terms are its vocabulary; FenrirEngine-specific terms such as *script* build on them.

**Host**
The native object (`Iris::Host`) that starts or joins the .NET runtime and connects to the bridge. Creating one does not start .NET; `Initialize` does. The runtime and bridge stay loaded for the rest of the process whatever happens to the host.
*Avoid:* runtime (the runtime is CoreCLR itself, which outlives every host), engine.

**Runtime**
CoreCLR, the .NET runtime, which Iris starts through hostfxr (found with nethost). There is one per process, and it is never unloaded.

**Bridge**
The managed assembly `Iris.Bridge`. It loads once into the default load context and stays resident for the life of the process. Native code reaches managed code only through it.
*Avoid:* managed host, interop layer.

**Bridge function table, handshake**
The bridge's single public entry point fills a versioned table of function pointers. That exchange is the *handshake*. A version or size mismatch fails `Initialize`. The table is private to Iris, and changing it bumps the bridge version: it is not part of the public API.

**Component**
A managed assembly loaded with `Host::LoadComponent` into its own collectible load context, together with its private dependencies. In code, `Iris::Component` is the *owner* of one loaded component. Where the engine's ECS sense of *component* could also be meant, say **Iris component**. The engine loads each *script assembly* as one Iris component.
*Avoid:* plugin, module.

**Load context**
The .NET `AssemblyLoadContext` a component lives in. It is collectible, so it can be unloaded once nothing roots it.

**Shared assembly, contract**
An assembly named in `HostConfiguration::sharedAssemblies`. It loads once into the default load context, and every component's reference to it binds to that copy, so its types have one identity across components. Typically this is an API that components compile against (a *contract*). Sharing is permanent for the process (ADR-0004).

**Owner**
A move-only C++ object that holds something alive in the bridge and releases it when destroyed. The owners are `Component`, `Object` and `Array`. They can't be copied. A moved-from owner is empty: its calls report `InvalidHandle`, and releasing it again succeeds.
*Avoid:* wrapper, reference.

**Handle**
A cheap, copyable C++ value that names something in a loaded component without keeping it alive. The handles are `Type`, `Method`, `InstanceMethod` and `Member`. A handle is validated on every call. It stops working (`InvalidHandle`) when its component is released and is never reused. Handles are opaque numbers, never CLR addresses or `GCHandle`s.
*Avoid:* pointer, reference, id (in API docs).

**Token**
The bridge's internal number for one loaded component, and the thing every handle is bound to. Never reused. Not part of the public API.

**Generation**
One load of a component, identified by its token. Loading the same assembly again, or a new build of it, is a new generation. Handles from one generation never resolve in another. The code says "component token"; tickets say "generation". During hot reload the engine keeps the data of script components and events whose layout is unchanged from one generation to the next.

**Signature**
The C++ function type that names a managed method, such as `std::int32_t(std::int32_t, std::int32_t)` for `int Add(int, int)`. Lookups match it exactly. The C++ types decide the managed types, and an unsupported type does not compile.

**Mapped struct**
A C++ struct with a `StructMapping` that mirrors a managed blittable struct field by field. The mapping is validated against the managed type the first time a component uses it.

**Object**
A managed object that native code owns through an `Iris::Object`. While the `Object` exists, the managed object is rooted and its component can't be released.

**Internal call**
A native function that managed component code calls, registered with `RegisterInternalCall`. Component code declares a public static unmanaged function pointer field, and the bridge binds the field to the registration by name when the component loads. This is called *binding*. A component with any unbound field fails to load. Its parameters and result are numbers, `bool`, `char16_t` and pointers; a parameter can also be a `std::string_view`, which the field declares as a UTF-8 byte pointer and a length.
*Avoid:* callback, P/Invoke, native export.

**Release**
Dropping the bridge's strong references to a component, object or array, which requests unloading (`Release()`, or destroying the owner). Release is explicit and immediate. `Component::Release` refuses with `InUse` while objects are alive, but destroying a `Component` always releases. That is a *forced release* (ADR-0005).
*Avoid:* unload (for the act of releasing), free, dispose.

**Collection**
The garbage collector actually freeing a released component's load context and assemblies. It happens later, only once nothing else roots the component. Iris observes it but never forces it.
*Avoid:* unload (ambiguous between release and collection).

**Unload status**
What `Component::GetUnloadState` observes:
- `NotRequested`: still loaded.
- `Pending`: released, not yet collected.
- `Collected`: gone.

**Root, retention**
A strong reference that keeps a released component from being collected, such as a live `Object`, a static field elsewhere, or a thread. Iris's own tables hold plain ids, never roots.

**Snapshot**
An owning copy of managed metadata returned to native code: a type list, base type, interfaces or attributes. A snapshot holds no reference to the component.

**Result, status, diagnostic**
Every Iris call returns a `CallResult<T>`. Its *status* (`CallStatus`) says what happened. Its value is present only on `Success`. Its *diagnostic* is owned UTF-8 text explaining a failure, which for `ManagedException` is the managed exception's `ToString()`. Iris reports failures as results, not C++ exceptions.

**Log sink**
The one process-wide native callback (`Host::SetLogSink`) that receives bridge events and exception reports with a `LogLevel`.

## FenrirEngine scripting

Scripting is ECS-first (ADR-0007): C# code adds components and systems to the same ECS as C++, not per-entity behaviour objects.

**Entity**
An ID in the active scene's ECS, including a version, so an ID that is destroyed and reused is detected as stale. In C# it is a small value with helper methods, not an object.
*Avoid:* game object, actor, node.

**Entity link**
An entity named inside a component or event (a parent, a contact's body, a joint's bodies), as scripts see it. A link may be empty, naming no entity. A script reads the entity a link names and makes a link from any entity of the active scene; it never sees which scene the link belongs to.
*Avoid:* entity reference, entity handle (a *handle* is Iris's term).

**Component**
Plain data attached to an entity, with no behaviour. A *native component* is a C++ struct, and a *C# component* is an `IComponent` struct. Both live in the ECS and are queried the same way. In engine code and docs, *component* always means this. Say *Iris component* for the other sense.

**Component watch**
A record, kept for whoever asked, of every time one component type was added to or removed from entities in one scene, in order, until it is drained. It reports adds and removes only, never edits.
*Avoid:* observer, listener, callback (a watch is read later, not called at the time).

**Component type**
A value naming one kind of component, native or C#, that the ECS's runtime operations and queries take (`ComponentType` in FenrirECS). Code that knows a component only at run time, such as scripting or an editor, holds one instead of a C++ type. It carries no name.
*Avoid:* selector, registration (a registration is scripting's record of what C# sees).

**System**
Logic that runs once per frame phase over the entities a query selects. Native systems are C++ functions, and C# systems are `ISystem` classes.
*Avoid:* script, behaviour, manager.

**Event**
Plain data one system sends and others read. Each reader sees each event once, including events sent after it last ran, as long as it reads before its queue swaps twice; a queue swaps at the end of each frame that ran a tick, so reading once a frame is enough (ADR-0012). A new reader starts at the oldest event its queue still holds. C++ and C# events of the same name share one queue. A C# send waits until the sending system returns.

**Event reader**
What a system reads events through: it remembers which events of one kind it has already seen, so each reaches it once.
*Avoid:* listener, subscriber (nothing is called when an event is sent).

**Query**
A request for every entity that has all of a given set of components, native and C# alike. A C# query yields one row per entity: the `Entity` and a `Ref<T>` per component. The rows are valid until the system returns.

**Structural change**
Creating or destroying an entity, or adding or removing a component. A structural change made from a C# system takes effect only when that system returns.

**Script assembly**
A user's C# assembly holding C# components and systems. The engine loads it as one Iris component, and it reloads as a new generation.
*Avoid:* script (as the name of a type or file), plugin.

**Script report**
What a script assembly says it defines when it loads: the C# components and events, each with its size and alignment, and a fingerprint of its layout that the engine compares across builds. The engine checks a report before it keeps the assembly, and refuses the whole load if any part of it is wrong.
*Avoid:* manifest, schema.

**Scripting contract**
The shared assembly `Fenrir.Scripting` that every script assembly compiles against. It holds the stable engine API and never reloads.
*Avoid:* engine API assembly, SDK (that is the contract plus its source generator).

**Scripting extension**
What one engine package, or the app, gives scripting (`ScriptingExtension`, ADR-0009): its native functions, its native components and C++ events, and the shared assemblies that are its C# contract. FenrirScripting knows no package beyond its extension.
*Avoid:* plugin, module (*module* is the review vocabulary, and the Iris glossary avoids it for Iris components).

**Native function**
A C++ function that C# calls, declared once on each side and named by the full name of the C# method that calls it (`Namespace.Type.Method`). In C++ its first parameter is the call context; the C# field Iris binds is named by the binding-name rule (`Namespace.Type.__native_Method`). Built on an Iris internal call.
*Avoid:* binding, internal call (the Iris-level term), engine API.

**Call context**
What a native function receives first (`CallContext`): the App the running Scripting serves, and through it the active scene's entities. A native function may read and write component data through it, but makes no structural changes.

**Script**
Informal: user C# code in general. Not the name of any type or concept.

## FenrirEngine core

**World transform**
Where an entity is in the scene, as opposed to its `Transform`, which is relative to its parent. Always computed from the entity's ancestors, never stored. Its scale is approximate when a parent's non-uniform scale meets a child's rotation (the exact form is the world matrix).
*Avoid:* global transform, absolute transform.

**Scene resource**
State a package keeps in one scene, one value per type, ended with the scene (a scene's physics world is one).
*Avoid:* singleton, global, scene component (a *component* belongs to an entity).

## FenrirEngine physics

**FenrirPhysics**
The engine's physics package: its public API, its components and its systems. An app may leave it out. Nothing in its public interface names, or needs at compile time, the library that simulates underneath.
*Avoid:* physics library, physics engine (both blur it with the physics backend).

**Physics backend**
The third-party library that does the simulation behind FenrirPhysics (Jolt today). Only FenrirPhysics's private code names it, so it can be replaced without changing FenrirPhysics's public interface.
*Avoid:* physics engine, physics library.

**Physics world**
One running simulation, holding the bodies of one scene. Each scene that uses physics has its own; it is paused while its scene is inactive and ends with its scene.
*Avoid:* physics scene, physics system (*system* is ECS logic).

**Rigid body**
An entity the physics world simulates as one solid object that does not deform. Its *motion type* is one of:
- *static*: never moves;
- *kinematic*: moved by game code, pushes dynamic bodies, is not pushed;
- *dynamic*: moved by the simulation.

*Avoid:* physics body, actor.

**Soft body**
An entity the physics world simulates as a deformable object (cloth, jelly).
*Avoid:* cloth (one kind of soft body).

**Character body**
An entity moved by game code through the physics world with character-style rules (walking, slopes, steps), rather than simulated as a rigid body. Game code sets the velocity it should move at, and physics moves it by sliding along whatever is in its way. It has exactly one collider, its own, and other bodies, queries and triggers see that collider as they see a body's. An entity is either a character body or a rigid body, never both. Gravity and jumping are part of the velocity game code sets; physics adds the velocity of the ground it stands on, and of ground it has just left, without changing the velocity game code set. It pushes the loose bodies it walks into, with a limited force, and presses down on what it stands on; loose bodies never push it back, but moving fixed-path bodies (doors, platforms) shove it along. A change to its shape (crouching, standing up) takes effect only once the new shape fits where it is; until then it keeps the old shape and reports that it is blocked.
*Avoid:* character controller (as the name of the thing), player body, kinematic character (it is not a kinematic rigid body).

**Carried velocity**
The velocity of the ground a character body last stood on, which it keeps moving with after walking off or jumping, until it lands again or is teleported.
*Avoid:* inherited velocity, momentum, platform velocity (the ground's own velocity while standing on it).

**Ground state**
What a character body was standing on at the end of its last move: walkable ground, steep ground, nothing underneath while touching something (a wall, a ledge), or nothing at all (in the air). With ground under it, it also names the ground's body and collider, where and at what angle it touches, and how the ground is moving. Physics reports it; landing and leaving the ground are changes in it that game code notices, not separate happenings.
*Avoid:* grounded (as a yes/no; there are four states), floor (ground covers any surface underneath).

**Walkable ground**
Ground under a character body no steeper than its slope limit, so it can stand and walk on it, and ride it if it moves.
*Avoid:* floor, flat ground.

**Steep ground**
Ground under a character body steeper than its slope limit: it is supported, but cannot walk up it and does not ride it.
*Avoid:* wall (a wall is touched from the side, with nothing underneath).

**Motion test**
A query that asks how a shape would fare if it were moved, without moving anything: how far it can go along a motion before it is blocked and still stay clear, or which way and how far it must move to stop overlapping what it is in. Game code uses motion tests to build its own movement and to check for room, such as a ledge or headroom.
*Avoid:* move (nothing moves), character query (any shape can be tested).

**Collider**
Geometry attached to a body: one collision shape, placed where its entity is, with a physics material, a collision layer, and optionally marked as a trigger. An entity has at most one; a collider with no rigid body or character body on its entity or above it is static.
*Avoid:* hitbox.

**Compound body**
A rigid body whose colliders sit on several entities: its own and those beneath it that have no rigid body of their own.
*Avoid:* compound shape (the backend's term for the merged geometry).

**Collision shape**
The geometry of a collider on its own, as a value: exactly one kind (box, sphere, capsule, convex hull or triangle mesh) and that kind's dimensions. Convex hull and triangle mesh kinds refer to a *collision mesh*, vertex data that FenrirPhysics prepares once and several colliders may share.
*Avoid:* mesh (for the shape in general), geometry.

**Physics material**
How a collider's surface responds to contact: its friction and restitution.

**Contact**
Two solid colliders of different bodies touching. It begins when they first touch and ends when they separate or one is removed; a body falling asleep does not end it.
*Avoid:* collision (as the name of the state), hit.

**Trigger**
A collider that reports what overlaps it but never pushes anything.
*Avoid:* sensor (the physics backend's word), area (unless a later decision names a trigger volume so).

**Joint**
A link between two rigid bodies, or between a rigid body and the world, that restricts how they move relative to each other. It has exactly one *kind* (hinge, slider, ...) and lives on its own entity, the *joint entity*, which names the bodies it joins; a body may take part in any number of joints. While a joint is enabled, the two bodies it joins do not collide with each other unless it says they may; bodies further along a chain still do.
*Avoid:* constraint (the physics backend's word), connection.

**Joint entity**
The entity a joint lives on. It may be one of the bodies it joins, or any other entity; where it sits does not move the joint.
*Avoid:* joint body, joint node.

**Joint kind**
Which way a joint lets its bodies move relative to each other. One of:
- *fixed*: not at all;
- *ball*: turn freely about a shared point;
- *distance*: keep two points between a shortest and a longest distance (a rod, a rope or a spring);
- *hinge*: turn about one axis (a door);
- *slider*: move along one axis (a drawer);
- *swing-twist*: swing within a cone and twist within a range about one axis (a shoulder).
*Avoid:* point joint (the physics backend's name for ball), joint type (for the idea; the code's enum may say type).

**Rest pose**
The pose of a joint's two bodies in which its anchor on each body coincides: a hinge's angle and a slider's position are zero there, and its limits and targets are measured from it. The rest pose always lies within a joint's limits.
*Avoid:* bind pose, zero pose.

**Breaking (a joint)**
A joint breaks when, in one step, the force or torque it transmits exceeds its *break force* or *break torque*: physics then disables it and announces that it broke. A broken joint is a disabled one, nothing more; enabling it again repairs it.
*Avoid:* snapping, destroying (nothing is removed).

**Joint state**
What a joint did in the last step: the force and torque it carried, and where its bodies sit relative to the rest pose. Game code asks for it when it wants it; it is never stored on the joint. An inactive joint, or one not yet stepped, reads as all zero.
*Avoid:* joint reading, joint feedback.

**Inactive joint**
A joint that does nothing because one of the things it names is not currently a rigid body it can join (gone, never a body, the same body twice, or in another scene). It is kept as it is and becomes active again by itself once both names are valid. Unlike a disabled joint, nobody switched it off; a joint whose bodies all cannot move is not inactive, only idle.
*Avoid:* dormant joint, dangling joint, invalid joint.

**Collision layer**
One of 32 groups a body belongs to (it may belong to several). With its *collision mask*, the layers it collides with, it decides which bodies can touch: two touch only when each one's mask includes a layer of the other.
*Avoid:* channel, group (a *collision group*, bodies that ignore each other, is a separate idea).

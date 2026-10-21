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
What one engine package, or the app, gives scripting (`ScriptingExtension`, ADR-0009): its native functions, its native components, native resources and C++ events, and the shared assemblies that are its C# contract. FenrirScripting knows no package beyond its extension.
*Avoid:* plugin, module (*module* is the review vocabulary, and the Iris glossary avoids it for Iris components).

**Tagged union**
A value that holds exactly one of several kinds, with a tag saying which one (a collision shape, a joint kind, a vehicle kind). In C# it is a struct with one view per kind, and the view throws if the value holds another kind. The author writes the tag and the storage; FenrirScripting's `[TaggedUnion]` generator writes the rest.
*Avoid:* variant, sum type, discriminated union (a proposed C# language feature, not this).

**Native function**
A C++ function that C# calls, declared once on each side and named by the full name of the C# method that calls it (`Namespace.Type.Method`). In C++ its first parameter is the call context; the C# field Iris binds is named by the binding-name rule (`Namespace.Type.__native_Method`). Built on an Iris internal call.
*Avoid:* binding, internal call (the Iris-level term), engine API.

**Native resource**
A plain-data C++ resource, scene or app, that C# sees as a blittable struct marked `[NativeResource]` and reaches through `Resources.Get<T>()` (ADR-0029). A resource with methods (`Keyboard`, `DebugDraw`) is not one: C# gets a facade of native functions instead.
*Avoid:* singleton, global, resource mirror (informal).

**Call context**
What a native function receives first (`CallContext`): the App the running Scripting serves, and through it the active scene's entities and the change buffer. A native function may read and write component and resource data through it, and makes structural changes only by recording them in the change buffer (ADR-0029).

**Script**
Informal: user C# code in general. Not the name of any type or concept.

## FenrirEngine core

**Colour**
`Fenrir::Colour`: a colour as linear red, green, blue and alpha, however it was written (an sRGB hex value is converted when it is made).
*Avoid:* color (spelling), packed colour, RGBA (alone).

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
An entity moved by game code through the physics world with character-style rules (walking, slopes, steps), rather than simulated as a rigid body. Game code, in C++ or C#, sets the velocity it should move at, and physics moves it by sliding along whatever is in its way. It has exactly one collider, its own, and other bodies, queries and triggers see that collider as they see a body's. An entity is either a character body or a rigid body, never both. Gravity and jumping are part of the velocity game code sets; physics adds the velocity of the ground it stands on, and of ground it has just left, without changing the velocity game code set. It pushes the loose bodies it walks into, with a limited force, and presses down on what it stands on; loose bodies never push it back, but moving fixed-path bodies (doors, platforms) shove it along. Character bodies block each other without pushing, and one standing on another rides it. A change to its shape (crouching, standing up) takes effect only once the new shape fits where it is; until then it keeps the old shape and reports that it is blocked.
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

**Vehicle**
A rigid body that rolls over the ground on wheels or tracks, driven by driver input game code sets (or, for a *trailer*, towed). Its rigid body, the *chassis*, is the only part of it that collides; its wheels touch the ground without being bodies. It has exactly one *vehicle kind*. Switched off, it is a plain rigid body resting on its chassis.
*Avoid:* vehicle constraint, vehicle controller (the physics backend's words), car (one kind of vehicle).

**Chassis**
A vehicle's rigid body: the entity the vehicle lives on, and the only part of the vehicle that collides with anything.
*Avoid:* hull (the physics backend's word for its shape), vehicle body.

**Wheel**
Where a vehicle meets the ground: it finds the ground beneath it, carries the chassis on its suspension, and drives, brakes and steers. It is never a body, and nothing collides with it; it never stands on a character body. Each wheel lives on its own entity, the *wheel entity*, which names its vehicle and is posed by physics so the wheel can be drawn.
*Avoid:* tyre (only the part that grips), wheel collider.

**Track**
One of a tracked vehicle's two belts, left or right, driving every wheel that sits on it.
*Avoid:* tread.

**Vehicle kind**
How a vehicle is driven. One of:
- *wheeled*: steered by turning wheels (a car, a truck);
- *motorcycle*: two wheels, leaning into turns;
- *tracked*: steered by driving its two tracks at different speeds;
- *trailer*: no engine, only rolling, braking and steering (a trailer, a hand cart).
*Avoid:* tank (for the kind), vehicle type (for the idea; the code's enum may say type).

**Trailer**
A vehicle with no engine, which rolls, brakes and may steer but never drives itself. It is usually towed by a joint to another vehicle's chassis; its brakes are whatever game code sets on it.
*Avoid:* unpowered vehicle, towed vehicle.

**Driver input**
What game code sets on a vehicle to drive it: how hard to drive forward or back, which way to steer, how hard to brake and to pull the handbrake (for a tracked vehicle, each track's speed instead of steering), and, with a manual gearbox, which gear and how far the clutch is pressed. It stays as set until game code changes it; physics never writes it.
*Avoid:* controls, commands.

**Drivetrain**
Everything that turns driver input into turning wheels: the engine, the gearbox (automatic, or manual with gears game code picks), and the differentials or tracks that share the engine's torque out. Brakes and suspension are not part of it.
*Avoid:* powertrain, engine (one part of it).

**Differential**
What shares a vehicle's engine torque between two wheels (or gives one wheel its share), letting them turn at different speeds through a corner. A wheel in no differential is not driven: it only rolls and brakes. A vehicle with no differential at all drives every wheel.
*Avoid:* diff, axle (an axle is where two wheels sit, not what drives them).

**Anti-roll bar**
A link between two wheels' suspensions that resists the chassis leaning in a corner by pushing the less compressed side down.
*Avoid:* sway bar, stabiliser.

**Suspension travel**
How far a wheel can rise from full extension, where it hangs when the chassis is lifted, toward the chassis. A wheel's position is its centre at full extension.
*Avoid:* suspension length (the backend measures from the top of the suspension instead).

**Compression**
How far a wheel has risen from full extension at this moment, never more than its suspension travel. Physics reports it for each wheel.
*Avoid:* suspension length, ride height (the chassis's height, not one wheel's).

**Slip**
How much a tyre is sliding over the ground rather than rolling. Along the wheel, it is how much faster or slower the tyre spins than the ground passes under it. Across the wheel, it is the angle between where the wheel points and where it is going. A tyre grips best at a little slip and slides beyond that.
*Avoid:* skid (one range of slip, past the peak), drift.

**Collision layer**
One of 32 groups a body belongs to (it may belong to several). With its *collision mask*, the layers it collides with, it decides which bodies can touch: two touch only when each one's mask includes a layer of the other.
*Avoid:* channel, group (a *collision group*, bodies that ignore each other, is a separate idea).

## FenrirEngine rendering

**Graphics API**
An interface a GPU driver offers: OpenGL, Vulkan, D3D12, Metal.
*Avoid:* backend (a graphics backend is FenrirRenderer's code for one), renderer.

**Graphics backend**
FenrirRenderer's private adapter for one graphics API, or the null backend, which draws nothing. One is chosen when the renderer is installed, and nothing outside FenrirRenderer names it.
*Avoid:* renderer, RHI, driver.

**Graphics device**
The thin, private interface every graphics backend implements: GPU resources, passes and the commands recorded into them. The rest of FenrirRenderer draws only through it.
*Avoid:* RHI, GPU layer, render API.

**Shader container**
The one versioned file `FenrirShaderCompiler` writes per shader program: each stage's compiled output per graphics backend (SPIR-V for OpenGL today), every variant, and the reflection debug builds check layouts against. FenrirRenderer's own shaders are embedded as containers; users' are loaded as assets.
*Avoid:* shader binary, shader cache, effect.

**Shader variant**
One combination of a shader program's `#define` keys (lit or unlit, later skinned), compiled offline into its shader container.
*Avoid:* permutation, specialisation (specialisation constants are a different mechanism, not used yet).

**FenrirShaderCompiler**
FenrirRenderer's build-time executable that compiles GLSL to SPIR-V and writes shader containers, run by the CMake function `fenrir_add_shaders`. It is never linked into the engine.
*Avoid:* shaderc (a Google library), glslc.

**Renderer core**
The middle of FenrirRenderer's three layers: meshes, materials, the draw list, the render graph and its render features, over the graphics device. It knows nothing of the app, scenes or entities; the ECS facade above it reads the scene for it.
*Avoid:* render engine, render backend.

**Draw list**
The plain-data list of what a frame draws, rebuilt each frame from the active scene: one item per draw, with its mesh, material, world matrix and sort key.
*Avoid:* render queue, render world (a copy of the scene kept in sync, which FenrirRenderer does not have).

**Render graph**
The passes of one frame and the GPU resources they use, built and compiled every frame: ordered by render stage and dependencies, unused passes culled, transient targets pooled.
*Avoid:* frame graph, render pipeline.

**Render pass**
One step of the render graph: the resources it uses and how, a render stage, and a function that records its commands.
*Avoid:* pass (alone, when a graphics device pass could be meant), render node.

**Render stage**
Where in the frame a render pass runs, from a fixed list: `Shadow`, `Opaque`, `Background`, `Transparent`, `Debug`, `PostProcess`, `Output`, `Overlay`.
*Avoid:* phase (a *phase* is FenrirApp's, such as `Render`), layer, queue.

**Render feature**
A part of the renderer core that adds render passes to each frame's render graph, such as the background or debug drawing.
*Avoid:* plugin, render module.

**View**
One camera's drawing of the scene in a frame: its chain of render passes, from its scene targets to its output (the window or a texture).
*Avoid:* viewport (the rectangle a view draws into), camera (the component a view comes from).

**Graphics surface**
What a window offers a graphics backend to draw into and present: the OpenGL context today, later a Vulkan surface, an `HWND` or a Metal layer. FenrirWindow creates it on the renderer's request; the backend owns what is built on it, such as a swapchain (ADR-0022).
*Avoid:* swapchain (the backend's, built on a surface), context (alone), canvas.

**Main window**
The window FenrirWindow creates first and the foundation renderer draws to; closing it stops the app by default.
*Avoid:* primary window, root window.

**MeshRenderer**
The component that makes an entity drawn: one mesh, one material and whether it is visible, one draw. A mesh with several materials is several entities (ADR-0023).
*Avoid:* renderable, model (FenrirAssets' asset), mesh instance.

**Fallback material**
The built-in flat magenta unlit material a `MeshRenderer` draws with when its material is empty or stale, so the mistake shows on screen.
*Avoid:* error material, default material (a default suggests it is meant to be used).

**Material**
A shared `MaterialData` asset saying how a mesh is drawn: a shader, a blend mode, whether it is double-sided, and its material parameters. Many entities name one material by its asset handle (ADR-0024).
*Avoid:* material instance (there are no per-entity overrides), shader (a material names one), effect.

**Material parameter**
One named value in a material, set by name from C++ or C#: a scalar, vector, colour or texture with its sampler. The shader's material set gives each name its place.
*Avoid:* uniform, property.

**Blend mode**
How a material's pixels combine with what is behind them: `Opaque`, `Masked` (alpha cutoff), `AlphaBlend` or `Additive`. It picks the render stage and whether depth is written.
*Avoid:* alpha mode (glTF's term, mapped onto it), render queue.

**Material set**
Binding set 2 of every material shader: its `MaterialParams` block and textures. Unlike the frame, view and draw sets, its layout is read from the shader container's build-time reflection.
*Avoid:* descriptor set (Vulkan's), material block (only the uniform block in it).

**Camera**
The component that makes a view: its entity's world transform is where it looks from, plus a projection, an order, a viewport, a render target and a background. Every enabled camera in the active scene draws (ADR-0026).
*Avoid:* active camera (there is none; every enabled camera draws), view (what a camera makes each frame), eye.

**Viewport**
The rectangle of its render target a view draws into, given as fractions of the target from its top-left corner.
*Avoid:* view (the drawing itself), screen rect, scissor.

**Render target**
Where a view's output goes: a window, or an image asset made with a size but no pixels, which materials and the overlay sample like any image (ADR-0026).
*Avoid:* render texture (no separate type), framebuffer, scene targets (a view's own intermediate colour and depth).

**Sky**
The scene resource saying what is drawn behind everything: a solid colour or a skybox. A camera uses it, or overrides it with a colour.
*Avoid:* environment, clear colour (one of its forms), skydome.

## FenrirEngine debug drawing

**FenrirDebugDraw**
The engine's debug-drawing package: the shapes and text game code and other packages ask to see, kept per scene as plain data with no GPU code. FenrirRenderer draws them; FenrirPhysics is one of the packages that draws into it (ADR-0025).
*Avoid:* gizmos (Bevy's name; an editor's gizmos are handles you drag), debug renderer, line batcher.

**Debug draw item**
One shape or piece of text in a scene's debug drawing, kept for its duration: once, some seconds, or until cleared.
*Avoid:* debug line (one kind of item), primitive, gizmo.

**Debug group**
A named set of debug draw items that is shown, hidden or cleared together, such as physics's.
*Avoid:* category (physics's categories choose what it generates), layer, channel.

**On-screen message**
Debug text stacked in a corner of the window, newest first, which a later message with the same key replaces.
*Avoid:* log (FenrirLogger's), toast, notification.

## FenrirEngine 2D drawing

**FenrirDraw2D**
The engine's package of 2D draw data: plain data with no GPU code, knowing no renderer or UI. UI libraries and a future painter produce it; FenrirRenderer draws it (ADR-0027).
*Avoid:* UI renderer, canvas (a future painter's surface), ImGui backend.

**2D draw list**
`DrawList2D`: one batch of 2D drawing as plain data: vertex and index bytes, 2D pipeline descriptions, and commands with textures, a scissor and an index range, in local logical coordinates. The same list can be shown on screen, in a texture or in the world; the target decides where.
*Avoid:* draw list (alone: the frame's 3D one), draw data (ImGui's), display list.

**2D pipeline description**
A plain value saying how a 2D draw list's triangles are drawn: shader, vertex layout, index type, blend mode, input colour space and blend space. The renderer makes and caches a device pipeline from it.
*Avoid:* material (a 3D asset), pipeline handle (the device's).

**2D target**
Where FenrirRenderer's 2D render feature draws a 2D draw list, supplying the transform from local to clip space, depth and how a scissor is realised. The window overlay is the only one built; an image target and a world-space target are planned.
*Avoid:* render target (a camera's image), canvas, layer.

**Overlay layer**
The integer a 2D draw list is submitted to a window's overlay with: layers draw lowest first, and debug screen text draws above them all.
*Avoid:* z-order, render stage (`Overlay` is the stage they all draw in), sort key.

## FenrirEngine assets

**FenrirAssets**
The engine's asset package: loads files into plain CPU-side data and owns it behind asset handles. It has no GPU code; FenrirRenderer and animation each turn its data into their own form (ADR-0021).
*Avoid:* resource manager, asset server, asset database (cooking and an editor database are out of scope).

**Asset**
One piece of plain data FenrirAssets holds: a mesh, image, material, skeleton, clip, shader container or model, loaded from a file or added from code.
*Avoid:* resource (Iris and physics use it otherwise), file.

**Asset handle**
`AssetHandle<T>`: a typed index plus generation naming an asset. Plain data that keeps nothing alive; once its asset is freed, the generation no longer matches and users skip it.
*Avoid:* reference, pointer, asset id.

**Asset owner**
The app or a scene, named when an asset is loaded or added. An asset is freed when its last owner ends, or when it is unloaded explicitly.
*Avoid:* reference count, scope.

**Asset root**
The directory relative asset paths resolve against, set in `AssetsConfig`; by default `assets/` beside the executable.
*Avoid:* content directory, mount, virtual file system.

**Model**
The asset a model file loads as: its node tree, pointing at its meshes, materials and skeleton, and its clips.
*Avoid:* scene (FenrirScene's), prefab.

**Sub-asset**
An asset inside a model file, loadable on its own by label: `hero.fbx#Run`.
*Avoid:* sub-resource, embedded asset.

## FenrirEngine animation

**FenrirSkeleton**
The engine's skeleton package: each skeleton's runtime pose, bone lookup, pose modifiers, finalisation and bone attachments. It works without animation, and FenrirRenderer reads it without knowing FenrirAnimation (ADR-0028).
*Avoid:* rig, armature (Blender's).

**FenrirAnimation**
The engine's animation package: plays and blends clips into a skeleton's base pose. It has no GPU code and needs no renderer (ADR-0028).
*Avoid:* animator (as the package's name), animation system.

**Bone**
One node of a skeleton, named by its stable index in the skeleton's parents-first order and found by name. Bones are entries in arrays, not entities.
*Avoid:* joint (a *joint* is a physics constraint between two bodies), node (a model's), socket.

**Bone weight**
How much one bone moves one vertex of a skinned mesh; each vertex has at most four.
*Avoid:* joint weight, influence (alone), skin weight.

**Clip**
An asset of keyed bone transforms over time in seconds, naming its bones by name so it plays on any skeleton whose bone names match.
*Avoid:* animation (alone), take, sequence, action (Blender's).

**Skeleton entity**
The entity that carries a skeleton instance's `SkeletonPose`. Skinned meshes and bone attachments name it.
*Avoid:* rig entity, armature, root bone (a bone, not an entity).

**Skeleton pose**
`SkeletonPose`: one skeleton instance's mutable pose, as arrays indexed by bone: the base pose, the final pose, model-space matrices and the skinning palette. Model space is relative to the skeleton entity, never including its `Transform`; a bone's world matrix is the skeleton entity's world matrix times its model matrix. The skeleton asset it instances, `SkeletonData`, never changes.
*Avoid:* skeleton instance (alone), rig state.

**Base pose**
A skeleton's local bone transforms as animation produced them, before any pose modifier. Powered ragdolls drive towards it.
*Avoid:* animated pose (ambiguous once modifiers exist), rest pose (the skeleton's bind-time pose), bind pose.

**Final pose**
A skeleton's local bone transforms after every pose modifier; finalisation derives model-space matrices and the skinning palette from it.
*Avoid:* output pose, blended pose.

**Pose modifier**
Something that changes a skeleton's pose after animation, in order: IK, procedural posing, ragdoll blending, gameplay adjustments. It aims in local, model or world space, and finalisation resolves it into local transforms.
*Avoid:* override (the first modifier's kind, not the general term), post-process, constraint (physics's sense).

**Skinning palette**
One matrix per bone (model-space × inverse bind) that skinned vertices are moved by; one per skeleton instance, shared by every mesh skinned to it.
*Avoid:* bone-matrix buffer (the renderer's upload of it), joint matrices.

**Bone attachment**
`BoneAttachment`: a component that places its entity at a bone of a skeleton entity, plus an offset. Finalisation owns the entity's resulting `Transform`.
*Avoid:* socket, mount point, parent bone.

## FenrirEngine input

**Key code**
Which key was pressed as the keyboard layout names it: the key that types Z on an AZERTY keyboard is `Key::W` on QWERTY and `Key::Z` on AZERTY. Use it for shortcuts named by letter.
*Avoid:* keycode, virtual key, key (alone, when the difference matters).

**Scancode**
Where a key sits on the keyboard, whatever the layout: the same physical key on every layout. Use it for movement keys placed by position, such as WASD.
*Avoid:* physical key, raw key.

**Input feed**
FenrirInput's public entry point, through which raw input comes in during `PreUpdate`: FenrirWindow passes on what its windowing library delivers, and tests or headless apps feed input by hand (ADR-0017).
*Avoid:* input backend, input driver.

**Input edge**
A press or release since the reader last looked: "just pressed" or "just released". Read from `PreTick`, `Tick` or `PostTick`, the edges are those since the last tick; from any other phase, those since the last frame. So each press reaches each phase once.
*Avoid:* trigger, down/up (they mean held or not held).

**Input router**
A callback registered on FenrirInput with a priority, such as a UI, that sees each input event before game code and may consume it. Consumed input never reaches the `Keyboard`, `Mouse` or `Gamepads` resources or game event readers (ADR-0027).
*Avoid:* input handler, input layer, listener.

**Input claim**
What a router says it is doing with input in a window: the pointer is over it, it has captured the pointer, it has keyboard focus, or it wants text; plus an app-wide gamepad claim. Claims describe the interaction and drive text input; routers decide what is consumed.
*Avoid:* capture flag, want-capture (ImGui's), focus (alone).

**Control owner**
Whoever took a key's, button's or pointer's press: the router that consumed it, or the game. Its repeats and its release go to the same owner, and a cancelled control's release goes to no one.
*Avoid:* device owner (ownership is per control), grab.

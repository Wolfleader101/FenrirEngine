# How other engines deliver events to their systems

Research for [17-how-engines-deliver-events](../issues/17-how-engines-deliver-events.md); feeds [18-read-each-event-once](../issues/18-read-each-event-once.md).

Sources are pinned (Godot docs were read at 4.5). FenrirEngine paths are at commit `fb9e929`. Engine source is cited at these tags or commits:

- Bevy `v0.20.0` (`b3bdd75b`)
- Godot `4.7.2-stable`
- EnTT `v3.16.0`
- Flecs `v4.1.6`
- Unity `EntityComponentSystemSamples` at `6786a741`

Docs are cited by URL and section. Anything that is my reading of code and not something a source states is marked **(inference)**.

---

## 1. FenrirApp today

### The queue

- **Send** appends to `currentBuffer` (`packages/FenrirApp/include/FenrirApp/App.hpp` L87-90). `SendRaw` does the same through a `memcpy` (L133-147).
- **Rotation**: `EventQueue::Update` clears `previousBuffer` and then swaps the two buffers. Afterwards `previousBuffer` holds this frame's sends and `currentBuffer` is empty (App.hpp L96-103). `ByteEventQueue::Update` does the same over bytes (`packages/FenrirApp/src/App.cpp` L61-66).
- **Read**: `ReadEvents` copies `previousBuffer` followed by `currentBuffer` into a `mutable combinedBuffer` and returns a reference to it (App.hpp L110-117, L158). The order is oldest first. There is no per-reader state.
- **ReadRaw** returns the same window (App.hpp L149-153). The byte queue caches the window behind a `mutable m_dirty` flag (App.cpp L86-97).
- **Payloads**: any C++ type can be sent. A queue is byte-copyable only if `TEvent` is trivially copyable and default-constructible; otherwise `EventSize()` is 0 and C# cannot reach it (App.hpp L119-126, L58-59). Queues are created lazily on first `SendEvent`/`ReadEvents` by inserting into an `unordered_map` (App.hpp L348-363).
- **Glossary**: "kept in a queue with two buffers: a read sees the current frame's events and the previous frame's" (`GLOSSARY.md` L105-106).

### The frame

`App::Run` (App.cpp L153-179) runs these steps once per loop iteration:

1. `m_time.Update()` adds the elapsed wall time to `accumulator` (`packages/FenrirTime/src/Time.cpp` L10-17).
2. `PreUpdate` runs.
3. `Tick` runs in `while (accumulator >= tickRate)` (App.cpp L163-168). The default `tickRate` is 1/60 s (`packages/FenrirTime/include/FenrirTime/Time.hpp` L13). So `Tick` runs **k = 0, 1 or more times** per frame.
4. `Update` runs, then `PostUpdate`.
5. `UpdateEvents()` rotates every queue (App.cpp L38-48, L176).

Within a phase, `Scheduler::RunSystems` first submits every `AddSystems` system to a thread pool and waits for all of them. Only then does it run the `AddSequentialSystems` systems in order (`packages/FenrirScheduler/src/Scheduler.cpp` L18-47).

### When a reader sees an event

Take an event E sent during frame N.

| Reader | Sees E in frame N | Sees E in frame N+1 | Total |
|---|---|---|---|
| Runs every frame (`Update`, `PostUpdate`, `PreUpdate`), after the sender in frame N | yes (in `current`) | yes (in `previous`) | **2** |
| Runs every frame, before the sender in frame N (earlier phase, or earlier in a sequential list) | no | yes | **1** |
| Runs every frame, in the same parallel batch as the sender | data race (below) | yes | 1 or 2, undefined |
| `Tick` reader | once for each `Tick` run after the send in frame N | once for each of the k(N+1) runs | **(runs after send in N) + k(N+1)**: anywhere from 0 to many |

Consequences for `Tick`:

- **Lost events.** With `tickRate` = 1/60 and 240 fps, about three frames in four have k = 0. An event sent in `Update` of frame N reaches a `Tick` reader only if a tick runs in frame N+1. If none does, the `Tick` reader never sees it.
- **Repeated events.** At 20 fps, k is about 3, so the reader sees the event 3 times in frame N+1.
- **Events sent in `Tick`.** Contact events are planned to be sent once per step in `Tick` (`.scratch/fenrir-physics/issues/08-contact-and-trigger-events.md`, "Order and timing"). Step 1's events then stay in `current`, so a `Tick` reader in steps 2..k of the same frame sees them again. It also sees them in every tick of frame N+1.
- **`Update` readers of `Tick` events.** An `Update` reader sees step 1's and step 2's events in one combined list. It sees that list again the next frame. Nothing marks which tick or frame an event belongs to.

### Parallel readers and writers

Nothing in the queue is synchronised **(inference from the code)**:

- `ReadEvents` is `const` but rewrites the shared `mutable combinedBuffer` (App.hpp L113-116). Two parallel readers of the same type therefore write the same vector concurrently. Each also holds a reference to a vector that the other reader may reallocate.
- `Send` does `push_back` on `currentBuffer` (L89) with no lock. It races with other senders and with readers.
- The first use of a type inserts into `m_eventQueues` (L355), which races with any other queue lookup.

So the two-frame window does not currently make parallel systems safe. The only thing it reliably does is keep an event readable for a sender that runs after the reader.

### Where the double buffer came from

The queue arrived in the first commit, `25b1c18` (2023-12-18). Its shape matches Bevy 0.12's `Events<E>::update`, which also swaps two buffers once per frame ([bevy v0.12.0 event.rs L233](https://github.com/bevyengine/bevy/blob/v0.12.0/crates/bevy_ecs/src/event.rs#L233)). The comment on Bevy's queue reads "Each event can be consumed by multiple systems, in parallel" ([L96](https://github.com/bevyengine/bevy/blob/v0.12.0/crates/bevy_ecs/src/event.rs#L96)). But Bevy pairs the double buffer with a per-system cursor, `EventReader { reader: Local<ManualEventReader<E>> }` ([L392-393](https://github.com/bevyengine/bevy/blob/v0.12.0/crates/bevy_ecs/src/event.rs#L392-L393), [L541](https://github.com/bevyengine/bevy/blob/v0.12.0/crates/bevy_ecs/src/event.rs#L541)), and FenrirApp has no counterpart. **(inference)**: FenrirApp copied Bevy's buffer rotation without the cursor that turns the two-frame window into "each reader sees each event once".

### C#

- `Events.Send` is recorded and applied only when the C# system returns. "the sender does not read its own send, and the systems after it do" (`packages/FenrirScripting/Managed/Fenrir.Scripting/Events.cs` L150-152; `packages/FenrirScripting/src/Bindings.cpp` L182-187; `packages/FenrirScripting/src/ChangeBuffer.cpp` L146-157 applies them through `SendRaw`).
- `Events.Read` returns an `EventReader<T>` ref struct over `ReadRaw`: "this frame's so far and the last frame's, the same window C++ ReadEvents gives" (Events.cs L92-96, L166-175; Bindings.cpp L206-208).
- C# phases are scheduled as sequential systems (`packages/FenrirScripting/src/Scripting.cpp` L781).
- `EventsHaveTheSameFrameWindowInBothLanguages` pins the two-frame window. An event sent in frame 1 is seen in frames 1 and 2, then it is gone (`packages/FenrirScripting/Tests/ScriptingTests.cpp` L2175-2215).

### Existing readers

- **Window input.** It is sent from GLFW callbacks during `glfwPollEvents()`, which runs at the end of `Window::PostUpdate` (`examples/DemoApp/src/Window.cpp` L100-171, L183-198). That is after almost every reader in the frame. Per-frame readers such as `CameraController::Update` (`examples/DemoApp/src/CameraController.cpp` L70-90) therefore see window input exactly once, but only by luck of where the poll sits. The handlers are also idempotent: pressed keys go into a set (CameraController.cpp L16-20).
- **AutoPlay.** `ScriptingDemo::AutoPlay` sends `KeyboardKeyEvent` from a sequential `Update` system (`examples/DemoApp/src/ScriptingDemo.hpp` L131-152). `Window::PostUpdate` therefore sees that event twice.
- **LogBursts** works around the window by hand. It remembers how many of the visible events were this frame's (ScriptingDemo.hpp L113-127). That is a hand-rolled cursor.

---

## 2. Bevy (v0.20.0): buffered `Messages<M>` plus immediate observers

Bevy 0.17 split buffered "events" into **messages** (`Message`, `Messages<M>`, `MessageReader`, `MessageWriter`) and kept the name **events** for observer triggers. Paths below are under `crates/bevy_ecs/src/` unless they name another crate.

### Messages

- **Storage.** `Messages<M>` is a world resource with two `MessageSequence`s (`messages_a` older, `messages_b` newer) and a running `message_count` ([message/messages.rs L93-102](https://github.com/bevyengine/bevy/blob/v0.20.0/crates/bevy_ecs/src/message/messages.rs#L93-L102)). Writing pushes to `messages_b`, stamps the message with `id = message_count`, and increments the count ([L129-147](https://github.com/bevyengine/bevy/blob/v0.20.0/crates/bevy_ecs/src/message/messages.rs#L129-L147)).
- **Update.** `update()` swaps the buffers, clears the new `messages_b`, and sets its `start_message_count` ([L193-201](https://github.com/bevyengine/bevy/blob/v0.20.0/crates/bevy_ecs/src/message/messages.rs#L193-L201)).
- **Lifetime.** The docs describe the guarantees ([L69-79](https://github.com/bevyengine/bevy/blob/v0.20.0/crates/bevy_ecs/src/message/messages.rs#L69-L79)):
  - readers "that read at least once per update will never drop messages";
  - readers that read after two updates "are guaranteed to drop" them;
  - "messages will persist across a single frame boundary and so ordering of message producers and consumers is not critical" ([L34-38](https://github.com/bevyengine/bevy/blob/v0.20.0/crates/bevy_ecs/src/message/messages.rs#L34-L38)).
- **Exactly once per reader.** `MessageCursor` holds a single `last_message_count` ([message/message_cursor.rs L53-57](https://github.com/bevyengine/bevy/blob/v0.20.0/crates/bevy_ecs/src/message/message_cursor.rs#L53-L57)).
  - Reading slices both buffers from that count onward, then advances the cursor for each message yielded ([message/iterators.rs L54-101](https://github.com/bevyengine/bevy/blob/v0.20.0/crates/bevy_ecs/src/message/iterators.rs#L54-L101)).
  - `missed_messages` reports messages that fell off the old buffer before the reader got to them ([message_cursor.rs L131-136](https://github.com/bevyengine/bevy/blob/v0.20.0/crates/bevy_ecs/src/message/message_cursor.rs#L131-L136)).
  - The doc example asserts "messages are only processed once per reader" ([messages.rs L65-66](https://github.com/bevyengine/bevy/blob/v0.20.0/crates/bevy_ecs/src/message/messages.rs#L65-L66)).
  - `MessageReader` is `{ reader: Local<MessageCursor<M>>, messages: Res<Messages<M>> }`, so the cursor is per-system state ([message/message_reader.rs L33-38](https://github.com/bevyengine/bevy/blob/v0.20.0/crates/bevy_ecs/src/message/message_reader.rs#L33-L38)). The trait docs say the same: "tracks the consumption of these events on a per-system basis using a `Local<MessageCursor>`" ([message/mod.rs L41-42](https://github.com/bevyengine/bevy/blob/v0.20.0/crates/bevy_ecs/src/message/mod.rs#L41-L42)).
- **When it updates.** `message_update_system` is added to the `First` schedule by default ([bevy_app/src/sub_app.rs L90-101](https://github.com/bevyengine/bevy/blob/v0.20.0/crates/bevy_app/src/sub_app.rs#L90-L101)). `First` runs before `PreUpdate`, `RunFixedMainLoop`, `Update`, `PostUpdate` and `Last` ([bevy_app/src/main_schedule.rs L29-38](https://github.com/bevyengine/bevy/blob/v0.20.0/crates/bevy_app/src/main_schedule.rs#L29-L38)). The registry skips a swap when a queue has not changed for two updates ([message/message_registry.rs L66-84](https://github.com/bevyengine/bevy/blob/v0.20.0/crates/bevy_ecs/src/message/message_registry.rs#L66-L84)).
- **Fixed timestep.**
  - `RunFixedMainLoop` runs `FixedMain` "zero to many times" ([main_schedule.rs L33-34](https://github.com/bevyengine/bevy/blob/v0.20.0/crates/bevy_app/src/main_schedule.rs#L33-L34); the loop is in [bevy_time/src/fixed.rs L243-258](https://github.com/bevyengine/bevy/blob/v0.20.0/crates/bevy_time/src/fixed.rs#L243-L258)).
  - So that fixed systems do not lose messages, `TimePlugin` adds `signal_message_update_system` to `FixedPostUpdate` and starts the registry in `Waiting` ("Ensure the messages are not dropped until `FixedMain` systems can observe them", [bevy_time/src/lib.rs L100-104](https://github.com/bevyengine/bevy/blob/v0.20.0/crates/bevy_time/src/lib.rs#L100-L104)).
  - `message_update_condition` then lets the swap in `First` run only after at least one fixed step since the last swap ([message/update.rs L16-57](https://github.com/bevyengine/bevy/blob/v0.20.0/crates/bevy_ecs/src/message/update.rs#L16-L57); states documented in [message_registry.rs L33-41](https://github.com/bevyengine/bevy/blob/v0.20.0/crates/bevy_ecs/src/message/message_registry.rs#L33-L41)).
  - **(inference)** Messages survive frames with zero fixed steps, so a `FixedUpdate` reader cannot miss them. A `FixedUpdate` reader that runs 3 times in a frame sees each message once, on its first run after the write, because its cursor advances. A message written in fixed step 1 is new to readers in step 2. The same message reaches `Update` readers once too, through their own cursors.
- **Input in the fixed step.** The official example accumulates input in `RunFixedMainLoopSystems::BeforeFixedMainLoop` rather than reading it in `FixedUpdate`, because "it would sometimes not register player input, as that schedule may run zero times per frame". It clears the accumulated input only once a fixed step has run ([examples/movement/physics_in_fixed_timestep.rs L105-129](https://github.com/bevyengine/bevy/blob/v0.20.0/examples/movement/physics_in_fixed_timestep.rs#L105-L129), [L291-298](https://github.com/bevyengine/bevy/blob/v0.20.0/examples/movement/physics_in_fixed_timestep.rs#L291-L298)).
- **Parallel readers.** Readers take `Res` (shared) and their own `Local` cursor. "systems with `MessageReader<T>` param can be executed concurrently (but not concurrently with `MessageWriter<T>` or `MessageMutator<T>` systems for the same message type)" ([message_reader.rs L27-30](https://github.com/bevyengine/bevy/blob/v0.20.0/crates/bevy_ecs/src/message/message_reader.rs#L27-L30)). Writers take `ResMut`, so two writers of the same type never run at once ([message/message_writer.rs L28-31](https://github.com/bevyengine/bevy/blob/v0.20.0/crates/bevy_ecs/src/message/message_writer.rs#L28-L31)). The scheduler enforces this from declared access. Without ordering, "whether the messages arrive before or after the next `Messages::update` is unpredictable" ([messages.rs L25-27](https://github.com/bevyengine/bevy/blob/v0.20.0/crates/bevy_ecs/src/message/messages.rs#L25-L27)).
- **Payloads.** `pub trait Message: Send + Sync + 'static {}` allows any thread-safe Rust type, including ones with heap data ([message/mod.rs L100](https://github.com/bevyengine/bevy/blob/v0.20.0/crates/bevy_ecs/src/message/mod.rs#L100); "Messages must be thread-safe", [L51](https://github.com/bevyengine/bevy/blob/v0.20.0/crates/bevy_ecs/src/message/mod.rs#L51)). Bevy has no first-party scripting language, so there is no foreign-language reader to compare.

### Observers (Bevy "events")

- **Timing.** Triggering an `Event` runs every matching `Observer` "_immediately_, as part of the `World::trigger` call" ([event/mod.rs L15-19](https://github.com/bevyengine/bevy/blob/v0.20.0/crates/bevy_ecs/src/event/mod.rs#L15-L19)). `Commands::trigger` defers it to "the next sync point in the ECS schedule" ([observer/distributed_storage.rs L146-148](https://github.com/bevyengine/bevy/blob/v0.20.0/crates/bevy_ecs/src/observer/distributed_storage.rs#L146-L148)). Nested triggers queued from observers "will be recursively evaluated until there are no commands left" ([L123-125](https://github.com/bevyengine/bevy/blob/v0.20.0/crates/bevy_ecs/src/observer/distributed_storage.rs#L123-L125)).
- **Lifetime.** Nothing is stored. Each observer runs exactly once per trigger. Observer order is arbitrary ([L153-155](https://github.com/bevyengine/bevy/blob/v0.20.0/crates/bevy_ecs/src/observer/distributed_storage.rs#L153-L155)).
- **Parallelism.** `World::trigger` needs `&mut World`, and observers run one after another inside it. Exclusive systems cannot be observers ([L223](https://github.com/bevyengine/bevy/blob/v0.20.0/crates/bevy_ecs/src/observer/distributed_storage.rs#L223)).
- **Payloads.** `pub trait Event: Send + Sync + Sized + 'static` ([event/mod.rs L89](https://github.com/bevyengine/bevy/blob/v0.20.0/crates/bevy_ecs/src/event/mod.rs#L89)).
- **When to use which.** Messages are "more efficient than `Event`s (which are handled via `Observer`s)" for batch processing, and "evaluated at fixed points in the schedule rather than immediately" ([message/mod.rs L44-49](https://github.com/bevyengine/bevy/blob/v0.20.0/crates/bevy_ecs/src/message/mod.rs#L44-L49)).

---

## 3. Unity

### MonoBehaviour collision callbacks

- **Fixed step.** "FixedUpdate may be called zero, one, or multiple times per frame" ([ScriptReference: MonoBehaviour.FixedUpdate](https://docs.unity3d.com/6000.2/Documentation/ScriptReference/MonoBehaviour.FixedUpdate.html)). A backlog of fixed steps "accumulates ... and Unity executes all of them in the next frame to catch up" ([Manual: Fixed updates](https://docs.unity.com/en-us/engine/6000.3/manual/scripting/managing-time-and-frame-rate/fixed-updates.md)).
- **Where the simulation runs.** With `SimulationMode.FixedUpdate`, the simulation runs "immediately after the MonoBehaviour.FixedUpdate" ([ScriptReference: SimulationMode](https://docs.unity3d.com/6000.2/Documentation/ScriptReference/SimulationMode.html)). A simulation step includes "filing of the physics callbacks (contact, trigger and joints)" ([ScriptReference: Physics.Simulate](https://docs.unity3d.com/6000.2/Documentation/ScriptReference/Physics.Simulate.html)).
- **Callback timing.** The Manual's execution-order flowchart places `OnTriggerXXX`/`OnCollisionXXX` in the Physics loop ([Manual: Execution order](https://docs.unity3d.com/6000.2/Documentation/Manual/execution-order.html)). That placement is shown only in the image, not in the text. **(inference)** Callbacks are pushed once per simulation step, inside the fixed loop. So they fire zero times in a frame with no step and k times in a frame with k steps. Each callback reports one change (Enter, Stay or Exit), and the engine derives the change from contact state.
- **Exactly once.** Delivery is push-based: each subscribed MonoBehaviour gets one call per contact change. "Collision events will be sent to disabled MonoBehaviours" ([ScriptReference: OnCollisionEnter](https://docs.unity3d.com/6000.2/Documentation/ScriptReference/MonoBehaviour.OnCollisionEnter.html)).
- **Payloads.** The `Collision` argument is a managed class. With **Reuse Collision Callbacks**, which is on by default, Unity reuses a pool and "the `Collision` data passed to the callbacks is then only valid during that specific callback" ([Manual: Optimize collision callbacks](https://docs.unity.com/en-us/engine/6000.3/manual/physics-section/physics-overview/physics-optimization/memory/collision-callbacks.md)).

### Input System

- **Update modes.** Events are processed in dynamic update, in fixed update ("corresponds to how MonoBehaviour.FixedUpdate operates"), or manually ([Input System 1.14: Settings, Update Mode](https://docs.unity3d.com/Packages/com.unity.inputsystem@1.14/manual/Settings.html)).
- **Polling.** `WasPressedThisFrame()` is true if the press point was crossed "at any point during the current frame" ([Responding to actions](https://docs.unity3d.com/Packages/com.unity.inputsystem@1.14/manual/RespondingToActions.html)).
- **Polling under fixed-update processing.** The `InputAction` API notes that when the mode is `ProcessEventsInFixedUpdate` or `ProcessEventsManually`, code reading in dynamic `Update` should call `WasPressedThisDynamicUpdate()` instead ([API: InputAction.WasPressedThisFrame](https://docs.unity3d.com/Packages/com.unity.inputsystem@1.14/api/UnityEngine.InputSystem.InputAction.html)). In effect, a "this frame" flag is stamped with the update that processed it, and Unity keeps one stamp for each kind of update.
- **Callbacks.** `started`/`performed`/`canceled` callbacks deliver an `InputAction.CallbackContext` whose "contents ... are only valid for the duration of the callback" ([Responding to actions](https://docs.unity3d.com/Packages/com.unity.inputsystem@1.14/manual/RespondingToActions.html)). The same page says `InputActionTrace` records action events for later reading. It "can be safely read from multiple threads as long as it is not concurrently being written to".

### Unity Physics (DOTS/Entities)

- **Fixed step.** `PhysicsSystemGroup` "is a subgroup of `FixedStepSimulationSystemGroup`", so "multiple physics steps can occur during one frame" ([Unity Physics 1.3: Physics pipeline](https://docs.unity3d.com/Packages/com.unity.physics@1.3/manual/physics-pipeline.html)). The group updates "as many times as necessary at the fixed timestep" ([API: FixedStepSimulationSystemGroup](https://docs.unity3d.com/Packages/com.unity.entities@1.3/api/Unity.Entities.FixedStepSimulationSystemGroup.html)).
- **Pull-based access.** Collision and trigger events are read through `ICollisionEventsJob`/`ITriggerEventsJob` scheduled against the `SimulationSingleton`, or straight from the simulation's `CollisionEvents`/`TriggerEvents` streams.
- **Lifetime.** The streams are valid "after the `PhysicsSimulationGroup` has finished, and up until it starts in the next frame". Reading them during the group is undefined. The examples schedule their readers in `FixedStepSimulationSystemGroup`, either `UpdateBefore` the simulation (reading the previous step's events) or `UpdateAfter` it ([Unity Physics 1.3: Simulation results](https://docs.unity3d.com/Packages/com.unity.physics@1.3/manual/simulation-results.html)).
- **Stateless by default.** The streams carry only the step's pairs, with no enter or exit. The official sample adds state:
  - `StatefulSimulationEventBuffers` keeps `Previous`/`Current` lists and `SwapBuffers()` each step ([StatefulSimulationEventBuffers.cs L7-27](https://github.com/Unity-Technologies/EntityComponentSystemSamples/blob/6786a741ee1f118ed14cecfa02beae8e926937b0/PhysicsSamples/Assets/6.%20Events/Scripts/Stateful/StatefulSimulationEventBuffers.cs#L7-L27)).
  - It diffs the sorted lists into `Enter`/`Stay`/`Exit` ([L30-48](https://github.com/Unity-Technologies/EntityComponentSystemSamples/blob/6786a741ee1f118ed14cecfa02beae8e926937b0/PhysicsSamples/Assets/6.%20Events/Scripts/Stateful/StatefulSimulationEventBuffers.cs#L30-L48); states in [IStatefulSimulationEvent.cs L11-17](https://github.com/Unity-Technologies/EntityComponentSystemSamples/blob/6786a741ee1f118ed14cecfa02beae8e926937b0/PhysicsSamples/Assets/6.%20Events/Scripts/Stateful/IStatefulSimulationEvent.cs#L11-L17)).
  - It writes the result into a per-entity `DynamicBuffer`. The buffer is **cleared every step** by a system `[UpdateInGroup(PhysicsSystemGroup)] [UpdateAfter(PhysicsSimulationGroup)]` ([StatefulCollisionEventBufferSystem.cs L14-16, L61-98](https://github.com/Unity-Technologies/EntityComponentSystemSamples/blob/6786a741ee1f118ed14cecfa02beae8e926937b0/PhysicsSamples/Assets/6.%20Events/Scripts/Stateful/StatefulCollisionEventBufferSystem.cs#L14-L98)).
- **(inference)** These events have a one-step lifetime. A reader inside the fixed group sees each step's events once. A reader in the variable-rate group sees only the last step's buffer, so it misses earlier steps when two steps run in one frame. This is the problem ticket 08 rejected per-world lists for.
- **Parallel readers.** Readers are Burst jobs ordered through the job dependency chain (`state.Dependency`, same file L72-98). The payloads are unmanaged structs.

---

## 4. Godot (4.7.2-stable)

- **Signals: immediate or deferred.** `CONNECT_DEFERRED`: "Deferred connections trigger their Callables on idle time (at the end of the frame), rather than instantly" ([Object: ConnectFlags](https://docs.godotengine.org/en/4.5/classes/class_object.html#enum-object-connectflags)). Other connections run synchronously inside `emit_signal`. "Idle time happens mainly at the end of process and physics frames", and deferred calls run "until there are none left" ([Object.call_deferred](https://docs.godotengine.org/en/4.5/classes/class_object.html#class-object-method-call-deferred)). A signal is a call, not a stored event. `CONNECT_ONE_SHOT` disconnects after the first emission.
- **Payloads.** Signal arguments are `Variant`s. For C#, "Signals support arguments of any Variant-compatible type", and custom objects must inherit `GodotObject` ([C# signals](https://docs.godotengine.org/en/4.5/tutorials/scripting/c_sharp/c_sharp_signals.html)). Signals are exposed to C# as events through `[Signal]` delegates.
- **Main loop.** The physics loop runs `advance.physics_steps` times per frame ([main/main.cpp L4969-5053](https://github.com/godotengine/godot/blob/4.7.2-stable/main/main.cpp#L4969-L5053)). Each step does the following:
  1. increment `_physics_frames` (L4978);
  2. `PhysicsServer3D::sync()` and `flush_queries()` (L4990-4991);
  3. call `physics_process`, which runs `_physics_process` (L5001);
  4. step the physics server (L5034-5035);
  5. flush the deferred-call queue (L5044).

  `_process` runs once afterwards (L5062), and `_process_frames++` follows at L5119. Physics processing "happens at a fixed rate, 60 times per second by default" ([Idle and physics processing](https://docs.godotengine.org/en/4.5/tutorials/scripting/idle_and_physics_processing.html)).
- **Physics signals are emitted once per change, at the start of the next step, before `_physics_process`.**
  - `flush_queries()` calls `call_queries()` on every active space ([modules/godot_physics_3d/godot_physics_server_3d.cpp L1696-1708](https://github.com/godotengine/godot/blob/4.7.2-stable/modules/godot_physics_3d/godot_physics_server_3d.cpp#L1696-L1708)).
  - For an `Area3D`, `call_queries` walks the pending `monitored_bodies` changes, reports each as added or removed, and **removes it** as it goes ([godot_area_3d.cpp L235-265](https://github.com/godotengine/godot/blob/4.7.2-stable/modules/godot_physics_3d/godot_area_3d.cpp#L235-L265)). Each enter or exit is therefore delivered once. Nodes then `emit_signal(body_entered/body_exited)` synchronously ([scene/3d/physics/area_3d.cpp L224-302](https://github.com/godotengine/godot/blob/4.7.2-stable/scene/3d/physics/area_3d.cpp#L224-L302)).
  - `RigidBody3D` emits `body_entered`/`body_exited` from its body-state callback (`_body_state_changed`, [rigid_body_3d.cpp L170-263](https://github.com/godotengine/godot/blob/4.7.2-stable/scene/3d/physics/rigid_body_3d.cpp#L170-L263)). It requires `contact_monitor` ([RigidBody3D](https://docs.godotengine.org/en/4.5/classes/class_rigidbody3d.html)).
  - The overlap lists are "modified once during the physics step" ([Area3D.get_overlapping_bodies](https://docs.godotengine.org/en/4.5/classes/class_area3d.html)).
- **Input stamped with a frame number.**
  - When an action becomes pressed, Godot records `pressed_physics_frame = get_physics_frames() + 1` and `pressed_process_frame = get_process_frames()`. The source comment says: "As input may come in part way through a physics tick, the earliest we can react to it is the next physics tick" ([core/input/input.cpp L1031-1036](https://github.com/godotengine/godot/blob/4.7.2-stable/core/input/input.cpp#L1031-L1036)).
  - `is_action_just_pressed` compares against the physics frame counter when called during physics, and against the process frame counter otherwise ([L438-441](https://github.com/godotengine/godot/blob/4.7.2-stable/core/input/input.cpp#L438-L441)).
  - The docs: true "in the current frame or physics tick ... only ... on the frame or tick that the user pressed down the button" ([Input.is_action_just_pressed](https://docs.godotengine.org/en/4.5/classes/class_input.html#class-input-method-is-action-just-pressed)).
  - **(inference)** A press is seen exactly once in `_process` and exactly once in `_physics_process`, in the first physics step after the press, even if that step comes frames later. When several steps run in one frame, only the first matches. Input events themselves reach `_input` callbacks as `InputEvent` objects, and are optionally accumulated and flushed "at least once per frame" ([Input.flush_buffered_events](https://docs.godotengine.org/en/4.5/classes/class_input.html)). Agile flushing additionally flushes before each physics step ([main.cpp L4973-4975](https://github.com/godotengine/godot/blob/4.7.2-stable/main/main.cpp#L4973-L4975)).
- **Parallelism.** Signals run on the emitting thread. Physics signals run on the main thread inside `flush_queries`.

---

## 5. EnTT (v3.16.0): `dispatcher`

- **Immediate.** `trigger` sends "an immediate event to all the listeners registered so far ... Listeners are invoked immediately, order of execution is not guaranteed" ([docs/md/signal.md L425-435](https://github.com/skypjack/entt/blob/v3.16.0/docs/md/signal.md#L425-L435); `trigger` calls `signal.publish(event)`, [dispatcher.hpp L68-70](https://github.com/skypjack/entt/blob/v3.16.0/src/entt/signal/dispatcher.hpp#L68-L70)).
- **Queued.** `enqueue` stores events "until the `update` member function is invoked", which "emits all the events ... at once". This lets users "literally dispatch events once per tick to their systems" ([signal.md L437-456](https://github.com/skypjack/entt/blob/v3.16.0/docs/md/signal.md#L437-L456)).
- **Exactly once.** `publish()` delivers the events present when it starts, then erases exactly those. Events enqueued by listeners during the publish wait for the next `update` ([dispatcher.hpp L46-54](https://github.com/skypjack/entt/blob/v3.16.0/src/entt/signal/dispatcher.hpp#L46-L54); `update` at [L374-389](https://github.com/skypjack/entt/blob/v3.16.0/src/entt/signal/dispatcher.hpp#L374-L389)). Each listener gets each event once. There is no per-reader buffer: whoever is connected at `update` time receives the event.
- **Timing.** Events stay queued until the application calls `update`, so they are visible exactly when the application chooses. With a fixed step, the application decides whether to call `update<T>()` per tick or per frame. Separate named queues per event type exist (`enqueue_hint`, [signal.md L460-476](https://github.com/skypjack/entt/blob/v3.16.0/docs/md/signal.md#L460-L476)).
- **Parallelism.** Neither the header nor the doc mentions threads. Storage is a plain `std::vector` per type ([dispatcher.hpp L37](https://github.com/skypjack/entt/blob/v3.16.0/src/entt/signal/dispatcher.hpp#L37)) **(inference: not thread-safe)**.
- **Payloads.** Any decayed C++ type (`static_assert(std::is_same_v<Type, std::decay_t<Type>>)`, [L33](https://github.com/skypjack/entt/blob/v3.16.0/src/entt/signal/dispatcher.hpp#L33)).

---

## 6. Flecs (v4.1.6): observers

- **Timing.** "Observers are always executed when the operation that triggered the observer happens, on the thread where the operation is executed." Most operations are deferred while systems run, so "most observers will also be invoked during sync points" ([docs/ObserversManual.md L2157-2158](https://github.com/SanderMertens/flecs/blob/v4.1.6/docs/ObserversManual.md#L2157-L2158)).
- **Custom events.** `emit` invokes observers directly. `enqueue` puts the event in the command queue when the world is deferred, copying the payload with the type's copy hook, or "a `memcpy`" if none is registered ([L2152-2155](https://github.com/SanderMertens/flecs/blob/v4.1.6/docs/ObserversManual.md#L2152-L2155)). A custom event names an event entity, components and a source ([L1821-1835](https://github.com/SanderMertens/flecs/blob/v4.1.6/docs/ObserversManual.md#L1821-L1835)). Custom event order is maintained ([L2262-2263](https://github.com/SanderMertens/flecs/blob/v4.1.6/docs/ObserversManual.md#L2262-L2263)).
- **Lifetime and exactly-once.** Nothing is stored. Each matching observer is invoked once per emit.
- **Stated limits.** Flecs says observers cannot easily peek ahead, sort events, or "Process events on multiple threads". It adds that "for simple use cases, a basic event queue is always going to outperform observers", and "if you can solve something with a system, it should probably be solved with a system" ([L86-111](https://github.com/SanderMertens/flecs/blob/v4.1.6/docs/ObserversManual.md#L86-L111)). Flecs ships no buffered multi-reader queue. Observers are its event mechanism.

---

## 7. Unreal (brief)

- **Delegates.** There are three types: "Single, Multicast, and Dynamic (UObject, serializable)" ([Delegates and lambda functions](https://dev.epicgames.com/documentation/en-us/unreal-engine/delegates-and-lambda-functions-in-unreal-engine)). `Broadcast()` calls all bound objects immediately, and "The execution order of bound functions when calling `Broadcast()` is not defined" ([Multicast delegates](https://dev.epicgames.com/documentation/en-us/unreal-engine/multicast-delegates-in-unreal-engine)). They are calls, not stored events.
- **Hit and overlap.** Physics-simulated hits fire `Event Hit`/`OnComponentHit` only with **Simulation Generates Hit Events**. Overlap begin and end need **Generate Overlap Events** on both components ([Collision overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/collision-in-unreal-engine---overview); [Collision response reference](https://dev.epicgames.com/documentation/en-us/unreal-engine/collision-response-reference-in-unreal-engine)). Blueprints receive them through dynamic delegates.
- I could not reach Unreal's source (it needs an Epic GitHub account) or a doc that states the exact tick timing, so that is not covered here.

---

## 8. Summary across engines

| | Becomes visible | Readable for | Exactly once per reader, by | Fixed step k times per frame | Parallel readers | Payloads / scripting |
|---|---|---|---|---|---|---|
| **FenrirApp** | at `Send` | rest of frame + next frame | not guaranteed: 2× for per-frame readers after the sender | `Tick` reader sees an event 0..many times | unsynchronised (`mutable combinedBuffer`) | any C++ type; byte-copyable only for C# |
| **Bevy messages** | at `write` | until 2 updates; the update waits for a fixed step | per-system cursor (`Local<MessageCursor>`) | swap gated on a fixed step having run; cursor gives once per reader | many readers concurrently; writers exclusive | any `Send + Sync + 'static` |
| **Bevy observers** | immediately (or next sync point via `Commands`) | the call | one call per trigger | n/a (a call) | no; `&mut World` | any `Send + Sync + 'static` |
| **Unity callbacks** | after each simulation step | the callback | engine diffs contacts, pushes Enter/Stay/Exit | once per step, so 0..k per frame | main thread | managed `Collision`, valid in the callback |
| **Unity Physics** | after `PhysicsSimulationGroup` | until the next step | one-step lifetime; read inside the fixed group | readers in the fixed group run per step | jobs with dependencies | unmanaged structs |
| **Unity Input** | at input update | "this frame" of the processing update | per-update-type flags (`ThisFrame` / `ThisDynamicUpdate`) | chosen by Update Mode | n/a | structs / callbacks |
| **Godot signals** | at `emit` (or idle for deferred) | the call | one call per emit; physics diffs drained once | emitted per step before `_physics_process` | emitting thread | `Variant`; C# via `[Signal]` |
| **Godot input** | at the event | 1 process frame / 1 physics frame | frame-number stamps | first step after the press | n/a | n/a |
| **EnTT dispatcher** | at `trigger`, or at `update` for queued | until `update` | delivered then erased | application picks when to call `update` | not thread-safe | any decayed C++ type |
| **Flecs observers** | at the operation (often a sync point) | the call | one call per emit | n/a | "can't ... process events on multiple threads" | memcpy or copy hook |
| **Unreal delegates** | at `Broadcast` | the call | one call per broadcast | n/a | n/a | dynamic delegates for Blueprints |

---

## 9. Options for FenrirApp

These are listed without choosing. Each option keeps byte-copyable plain data unless it says otherwise.

### A. Per-reader cursors on the existing double buffer (Bevy)

Stamp each queue with a running `count` and each buffer with its `start`. A reader keeps `lastCount` and reads only the events after it.

- **Gains.**
  - Exactly once per reader for any reader that runs at least once per two rotations.
  - Late sends are kept: an event sent after a reader ran is still seen next frame, and only once.
  - `Tick` readers that run several times see only the events new since their last run.
  - Matches the shape the queue was copied from.
- **Costs.**
  - Every reader needs identity and state. C++ `SystemFunc` is a plain callable with no `Local`, so the cursor would live in a reader object the system owns (`EventReader<T>` member or captured `shared_ptr`) or in a scheduler-assigned slot.
  - C# systems are classes, so a field holds the cursor, but `Events.Read<T>()` would need to take or return it (ticket 18).
  - A rotation once per frame still loses events for a `Tick` reader that runs zero times in two frames. This needs Bevy's gating, which delays the rotation until a fixed step has run, so `Update` events live longer when no tick runs.
  - It does not fix the data races. Reads still need concurrent-safe access: no shared `mutable combinedBuffer`, a returned span over two ranges, and sends that do not reallocate under a reader (Bevy forbids reader/writer overlap through the scheduler).

### B. Events stamped with a frame or tick (Godot input)

Each event records the frame number and the tick number at send. Readers ask for "events from tick t" or "events since frame f".

- **Gains.**
  - Stateless reads for the common case: "this tick's events" or "this frame's events".
  - An `Update` reader can tell which tick an event came from.
  - Byte-copyable stays possible if the stamp lives beside the payload rather than in it.
- **Costs.**
  - "Exactly once" holds only if each reader runs exactly once per stamp unit. An `Update` reader asking for "frame N's events" misses events sent after it ran (late sends). A `Tick` reader asking for "this tick's" misses events sent in `Update` unless they are stamped for the next tick, as Godot does with `physics_frames + 1`. That needs a rule per send phase.
  - Retention must cover the slowest reader.
  - In practice this is a cursor whose position is implicit, which is less flexible than A when a reader skips a frame.

### C. Separate queues or rotation for the fixed step

Rotate fixed-step event queues per tick, and others per frame. Alternatively, keep `Tick`-sent events in a per-tick buffer that `Update` reads as an accumulated list.

- **Gains.**
  - The rotation matches the cadence of the sender (physics sends once per step).
  - `Tick` readers see a step's events once.
  - The Unity Physics model, but with the accumulated list for `Update`, which avoids its missed-step problem.
- **Costs.**
  - Each event type needs a declared cadence, or a queue per phase.
  - An `Update` reader of a per-tick queue still needs an accumulation rule (all ticks since its last read, which is a cursor again).
  - Events crossing cadences (`Update` to `Tick` input) need Bevy's "accumulate before the fixed loop" pattern, or B's next-tick stamp.

### D. Immediate observers / callbacks (Bevy observers, EnTT `trigger`, Flecs, Godot signals, Unity callbacks, Unreal delegates)

`app.Trigger(e)` calls registered handlers synchronously.

- **Gains.**
  - Exactly once by construction, nothing stored, no frame window.
  - Natural for rare events: window resize, app exit.
  - Arbitrary payload types are easy in C++, since nothing is copied or stored.
- **Costs.**
  - Handlers run on the sender's thread, in the middle of its system, so they need world access rules. Bevy needs `&mut World` and Flecs runs observers at sync points. Fenrir has no sync-point machinery for C++ systems.
  - Order is unspecified in Bevy, EnTT and Unreal.
  - Every engine surveyed says this is worse for high-volume batched events such as per-step contacts (Bevy message/mod.rs L44-46; Flecs L91).
  - A C# handler needs a managed callback across the boundary per event, unlike today's single pointer and count.
  - Deferring handlers to a sync point gives back a queue.

### E. Queue with explicit `update` (EnTT `enqueue` + `update`)

Events queue up and are delivered to registered listeners when the engine flushes, for example once per tick for physics and once per frame for input.

- **Gains.**
  - Exactly once per listener.
  - Flush points are explicit and can be per cadence (combines with C).
- **Costs.**
  - Same listener and threading questions as D.
  - Pull-style systems (`for e in Read<T>()`) become push-style handlers, which changes every existing reader.

### Cross-cutting costs, whichever option is chosen

- **Concurrency.** Every option needs the current data races fixed, or the two-buffer queue made read-only during a parallel batch: no `mutable combinedBuffer`, no growth under readers, queues created at registration rather than lazily. Bevy solves this with scheduler-declared access. Fenrir's scheduler has no access declarations (Scheduler.cpp L18-36).
- **C#.** C# already delays its own sends to the end of the system (Events.cs L150-152). A cursor (A) maps onto a field in the C# system class. A stamp (B) maps onto a query argument. Callbacks (D, E) need a managed delegate per event type and a crossing per call.
  - Today the managed side needs fixed layout to read through a pointer: an `unmanaged` struct (Events.cs L97). Richer payloads (strings, arrays, objects) would need one of two things. Either the managed side marshals them, as Godot's `Variant` does, at a cost per event. Or events carry handles or indices into side storage while staying plain data, which is what Unity Physics' unmanaged event structs and Fenrir's entity-naming contact events already do.
- **Existing readers.**
  - DemoApp window input is sent at the end of `PostUpdate`, so it happens to be read once today. Under A or B it stays once without depending on poll placement.
  - `LogBursts`' hand-rolled count (ScriptingDemo.hpp L113-127) becomes unnecessary under A.
  - `EventsHaveTheSameFrameWindowInBothLanguages` (ScriptingTests.cpp L2175-2215) pins today's two-frame contract and would change under any option.

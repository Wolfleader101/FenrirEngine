---
status: proposed
---

# Render passes are timed on the GPU by timestamp queries read back late, and the renderer reports each pass's time by name

The lighting family added many GPU passes: GTAO, bloom, light clusters, shadow cascades and atlases, skinning, and soon screen-space reflections (ADR-0056). Each has a cost target, and today the only way to check one is an external profiler. While grilling ADR-0056 (2026-11-03) the user decided to measure SSR's 1.5 ms budget with RenderDoc or Nsight rather than add a timer just for it, and to give per-pass GPU timing a design of its own, since every effect benefits. This is that design. It amends ADR-0016's device interface and ADR-0023's stats.

## What exists

- **The device** (`src/Device/GraphicsDevice.hpp`) has resources, `Submit`, `Present`, `WaitIdle` and `SetVsync`, and no queries of any kind. `DeviceFeatures` has one member, `vertexLayer`.
- **The render graph** (`src/Core/RenderGraph.*`) records each running pass into its own `CommandList`, which starts with a `CmdBeginPass` carrying the pass's `PassDesc` (its `label` is the render pass's name) and ends with a `CmdEndPass`. `RendererCore::RenderFrame` submits the lists in order, then presents. Compute passes are passes too, with `QueueKind::Compute`.
- **Pass names repeat.** Each view adds its own `DepthPrepass`, `Opaque`, `Transparent`, `Background` and `Output`; bloom adds one `BloomDownsample` and one `BloomUpsample` per mip level; point shadows add `PointShadowFaces` per face. A name is a kind of pass, not one pass.
- **The OpenGL backend** (`src/Backend/OpenGL/OpenGLDevice.cpp`) already wraps each pass in `glPushDebugGroup` with its label, which is what RenderDoc and Nsight show. It keeps no fences: `framesInFlight` only ages destroyed resources, and the driver decides how far ahead the CPU runs. The glad loader already has `glGenQueries`, `glQueryCounter` and `glGetQueryObjectui64v` (core since 3.3), and `GL_QUERY_RESULT_NO_WAIT` (core since 4.4); the backend asks for 4.6.
- **`RendererStats`** (`include/FenrirRenderer/Renderer.hpp`) is eight `uint32_t` counters of the last drawn frame, exact for that frame, and is reached from C# through the generated scripting registration.

## The decisions

### The device times every pass when asked, and hands back whole frames late

```cpp
struct DeviceFeatures
{
    bool vertexLayer = false;
    /** @brief Whether the device can time passes on the GPU (SetGpuTiming). */
    bool gpuTiming = false;
};

/** @brief One pass's time on the GPU, in a frame whose times have come back. */
struct TimedPass
{
    /** @brief The pass's PassDesc::label. */
    std::string label;
    std::uint64_t nanoseconds = 0;
};

/** @brief One frame's pass times, in the order its passes were submitted. */
struct GpuFrameTimes
{
    std::uint64_t frameIndex = 0;
    /** @brief From the first pass's start to the last pass's end, gaps between passes included. */
    std::uint64_t totalNanoseconds = 0;
    std::vector<TimedPass> passes;
};

class GraphicsDevice
{
    // ...
    /** @brief Times every pass submitted from the next frame on, or stops; returns whether timing is now on. */
    virtual bool SetGpuTiming(bool on) = 0;
    /** @brief The newest timed frame whose times have come back since the last call, if any. Never waits. */
    [[nodiscard]] virtual std::optional<GpuFrameTimes> TakeGpuTimes() = 0;
};
```

- **The device times passes itself, at `CmdBeginPass` and `CmdEndPass`.** No new command, no query handles in the renderer, and the render graph is unchanged: every backend already sees each pass's start and end in `Submit`, with its label. Vulkan would write `vkCmdWriteTimestamp` and D3D12 `EndQuery(D3D12_QUERY_TYPE_TIMESTAMP)` at the same two points, into a query pool per frame in flight, so the interface fits the explicit APIs ADR-0016 shapes it after.
- **Every pass or none.** A per-pass flag in `PassDesc` would let the renderer time some passes only, which nothing needs, and would change a struct every pass and every test compares.
- **Whole frames, late, and never waiting.** `TakeGpuTimes` returns the newest frame all of whose times have arrived, or nothing. Frames whose times arrive while a newer one has too are dropped, not queued: the renderer reports one frame, the newest.
- **Labels, not graph indices.** The device knows labels; grouping by name is the renderer's job (below).

### OpenGL: two `GL_TIMESTAMP` queries per pass, a ring of query sets, polled without stalling

- **`glQueryCounter(query, GL_TIMESTAMP)` at each pass's begin and end.** Not `GL_TIME_ELAPSED`: elapsed queries cannot nest or overlap, there is no elapsed query in Vulkan or D3D12 to match it, and they cannot give the frame's span with its gaps. Two timestamps per pass give each pass's time (end minus begin) and the frame's (last end minus first begin). The timestamps go after the debug group is pushed and before it is popped, and before a skipped pass's early return, so a pass the backend skips reads about zero.
- **A ring of `framesInFlight + 1` query sets**, each a growable array of query names made with `glGenQueries` and kept for the device's life, with the labels its frame submitted. A frame takes the next set. The ring is one longer than `framesInFlight` so a set is not reused while its frame is likely still on the GPU.
- **Polled at `BeginFrame`, oldest first.** For each pending set: `GL_QUERY_RESULT_AVAILABLE` on its last query; if that is not yet true, stop. If it is, every query is read with `GL_QUERY_RESULT_NO_WAIT`, and a set any of whose reads comes back unavailable is dropped instead of read, so reading never stalls even on a driver that makes timestamps available out of order. The newest set read becomes what `TakeGpuTimes` returns.
- **A set still pending when its turn comes round is dropped**, its queries reused, never waited on. This happens only when the GPU is more than `framesInFlight + 1` frames behind; the times then skip frames, which says the same thing.
- **`gpuTiming` is true** on the OpenGL backend: timer queries are core in the 4.6 it requires.

### The null backend times every pass at zero, with the same lag

The null device reports `gpuTiming` true and keeps the same books: each timed frame's labels in submit order, every pass and the total at 0 ns, coming back when the frame is `framesInFlight` frames old, at `BeginFrame`. Tests then see the names, their order, the grouping and the lag through `Renderer::GpuTimes()` exactly as a GL run would, and times that cannot flake. Faked non-zero times would only test the fakes.

### The renderer reports each pass's time by name, in a type of its own

```cpp
/** @brief One render pass name's time on the GPU in one frame: every pass of that name, summed. */
struct PassGpuTime
{
    std::string name;
    /** @brief How many passes of the name ran: one per view for a view's passes, one per mip for bloom's. */
    std::uint32_t passes = 0;
    float milliseconds = 0.0f;
};

/** @brief The GPU's time for each render pass name in a recent frame (ADR-0058). */
struct GpuTimes
{
    /** @brief Whether any frame's times have come back since timing was turned on; false with timing off. */
    bool available = false;
    /** @brief The frame they are from: one to three frames before the last drawn, with the default two in flight. */
    std::uint64_t frame = 0;
    /** @brief From the first pass's start to the last pass's end, so more than the passes' sum by any gaps. */
    float totalMilliseconds = 0.0f;
    /** @brief In the order each name first ran. */
    std::vector<PassGpuTime> passes;
};

/** @brief The newest frame's pass times on the GPU; unavailable while timing is off. */
[[nodiscard]] GpuTimes Renderer::GpuTimes() const;
/** @brief Turns GPU timing on or off from the next frame; returns whether it is now on. */
bool Renderer::SetGpuTiming(bool on);
```

Both get the static `(App& app, ...)` twins the other renderer functions have.

- **Not in `RendererStats`.** Stats are exact counters of the last drawn frame; GPU times are of an older frame, arrive or not, and are a variable-length list of strings. Putting them in `RendererStats` would make its every field's meaning "of the last drawn frame, except this one", and would drag a vector through the generated C# registration and its defaults test.
- **Grouped by name, in first-run order.** `RendererCore` turns the device's per-pass labels into one entry per name, summing times and counting passes. "Opaque: 2 passes, 3.1 ms" with two cameras is what a budget is set against; one pass of one view is what an external profiler is for. A pass whose times should be read on their own takes a name of its own.
- **The latest frame, raw.** The renderer does not smooth, average or keep history: it reports what the GPU did in one frame, and whoever shows it decides how to smooth it, as the demos already smooth their frame time.
- **A frame whose times never come back changes nothing**: `GpuTimes()` keeps answering with the last frame that did, and `frame` says which. Turning timing off clears it to unavailable, and drops sets still pending.
- **C# is left out.** Nothing in a script needs it yet; when something does, a native function that fills a list goes with the rest of the renderer's generated API.

### Off by default, behind `RendererConfig::gpuTiming`, and free when off

`RendererConfig` gains `bool gpuTiming = false`, changeable later with `Renderer::SetGpuTiming`, as the other renderer switches are.

- **Off costs one branch per pass in the backend's `Submit`**: no queries, no polling, no allocation; the query sets are made the first time timing is turned on and kept.
- **On costs two queries per pass** (about 80 in a lighting demo frame) and one poll per frame, which is not measurable against the passes. It is still **off by default**, because timestamps between passes can stop the GPU overlapping one pass's tail with the next pass's head on some drivers, so turning timing on can itself move the times a little; a game asks for it, a shipped frame does not pay for it.
- **Not tied to Debug builds** (as `debugValidation` is): timing a Release build is the main use.

### LightingDemo's Lighting panel shows the times in a section of their own

The Lighting panel (`examples/LightingDemo/src/LightingPanel.cpp`), which every LightingDemo scene shows, gains a **"GPU times"** section at its end, under the scene's own controls:

- A **"GPU timing (F)" checkbox**, kept in `LightingSettings` and applied with `Renderer::SetGpuTiming`; **F** toggles it too, and the panel's keys line names it. LightingDemo turns it on at start (its `RendererConfig` sets `gpuTiming`), so the times are there when a scene opens; the smoke runs press F to cover turning it off and on.
- The **total**, as "GPU frame: 6.42 ms", beside the CPU frame time the scenes already show, so whether a scene is GPU-bound reads at a glance.
- A **table** with one row per pass name, in run order: the name, how many passes, the milliseconds, and a bar of the row's share of the total (`ImGui::ProgressBar`). Each row's time is smoothed in the panel by the same exponential average the scenes use for frame time (0.05 per frame), kept per name in `LightingSettings` and reset when timing is turned off.
- **"No GPU times yet"** in grey while `available` is false: the first frames after turning timing on, and on the null backend's first `framesInFlight` frames.

ADR-0056's SSR ticket then checks its 1.5 ms by reading `HiZ`, `ReflectionTrace` and `SceneHistory` off the panel in the neon city, instead of an external profiler, if this lands first; the external profiler stays the way to see one pass of one view.

## Considered options

- **`GL_TIME_ELAPSED` around each pass.** One query per pass instead of two. Rejected: elapsed queries cannot nest, which rules out ever timing a stage around its passes; there is no Vulkan or D3D12 counterpart; and they leave the gaps between passes, and so the frame's GPU span, unmeasured.
- **One timestamp per pass boundary** (N + 1 per frame, each pass's time the next timestamp minus its own). Rejected: half the queries for no measurable saving, and each pass's time would take in the CPU-side gap before the next pass's work reaches the GPU.
- **Reading the results at once, or a frame late with `GL_QUERY_RESULT`.** Rejected: either waits for the GPU, which serialises CPU and GPU and changes the very frame being measured.
- **Timing commands in the `CommandList`** (`CmdWriteTimestamp`, recorded by the graph or a pass). More flexible: a pass could time its own sub-steps. Rejected for now: nothing needs times finer than a pass, and the render graph and every recorded-list test would change for it. If a pass ever needs it, the command can be added beside this, and the device's per-pass timing stays.
- **Extending `RendererStats`** with the times. Rejected above: different frame, variable length, and C# bindings.
- **Per-pass entries, not grouped by name.** Rejected as the report's shape: two cameras' `Opaque` passes would show as two rows that cannot be told apart, and bloom's chain as a dozen. The device still reports them per pass, so a later tool could show them.
- **Faked non-zero times on the null backend** (each pass's command count, say). Rejected: tests would assert on the fake's formula, not on anything the GPU does.
- **On by default in Debug builds.** Rejected: Debug builds are slow on the CPU and their GPU times are what Release's are; the switch is for whoever is measuring.

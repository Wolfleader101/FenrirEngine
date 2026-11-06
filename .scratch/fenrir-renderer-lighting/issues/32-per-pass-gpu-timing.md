# 32: Per-pass GPU timing

**What to build:** Each render pass timed on the GPU and reported by name, as ADR-0058 decides. Requested by the user while grilling ADR-0056 (screen-space reflections), 2026-11-03: SSR's 1.5 ms budget is measured with an external profiler for now, and every lighting effect's cost should be readable in the engine. This ticket builds on the lighting stack (`fenrir-renderer-lighting`), whose passes and LightingDemo it times.

- **Device:** `DeviceFeatures::gpuTiming`, `GraphicsDevice::SetGpuTiming(bool)` and `TakeGpuTimes()` returning `std::optional<GpuFrameTimes>` (each pass's label and nanoseconds in submit order, and the frame's span). The device times every pass at `CmdBeginPass` and `CmdEndPass` while timing is on; the render graph and `PassDesc` are unchanged.
- **OpenGL backend:** two `glQueryCounter(GL_TIMESTAMP)` queries per pass, inside the pass's debug group and before a skipped pass's early return; a ring of `framesInFlight + 1` query sets made on first use and kept; polled at `BeginFrame` oldest first, `GL_QUERY_RESULT_AVAILABLE` on a set's last query, then every query read with `GL_QUERY_RESULT_NO_WAIT`, a set with any unavailable read dropped, and a set still pending when its turn comes round dropped and reused. Never `GL_QUERY_RESULT`, never a wait.
- **Null backend:** `gpuTiming` true; each timed frame's labels kept, every time 0, coming back when the frame is `framesInFlight` frames old.
- **Renderer:** `RendererConfig::gpuTiming` (off), `Renderer::SetGpuTiming` and `Renderer::GpuTimes()` with their `(App&, ...)` twins; `GpuTimes { available, frame, totalMilliseconds, passes }`, one `PassGpuTime { name, passes, milliseconds }` per pass name in first-run order, grouped in `RendererCore` from the device's labels. Turning timing off clears it and drops pending sets. `RendererStats` is unchanged; nothing reaches C#.
- **LightingDemo:** the Lighting panel's "GPU times" section at its end: a "GPU timing (F)" checkbox and F key (named in the panel's keys line), the GPU frame total beside the scenes' CPU frame time, and a table of name, passes, smoothed milliseconds (0.05 exponential average, per name, reset on turning off) and a share-of-total bar; "No GPU times yet" in grey while unavailable. LightingDemo's `RendererConfig` turns timing on; the smoke runs press F.
- **README:** FenrirRenderer's README describes the switch, the report and its lag.

**Blocked by:** 07

**Touches:** `packages/FenrirRenderer/src/Device/`, `packages/FenrirRenderer/src/Backend/`, `packages/FenrirRenderer/src/Core/RendererCore.*`, `packages/FenrirRenderer/src/Facade/`, `packages/FenrirRenderer/include/FenrirRenderer/Renderer.hpp`, `packages/FenrirRenderer/Tests/`, `packages/FenrirRenderer/README.md`, `examples/LightingDemo/src/LightingPanel.*`, `examples/LightingDemo/src/main.cpp`

**Status:** needs-triage

- [ ] Null device unit tests: with timing off nothing comes back; with it on, a frame's labels come back in submit order, with zero times, exactly when the frame is `framesInFlight` frames old, and only the newest arrived frame is returned; turning timing off drops pending frames.
- [ ] Null `RendererProbe`: `GpuTimes()` is unavailable for the first `framesInFlight` frames, then lists the run passes' names in first-run order; two cameras give `Opaque` with `passes` 2; bloom's chain gives one `BloomDownsample` entry counting its levels; culled passes are absent; timing off leaves it unavailable.
- [ ] GL test: with timing on, times arrive within a few frames, every pass's milliseconds is at least 0, and the total is at least the largest pass's; with it off, the backend issues no queries (no query sets made).
- [ ] `RendererConfig::gpuTiming` defaults to off; `SetGpuTiming` on an invalid owner does nothing and returns false.

Hand check: LightingDemo's neon city shows the GPU times table, the total near what RenderDoc's frame capture reports for the same view, and F turns it off and on.

- [ ] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.

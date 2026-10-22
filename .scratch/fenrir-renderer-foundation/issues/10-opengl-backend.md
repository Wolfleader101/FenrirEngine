# 10: The OpenGL 4.6 backend and GL pixel probes

**What to build:** The graphics device runs on OpenGL 4.6 core through a FenrirWindow surface, and GL tests read pixels back from small offscreen targets, on developers' GPUs and on CI's llvmpipe (spec: "FenrirRenderer: layers and the graphics device" (OpenGL backend, surface and present), "Build" (the GL loader), "Testing Decisions" seam 2 (how the tests run); ADR-0016, ADR-0020, ADR-0022, ticket 20). The test cases that draw scenes arrive with the features that draw them (tickets 11, 13, 14, 19).

- **glad 2** (GL 4.6 core, `--mx`), generated once by hand and checked in under `packages/FenrirRenderer/libs/glad/` with a README holding the command; a static target `FenrirGlad` linked `PRIVATE` by the OpenGL backend only. No Python in the build.
- **The backend** in `src/Backend/OpenGL/`, the only directory including GL or its loader: a 4.6 core context through `Window`'s surface functions; DSA, immutable storage, samplers, `glBindBufferRange`, `glBindTextureUnit`, clip control, SPIR-V ingestion (`glSpecializeShader`) using the shared flattening formula; one linked program per vertex and fragment pair.
- **Clip origin per pass** (spike lesson 3): upper-left for offscreen targets, stored top row first like uploaded images; lower-left for passes drawing to the window; front faces counter-clockwise in both.
- **Validation:** with debug validation, a `KHR_debug` context with a synchronous callback routing messages to FenrirLogger.
- **Swap interval:** `Off`, `On`, `Adaptive` through the surface, an unsupported mode falling back to `On` and logged.
- **A backend that cannot start** reports why (no 4.6 core context, with SDL's message), so ticket 11's `Install` can fail with it.
- **GL test harness:** the context made the app's way, through FenrirWindow with a hidden window, never presented; a probe helper that renders into about 64×64, reads back and checks chosen pixels within a per-channel tolerance, hiding the read-back orientation; a GL error or high-severity message fails the test that caused it; without a 4.6 context the tests report skipped (CTest `SKIP_RETURN_CODE`). No golden images.
- **CI:** a Linux job running the GL suite on llvmpipe 4.6, on the newest Ubuntu runner if its Mesa reports 4.6, else a newer-Mesa PPA, under `xvfb-run` if a display is needed. The job prints the GL version; `MESA_GL_VERSION_OVERRIDE` is never used. Windows CI runs null tests only.

**Blocked by:** 09 (The renderer core, the graphics device and the null backend).

**Touches:** `packages/FenrirRenderer/`, `.github/workflows/build-and-test.yml`, `README.md`

**Status:** resolved

- [x] GL tests (skipped without 4.6): clearing an offscreen target to a colour reads back that colour; a pass drawing a full-screen triangle from an embedded test container reads back its colour; the top row of an offscreen target is row 0 of the read-back.
- [x] A deliberately bad GL call under validation fails its test through the debug callback.
- [x] Deferred destruction frees GL objects after `framesInFlight` frames.
- [ ] The CI Linux job prints a 4.6 llvmpipe version and runs the GL suite green; Windows CI runs the null tests.
- [x] No GL or glad include is reachable outside `src/Backend/OpenGL/` and `libs/glad`; Clang and MSVC x64 debug and release presets build with no new warnings.
- [x] The GL suite passes on the developer's own GPU (checked by hand).

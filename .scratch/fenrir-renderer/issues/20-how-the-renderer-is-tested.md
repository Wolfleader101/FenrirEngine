# How are the renderer and window packages tested?

Type: grilling
Status: resolved
Blocked by: 08

## Question

How do FenrirRenderer, FenrirWindow and the input package get tested, given that physics tests against real Jolt (ADR-0010)?

- Against a real OpenGL context (hidden window, or a headless or software context such as Mesa llvmpipe in CI), against a null backend, or both?
- What is asserted: GPU resource lifetimes, draw lists, pixels read back (image comparison and its tolerance)?
- What CI can run on Windows and Linux, and what stays a hand check in the demo apps.

## Answer

No ADR: the null backend that makes this possible is ADR-0016's, and the rest is test layout that is cheap to change. While resolving it the OpenGL floor was raised to 4.6 core (ADR-0016 amended).

- **Two backends, unlike physics.** ADR-0010 tests physics against real Jolt only, because Jolt runs everywhere; a GPU does not. So FenrirRenderer's tests are mostly null-backend tests, run on every machine and preset, plus a smaller OpenGL suite that renders the same kinds of scene and reads pixels back.
- **Null tests go through the public interface and a `RendererProbe`.** As `PhysicsProbe`: a friend struct in `tests/` that reads the `CommandList`s submitted to the null backend and the graphics device's live handles. They assert draw lists (which draws, in which pass, in which order, with which pipeline and bindings), resource lifetimes (creation, stale-handle detection, destruction deferred by frame) and frame sequencing. No test includes a private header; a `PublicHeaders.cpp` fails the build if GL or its loader reaches a public header, as physics does for Jolt.
- **GL tests assert pixels by analytic probes, no stored images.** Each renders into a small offscreen target (e.g. 64x64), reads it back and checks colours at chosen pixels within a per-channel tolerance: the clear colour, an unlit quad's colour, depth order, a lit face against N·L with ambient. Read-back is upside down under the upper-left origin (`research/opengl-target`); the probe helper hides that. Golden images wait for the first feature family that cannot be probed this way (map, Not yet specified).
- **GL errors fail.** GL tests run with debug validation on (a `KHR_debug` context, synchronous callback); a GL error or a high-severity message fails the test that caused it, lower severities go to FenrirLogger.
- **The context comes the app's way.** GL tests create it as ticket 11 decides for apps (through FenrirWindow, e.g. a hidden window) and never present; no test-only context code. If that needs a display on Linux, CI runs the GL tests under `xvfb-run`.
- **CI.** Null tests on every preset. GL tests on Linux on llvmpipe at 4.6: the newest Ubuntu runner (`ubuntu-26.04`) if its Mesa's llvmpipe reports 4.6 (confirmed from Mesa 26.2; 26.0 and 26.1 unchecked), else that job adds a newer-Mesa PPA such as kisak-mesa; the job prints the reported version. Without a 4.6 context the GL tests report skipped (CTest `SKIP_RETURN_CODE`), never passed, so Windows CI runs the null tests only; developers run the GL suite on their own GPU before a PR. No `MESA_GL_VERSION_OVERRIDE`: it would claim features llvmpipe lacks.
- **Input is tested by events, the window thinly.** The input package is tested headlessly by feeding it the neutral events FenrirWindow emits (key down and up, mouse move, focus lost) and checking per-frame state and edges (pressed this frame, released, held). FenrirWindow gets a few tests (create hidden, resize, close) that skip without a display. Real devices, focus and DPI are hand checks in the demo apps.
- **Demo apps get smoke runs.** Each demo app has a smoke mode that runs a fixed number of frames of each scene and exits, as the DemoApp's smoke run does today. CI runs it on the null backend everywhere and on llvmpipe on Linux, catching crashes and GL errors; whether a scene looks right stays a hand check.
- **Revisit** if analytic probes start missing real regressions (then golden images, perhaps generated on llvmpipe only), or if a GPU runner becomes available to CI.

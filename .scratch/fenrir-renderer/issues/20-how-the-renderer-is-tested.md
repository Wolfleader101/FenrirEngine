# How are the renderer and window packages tested?

Type: grilling
Status: claimed
Blocked by: 08

## Question

How do FenrirRenderer, FenrirWindow and the input package get tested, given that physics tests against real Jolt (ADR-0010)?

- Against a real OpenGL context (hidden window, or a headless or software context such as Mesa llvmpipe in CI), against a null backend, or both?
- What is asserted: GPU resource lifetimes, draw lists, pixels read back (image comparison and its tolerance)?
- What CI can run on Windows and Linux, and what stays a hand check in the demo apps.

# How are the demo apps split, and what does each show?

Type: grilling
Status: claimed
Blocked by: 14, 16

## Question

The single DemoApp is replaced by one demo app per feature area on a shared scaffold. How?

- The scaffold: what it holds (window, renderer, free-fly camera, scene switching, smoke-run switches like `FENRIR_DEMO_SCENE`), and whether it is a library under `examples/` or a CMake helper.
- The renderer demo app: what it shows (meshes, both materials, cameras and render targets, skybox, debug lines and text, C++ and C# scenes).
- The physics demo apps: one app or one per family; the joints, character and vehicle scenes rewritten on the new stack, C++ and C# alike; the hand-check lists they inherit.
- Where the ImGui demo editor sits among them, and when `examples/DemoApp` is deleted.

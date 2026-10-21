# How is a frame built and submitted?

Type: grilling
Status: open
Blocked by: 05, 08

## Question

How does FenrirRenderer turn the active scene into a frame?

- Fixed passes or a render graph in the foundation; what passes the foundation has (opaque, skybox or background, debug lines and text, the UI library's overlay) and how a later family (shadows, post-processing) adds one.
- Main thread or a render thread; does the renderer read the ECS directly or extract into its own copy?
- Which FenrirApp phases the renderer's systems run in, relative to `AfterTicks` (interpolated transforms), C# `Update` systems and window event pumping; and where present happens.
- Sorting and batching, and what is left out of the foundation.

# What does the renderer offer a UI library drawn over it?

Type: grilling
Status: open
Blocked by: 10, 11

## Question

ImGui lives in a demo editor for now and will later be replaced by the user's own GUI library, so the renderer builds no UI in. What does FenrirRenderer (and FenrirWindow or the input package) offer a UI library that draws over the frame?

- What the library hands the renderer (vertex and index data, textures, scissor rectangles, draw commands) and through which seam (the graphics seam if it is public, or a narrower overlay interface).
- How the library gets input and can claim it (a UI capturing the mouse from game code).
- How the scene can be drawn into a texture the UI shows (an editor's scene view).
- Where the demo's ImGui integration lives (demo scaffold or the demo editor).

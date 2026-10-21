# How are meshes, textures, shaders, skeletons and clips loaded and owned?

Type: grilling
Status: claimed
Blocked by: 06, 08

## Question

How does game code get a mesh, texture, shader, skeleton or animation clip, and who owns it?

- One asset package (FenrirAssets?) or each package loading its own? Which importer, behind which seam?
- How components name assets (typed handles), how assets are shared and unloaded, and how a mesh made in code (or from physics data) uses the same handles.
- Synchronous or asynchronous loading in the foundation; when the GPU upload happens.
- Which formats the foundation reads (glTF? OBJ? images?), and where paths are resolved from.

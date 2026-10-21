# What are materials, and how do they bind shaders and parameters?

Type: grilling
Status: resolved
Blocked by: 12, 15

## Question

What is a material in FenrirRenderer, and how does it reach a shader?

- The foundation's two materials: unlit, and one simple lit material (one directional light and ambient, no shadows), built through the same general path the lighting spec will extend.
- Is a material an asset shared by many entities, a component value, or both (instances with overrides)?
- How parameters (colours, scalars, textures) are named and set, from C++ and C#, and how a user's own shader is added.
- Shader variants (lit or unlit, later skinned), and render state (blending, culling, depth).
- Where the directional light and ambient live (a component, a scene resource, renderer config).

## Answer

ADR: `docs/adr/0024-fenrirrenderer-materials-are-shared-assets-bound-through-a-reflected-material-set.md`. Glossary: Material, Material parameter, Blend mode, Material set.

- **What a material is:** one shared, plain-data `MaterialData` asset in FenrirAssets, generalised from ADR-0021's import shape: a shader handle (`builtin:lit`, `builtin:unlit` or a user's), a blend mode, `doubleSided`, and a name → value parameter table. Components hold an `AssetHandle<MaterialData>`; no per-entity overrides yet (make another material with `Add`).
- **Parameters:** `float`, `vec2`, `vec3`, `vec4`, colour, or a texture with its sampler description. Edited through FenrirAssets (`Replace` or an edit call), each edit bumping the revision so the prepare step re-uploads. Unknown names or wrong types are logged once and ignored; unset parameters take the shader's GLSL default, else zero. C# sets them by name the same way (details: ticket 22).
- **Binding sets:** 0 frame (time, directional light, ambient), 1 view (camera, viewport), 2 material (`MaterialParams` block at binding 0, then textures), 3 draw (storage buffer of world and normal matrices, indexed by base instance or `gl_DrawID`). Engine includes `Fenrir/Frame.glsl`, `View.glsl`, `Draw.glsl`, `Vertex.glsl` declare sets 0, 1, 3 and the vertex locations.
- **Reflection:** set 2's layout and name map come from the shader container's build-time reflection, narrowing ADR-0018's "declared, not reflected"; sets 0, 1 and 3 stay declared in C++ and checked in debug builds.
- **Render state:** blend mode `Opaque`, `Masked` (alpha cutoff), `AlphaBlend` or `Additive`, picking the render stage; depth test always, depth write for `Opaque` and `Masked`; `doubleSided` disables culling. Nothing else.
- **Built-ins:** unlit is base colour × texture × vertex colour; lit is metallic-roughness GGX with one directional light and constant ambient, no IBL or shadows. Parameters `baseColour`, `baseColourTexture`, `metallic`, `roughness`, `metallicRoughnessTexture`, `normalTexture`, `emissive`, `emissiveTexture`.
- **Lights:** a `DirectionalLight` component (colour, intensity) along its entity's −Z; first per active scene, more warned once. An `AmbientLight` scene resource (colour, intensity), dim grey by default. Intensities are plain multipliers; physical units come with exposure in post-processing.
- **Variants:** picked by the renderer from material and mesh (`ALPHA_MASK` from `Masked`, `SKINNED` reserved); missing textures bind defaults (white, flat normal, black emissive) rather than adding keys. User shaders declare their own keys; unknown ones stay off.
- **User shaders:** a full vertex and fragment program on the engine includes, its own set 2, built by `fenrir_add_shaders`, loaded as `ShaderData`; lighting is the shader's choice. No surface functions yet.
- **Import mapping:** unlit files to `builtin:unlit`, the rest to `builtin:lit`, standard names, alpha mode to blend mode, double-sidedness kept, occlusion dropped. Colour space by use (base colour and emissive sRGB, the rest linear), set by the first material using the image, conflicts warned.
- **Samplers:** per texture parameter, from the file or repeat, trilinear, anisotropy 8; deduplicated in the renderer core.
- **Pipelines:** keyed by program and variant, render state, vertex layout, target formats and MSAA; created lazily in the prepare step and cached while the shader lives. A missing vertex attribute binds a constant default (normal +Y, UV 0, white), logged once.
- **Later:** per-entity overrides, surface functions, physical light units (*Not yet specified*). No new tickets: which components carry a mesh and a material is ticket 14's.

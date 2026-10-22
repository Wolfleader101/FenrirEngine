# Where do a material parameter's defaults come from?

Type: grilling
Status: resolved
Blocked by: 25

## Question

ADR-0024 says a parameter a material leaves unset "takes the shader's GLSL default, or zero". The spike (ticket 25) found no such thing: a std140 `MaterialParams` block cannot hold initialisers, and SPIR-V reflection carries offsets and types, not values. It seeded the built-ins' defaults in C++ (base colour white, roughness 0.5) and zeroed everything else, so a user shader's colour left unset comes out black.

- Where defaults are written: in C++ beside the built-ins only, in an annotation in the GLSL that `FenrirShaderCompiler` reads into the container's reflection, in a sidecar file beside the shader, or nowhere (zero, documented).
- How C# and an editor learn them, and whether reading back an unset parameter gives the default.
- The wording that replaces ADR-0024's sentence (an amendment; a new ADR only if the answer changes the material model).

## Answer

No new ADR: an amendment to ADR-0024 (2026-10-21), which gains a "Parameter defaults" section, with its parameters bullet and texture-default sentence amended. Glossary: Material parameter updated; Parameter default and Parameter override added.

- **Where defaults are written:** in the shader, as `#pragma fenrir default(name, values…)` beside the `MaterialParams` block or a sampler. A pragma survives preprocessing, so it follows includes and variants. A member with no pragma is zero, without a warning. There is no sidecar file and no C++ seeding.
- **Checked per variant:** `FenrirShaderCompiler` reads the pragmas from each variant's preprocessed source and checks them against that variant's final reflection (type, component count). It is a build error to name an unknown member, to write a malformed pragma, to give the wrong number of values, to give one name two pragmas in a stage, for stages to disagree, or to put a default on an array, struct or matrix member (none is a parameter type). Each variant's container entry carries its own defaults.
- **Default block image:** the container also stores the `MaterialParams` block packed with its defaults at the reflected std140 offsets and strides. The prepare step copies it, then writes the overrides over it.
- **Textures:** a pragma names `white`, `black` or `normal`, else white. The renderer owns one shared 1×1 texture of each. `normal` is the flat tangent-space (0.5, 0.5, 1) in linear, in `normalTexture`'s convention. Each is sampled with the parameter's default sampler.
- **Built-ins:** `lit.frag` and `unlit.frag` declare `baseColour` white, `emissive` black, `metallic` 0 and `roughness` 0.5, and their texture defaults, by pragma. The spike's C++ seeding goes. The importer keeps writing every factor a file gives, or that format's default, so glTF keeps metallic 1 and roughness 1.
- **Overrides only:** a material stores only the values it sets (its parameter overrides). When a shader's default changes (a rebuild, later hot reload), unset parameters follow it and overrides stay. An override the new shader lacks is kept, logged once and ignored.
- **Reads, consistent in C++ and C#** (adds to ticket 22's API):
  - `TryGet…` returns the effective value, the override or else the default. It fails only for an unknown name or a wrong type.
  - `TryGetOverride…` returns only an override, and `IsSet(name)` says whether one exists.
  - `Reset(name)` removes an override. It bumps the revision only when it removed something; reads never bump it.
  - `Clone` copies overrides, not resolved defaults.
  - A shader's reflected parameters (name, type, default) can be read from its `ShaderData` alone. An editor can then show an inherited value as the shader's default and offer to reset to it.
- **Naming:** "override" here is a value relative to the shader's default. The deferred per-entity overrides of ADR-0024 would be the next layer above, and the glossary keeps the two apart.

No new tickets. The foundation spec writes the pragma grammar, the container fields and the exact C++ and C# signatures.

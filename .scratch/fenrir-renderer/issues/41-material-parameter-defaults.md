# Where do a material parameter's defaults come from?

Type: grilling
Status: open
Blocked by: 25

## Question

ADR-0024 says a parameter a material leaves unset "takes the shader's GLSL default, or zero". The spike (ticket 25) found no such thing: a std140 `MaterialParams` block cannot hold initialisers, and SPIR-V reflection carries offsets and types, not values. It seeded the built-ins' defaults in C++ (base colour white, roughness 0.5) and zeroed everything else, so a user shader's colour left unset comes out black.

- Where defaults are written: in C++ beside the built-ins only, in an annotation in the GLSL that `FenrirShaderCompiler` reads into the container's reflection, in a sidecar file beside the shader, or nowhere (zero, documented).
- How C# and an editor learn them, and whether reading back an unset parameter gives the default.
- The wording that replaces ADR-0024's sentence (an amendment; a new ADR only if the answer changes the material model).

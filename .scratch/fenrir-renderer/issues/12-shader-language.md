# Which shader language do we write, and how is it compiled for each backend?

Type: grilling
Status: claimed
Blocked by: 04, 08

## Question

Which language are FenrirRenderer's shaders (and users' shaders) written in, and how do they reach each backend? **ADR.**

- GLSL only for now, or one language translated per backend (GLSL or HLSL or Slang through SPIR-V)?
- Compiled at build time or at runtime; is hot reload wanted in the foundation?
- How are resource bindings and material parameters discovered (reflection or declared by hand)?
- Is the compiler a build-time tool only, or linked into the engine (and then `PRIVATE`)?

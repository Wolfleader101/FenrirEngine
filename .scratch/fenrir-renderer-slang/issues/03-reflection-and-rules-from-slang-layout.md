# 03: Reflection and the rules come from Slang's program layout, never from its SPIR-V

**What to build:** a Slang stage's `StageReflection` is read from Slang's `ProgramLayout` (ADR-0073, "Reflection comes from Slang's program layout, not from the SPIR-V"), and ADR-0018's rules are checked on that layout with their messages kept. Slang strips a resource a stage never reads, and `GlBindingsOf` numbers bindings by their index in the variant, so reflecting the SPIR-V would move ADR-0057's indirection buffer one GL point down in every non-`SKINNED` mesh variant (research §2.3, trap 4). The layout lists every declared parameter, used or not.

- **`SlangReflection.cpp`** fills the same `StageReflection` `Reflection.cpp` fills from SPIRV-Reflect: for every global parameter with a `descriptorTableSlot` binding, its set and binding, kind (`ConstantBuffer<T>` a uniform block, `StructuredBuffer<T>` and `RWStructuredBuffer<T>` a storage block, `Sampler2D`, `Sampler2DShadow`, `SamplerCube`, `SamplerCubeArrayShadow`, `Sampler2DMS` a texture) and name: a block by its element type's name (`MaterialParams`, `Target2D`, `ViewData`), so `Program.cpp`'s `MaterialBlockName` and `Target2DBlockName` matches hold, a texture by its variable name. A block's members with offset, size and `ShaderValueType`; a block's size as std140 or std430 lays it out, and a block ending in an unbounded array as the array's offset plus its stride, as `Reflection.cpp` computes it (ADR-0024, ADR-0035). The vertex stage's inputs by `[vk::location]`.
- **Layout parity:** a test compiles `material.frag`'s GLSL and its Slang twin and asserts the two containers' `materialMembers` (names, types, offsets, sizes) and `bindings` are equal, and does the same for a storage block with a runtime array (`DrawData`'s 192-byte record). A Slang block whose layout differs from std140 is a tool error, not a silent difference.
- **The rules on the layout**, each a `Diagnostics` error naming the file and the declaration's line (Slang gives source locations for declarations):
  - a resource with no `[vk::binding]` ("'picture' has no [vk::binding(binding, set)]: every resource names its binding set and binding", the GLSL message's shape): Slang assigns a set and binding to such a resource itself, so the check reads the declaration's attribute through the reflection API's modifiers and refuses any resource whose binding was not written in source (ADR-0073, "Rules move from glslang's AST to Slang's reflection");
  - a push constant (an entry-point `uniform` parameter, or `[vk::push_constant]`);
  - a resource kind the engine does not bind (a separate texture or sampler, a storage image, a texel buffer, an array of resources), with `Reflection.cpp`'s wording;
  - more than `ShaderBindingsPerSet` of a kind in a set, checked in `MergeReflection` as now;
  - a stage output or vertex input with no `[vk::location]`.
- **The instance index:** a `.slang` stage may read `SV_InstanceID`, `SV_VulkanInstanceID` or `SV_StartInstanceLocation` only in the module `Fenrir.Draw` (ticket 06 writes it; this ticket enforces the rule by module name through the layout's entry-point parameters and the IR's global usage, with Slang's reflection of varying inputs). Anywhere else is refused with `RuleChecker`'s message ("read the draw's record with Fenrir.Draw's CurrentDraw() instead").
- **Must-fail fixtures:** `Tests/fixtures/errors/*.frag.slang` twins of `no_set`, `no_location`, `nine_textures`, `instance_index`, and a new `push_constant.frag.slang`, each a `fenrir_shader_build_fails` case in `Tests/CMakeLists.txt` with its expected message.

**Blocked by:** 02 (The Slang front end).

**Touches:** `packages/FenrirRenderer/Tools/ShaderCompiler/src/`, `packages/FenrirRenderer/Tools/ShaderCompiler/Tests/`

**Status:** ready-for-agent

- [ ] A Slang fragment declaring two textures and reading one reflects both, and its GL copy binds the read one where glslang's copy of the GLSL twin does (the research's `gap.frag`, as a test).
- [ ] `material.frag.slang` and `material.frag` give equal `bindings` and `materialMembers`; a `DrawData`-shaped storage block reflects a 192-byte record.
- [ ] Each must-fail fixture fails with its expected message and line.
- [ ] `MaterialReflection.cpp`'s cases pass against the Slang twin of `material.frag`.

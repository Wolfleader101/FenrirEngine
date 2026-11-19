# 04: Material defaults are `[FenrirDefault]` and `[FenrirDefaultTexture]` attributes

**What to build:** a Slang material shader declares its parameter defaults as attributes on the declarations they describe, and the tool reads them from Slang's user-attribute reflection into the same `ShaderParameter` defaults and default block image the pragma gives today (ADR-0073, "Material defaults are attributes"; ADR-0024 amended). GLSL stages keep `#pragma fenrir default` until ticket 12.

- **The attributes**, declared once in a module the engine ships, `Fenrir.Material` (`Shaders/include/Fenrir/Material.slang`, the first module in the tree; ticket 06 adds the rest): `[__AttributeUsage(_AttributeTargets.Var)] struct FenrirDefaultAttribute { float x; float y; float z; float w; }` and `struct FenrirDefaultTextureAttribute { string value; }`. A `float` member takes `[FenrirDefault(0.5)]`, a `vec4` `[FenrirDefault(1, 1, 1, 1)]`: Slang fills missing trailing arguments only if the struct gives defaults, so the attribute struct's fields default to 0 and the tool checks the count written against the member's components (the "wrong value count" message). A texture takes `[FenrirDefaultTexture("white")]`, `"black"` or `"normal"`.
- **Reading them:** `SlangReflection.cpp` (ticket 03) collects, per stage, every `FenrirDefault` on a `MaterialParams` member and every `FenrirDefaultTexture` on a texture variable, as `DefaultPragma` values with the declaration's file and line, so `Program.cpp`'s `ApplyDefaults` runs unchanged: unknown member, value count, two defaults for one name in a stage, stages disagreeing, a default on an array, struct or matrix, a texture default that is not one of the three words. The attribute's argument count comes from Slang's `getArgumentCount`, so `[FenrirDefault(1, 1, 1)]` on a `vec4` is the count error, not a silent zero.
- **Per variant:** an attribute inside `#ifdef ALPHA_MASK` exists only in the variants that define it, as the pragma did, because the preprocessor runs before Slang's parse.
- **Fixtures:** a Slang twin of `material.frag` with its defaults as attributes, and twins of the pragma error fixtures (`pragma_unknown`, `pragma_count`, `pragma_twice`, `pragma_disagree`, `pragma_array`, `pragma_matrix`, `pragma_struct`, `pragma_malformed` as a wrong argument type) under `errors/`, each a `fenrir_shader_build_fails` case.
- **Docs:** ADR-0024's "Parameter defaults" section gets an amendment line naming the attributes; `packages/FenrirRenderer/README.md`'s "User shaders" shows both spellings until ticket 12.

**Blocked by:** 03 (Reflection and the rules from Slang's program layout).

**Touches:** `packages/FenrirRenderer/Tools/ShaderCompiler/src/`, `packages/FenrirRenderer/Tools/ShaderCompiler/Tests/`, `packages/FenrirRenderer/Shaders/include/Fenrir/Material.slang` (new), `docs/adr/0024-*.md`, `packages/FenrirRenderer/README.md`

**Status:** ready-for-agent

- [ ] `material.slang` builds to a container whose `parameters` and `defaultBlock` equal `material.frag`'s byte for byte.
- [ ] Each attribute error fixture fails with the pragma fixture's message, at the attribute's line.
- [ ] A default under `#ifdef ALPHA_MASK` is in the `ALPHA_MASK` variant's parameters and absent from the other.
- [ ] `FenrirAssetsTests`' `ShaderParameters` cases pass on the Slang twin's container, and C# `Shaders.GetParameter` reads the same defaults (the scripting fixture, run on both containers).

# 03: `[NativeFunction]` and its generator

**What to build:** a contract author declares a native function once in C#, as a `static partial` method marked `[NativeFunction]`, and calls it like any other method (spec: `../spec.md`, "C#: `[NativeFunction]` and its generator").

A new source generator, next to the existing ones, emits two things:
- the unmanaged (Cdecl) function pointer field Iris binds, named by ticket 02's binding-name rule;
- the method body that converts the arguments.

How each type crosses:
- sized numbers, `float`, `double`, `bool`, `char` and pointers pass through;
- `string` and `ReadOnlySpan<char>` parameters become UTF-8 and a length, using ticket 01's Iris string parameters (a lone surrogate becomes U+FFFD);
- `Entity` crosses as its value;
- `in`, `ref` and `out` of an unmanaged struct cross as pointers.

A malformed declaration is a compile error with a FENRIR id. The test extension from ticket 02 moves from hand-written fields to generated declarations, and gains a string case.

**Blocked by:** 01 (Iris: string parameters for internal calls), 02 (The scripting extension seam, with a test extension).

**Status:** ready-for-agent

- [ ] The scripting contract gains `[NativeFunction]` for `static partial` methods in partial classes. It works in any assembly Iris binds.
- [ ] For each declaration the generator emits the bound field, named by the binding-name rule, and a body that marshals as listed above. Short text is encoded on the stack, as `Log` does now.
- [ ] Compile errors, with new FENRIR ids numbered after the existing ones, for:
  - a method that is not static;
  - a method that is not partial;
  - a containing type that is not partial;
  - a generic method or a generic containing type;
  - overloads of a native function;
  - a `string` result;
  - a parameter or result type that cannot cross.
- [ ] Each diagnostic has a compile-fail case in the existing `CompileFail` project and `RefCompile-*` CTest loop, plus a control case that compiles.
- [ ] The test extension's C# contract uses `[NativeFunction]` declarations only, with no hand-written fields. Its existing scripting tests still pass.
- [ ] A new scripting test passes a `string` (multi-byte UTF-8 and empty) to a native function that takes `std::string_view`, and checks what arrived.
- [ ] The FenrirScripting README's "Adding an engine API" section becomes "Exposing a native function": the C++ function, the C# declaration, and listing them in an extension.
- [ ] The full CTest suite passes.

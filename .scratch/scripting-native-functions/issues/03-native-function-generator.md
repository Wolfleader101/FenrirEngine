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

**Status:** resolved

- [x] The scripting contract gains `[NativeFunction]` for `static partial` methods in partial classes. It works in any assembly Iris binds.
- [x] For each declaration the generator emits the bound field, named by the binding-name rule, and a body that marshals as listed above. Short text is encoded on the stack, as `Log` does now.
- [x] Compile errors, with new FENRIR ids numbered after the existing ones, for:
  - a method that is not static;
  - a method that is not partial;
  - a containing type that is not partial;
  - a generic method or a generic containing type;
  - overloads of a native function;
  - a `string` result;
  - a parameter or result type that cannot cross.
- [x] Each diagnostic has a compile-fail case in the existing `CompileFail` project and `RefCompile-*` CTest loop, plus a control case that compiles.
- [x] The test extension's C# contract uses `[NativeFunction]` declarations only, with no hand-written fields. Its existing scripting tests still pass.
- [x] A new scripting test passes a `string` (multi-byte UTF-8 and empty) to a native function that takes `std::string_view`, and checks what arrived.
- [x] The FenrirScripting README's "Adding an engine API" section becomes "Exposing a native function": the C++ function, the C# declaration, and listing them in an extension.
- [x] The full CTest suite passes.

## Comments

Done. The generator copies each declaration's signature, together with the `using` directives of its file, into the implementing part. A declaration it refuses still gets a body that throws, so the build reports only the FENRIR error and not also a partial method with no implementation.

The exception is FENRIR0022 (a containing type that is not partial). Nothing can be generated inside such a type, so the compiler adds its own error. For a nested method that is CS8795, "must have an implementation part". `ExpectCompile.cmake` gained an optional `ALSO` code for exactly that case.

The README's "Exposing a native function" section describes the new process. "Adding an engine API" stays, marked as the core's current process, until ticket 04 moves the core over.

Review follow-ups: fixed here are parameters named by C# keywords (`@class`), a `ref` result (now FENRIR0026), and the comments that state the compile-test rule.

Follow-ups, done afterwards:
- **Generic containing type.** The container headers now carry the type parameters, so the refusal stub compiles and only FENRIR0023 is reported. Pinned by `RefCompile-NativeFunctionGenericType`, which failed before the change.
- **Partial type split across files.** One generated part is written per declaring file, with that file's `using` directives, and overloads are still found across all of a type's files. Pinned by `RefCompile-NativeFunctionTwoFiles` (two files giving one alias two meanings), which failed before the change.
- **Lone surrogate.** A scripting test sends text with a lone surrogate through the generated body and checks C++ receives U+FFFD. It was a missing test, not a bug. It has to be sent from C#, because UTF-8 from C++ cannot carry one.
- **Stack buffer size.** It is now 512 bytes, as in `Log`.
- **Generator cleanup.** `LocationInfo` and `DiagnosticInfo` moved out of `ComponentFieldsGenerator` into a shared file and the native function generator uses them, so it no longer carries its own copy and its `Report` switch on id strings is gone. The container chain is walked in one place, `Model` is split into a `Part` (where the code goes) and the declaration's own data, the `ref readonly` value is a named constant, and diagnostic arguments are an array.

A regression from the per-file change was found in review and fixed: one file opening the same namespace twice, each time with its own `using`s, made two parts with one hint name, so the generator threw. The hint name now hashes the namespace usings as well as the path. Pinned by `RefCompile-NativeFunctionTwoNamespaceBlocks`, which failed before the fix.

Not changed: `EventGenerator` and `SystemGenerator` still hold their own location data inline, and the `Error(...)` descriptor helper stays (shorter than the sibling generators' `new(...)`).

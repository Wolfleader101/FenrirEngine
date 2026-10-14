# 01: Built-in extension carries the engine components and the scripting contract

**What to build:** The engine's own pieces come into scripting through the built-in scripting extension, just as a package's pieces do. Today the engine components Transform, Name and Relationship are added to the registry by a step of their own before any extension is read. The Fenrir.Scripting contract is also shared by a separate step, ahead of everything else. After this ticket:
- the built-in extension carries the engine components, with today's mappings and flags (Transform and Name required, Relationship required and read-only);
- it also carries the contract from the bridge directory as its shared assembly;
- Install turns all its sources into registrations in one loop, in order.

The built-in extension is built once the bridge directory is known. It stays first among the extensions, so:
- the engine components are registered before anyone else's, and no extension can take their names;
- the contract is shared before every other shared assembly.

Every component or event diagnostic names its source, for example "extension 'built-in'" or "extension 'X'".

This is a prefactor. The config's own lists still work, folded through the same loop. Nothing an integrator writes changes, which makes ticket 02 a pure deletion. See the spec, `.scratch/scripting-extension-only/spec.md`, "The built-in extension" and "Install's assembly of extensions".

**Blocked by:** None (can start immediately).

**Status:** resolved

- [x] The built-in extension holds the built-in native functions (unchanged), Transform, Name and Relationship, and the Fenrir.Scripting contract as its only shared assembly.
- [x] The function that today adds the engine components to the registry instead returns their registrations for the built-in extension. The mappings and layout `static_assert`s stay with it.
- [x] Install has no separate engine-component step and no separate contract-first sharing step. The host's shared assemblies are every source's, in order, with the contract first because the built-in extension is first.
- [x] A missing Fenrir.Scripting contract in the bridge directory still fails Install with today's diagnostic, checked before the built-in extension is built.
- [x] A component or event diagnostic from Install names the extension it came from.
- [x] New test: after Install, `FindComponent` finds Fenrir.Transform and Fenrir.Name as required, and Fenrir.Relationship as required and read-only.
- [x] New test: an extension that declares a component named "Fenrir.Transform" fails Install. The log names that extension, and the app keeps running without scripts.
- [x] The test extension, whose contract refers to Fenrir.Scripting, still loads and its native functions still work.
- [x] Every existing scripting test passes, both the .NET-backed tests and the tests that need no .NET.

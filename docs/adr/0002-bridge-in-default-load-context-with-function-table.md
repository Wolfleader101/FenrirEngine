---
status: accepted
---

# The bridge lives in the default load context and exposes one function table

The native host loads the managed bridge with `hdt_load_assembly`, which puts it in the default `AssemblyLoadContext`. It then resolves a single entry point, `Bootstrap.GetBridgeFunctions`, through `hdt_get_function_pointer`. That entry point fills a versioned table of Cdecl function pointers, one per bridge export, after checking the caller's version and table size.

We chose this over the original `load_assembly_and_get_function_pointer`, which puts every assembly path in its own isolated load context and resolves methods one by one by name:

- **One shared bridge.** In an isolated context, two bridge paths meant two bridges with separate registries, and the bridge was shared with components only because of a name special case. The scripting API that scripts compile against needs exactly one copy. The default context gives us that, and matches how the managed checks already run the bridge.
- **Exports stay cheap.** Looking up each method by name meant a by-name resolution, a duplicated per-platform name string and a readiness flag for every export. With the table, adding an export is a new field on both sides plus a version bump, and the export's own work. The managed side type-checks every signature when it assigns `&Method`, and the export's body is only its work: the managed export envelope (`DiagnosticTail`, `ExceptionMapping`, `ExportEnvelope`) validates the diagnostic tail, decodes names, maps lookups and exceptions to statuses, and writes the diagnostic. The steps, then, are: the field in the native and managed table declarations and the name in the native name list (with `BridgeVersion` raised natively and `BridgeFunctions.Version` managed), the assignment in `GetBridgeFunctions`, the export's work using the envelope, a row in the managed export-contract table, and a slot in the test fake's list (a compile-time count check fails until it is added).

## Consequences

- A process can hold only one bridge assembly. Initializing with a different bridge file after one is loaded fails with a diagnostic. Test variants of the bridge therefore run in separate processes.
- The bridge's public surface is just `GetBridgeFunctions`. The individual exports are `internal`.
- The two native/managed declarations of the table must stay in order manually. Size assertions catch a different slot count; slot order is caught by the behaviour checks calling every slot.

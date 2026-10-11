# Glossary

The terms this repository uses, and the words to avoid for them. When a name in code, an issue or a test describes one of these concepts, use the term defined here.

## Iris (.NET hosting)

Iris is the engine-independent C++ library in `packages/Iris` that hosts .NET. These terms are its vocabulary; FenrirEngine-specific terms such as *script* build on them.

**Host**
The native object (`Iris::Host`) that starts or joins the .NET runtime and connects to the bridge. Creating one does not start .NET; `Initialize` does. The runtime and bridge stay loaded for the rest of the process whatever happens to the host.
*Avoid:* runtime (the runtime is CoreCLR itself, which outlives every host), engine.

**Runtime**
CoreCLR, the .NET runtime, which Iris starts through hostfxr (found with nethost). There is one per process, and it is never unloaded.

**Bridge**
The managed assembly `Iris.Bridge`. It loads once into the default load context and stays resident for the life of the process. Native code reaches managed code only through it.
*Avoid:* managed host, interop layer.

**Bridge function table, handshake**
The bridge's single public entry point fills a versioned table of function pointers. That exchange is the *handshake*. A version or size mismatch fails `Initialize`. The table is private to Iris, and changing it bumps the bridge version: it is not part of the public API.

**Component**
A managed assembly loaded with `Host::LoadComponent` into its own collectible load context, together with its private dependencies. In code, `Iris::Component` is the *owner* of one loaded component. FenrirEngine scripts will live in components.
*Avoid:* plugin, module, script assembly (a *script* is an engine concept built on top).

**Load context**
The .NET `AssemblyLoadContext` a component lives in. It is collectible, so it can be unloaded once nothing roots it.

**Shared assembly, contract**
An assembly named in `HostConfiguration::sharedAssemblies`. It loads once into the default load context, and every component's reference to it binds to that copy, so its types have one identity across components. Typically this is an API that components compile against (a *contract*). Sharing is permanent for the process (ADR-0004).

**Owner**
A move-only C++ object that holds something alive in the bridge and releases it when destroyed. The owners are `Component`, `Object` and `Array`. They can't be copied. A moved-from owner is empty: its calls report `InvalidHandle`, and releasing it again succeeds.
*Avoid:* wrapper, reference.

**Handle**
A cheap, copyable C++ value that names something in a loaded component without keeping it alive. The handles are `Type`, `Method`, `InstanceMethod` and `Member`. A handle is validated on every call. It stops working (`InvalidHandle`) when its component is released and is never reused. Handles are opaque numbers, never CLR addresses or `GCHandle`s.
*Avoid:* pointer, reference, id (in API docs).

**Token**
The bridge's internal number for one loaded component, and the thing every handle is bound to. Never reused. Not part of the public API.

**Generation**
One load of a component, identified by its token. Loading the same assembly again, or a new build of it, is a new generation. Handles from one generation never resolve in another. The code says "component token"; tickets say "generation".

**Signature**
The C++ function type that names a managed method, such as `std::int32_t(std::int32_t, std::int32_t)` for `int Add(int, int)`. Lookups match it exactly. The C++ types decide the managed types, and an unsupported type does not compile.

**Mapped struct**
A C++ struct with a `StructMapping` that mirrors a managed blittable struct field by field. The mapping is validated against the managed type the first time a component uses it.

**Object**
A managed object that native code owns through an `Iris::Object`. While the `Object` exists, the managed object is rooted and its component can't be released.

**Internal call**
A native function that managed component code calls, registered with `RegisterInternalCall`. Component code declares a public static unmanaged function pointer field, and the bridge binds the field to the registration by name when the component loads. This is called *binding*. A component with any unbound field fails to load.
*Avoid:* callback, P/Invoke, native export.

**Release**
Dropping the bridge's strong references to a component, object or array, which requests unloading (`Release()`, or destroying the owner). Release is explicit and immediate. `Component::Release` refuses with `InUse` while objects are alive, but destroying a `Component` always releases. That is a *forced release* (ADR-0005).
*Avoid:* unload (for the act of releasing), free, dispose.

**Collection**
The garbage collector actually freeing a released component's load context and assemblies. It happens later, only once nothing else roots the component. Iris observes it but never forces it.
*Avoid:* unload (ambiguous between release and collection).

**Unload status**
What `Component::GetUnloadState` observes:
- `NotRequested`: still loaded.
- `Pending`: released, not yet collected.
- `Collected`: gone.

**Root, retention**
A strong reference that keeps a released component from being collected, such as a live `Object`, a static field elsewhere, or a thread. Iris's own tables hold plain ids, never roots.

**Snapshot**
An owning copy of managed metadata returned to native code: a type list, base type, interfaces or attributes. A snapshot holds no reference to the component.

**Result, status, diagnostic**
Every Iris call returns a `CallResult<T>`. Its *status* (`CallStatus`) says what happened. Its value is present only on `Success`. Its *diagnostic* is owned UTF-8 text explaining a failure, which for `ManagedException` is the managed exception's `ToString()`. Iris reports failures as results, not C++ exceptions.

**Log sink**
The one process-wide native callback (`Host::SetLogSink`) that receives bridge events and exception reports with a `LogLevel`.

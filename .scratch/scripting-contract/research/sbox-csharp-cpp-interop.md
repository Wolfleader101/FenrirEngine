# How s&box, Coral and Mono share structs and functions between C++ and C#

## Question

FenrirEngine hosts .NET through Iris and shares two things with C# scripts: physics components (plain-data C++
structs mirrored as C# blittable structs, declared today as a C++ struct, an `IRIS_FIELD` `StructMapping` and a
hand-written C# struct) and native functions C# calls. This note records, from primary sources only, how three
other C#/C++ interop systems describe shared types, bind native functions, check layouts, keep defaults in
agreement and survive hot reload: s&box (Facepunch's Source 2 + .NET engine, `sbox-public`), Coral
(StudioCherno's hostfxr wrapper) and the Mono embedding API. It closes with .NET's own facilities
(`LibraryImport`, `StructLayout`, `Unsafe.SizeOf`, `Marshal.OffsetOf`, ClangSharp) and what all this suggests
for Iris. Everything verified is cited; anything not confirmed from a primary source is marked **unverified**.
Sources were read on 2026-10-22; GitHub links point at the default branch, so line numbers may drift.

## Sources

- s&box: [Facepunch/sbox-public](https://github.com/Facepunch/sbox-public) —
  [README.md](https://github.com/Facepunch/sbox-public/blob/master/README.md),
  [AGENTS.md](https://github.com/Facepunch/sbox-public/blob/master/AGENTS.md),
  [engine/Tools/InteropGen/docs/README.md](https://github.com/Facepunch/sbox-public/blob/master/engine/Tools/InteropGen/docs/README.md),
  [docs/def-files.md](https://github.com/Facepunch/sbox-public/blob/master/engine/Tools/InteropGen/docs/def-files.md),
  [docs/classes.md](https://github.com/Facepunch/sbox-public/blob/master/engine/Tools/InteropGen/docs/classes.md),
  [docs/types.md](https://github.com/Facepunch/sbox-public/blob/master/engine/Tools/InteropGen/docs/types.md),
  [Writer/ManagedWriter.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Tools/InteropGen/Writer/ManagedWriter.cs),
  [Writer/ManagedWriter.Imports.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Tools/InteropGen/Writer/ManagedWriter.Imports.cs),
  [Writer/ManagedWriter.Exports.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Tools/InteropGen/Writer/ManagedWriter.Exports.cs),
  [Writer/NativeWriter.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Tools/InteropGen/Writer/NativeWriter.cs),
  [Writer/NativeExportTable.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Tools/InteropGen/Writer/NativeExportTable.cs),
  [Pipeline/InteropPipeline.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Tools/InteropGen/Pipeline/InteropPipeline.cs),
  [Pipeline/Mangler.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Tools/InteropGen/Pipeline/Mangler.cs),
  [Program.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Tools/InteropGen/Program.cs),
  [engine/manifest.def](https://github.com/Facepunch/sbox-public/blob/master/engine/manifest.def),
  [engine/Definitions/engine.def](https://github.com/Facepunch/sbox-public/blob/master/engine/Definitions/engine.def),
  [Definitions/readme.txt](https://github.com/Facepunch/sbox-public/blob/master/engine/Definitions/readme.txt),
  [Definitions/engine/HandleIndex.def](https://github.com/Facepunch/sbox-public/blob/master/engine/Definitions/engine/HandleIndex.def),
  [Definitions/common/Physics/IPhysicsBody.def](https://github.com/Facepunch/sbox-public/blob/master/engine/Definitions/common/Physics/IPhysicsBody.def),
  [Definitions/common/Physics/IPhysicsWorld.def](https://github.com/Facepunch/sbox-public/blob/master/engine/Definitions/common/Physics/IPhysicsWorld.def),
  [Definitions/engine/Physics/PhysicsTrace.def](https://github.com/Facepunch/sbox-public/blob/master/engine/Definitions/engine/Physics/PhysicsTrace.def),
  [Definitions/engine/vphysics2.def](https://github.com/Facepunch/sbox-public/blob/master/engine/Definitions/engine/vphysics2.def),
  [Definitions/tier3/mathlib.def](https://github.com/Facepunch/sbox-public/blob/master/engine/Definitions/tier3/mathlib.def),
  [Sandbox.Engine/Core/Interop/Handle.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Sandbox.Engine/Core/Interop/Handle.cs),
  [Core/Interop/InteropSystem.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Sandbox.Engine/Core/Interop/InteropSystem.cs),
  [Core/Interop/Interop.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Sandbox.Engine/Core/Interop/Interop.cs),
  [Sandbox.NetCore/NetCore.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Sandbox.NetCore/NetCore.cs),
  [Systems/Physics/PhysicsTrace.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Sandbox.Engine/Systems/Physics/PhysicsTrace.cs),
  [Systems/Physics/3d/PhysicsBody3d.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Sandbox.Engine/Systems/Physics/3d/PhysicsBody3d.cs),
  [Systems/Physics/3d/PhysicsWorld3d.Trace.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Sandbox.Engine/Systems/Physics/3d/PhysicsWorld3d.Trace.cs),
  [Systems/Physics/PhysicsBody.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Sandbox.Engine/Systems/Physics/PhysicsBody.cs),
  [Scene/Components/Collider/Rigidbody.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Sandbox.Engine/Scene/Components/Collider/Rigidbody.cs),
  [Sandbox.Hotload/Hotload.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Sandbox.Hotload/Hotload.cs),
  [Sandbox.Reflection/TypeLibrary/LoadContext.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Sandbox.Reflection/TypeLibrary/LoadContext.cs).
- Coral: [StudioCherno/Coral](https://github.com/StudioCherno/Coral) —
  [Coral.Native/Source/Coral/HostInstance.cpp](https://github.com/StudioCherno/Coral/blob/main/Coral.Native/Source/Coral/HostInstance.cpp),
  [Coral.Native/Source/Coral/Assembly.cpp](https://github.com/StudioCherno/Coral/blob/main/Coral.Native/Source/Coral/Assembly.cpp),
  [Coral.Native/Include/Coral/Assembly.hpp](https://github.com/StudioCherno/Coral/blob/main/Coral.Native/Include/Coral/Assembly.hpp),
  [Coral.Native/Include/Coral/Type.hpp](https://github.com/StudioCherno/Coral/blob/main/Coral.Native/Include/Coral/Type.hpp),
  [Coral.Native/Include/Coral/TypeCache.hpp](https://github.com/StudioCherno/Coral/blob/main/Coral.Native/Include/Coral/TypeCache.hpp),
  [Coral.Native/Source/Coral/Verify.hpp](https://github.com/StudioCherno/Coral/blob/main/Coral.Native/Source/Coral/Verify.hpp),
  [Coral.Managed/Source/InternalCalls.cs](https://github.com/StudioCherno/Coral/blob/main/Coral.Managed/Source/InternalCalls.cs),
  [Coral.Managed/Source/TypeInterface.cs](https://github.com/StudioCherno/Coral/blob/main/Coral.Managed/Source/TypeInterface.cs),
  [Coral.Managed/Source/ManagedObject.cs](https://github.com/StudioCherno/Coral/blob/main/Coral.Managed/Source/ManagedObject.cs),
  [Coral.Managed/Source/InteropTypes.cs](https://github.com/StudioCherno/Coral/blob/main/Coral.Managed/Source/InteropTypes.cs),
  [Example/Example.Native/Source/Main.cpp](https://github.com/StudioCherno/Coral/blob/main/Example/Example.Native/Source/Main.cpp),
  [Example/Example.Managed/Source/Main.cs](https://github.com/StudioCherno/Coral/blob/main/Example/Example.Managed/Source/Main.cs).
- Mono: [Embedding Mono](https://www.mono-project.com/docs/advanced/embedding/),
  [Interop with Native Libraries (P/Invoke)](https://www.mono-project.com/docs/advanced/pinvoke/),
  [mono/metadata/icall.c](https://github.com/mono/mono/blob/main/mono/metadata/icall.c),
  [mono/metadata/icall-def.h](https://github.com/mono/mono/blob/main/mono/metadata/icall-def.h),
  [mono/metadata/debug-helpers.c](https://github.com/mono/mono/blob/main/mono/metadata/debug-helpers.c).
- .NET: [P/Invoke source generation](https://learn.microsoft.com/en-us/dotnet/standard/native-interop/pinvoke-source-generation),
  [ComWrappers source generation](https://learn.microsoft.com/en-us/dotnet/standard/native-interop/comwrappers-source-generation),
  [Native interop best practices](https://learn.microsoft.com/en-us/dotnet/standard/native-interop/best-practices),
  [Disabled runtime marshalling](https://learn.microsoft.com/en-us/dotnet/standard/native-interop/disabled-marshalling),
  [StructLayoutAttribute](https://learn.microsoft.com/en-us/dotnet/api/system.runtime.interopservices.structlayoutattribute),
  [Unsafe.SizeOf](https://learn.microsoft.com/en-us/dotnet/api/system.runtime.compilerservices.unsafe.sizeof),
  [Marshal.OffsetOf](https://learn.microsoft.com/en-us/dotnet/api/system.runtime.interopservices.marshal.offsetof),
  [dotnet/ClangSharp README](https://github.com/dotnet/ClangSharp/blob/main/README.md).
- Iris (this repo): `packages/Iris/README.md` ("Structs" section), `packages/FenrirScripting/include/FenrirScripting/EngineMappings.hpp`.

## A. s&box (Facepunch/sbox-public)

### A1. One definition, two generated sides: InteropGen and `.def` files

- The public repo holds only the managed engine, editor and tools; the native Source 2 core is downloaded as
  prebuilt binaries. "The managed side calls the native code through a generated interop layer", and agents
  must not hand-edit `Interop.*.cs` or `obj/.generated/` ([AGENTS.md](https://github.com/Facepunch/sbox-public/blob/master/AGENTS.md)).
  Pulling from git "regenerates the interop bindings" ([README.md](https://github.com/Facepunch/sbox-public/blob/master/README.md)).
- The generator is `engine/Tools/InteropGen` (`Program.cs`, `Parsers/`, `Definition/`, `Arguments/`, `Pipeline/`,
  `Writer/`, `docs/`). It "reads `.def` files (a small DSL describing the classes and structs that cross the
  native/managed boundary) and writes, for each def: a managed file (`cs` directive) - typed C# wrappers,
  `[UnmanagedCallersOnly]` exports and the `NativeInterop` bootstrap; a native header (`hpp` directive);
  a native source file (`cpp` directive) - exported thunks, import function pointers and the `igen_*`
  initializer". Output "is **not committed**" ([docs/README.md](https://github.com/Facepunch/sbox-public/blob/master/engine/Tools/InteropGen/docs/README.md)).
- Input: ~200 `.def` files under `engine/Definitions/` (e.g. `common/Physics/IPhysicsBody.def`,
  `engine/Physics/PhysicsTrace.def`, `tier3/mathlib.def`), listed by `engine/manifest.def` (six root defs:
  `tools.def`, `tools.assetsystem.def`, `tools.hammer.def`, `tools.modeldoc.def`, `tools.animgraph.def`, `engine.def`).
  The build (`Tools/SboxBuild`) calls `Facepunch.InteropGen.Program.ProcessManifest("engine")`, which processes
  every def in parallel ([Program.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Tools/InteropGen/Program.cs)).
- A root def names the binding set, the native DLL, output paths and the exception sink
  ([engine.def](https://github.com/Facepunch/sbox-public/blob/master/engine/Definitions/engine.def)):

  ```
  ident "engine"
  nativedll engine2.dll
  exceptions	"Sandbox.Interop.BindingException"
  namespace "Managed.SandboxEngine"
  cpp "../../src/engine2/interop.engine.cpp"
  hpp "../../src/engine2/interop.engine.h"
  cs	"../Sandbox.Engine/Interop.Engine.cs"
  include "engine/*"
  include "common/*"
  ```

- A class definition is C#-like signatures with no bodies, plus a C++ header include. Each class says which side
  *implements* it: `native class` is C++ imported into C#, `managed class` is C# exported to C++
  ([classes.md](https://github.com/Facepunch/sbox-public/blob/master/engine/Tools/InteropGen/docs/classes.md)).
  From [IPhysicsBody.def](https://github.com/Facepunch/sbox-public/blob/master/engine/Definitions/common/Physics/IPhysicsBody.def):

  ```
  #include "vphysics2/iphysicsbody.h"
  [Handle:Sandbox.PhysicsBody3d]
  native class IPhysicsBody as NativeEngine.IPhysicsBody
  {
  	IPhysicsWorld GetWorld();
  	void SetMass( float f );
  	float GetMass();
  	Vector3 GetPosition();
  	void SetTransform( Vector3 v, Rotation q );
  	IPhysicsShape AddHullShape( Vector3 position, Rotation rotation, int numVertices, CastTo[Vector*] void* vertices );
  	bool ComputePenetration( IPhysicsBody body, Transform transform, cref out Vector3 direction, cref out float distance );
  	inline IPhysicsBody ManagedObject() { return self; }
  }
  ```

  And the reverse direction ([HandleIndex.def](https://github.com/Facepunch/sbox-public/blob/master/engine/Definitions/engine/HandleIndex.def)):

  ```
  managed static class Sandbox.HandleIndex
  {
  	static int RegisterHandle( IntPtr ptr, uint type );
  	static void FreeHandle( int handle );
  }
  ```

- Shared value types are *not* described field by field. A struct is declared as a name pair only, and "both
  sides must define the type with an identical layout" ([classes.md](https://github.com/Facepunch/sbox-public/blob/master/engine/Tools/InteropGen/docs/classes.md)).
  [mathlib.def](https://github.com/Facepunch/sbox-public/blob/master/engine/Definitions/tier3/mathlib.def) is
  entirely such pairs: `native struct Vector is Vector3`, `native struct Quaternion is Rotation`,
  `native struct CTransformUnaligned is Transform`, `[small] native struct Color is Color32`.
  [PhysicsTrace.def](https://github.com/Facepunch/sbox-public/blob/master/engine/Definitions/engine/Physics/PhysicsTrace.def)
  declares `native struct PhysicsTrace.Result;` / `native struct PhysicsTrace.Request;` and a
  `native static class PhysicsTrace { PhysicsTrace.Result Trace( PhysicsTrace.Request request ); ... }`.
  The C# mirror is hand-written in [PhysicsTrace.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Sandbox.Engine/Systems/Physics/PhysicsTrace.cs):
  `[StructLayout(LayoutKind.Sequential)] internal unsafe struct Request { public IPhysicsWorld World; public NativeEngine.IPhysicsBody Body; public Vector3 StartPos; ... public fixed uint TagRequire[NumTagFields]; public IntPtr FilterDelegate; public byte TriggerFilter; ... }`
  and `Result { ...; public float Fraction; public byte StartedInSolid; public int PhysicsBodyHandle; public int PhysicsShapeHandle; ... }`.
  The C++ side (`sbox/physics_trace.h`) is in the closed native tree (**unverified**: its contents).
- Generated output shape, from the writer sources (the generated files are not in the repo):
  - C# `native class` wrapper: a `struct` with `internal readonly IntPtr self;`, implicit `IntPtr` conversions,
    `IsNull`, and one method per function that calls `__N.<MangledName>(...)` after `if ( self == IntPtr.Zero ) Sandbox.Interop.ThrowNullSelf(...)`
    ([ManagedWriter.Imports.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Tools/InteropGen/Writer/ManagedWriter.Imports.cs) lines ~119-233).
  - Nested `__N` class: `internal static delegate* unmanaged< ... > <MangledName>;` per function, `[SuppressGCTransition]` when `[nogc]` (same file, ~326-356).
  - C# `Exports` class: one `[UnmanagedCallersOnly] internal static <ret> <MangledName>(...)` thunk per managed function, wrapped in `try { ... } catch (Exception) { <exceptions sink>(className, fn, e); return default; }`; instance methods resolve `self` via `Sandbox.InteropSystem.TryGetObject<T>(self, out var instance)` ([ManagedWriter.Exports.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Tools/InteropGen/Writer/ManagedWriter.Exports.cs)).
  - C++: `DLL_EXPORT void CC igen_<ident>( int hash, void** managedFunctions, void** nativeFunctions, int* structSizes )` that checks the hash, fills the native table, checks struct sizes, and assigns `Imports::<Mangled> = (ret (CC *)(args)) managedFunctions[i];` ([NativeWriter.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Tools/InteropGen/Writer/NativeWriter.cs) lines ~79-240).
- Pipeline: parse → `TypeResolver` (unknown type name fails the build with `Unknown Type <name>`) → sort structs/classes/functions by name → `Mangler` (`namespace_class_member`, vowel-stripped past 16 chars, numeric suffix on collision) → MD5 of the full def text truncated to a `UInt16` hash ([InteropPipeline.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Tools/InteropGen/Pipeline/InteropPipeline.cs), [Mangler.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Tools/InteropGen/Pipeline/Mangler.cs), [types.md](https://github.com/Facepunch/sbox-public/blob/master/engine/Tools/InteropGen/docs/types.md)).

### A2. How native functions are bound

- No P/Invoke per function. Managed loads the DLL with `NativeLibrary.TryLoad`, finds the single export
  `igen_<ident>` with `NativeLibrary.GetExport`, and calls it once with `(hash, managedFunctions, nativeFunctions, structSizes)`.
  `managedFunctions` is `(IntPtr)(delegate* unmanaged<...>)&Exports.<Mangled>` for every exported managed
  function; `nativeFunctions` is an `IntPtr[]` the C++ side fills and C# then casts slot by slot into the `__N`
  fields ([ManagedWriter.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Tools/InteropGen/Writer/ManagedWriter.cs) `NativeInterop`, `ManagedFunctionTable`, `BindNativeFunctions`).
- The export table is index-matched; slot 0 is the native error callback, then per native class: `dynamic_cast`
  thunks to/from each base, functions, variable get/set. One enumerator (`NativeExportTable.Slots`) drives both
  emitters "so the two emitters can never drift out of sync (which would silently misalign function pointers at
  runtime)" ([NativeExportTable.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Tools/InteropGen/Writer/NativeExportTable.cs)).
- Crossing rules ([types.md](https://github.com/Facepunch/sbox-public/blob/master/engine/Tools/InteropGen/docs/types.md)):
  `bool` crosses as `int` 0/1; `string` is `const char*` UTF-8, "Pinned for the call going in; copied to a
  thread-local on the way out (`SafeReturnString`)"; `enum` crosses as a 64-bit integer; `struct` "by pointer if
  8+ bytes, by value if marked `[small]`"; native class as raw `IntPtr`; `[Handle:...]` class as a handle id;
  managed class as "a uint object id, resolved through `Sandbox.InteropSystem`"; `delegate` as `IntPtr`.
  Flags: `out`, `ref`, `asref` (returns `ref T` via `Unsafe.AsRef`), `cref`, `CastTo[X]`, `stable`.
- Strings in: generated code passes a `stackalloc` buffer to `Interop.InteropString(str, Span<byte>)`, heap
  fallback via `NativeMemory.Alloc`; strings returned from managed to native go through
  `GetTemporaryStringPointerForNative`, queued and freed at frame end ([Interop.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Sandbox.Engine/Core/Interop/Interop.cs)).
- Handles: `[Handle:Sandbox.PhysicsBody3d]` means "Instances cross the boundary as handle ids instead of raw
  pointers. Managed resolves them with `Sandbox.HandleIndex.Get<T>`, native with `GetManagedHandle`"
  ([classes.md](https://github.com/Facepunch/sbox-public/blob/master/engine/Tools/InteropGen/docs/classes.md)).
  `HandleIndex` is a `ConcurrentDictionary<int, IHandle>`; `RegisterHandle(IntPtr ptr, uint type)` (called from
  native on creation) switches on a `StringToken` type id (`"PhysicsWorld"`, `"PhysicsBody"`, `"PhysicsShape"`,
  joint types…) and constructs the managed wrapper, or picks up a pre-created one via the `[ThreadStatic]`
  `nextObject` set by `IHandle.MakeNextHandle(this)`; `FreeHandle(int)` calls `HandleDestroy()` which nulls the
  pointer ([Handle.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Sandbox.Engine/Core/Interop/Handle.cs), [readme.txt](https://github.com/Facepunch/sbox-public/blob/master/engine/Definitions/readme.txt)).
  Managed objects given to native are `uint` ids from `InteropSystem.Alloc/AllocWeak` (two concurrent
  dictionaries, index pool, main-thread asserts) ([InteropSystem.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Sandbox.Engine/Core/Interop/InteropSystem.cs)).
- Bootstrap from C++: `Sandbox.NetCore` exposes `[UnmanagedCallersOnly] public static int InitializeEngine(IntPtr gameFolderPtr)` which `Assembly.LoadFrom`s `Sandbox.Engine.dll` and reflects `NetCore.InitializeInterop` ([NetCore.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Sandbox.NetCore/NetCore.cs)). How the native exe starts the CLR (hostfxr vs coreclr) is in the closed tree — **unverified**.

### A3. Layout checks

- Startup, not generation time: C# emits `var structSizes = new int[] { sizeof( Vector3 ), ... }` and passes it to
  `igen_*`; C++ runs `if ( sizeof( Vector ) != structSizes[i] ) Plat_FatalError( "Vector is the wrong size" );`
  per struct ([ManagedWriter.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Tools/InteropGen/Writer/ManagedWriter.cs) `StructSizeTable`, [NativeWriter.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Tools/InteropGen/Writer/NativeWriter.cs) `StructSizeChecks`). Docs: "Sizes are verified at startup (`structSizes` exchange); a mismatch is a fatal error" ([classes.md](https://github.com/Facepunch/sbox-public/blob/master/engine/Tools/InteropGen/docs/classes.md)).
- Compile time: `static_assert ( sizeof( X ) >= 8, "Please mark struct X with a [small] - it's smaller than a pointer" );` for unmarked structs (same `StructSizeChecks`).
- Whole-contract check: `if ( hash != <defHash> ) Plat_FatalError( "igen_<ident>: interop hash mismatch - managed sent %d, native was built with ... rebuild both." )`, plus a null check over every received managed pointer ([NativeWriter.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Tools/InteropGen/Writer/NativeWriter.cs) `Initialize`).
- Not checked: field offsets, field names, field types, enum underlying types. Only sizes and the def-text hash. No unit tests of layout were found in `engine/Tests` (searched `Marshal.SizeOf`, `static_assert`, `StructSize` — hits are only in the generator and unrelated code).

### A4. Default values across languages

- No mechanism found. The `.def` DSL has no field list for structs, so no place for defaults. Defaults live on
  the managed wrapper and are pushed through setters: e.g. `Rigidbody.GravityScale { get; set { ...; PhysicsBody?.GravityScale = field; } } = 1.0f;`
  ([Rigidbody.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Sandbox.Engine/Scene/Components/Collider/Rigidbody.cs) ~line 36-50).
  Whether `sbox/physics_trace.h` carries C++ defaults for `PhysicsTrace::Request` is **unverified** (closed source).

### A5. Hot reload and assembly load contexts

- Game/addon assemblies load into collectible `AssemblyLoadContext`s: `class LoadContext : AssemblyLoadContext`
  (`base("TLC", true)`) with child `IsolatedAssemblyContext`s "so it can be unloaded and can have the same name
  as other loaded dlls" ([LoadContext.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Sandbox.Reflection/TypeLibrary/LoadContext.cs)).
  `Sandbox.Hotload` swaps old→new assemblies and upgrades live instances with Mono.Cecil-driven "upgraders" ([Hotload.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Sandbox.Hotload/Hotload.cs)).
- The generated bindings are *not* in those contexts: they are emitted into `Sandbox.Engine` (`cs "../Sandbox.Engine/Interop.Engine.cs"`) and `Sandbox.Tools`, which are engine assemblies, and `NativeInterop.Initialize()` runs once (`_initialized` guard) ([engine.def](https://github.com/Facepunch/sbox-public/blob/master/engine/Definitions/engine.def), [ManagedWriter.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Tools/InteropGen/Writer/ManagedWriter.cs)). Game code cannot reach `NativeEngine.*` unless whitelisted: "Exposing a new .NET type to game code requires adding it to the allow-list in `engine/Sandbox.Access/`" ([AGENTS.md](https://github.com/Facepunch/sbox-public/blob/master/AGENTS.md)). Static interop state that must survive a hotload is marked `[SkipHotload]` (e.g. `FrameAllocatedStrings` in [Interop.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Sandbox.Engine/Core/Interop/Interop.cs)). That addon assemblies never hold native function pointers directly is inferred from this layering — **partly unverified** (the allow-list contents were not read).

### A6. How game code reaches physics

- Managed wrapper objects, never plain structs + native calls. Layers, from the game down:
  1. `Rigidbody : Component` (public, `[Property]` fields) holds a `PhysicsBody _body` and forwards (`PhysicsBody.SmoothMove`, `PhysicsBody.SetInertiaTensor`) ([Rigidbody.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Sandbox.Engine/Scene/Components/Collider/Rigidbody.cs)).
  2. `public sealed partial class PhysicsBody : IValid` wraps an internal `_body` (`PhysicsBodyInternal`), `IsValid => _body is not null && _body.IsValid` ([PhysicsBody.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Sandbox.Engine/Systems/Physics/PhysicsBody.cs)); there are 2d (Box2D) and 3d backends.
  3. `internal sealed partial class PhysicsBody3d : PhysicsBodyInternal, IHandle` holds `internal IPhysicsBody native;` (the generated `NativeEngine.IPhysicsBody` pointer struct) and does `get => native.GetPosition(); set { native.SetPosition( value ); Dirty(); }`; creation is `using ( var h = IHandle.MakeNextHandle( this ) ) { world.world.AddBody(); }` ([PhysicsBody3d.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Sandbox.Engine/Systems/Physics/3d/PhysicsBody3d.cs)).
  4. Traces: `PhysicsWorld3d.TraceSingle(in PhysicsTrace.Request request, ...)` fills the blittable `Request` (`r.World = native; r.Body = ((PhysicsBody3d)targetBody._body).native; r.FilterDelegate = (IntPtr)((delegate* unmanaged<int, byte>)&FilterFunctionInternal);`), calls generated `PhysicsTrace.Trace( r )`, and converts the raw `Result` (ints `PhysicsBodyHandle`/`PhysicsShapeHandle`) into the public `PhysicsTraceResult` via `HandleIndex.Get<PhysicsBody3d>(...)` ([PhysicsWorld3d.Trace.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Sandbox.Engine/Systems/Physics/3d/PhysicsWorld3d.Trace.cs), [PhysicsWorld.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Sandbox.Engine/Systems/Physics/PhysicsWorld.cs) ~296).
- Native-side objects: `IPhysicsWorld` (`[Handle:Sandbox.PhysicsWorld3d]`, `AddBody()`, `RemoveBody()`, `AddWeldJoint(...)`), `IPhysicsBody`, `IPhysicsShape`, `IPhysicsJoint`, and the `g_pPhysicsSystem` accessor (`CreateWorld()`, `CastHeightField(...)`) ([IPhysicsWorld.def](https://github.com/Facepunch/sbox-public/blob/master/engine/Definitions/common/Physics/IPhysicsWorld.def), [vphysics2.def](https://github.com/Facepunch/sbox-public/blob/master/engine/Definitions/engine/vphysics2.def)).

## B. Coral (StudioCherno/Coral)

- Hosting: loads `hostfxr` by hand (`LoadLibrary`/`dlopen`), resolves `hostfxr_set_error_writer`, `hostfxr_set_runtime_property_value`, `hostfxr_initialize_for_runtime_config`, `hostfxr_get_runtime_delegate`, `hostfxr_close`; then `hostfxr_initialize_for_runtime_config(Coral.Managed.runtimeconfig.json)`, sets `APP_CONTEXT_BASE_DIRECTORY`, gets `hdt_load_assembly_and_get_function_pointer`, and loads `Coral.Managed.ManagedHost, Coral.Managed` / `Initialize` ([HostInstance.cpp](https://github.com/StudioCherno/Coral/blob/main/Coral.Native/Source/Coral/HostInstance.cpp) ~lines 19-24, 238-272). The README calls it "a C++ wrapper around the .NET HostFXR library"; the GitHub About text says CoreCLR ([repo](https://github.com/StudioCherno/Coral)).
- Native→managed function table: ~50 `LoadCoralManagedFunctionPtr<Fn>("Coral.Managed.TypeInterface, Coral.Managed", "GetTypeSize")`-style calls, each a `load_assembly_and_get_function_pointer` by *string type name + method name* onto `[UnmanagedCallersOnly]` methods; a null result prints to `std::cerr` and `CORAL_VERIFY(false)` (debug break) ([HostInstance.cpp](https://github.com/StudioCherno/Coral/blob/main/Coral.Native/Source/Coral/HostInstance.cpp) ~303-398, [Verify.hpp](https://github.com/StudioCherno/Coral/blob/main/Coral.Native/Source/Coral/Verify.hpp)). Nothing is generated; the table is hand-maintained on both sides.
- Internal calls (managed→native) are *fields*, not attributes: C# declares `internal static unsafe delegate*<MyVec3*, MyVec3*, void> VectorAddIcall;` ([Main.cs](https://github.com/StudioCherno/Coral/blob/main/Example/Example.Managed/Source/Main.cs)); C++ calls `assembly.AddInternalCall("Example.Managed.ExampleClass", "VectorAddIcall", &VectorAddIcall); assembly.UploadInternalCalls();` ([Main.cpp](https://github.com/StudioCherno/Coral/blob/main/Example/Example.Native/Source/Main.cpp)). `AddInternalCall` builds the string `"<Class>+<Field>, <AssemblyName>"` into an `InternalCall { const CharType* Name; void* NativeFunctionPtr; }` ([Assembly.cpp](https://github.com/StudioCherno/Coral/blob/main/Coral.Native/Source/Coral/Assembly.cpp) lines 11-33). `InternalCallsManager.SetInternalCalls` (`[UnmanagedCallersOnly]`) splits on `+`/`,`, `TypeInterface.FindType(alcId, name)`, finds the `Static | NonPublic` field, checks `field.FieldType.IsFunctionPointer`, and `field.SetValue(null, ptr)`; each failure is logged and skipped ([InternalCalls.cs](https://github.com/StudioCherno/Coral/blob/main/Coral.Managed/Source/InternalCalls.cs)). No signature check: the C++ `void*` is poured into whatever `delegate*<>` the field declares.
- Type description: `Coral::Type` is a `TypeId` (int) handle onto a managed `System.Type` cached in `TypeInterface.s_CachedTypes`; it exposes `GetSize()`, `GetFullName()`, `GetBaseType()`, `GetMethods/Fields/Properties()`, `CreateInstance(args...)`, `InvokeStaticMethod(name, args...)` ([Type.hpp](https://github.com/StudioCherno/Coral/blob/main/Coral.Native/Include/Coral/Type.hpp)). `GetTypeSize` returns `Marshal.SizeOf(type)` ([TypeInterface.cs](https://github.com/StudioCherno/Coral/blob/main/Coral.Managed/Source/TypeInterface.cs) ~262-270). `TypeCache` is `[[deprecated]]` in favour of per-assembly `GetLocalType` ([TypeCache.hpp](https://github.com/StudioCherno/Coral/blob/main/Coral.Native/Include/Coral/TypeCache.hpp), [Assembly.hpp](https://github.com/StudioCherno/Coral/blob/main/Coral.Native/Include/Coral/Assembly.hpp)).
- Struct crossing: by raw pointer + a `ManagedType` tag per argument; `ManagedObject.InvokeMethod(IntPtr handle, NativeString name, IntPtr params, ManagedType* types, int count)` reflects the method by name (`TryGetMethodInfo` over `GetMethods(...)`) and `Marshalling.MarshalPointer` falls back to `Marshal.PtrToStructure` for value types ([ManagedObject.cs](https://github.com/StudioCherno/Coral/blob/main/Coral.Managed/Source/ManagedObject.cs) ~225-395). The example passes `MyVec3` (three floats) both as a by-pointer icall argument and as an `InvokeMethod` argument with no layout declaration on either side ([Main.cpp](https://github.com/StudioCherno/Coral/blob/main/Example/Example.Native/Source/Main.cpp)).
- Layout checks: the only compile-time ones are `static_assert(offsetof(ReflectionType, m_TypeID) == 0); static_assert(sizeof(ReflectionType) == 4);` on Coral's own handle type ([Type.hpp](https://github.com/StudioCherno/Coral/blob/main/Coral.Native/Include/Coral/Type.hpp) ~141-142), and `[StructLayout(LayoutKind.Sequential, Size=32, Pack=8)] NativeArray<T>`, `Size=16` `NativeInstance<T>`, `[StructLayout(LayoutKind.Explicit, Size=16)] NativeString`, `Bool32` for its own interop types ([InteropTypes.cs](https://github.com/StudioCherno/Coral/blob/main/Coral.Managed/Source/InteropTypes.cs) ~39, 170, 226). User structs are never checked.
- Compared with a function-table bridge (s&box): Coral binds each managed entry point by string at startup (one hostfxr lookup each) and each user icall by reflection, with runtime logging on miss; s&box exchanges two index-matched pointer arrays in one call, guarded by a def hash. Coral has `AssemblyLoadContext` support (`CreateAssemblyLoadContext`, `UnloadAssemblyLoadContext`) and internal calls are per-context ([HostInstance.hpp](https://github.com/StudioCherno/Coral/blob/main/Coral.Native/Include/Coral/HostInstance.hpp), [InternalCalls.cs](https://github.com/StudioCherno/Coral/blob/main/Coral.Managed/Source/InternalCalls.cs)).

## C. Mono embedding API

- Registration: `mono_add_internal_call (const char *name, gconstpointer method)`; the doc comment: "You must provide a fully qualified type name, that is namespaces and type name, followed by a colon and the method name, with an optional signature to bind", examples `MyApp.Services.ScriptService:Accelerate` and `MyApp.Services.ScriptService:Slowdown(int,bool)`; "The internal calls are invoked with no marshalling. This means that .NET types like `System.String` are exposed as `MonoString*` parameters" ([icall.c](https://github.com/mono/mono/blob/main/mono/metadata/icall.c) ~8670-8710). The embedding page's example is `mono_add_internal_call ("Hello::Sample", sample);` with C# `[MethodImplAttribute(MethodImplOptions.InternalCall)] extern static string Sample ();` and states internal calls "provide no support for marshalling" ([Embedding Mono](https://www.mono-project.com/docs/advanced/embedding/)).
- Lookup is a string hash: the key is stored verbatim (`g_hash_table_insert (icall_hash, key, value)`), and at JIT time Mono builds `"<Namespace>.<Class>::<Method>(<sig>)"` (nested classes joined with `/`), tries it with the signature, then without; a miss warns "cant resolve internal call to \"%s\" (tested without signature also)" ([icall.c](https://github.com/mono/mono/blob/main/mono/metadata/icall.c) `add_internal_call_with_flags`, `mono_lookup_internal_call_full_with_flags` ~8772-8925). Mono's own table uses `ICALL_TYPE(MATH, "System.Math", ...)` + `ICALL(MATH_19, "Abs(double)", ves_icall_System_Math_Abs_double)` ([icall-def.h](https://github.com/mono/mono/blob/main/mono/metadata/icall-def.h) ~464-467). `mono_method_desc_new` accepts both `:` and `::` ("allow two :: to separate the method name") ([debug-helpers.c](https://github.com/mono/mono/blob/main/mono/metadata/debug-helpers.c) ~357-385). Since the lookup string uses `::`, whether a registration with a single `:` is ever matched by the hash path is **unverified** from the code read here.
- Struct crossing: for `mono_runtime_invoke` arguments are "MonoObject* pointers for object instances and pointers to the value type otherwise" and "If the method returns a value type, it is boxed in an object" ([Embedding Mono](https://www.mono-project.com/docs/advanced/embedding/)). The P/Invoke page: structs default to `LayoutKind.Sequential`; a struct of only blittable types passed by reference "can be passed directly without an intermediate copy"; `bool` is not blittable; classes need explicit `StructLayout` because "The runtime can rearrange the order of members in any way it chooses" ([P/Invoke](https://www.mono-project.com/docs/advanced/pinvoke/)). That internal-call value-type parameters arrive by value with the managed layout (rather than boxed) is widely relied on but **unverified** against a primary document here.
- Layout check: none. Neither page nor the icall code compares managed and C struct layouts; a signature mismatch is only detected by name (with optional textual signature).

## D. .NET's own facilities

- `[LibraryImport]` (.NET 7+): a Roslyn source generator that replaces the runtime IL stub with generated C# marshalling for a `static partial` method; AOT/trim friendly; `StringMarshalling` replaces `CharSet`; project must set `AllowUnsafeBlocks` ([P/Invoke source generation](https://learn.microsoft.com/en-us/dotnet/standard/native-interop/pinvoke-source-generation)). Best practice: "DO prefer using function pointers and `UnmanagedCallersOnlyAttribute` as opposed to `Delegate` types"; "DO use the C# `sizeof()` instead of `Marshal.SizeOf<MyStruct>()` for blittable structures"; troubleshooting: "Compare `Marshal.SizeOf<T>()` to native `sizeof`" ([best practices](https://learn.microsoft.com/en-us/dotnet/standard/native-interop/best-practices)).
- `[GeneratedComInterface]`/`[GeneratedComClass]` (.NET 8+): generates `ComWrappers` vtable stubs from a C# interface, `IUnknown`-based only; derived interfaces across assemblies warn because "changes to any generated virtual method offsets in the base interface chain ... won't be accounted for ... until the project is rebuilt" ([ComWrappers source generation](https://learn.microsoft.com/en-us/dotnet/standard/native-interop/comwrappers-source-generation)). A COM vtable is itself an index-matched function table, like s&box's export table but per object.
- `[assembly: DisableRuntimeMarshalling]`: `bool` → C `bool` (1 byte), `char` → `char16_t`, any C# `unmanaged` type without `LayoutKind.Auto` fields "Treated as a blittable type"; everything else unsupported; disables `in/ref/out` parameter marshalling ([Disabled runtime marshalling](https://learn.microsoft.com/en-us/dotnet/standard/native-interop/disabled-marshalling)). With it on, `bool` can live in a mirrored struct at one byte, which is the Iris rule already ("`bool` is one byte", `packages/Iris/README.md`).
- `StructLayout`: "For blittable types, `LayoutKind.Sequential` controls both the layout in managed memory and the layout in unmanaged memory"; `Explicit` "requires that you use the `FieldOffsetAttribute`"; C# applies `Sequential` to structs by default, classes are `Auto` ([StructLayoutAttribute](https://learn.microsoft.com/en-us/dotnet/api/system.runtime.interopservices.structlayoutattribute)).
- `Unsafe.SizeOf<T>()` "returns the size of the *managed* view of the type. For the size of the *unmanaged* view ... use `Marshal.SizeOf`" ([Unsafe.SizeOf](https://learn.microsoft.com/en-us/dotnet/api/system.runtime.compilerservices.unsafe.sizeof)). `Marshal.OffsetOf<T>(string)` "provides the offset in terms of the unmanaged structure layout, which does not necessarily correspond to the offset of the managed structure layout" and requires `StructLayoutAttribute` ([Marshal.OffsetOf](https://learn.microsoft.com/en-us/dotnet/api/system.runtime.interopservices.marshal.offsetof)). For blittable sequential structs the two views coincide, so either is a valid cross-check against C++ `sizeof`/`offsetof`.
- ClangSharpPInvokeGenerator: input is C/C++ headers (`-f, --file`, `-t, --traverse`, response files `@generate.rsp`), output C# (`-o`, `-n`, `-om CSharp|Xml`). `--generate tests-nunit|tests-xunit`: "Basic tests validating size, blittability, and associated metadata should be generated". Layout controls: `-wp, --with-packing` ("Overrides the StructLayoutAttribute.Pack property"), `--generate native-alignment-attribute` ("over-alignment ... that .NET cannot honor"), `-wts, --with-transparent-struct`, `-r/--remap`, `-rf/--remap-field`. Defaults/constants: `--generate macro-bindings` ("only works with value like macros"), `--generate unmanaged-constants` (experimental), `-wcfv, --with-constant-folded-value` ("Emit the clang-evaluated constant value ... instead of translating the written initializer expression") ([ClangSharp README](https://github.com/dotnet/ClangSharp/blob/main/README.md) ~189-355). It does not emit C++ default member initializers as C# defaults — **unverified** beyond the option list (no option for it is documented).

## What this suggests for an engine like Iris

1. **One description, two generated sides.** s&box describes every crossing *function* once in a `.def` and
   generates the C# wrapper struct, the C# `[UnmanagedCallersOnly]` exports, the C++ thunks and the init function
   ([docs/README.md](https://github.com/Facepunch/sbox-public/blob/master/engine/Tools/InteropGen/docs/README.md)).
   Iris today has three hand-kept artefacts per struct (C++ struct, `IRIS_FIELD` mapping, C# struct; `EngineMappings.hpp`). A generator fed by the `StructMapping` (or by the C++ header via ClangSharp, which already emits `StructLayout` and size tests) could emit the C# mirror.
2. **But even s&box does not generate structs.** `native struct Vector is Vector3` is a name pair; both sides are hand-written and only `sizeof` is compared ([mathlib.def](https://github.com/Facepunch/sbox-public/blob/master/engine/Definitions/tier3/mathlib.def), [classes.md](https://github.com/Facepunch/sbox-public/blob/master/engine/Tools/InteropGen/docs/classes.md)). Iris's per-field name/offset/type validation (`packages/Iris/README.md`) is already stricter than s&box, Coral (no check) and Mono (no check). Keep it; the gap is duplication, not safety.
3. **Fail fast on the whole contract, not per call.** s&box hashes the def text into the generated code and `Plat_FatalError`s on mismatch at `igen_*` time ([NativeWriter.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Tools/InteropGen/Writer/NativeWriter.cs)). Iris could hash its mapping set (names, offsets, types) and compare against a constant compiled into the C# contract assembly at load, in addition to per-struct validation.
4. **Validate at load, not at first use, when pointers are involved.** s&box checks all struct sizes in one startup exchange; Iris's README already says to call `Component::ValidateStruct<T>()` at component load because internal calls "never check the layout". Make that automatic for every registered mapping (s&box's `structSizes` loop is the model).
5. **Function tables beat per-function string lookup.** s&box binds all functions with one export and two index-matched pointer arrays driven from one slot enumerator ([NativeExportTable.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Tools/InteropGen/Writer/NativeExportTable.cs)); Coral does ~50 `load_assembly_and_get_function_pointer` string lookups plus reflection per icall ([HostInstance.cpp](https://github.com/StudioCherno/Coral/blob/main/Coral.Native/Source/Coral/HostInstance.cpp)). For Iris's native-function set a generated, index-matched table (with slot count checked on both sides) is the cheaper and safer shape.
6. **Internal calls as `delegate* unmanaged` fields set from native.** Coral's model (`internal static delegate*<...> X;` filled by `FieldInfo.SetValue`) and s&box's `__N` class of `delegate* unmanaged<...>` fields are the same idea ([InternalCalls.cs](https://github.com/StudioCherno/Coral/blob/main/Coral.Managed/Source/InternalCalls.cs), [ManagedWriter.Imports.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Tools/InteropGen/Writer/ManagedWriter.Imports.cs)). Both skip Mono-style `[MethodImpl(InternalCall)]` string binding, which has no signature check at all ([icall.c](https://github.com/mono/mono/blob/main/mono/metadata/icall.c)). Neither checks the function *signature*; a generated table is the only place that can.
7. **`[SuppressGCTransition]` as an opt-in attribute in the definition.** s&box's `[nogc]` emits `delegate* unmanaged[SuppressGCTransition]` for short non-reentrant calls and `[callback]` cancels it ([classes.md](https://github.com/Facepunch/sbox-public/blob/master/engine/Tools/InteropGen/docs/classes.md)). Physics getters are the obvious candidates.
8. **Bool crosses as `int` in s&box, as one byte in Iris.** s&box avoids the `bool` marshalling trap by widening to `int` at the boundary ([types.md](https://github.com/Facepunch/sbox-public/blob/master/engine/Tools/InteropGen/docs/types.md)); .NET's `DisableRuntimeMarshalling` makes a 1-byte `bool` legitimate in blittable structs ([disabled marshalling](https://learn.microsoft.com/en-us/dotnet/standard/native-interop/disabled-marshalling)). Iris's contract assembly should carry that attribute so `sizeof`, `fixed` buffers and `bool` fields mean the same thing the mapping assumes.
9. **Handles, not pointers, for engine objects.** s&box passes physics bodies/shapes/worlds as `int` handle ids resolved by `HandleIndex`, and managed objects to native as `uint` ids via `InteropSystem`; native destruction calls `FreeHandle` so the managed wrapper is invalidated ([Handle.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Sandbox.Engine/Core/Interop/Handle.cs), [readme.txt](https://github.com/Facepunch/sbox-public/blob/master/engine/Definitions/readme.txt)). Iris's `EntityLinkView` already stands in for `Fenrir::Entity` (`EngineMappings.hpp`); the same pattern fits physics bodies that scripts must not hold raw pointers to.
10. **Plain structs for queries, wrappers for objects.** s&box's trace path is a blittable `Request`/`Result` struct pair with `fixed` tag arrays and `int` handles, converted once into a public `PhysicsTraceResult` ([PhysicsTrace.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Sandbox.Engine/Systems/Physics/PhysicsTrace.cs), [PhysicsWorld3d.Trace.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Sandbox.Engine/Systems/Physics/3d/PhysicsWorld3d.Trace.cs)). Iris's mapped structs are the right vehicle for trace/overlap requests; `Rigidbody`-style state belongs behind a managed wrapper that calls setters.
11. **Defaults live on the managed side and are pushed, not mirrored.** No system studied keeps struct defaults in sync across languages; s&box sets `= 1.0f` on the `Rigidbody` property and forwards to native in the setter ([Rigidbody.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Sandbox.Engine/Scene/Components/Collider/Rigidbody.cs)). If Iris wants shared defaults for plain-data components, the generator (point 1) is the only place that can emit them on both sides; otherwise document one owner.
12. **Keep bindings out of the collectible load context.** s&box's generated bindings live in `Sandbox.Engine`, initialise once, and game assemblies in collectible `AssemblyLoadContext`s reach only whitelisted types ([LoadContext.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Sandbox.Reflection/TypeLibrary/LoadContext.cs), [AGENTS.md](https://github.com/Facepunch/sbox-public/blob/master/AGENTS.md)). Coral instead re-registers icalls per context id ([InternalCalls.cs](https://github.com/StudioCherno/Coral/blob/main/Coral.Managed/Source/InternalCalls.cs)). For Iris, a stable shared contract assembly holding the mirrors and function-pointer fields, with script assemblies reloadable on top, matches s&box.
13. **Index-match from one enumerator.** s&box's stated reason for `NativeExportTable` is that two emitters iterating separately "would silently misalign function pointers at runtime" ([NativeExportTable.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Tools/InteropGen/Writer/NativeExportTable.cs)). Any Iris generator should emit both sides from one ordered model and sort deterministically ([InteropPipeline.cs](https://github.com/Facepunch/sbox-public/blob/master/engine/Tools/InteropGen/Pipeline/InteropPipeline.cs)).
14. **Snapshot-diff the generator.** Because output is not committed, s&box verifies pure refactors by hashing all generated files before and after ([docs/README.md](https://github.com/Facepunch/sbox-public/blob/master/engine/Tools/InteropGen/docs/README.md)). If Iris generates C# mirrors, committing them (unlike s&box) gives the same guarantee through ordinary review diffs.
15. **ClangSharp as the header reader, not the whole answer.** It can produce `StructLayout`-correct C# from the C++ header and size/blittability tests (`--generate tests-xunit`), and fold constants (`-wcfv`), but it does not model Iris's name aliasing (`IRIS_FIELD_AS`) or validation ([ClangSharp README](https://github.com/dotnet/ClangSharp/blob/main/README.md)). Using it to emit the C# mirror while keeping Iris's runtime check as the gate removes one of the three hand-written artefacts without lowering safety.

# 03: `RegistryIds<T, TKind>`: one generation-cached id lookup in C#

**What to build:** The one C# cache replacing `ComponentIds<T>`, `EventIds<T>` and `ResourceIds<T>`, which also throws the messages their callers each spell today (spec: "One C# cache").

- **`Managed/Fenrir.Scripting/RegistryIds.cs`**: `internal interface IRegistryKind` with static abstract members for the attribute's name on a type (`string? NativeName(Type)`), the native to call (`int Resolve(string name, string typeName)`), the noun (`"component"`, `"event"`, `"resource"`) and the hint for a type with no attribute (today's "an event a script defines is registered when its assembly loads..." for events, "has no [NativeResource] attribute..." for resources, and the component's). Three empty structs `ComponentKind`, `EventKind`, `ResourceKind` implement it. `internal static class RegistryIds<T, TKind> where T : unmanaged where TKind : struct, IRegistryKind`: `NativeName`, `Name`, the generation-cached `Id` (as today; a resource type with no attribute is `NotRegistered` without a native call, as `ResourceIds` does), and `Require()`, which returns the id or throws `InvalidOperationException` with the kind's messages: registered for a different C# type, not registered with an attribute, not registered without one.
- **Callers**: `Entity.IdOf` keeps its resource check (`IsResource` moves beside it) and calls `RegistryIds<T, ComponentKind>.Require()`; `Events.cs` uses `RegistryIds<T, EventKind>.Require()` where `EventIds<T>.Id` threw; `Resources.IdOf` becomes `RegistryIds<T, ResourceKind>.Require()`; `QueryNative.cs`, `Systems.cs` and the generators' output, where they name a cache, follow. `ComponentIds.cs` goes; the cache classes in `Events.cs` and `Resources.cs` go.
- Messages are unchanged: the .NET tests that pin "different C# type", "has no component registered as", "has no event registered as", "has no resource registered as" and "has no [NativeResource] attribute" pass before and after.

**Blocked by:** 02

**Touches:** `packages/FenrirScripting/Managed/Fenrir.Scripting/`, `packages/FenrirScripting/Managed/Fenrir.Scripting.Generators/`

**Status:** resolved

- [ ] `ScriptingTests` (.NET) pass unchanged; `grep -rn "ComponentIds<\|EventIds<\|ResourceIds<" Managed/` finds nothing.
- [ ] `grep -n "generation != current" Managed/Fenrir.Scripting/*.cs` finds one line.
- [ ] `check_tickets.py` and the msvc-x64-debug preset pass.

## Comments

Filed from the registry-table grilling (spec decisions 10, 13). Cut from `registry-table-02-error-enums`. When this merges the spec's last ticket is built: the commit that records it deletes `.scratch/registry-table/`.

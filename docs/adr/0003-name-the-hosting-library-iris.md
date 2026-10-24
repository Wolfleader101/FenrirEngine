---
status: accepted
---

# Name the hosting library Iris

The engine-independent hosting library from ADR-0001 is named **Iris**, after the messenger who carries word between the gods and mortals, as the library carries calls between native and managed code. It replaces the Fenrir-prefixed names (`FenrirScripting`, `FenrirDotNetHost`, `Fenrir::Scripting`, `Fenrir.ManagedHost`) so that nothing in the reusable core implies FenrirEngine. Charon was the other candidate; Iris read better as an identifier and in its connotations.

| Before | After |
| --- | --- |
| `packages/FenrirScripting/` | `packages/Iris/` |
| CMake target `FenrirDotNetHost` | `Iris` (alias `Iris::Iris`) |
| Namespace `Fenrir::Scripting` | `Iris` |
| `DotNetHost`, `ManagedComponent`, `ManagedCallResult<T>`, `ManagedCallStatus` | `Iris::Host`, `Iris::Component`, `Iris::CallResult<T>`, `Iris::CallStatus` |
| Headers `FenrirDotNetHost/*.hpp` | `Iris/Host.hpp`, `Iris/Component.hpp`, `Iris/CallResult.hpp` |
| Managed bridge `Fenrir.ManagedHost` | `Iris.Bridge` |
| CMake options `FENRIR_SCRIPTING_BUILD_*`, `FENRIR_DOTNET_*` | `IRIS_BUILD_*`, `IRIS_DOTNET_*` |

## Consequences

- `FENRIR_ENABLE_SCRIPTING` stays as the engine's switch for including Iris. Iris itself has no enable option; a project that adds it wants .NET.
- `FenrirScripting` is free for the later engine integration layer built on Iris.
- "Iris" is a common name. A published package would need a qualified ID; the code is unaffected.

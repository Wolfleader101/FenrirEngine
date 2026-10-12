---
status: accepted
---

# FenrirECS owns the ECS library; other packages use only its API

The engine's ECS is built on EnTT, but EnTT is an implementation detail of `FenrirECS`. We may swap it for another library, or write our own, and when we do, code in other packages (scripting, the editor, the renderer) should not need editing. So code outside `FenrirECS` uses `EntityList`, `Entity` and the types `FenrirECS` defines for the purpose, and does not name EnTT types, call its registry, or build its storages.

This came up when scripting needed storage for components that exist only at run time (a script defines them in C#, so they have no C++ type). The first design added a public `EntityList::GetRegistry()` and had `FenrirScripting` build an EnTT storage itself. That put EnTT types in a public header of another package and handed out the raw registry. It was committed, and is replaced by this design before anything else was built on it.

What `FenrirECS` provides instead, with no EnTT type in any signature:

- `DynamicComponentType`: a name, a size and an alignment. `EntityList` stores components of such a type (zeroed on add, removed with the entity, in pages that never move) and exposes `GetDynamic`, `HasDynamic`, `AddDynamic`, `RemoveDynamic` and `CountDynamic`.
- `ComponentSelector`, native (`Native<T>()`) or dynamic, with `EntityList::ForEachWith(selectors, func)` ("every entity that has all of these") and `CountOf(selector)` (constant time). This is how code outside the ECS queries a mix of component kinds.
- The byte storage (`ByteStorage`) lives in `FenrirECS/src` and is private to it. It also defends the registry: a dynamic type's storage id is a hash, so before touching anything under that id the ECS checks that the storage is its own and belongs to that type's name. A collision with another dynamic name, or with a native storage, is refused instead of corrupting memory.

## What this does and does not buy

Be precise about it, because the goal is easy to overstate.

- **Source:** code outside `FenrirECS` that follows the rule never names EnTT, so replacing the library means rewriting `FenrirECS` and not editing that code. A CTest in the scripting package (`ScriptingDoesNotUseEnTT`) enforces it for that package by scanning its source text.
- **Compilation:** it does not remove the dependency from the build. `FenrirECS`'s public headers still include EnTT, because `Entity` and `EntityList` have templates built on it (`GetComponent<T>`, `View`, `Group`, `ForEach`) that are instantiated in the callers' translation units, and so is `ComponentSelector::Native<T>()`. Other packages therefore still compile against EnTT's headers, and would need recompiling (not editing) after a swap. The guard cannot see that, since it reads text.
- **What would remove it:** redesigning `Entity` and `EntityList` around type-erased component access, so the headers need no library. That is a larger change, and a separate decision.
- **Not yet following the rule:** the DemoApp includes `<entt/container/dense_map.hpp>` in `Material.hpp` and uses `View`/`Group` in the renderer. They predate this decision and are not changed by it.

## Cost

- Native components are untouched: their storage, and how `Entity::GetComponent<T>` and the engine's own views reach it, are exactly as they were.
- A query visits each matching entity through one indirect call (a function pointer and a context), which cannot be inlined across the library boundary. That is the price of keeping the callable out of `FenrirECS`'s translation unit; a hot loop that needs it gone should fill a buffer in one call (the scripting queries will).
- Building a query costs one `std::function` call per selector to find its storage, plus the allocation EnTT's own runtime view makes. Selectors are made once and reused.
- Script components' `get`, `has` and `add` are one out-of-line call into `FenrirECS` plus a lookup of the storage by id. Native components' operations are unchanged.
- Selecting a native component creates its (empty) storage if the list has none, as asking EnTT for a view of it does. Selecting a dynamic component never creates one.

## Consequences

- A feature that needs the ECS to do something it cannot yet do adds a method to `FenrirECS`; the other package calls it. It does not reach for the registry.
- The ECS code is only tested through the scripting package's tests today (there is no `FenrirECS` test target), including a test that drives `EntityList`'s dynamic components directly. A test target for `FenrirECS` would be the right home for them.

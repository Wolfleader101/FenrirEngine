# 07: Queries

**What to build:** `ctx.Query<A, B, ...>()` over any mix of native and C# components. Native code builds the EnTT view and fills a per-query scratch buffer of the entity plus one component pointer per type, with one native-to-managed transition per query. C# iterates it as `foreach (var (entity, a, b) in ctx.Query<A, B>())`, where each row is a `ref struct` with a generated `Deconstruct` yielding `Entity` and `Ref<T>`s. `Query<A, B>().ForEach((Entity e, Ref<A> a, Ref<B> b) => ...)` mirrors native `ForEach`. The generator emits arities 1 to 8.

**Blocked by:** 06

**Status:** resolved

- [x] Writes through query rows are visible natively.
- [x] Structural changes during iteration are deferred (06), so iteration is never invalidated.
- [x] Calls per query are measured and are constant in the entity count. Record the figure for 10k entities in the ticket.
- [x] Empty queries and queries with types that were never registered behave sensibly (empty, or a clear error).

## Comments

### Resolution

- `World.Query<A, ..., H>()` (arities 1 to 8, emitted into the contract by the new `QueryGenerator`) returns a query you `foreach` over (rows deconstruct to `Entity` and `Ref<T>`s) or call `ForEach` on with a `QueryAction` delegate (a custom delegate, since a ref struct cannot be an `Action<>` argument on every runtime). `ctx.Query` will forward to it in ticket 08; there is no `SystemContext` yet.
- Native side: `Queries.cpp` runs `EntityList::ForEachWith` over the components' `ComponentSelector`s (so it works for native and C# components, and names no EnTT) and writes rows of `[entity, ptr...]` into a buffer the managed side allocates. The buffer is sized by a first call that returns the smallest component count, so a query is exactly two native calls. The managed enumerator frees it in `Dispose` (foreach, `break` and exceptions all dispose it).
- **Measured, 10,000 entities** (Transform + Velocity, one `Integrate` loop writing every Transform, Release): **2 native calls**, about **0.45 ms** (Debug: about 30 ms). Ten entities also make 2 calls. Checked by `QueryCallCount()` in a test. The first query of a process also pays JIT (about 3 ms).
- Queries are read-write and take `IComponent` only: a read-only component such as Relationship cannot be queried (`ReadOnlyRef` rows would be a later addition).
- The row buffer is per query, so nested queries are independent. Rows hold addresses, so they are valid only until the system returns, which deferral guarantees.

Spec: `.scratch/engine-scripting/spec.md` (Access).
- The ticket says one native-to-managed transition per query; this is two calls into the engine (count the matches, then fill the rows) because the managed side owns the buffer and must know its size first. They are constant in the entity count, which is what the criterion measures. A persistent native buffer could make it one call at the cost of per-query lifetime bookkeeping; not worth it yet.
- `Scripting::QueryCallCount()` is a diagnostic added for that test.

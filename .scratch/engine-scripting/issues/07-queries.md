# 07: Queries

**What to build:** `ctx.Query<A, B, ...>()` over any mix of native and C# components. Native code builds the EnTT view and fills a per-query scratch buffer of the entity plus one component pointer per type, with one native-to-managed transition per query. C# iterates it as `foreach (var (entity, a, b) in ctx.Query<A, B>())`, where each row is a `ref struct` with a generated `Deconstruct` yielding `Entity` and `Ref<T>`s. `Query<A, B>().ForEach((Entity e, Ref<A> a, Ref<B> b) => ...)` mirrors native `ForEach`. The generator emits arities 1 to 8.

**Blocked by:** 06

**Status:** ready-for-agent

- [ ] Writes through query rows are visible natively.
- [ ] Structural changes during iteration are deferred (06), so iteration is never invalidated.
- [ ] Calls per query are measured and are constant in the entity count. Record the figure for 10k entities in the ticket.
- [ ] Empty queries and queries with types that were never registered behave sensibly (empty, or a clear error).

## Comments

Spec: `.scratch/engine-scripting/spec.md` (Access).

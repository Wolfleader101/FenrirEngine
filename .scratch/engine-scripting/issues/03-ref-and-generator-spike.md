# 03: Spike: Ref<T> and generated field accessors

**What to build:** Prove the component access syntax before anything depends on it. `Ref<T>` is a `ref struct` wrapping a `T*`. A Roslyn source generator (`Fenrir.Scripting.Generators`) emits, for each `IComponent` struct, C# 14 extension properties on `Ref<T>` that return `ref` to each field. With these, `var t = entity.Get<Transform>(); t.Position.X += 1;` writes through to the pointed-to memory, and no `ref` is needed at the call site. Use native memory owned by the test (no ECS yet).

**Blocked by:** 02

**Status:** ready-for-agent

- [ ] Writes through generated properties, including nested struct fields (`t.Position.X += 1`), are visible in native memory.
- [ ] `Ref<T>` cannot be stored in a field or captured in a lambda (it's a compile error, shown by a negative compile test or documented).
- [ ] The generator runs on a user's script assembly that only references the SDK.
- [ ] If extension properties can't return `ref` as needed, record why, and switch the design to the accepted fallback `t.Value.Position.X += 1`. Update the spec either way.

## Comments

Spec: `.scratch/engine-scripting/spec.md` (Access). The user accepted the `t.Value` fallback in advance.

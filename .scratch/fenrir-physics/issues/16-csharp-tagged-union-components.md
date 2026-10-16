# How do C# components hold a tagged union?

Type: grilling
Status: resolved
Blocked by: none

## Question

`Collider` holds a `CollisionShape`, a plain-data tagged union ("How is physics modelled in the ECS?"). C# can express one as a blittable struct with overlapping fields (`[StructLayout(LayoutKind.Explicit)]` with `[FieldOffset]`), but FenrirScripting's component mapping and source generator do not support it today.

- How a native component containing a tagged union is described to scripting (its registration), and how the mapping checks the C# layout against the C++ one.
- What the generated C# looks like: the tag, a checked typed view per kind (`Shape.Box.HalfExtents`), and implicit conversions (`Shape = new BoxShape(...)`).
- Whether a C# component defined in a script assembly may declare its own tagged union, or only native components may.
- Whether this is a general scripting capability (other likely users: light kinds, camera projections), and so belongs in a scripting ADR.

## Answer

**No new mechanism: the existing Iris struct mapping already fits, and a test proves it.** From the code (not yet executed):

- Iris accepts explicit layout (`BlittableProblem` in `packages/Iris/Managed/Iris.Bridge/ComponentRegistry.cs` allows "sequential or explicit layout"), reads managed offsets from the runtime's real layout (`ManagedLayout.OffsetOf`), and matches fields in declaration (metadata-token) order by name, offset and kind; nothing rejects two fields at one offset.
- The native side describes the union with `IRIS_FIELD_AS(Type, member, "Name")` (`offsetof` works on anonymous-union members); `MakeComponent`'s separate `Mapping` type covers private members.
- `ComponentFieldsGenerator` only emits a `ref` property per field, so `Ref<Collider>` gives `ref CollisionShape Shape`; the rest is ordinary C# on the struct.

Decisions:

- **Proof:** FenrirScripting gains a test with a native component holding a tagged union: it validates, round-trips through C#, and is rejected when an offset or a name is wrong. If Iris turns out to reject something, the fix is an additive Iris change (ADR-0006 allows "a call that used to fail and now succeeds").
- **C# shape, hand-written in the physics contract:** `CollisionShape` is `[StructLayout(LayoutKind.Explicit)]` with a private tag at offset 0 and each kind's struct at the union's offset; a read-only `Type`; checked views `[UnscopedRef] public ref BoxShape Box` (and one per kind) that throw `InvalidOperationException` for the wrong kind, matching C++'s asserting `As<T>()`; implicit conversions from each kind. So `collider.Shape = new BoxShape(...)` and `collider.Shape.Box.HalfExtents.Y = 2f` write straight into ECS storage.
- **Script-defined components** may use the same pattern by hand (their struct is their definition and needs only be blittable). No generator support in the foundation; a `[TaggedUnion]` generator for views and conversions waits until a second union (light kinds, camera projections) shows it is worth it. The pattern is documented in FenrirScripting's README.
- **No ADR:** no mechanism added, no contract changed, easy to reverse.

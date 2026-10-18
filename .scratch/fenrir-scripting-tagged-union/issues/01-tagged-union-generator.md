# What does a `[TaggedUnion]` generator emit, and what does its author still write?

Type: grilling
Status: open
Blocked by: none

## Question

FenrirScripting should gain a `[TaggedUnion]` source generator. It must emit exactly the C# surface that is hand-written today for `CollisionShape` and is planned for `JointKind`, so that it can replace both without changing any caller. What is its input, what does it emit, what does it refuse, and what stays hand-written?

## Background

- **Ticket 16** ("How do C# components hold a tagged union?", `.scratch/fenrir-physics/issues/16-csharp-tagged-union-components.md`) found that Iris already maps a C# explicit-layout struct onto a C++ tagged union. Iris checks every field by name, offset and kind, and it matches fields in declaration (metadata-token) order. Nothing stops two fields sharing one offset. So ticket 16 hand-wrote `CollisionShape` (`packages/FenrirPhysics/Managed/Fenrir.Physics/CollisionShape.cs`) and deferred a generator until a second union appeared.
- **Ticket 28** ("What does C# get for joints?", `.scratch/fenrir-physics/issues/28-joints-in-csharp.md`, decision 2) made `JointKind` that second union. It is hand-written in the same pattern, with six kinds: `Fixed`, `Ball`, `Distance`, `Hinge`, `Slider` and `SwingTwist`. The generator is wanted but not yet built. Its **trigger** is "a script author wants a union, or a third engine union appears". The FenrirPhysics map lists it under "Not yet specified".
- **The README** (`packages/FenrirScripting/README.md`, section "Tagged unions") documents the hand-written pattern. It says that "one may come when a second union shows it is worth it". Ticket 28 updates that sentence to say a generator is planned, with the trigger above.
- **Proof that exists today:** `packages/FenrirScripting/Tests/Fixture/Shapes.cs` is a hand-written union. Four `ScriptingTests` cases use it: it validates, it round-trips, the wrong view throws, and a wrong offset or name is refused at load.
- **Existing generators** in `packages/FenrirScripting/Managed/Fenrir.Scripting.Generators`:
  - `ComponentFieldsGenerator` (FENRIR0001–0005);
  - `SystemGenerator` (FENRIR0010–0013);
  - `EventGenerator` (FENRIR0014);
  - `NativeFunctionGenerator` (FENRIR0020–0026);
  - `QueryGenerator`, which runs only on the contract assembly.

  That is five generator classes; the briefing said six, so recount before quoting a number. All of them are incremental and keep their models as plain data (`GeneratorDiagnostics.cs`: `LocationInfo`, `DiagnosticInfo`), and they target netstandard2.0 on Roslyn 4.8. Contract assemblies (`Fenrir.Physics`) and script assemblies both get them, through `Fenrir.Scripting.InRepo.props` and `Fenrir.Scripting.props`.
- **Compile-fail tests** are `fenrir_expect_compile(<Case> <expected id or OK>)` in `packages/FenrirScripting/Tests/CMakeLists.txt`. Each one builds a single file from `Tests/CompileFail/` through `cmake/ExpectCompile.cmake`.

## What the generator must emit (fixed by tickets 16 and 28)

This is the public surface of `CollisionShape` today, and of `JointKind` as planned:

1. `[StructLayout(LayoutKind.Explicit)]`, with a **private one-byte tag** at offset 0 (an enum with underlying type `byte`), and **each kind's struct at the union's offset**.
2. **`public readonly <TagEnum> Type => _type;`**
3. **One checked view per kind, named after the kind:** `[UnscopedRef] public ref <Kind> <Name> { get { Expect(<Tag>.<Name>); return ref _<field>; } }`. Every kind gets one, even a kind with no fields (`FixedJoint`).
4. **One implicit conversion from each kind:** `public static implicit operator <Union>(<Kind> k) => new() { _type = <Tag>.<Name>, _<field> = k };`
5. **`InvalidOperationException` on the wrong view**, thrown from a private `Expect`. The message has the form "The shape is a {_type}, not a {wanted}."

The hand-written unions also do two things that the list above leaves out. The generator must either emit these or leave them to the author:
- **A parameterless constructor that picks a default kind.** `new CollisionShape()` is a unit box, and `new JointKind()` is C++'s default kind. Ticket 28 adds a test that the C# defaults equal C++'s.
- **The private field names.** They are not public surface, but Iris checks them against the C++ mapping's `IRIS_FIELD_AS` names. `CollisionShape` names its `ConvexHull` kind's field `_hull`, not `_convexHull`. So a generator that derives field names would break the existing native mapping. "Replace without touching a caller" covers C# callers only, and the C++ mapping is a caller too.

## Sub-questions

### 1. What does the author write: fields or a list?

- **(a) The author declares the fields.** The author writes a `partial struct` with `[TaggedUnion]`, the tag field and one `[FieldOffset(n)] private Kind _name;` per kind. The generator adds the layout attribute (or requires it), `Type`, the views, the conversions and `Expect`. The author keeps control of offsets, private names and declaration order, which are all things Iris checks. The generator derives only public names.
- **(b) The author gives a list:** `[TaggedUnion(typeof(ShapeType), Offset = 8)]` plus `[UnionKind(typeof(BoxShape), Name = "Box")]` repeated for each kind. The generator emits the fields too. That needs a rule for private names and a field-order guarantee across partial files, because Iris matches in metadata-token order and generated fields come in a separate syntax tree.
- Which option keeps `CollisionShape`'s `_hull` and a byte-identical layout with no C++ change?

### 2. Where do view names and tag values come from?

- Is the view name the tag enum's member name (`ShapeType.ConvexHull` gives the view `ConvexHull`), the field name, or the kind type with a suffix removed (`HingeJoint` gives `Hinge`)? Today `ShapeType` members and view names agree, and the planned `JointType` follows them.
- Is the tag enum written by the author or emitted? The author's enum must mirror C++'s values, so writing it by hand seems right. In that case the generator checks that every enum member has exactly one kind and every kind has exactly one member.
- How does a field map to its enum member? By name, or with an explicit `[UnionKind(ShapeType.ConvexHull)]` on the field? The explicit form handles `_hull`.

### 3. How does the author supply the union's offset?

C# cannot see the C++ union's alignment. `CollisionShape` uses 8 because `CollisionMesh` holds a 64-bit id, and the README example uses 4.
- **Explicit only:** the author writes the offset (as a `[FieldOffset]` under 1a, or `Offset =` under 1b). Iris's load-time check is the safety net. A wrong offset fails the load and names the field.
- **Computed default:** round the tag size up to the largest kind's C# alignment. That matches C++ for the blittable kinds used so far, but C# has no `alignof`, and `Vector3`/`Quaternion` mirrors might differ. Is a default worth a rule that silently disagrees with C++ one day?
- **Compile-time checks the generator can make anyway:**
  - every kind shares one offset;
  - the offset is greater than or equal to the tag's size;
  - the offset is a multiple of each kind's natural alignment (warning or error?).
- Should the C++ side also `static_assert(offsetof(...) == N)` next to its mapping, so the number lives in both places and a mismatch fails the native build? Or is Iris's load check enough?

### 4. Default kind and `default(T)`

- How is the default kind declared? Options: `[TaggedUnion(Default = nameof(Box))]`; the first enum member; or a hand-written constructor in the author's partial, with the generator emitting none.
- `default(CollisionShape)` has tag 0 and a zeroed kind, which is not the same as `new()`. Should this only be documented, or should tag 0 be required to be the default kind?

### 5. The `Expect` message

- Where does the noun ("shape", "joint kind") come from? A `Noun` argument, the type name split into words, or a fixed generic message: "`CollisionShape` holds a `Sphere`, not a `Box`."? Do existing tests or docs quote the current message?

### 6. Diagnostics

The IDs need a block, perhaps FENRIR0030–0039, since 0006–0009 and 0015–0019 are gaps inside other generators' ranges. Each diagnostic needs a decision: error or warning, and whether it is in or out:
- the type is not a `struct`, or is not `partial` (and nor are the types containing it, as with FENRIR0022);
- the type is generic;
- the type is a `readonly struct`, which cannot hand out writable `ref` views;
- the tag is not an enum with underlying type `byte` (or is any one-byte tag fine?);
- a tag member has no kind, or a kind has no tag member;
- two fields share one kind type, which would produce duplicate implicit conversions (CS0557 otherwise, with a worse message);
- a kind is not unmanaged (as FENRIR0004), or is the union itself;
- a kind is not at the shared offset, or the offset overlaps the tag;
- a generated member (`Type`, a view, `Expect`) collides with a member the author wrote;
- a kind holds a `bool`. The README warns about this only for by-value copies from C++, which the generator cannot see. So is it a warning, an info diagnostic, or nothing?

### 7. Tests

- **Compile-fail:** a `TaggedUnionControl OK` case plus one `fenrir_expect_compile` case per diagnostic that is kept. Also an OK case for each tricky shape: an empty kind, a nested union type, and a field name that does not match its view (`_hull`).
- **Equivalence:** replace `Tests/Fixture/Shapes.cs` with a generated union, and require the four existing `ScriptingTests` tagged-union cases to pass unchanged. Is that the acceptance test?
- **Generator unit tests:** none of the existing generators have Roslyn-level snapshot tests; they rely on `ExpectCompile`. Should this generator stay consistent with that, or add golden-file output tests?

### 8. Do script-defined unions use it?

- The trigger includes "a script author wants a union", so presumably yes: it is the same attribute in any assembly with the generators. Should scripts have looser rules than contracts? A script union has no C++ side, so a computed offset could be safe for scripts and explicit-only for contracts.
- A union is a field inside a component, and `ComponentFieldsGenerator` gives `ref CollisionShape Shape` on `Ref<Collider>`, which needs no change. Can a union be a component itself? `ComponentFieldsGenerator` emits nothing for private fields, so `Ref<U>.Value.Box` would be the only way in. Allow it, refuse it, or ignore it?

### 9. How does the C++ side stay hand-written?

- The generator emits no C++. The native union, its `As<T>()`, and its `Iris::StructMapping` (or, for packages that must not name Iris, the plain stand-in mapped through `MakeComponent<T, Mapping>`, as for `CollisionShape` and `JointKind`) stay hand-written in the owning package.
- The coupling is the private field names, the tag values, the offset and declaration order, and Iris already checks all of them at load. Is anything more wanted? For example, a FenrirScripting C++ helper macro that lists the tag and kinds once, or a doc rule saying "add the kind on both sides in the same order"?
- Adding a kind: the C++ union, the C++ enum, the mapping, the C# enum, and one C# field (under 1a) or one attribute (under 1b). Is that acceptable, or should the generator check against the enum (sub-question 2) so that forgetting the C# field fails at compile time rather than at load?

### 10. Migration, timing and records

- Does the ticket that builds the generator also move `CollisionShape` and `JointKind` (and `Tests/Fixture/Shapes.cs`) onto it, or does each move as a separate follow-up?
- Build it now, ahead of the trigger, because the user considers it important? Or keep it specified and ready, waiting on the trigger?
- The README's "Tagged unions" section is rewritten around the attribute, keeping the hand-written form as "what it expands to".
- Is an ADR needed? It adds a public attribute to the `Fenrir.Scripting` contract (ADR-0004) without touching Iris (ADR-0006), and it is additive and easy to reverse. Ticket 16 skipped an ADR on the same grounds.

### Out of scope unless grilling pulls them in

- Equality, `ToString`, a `Match`/`Switch` helper, or pattern-matching support. None of these exist on the hand-written unions, so emitting them would break "exactly the hand-written surface".
- Generating C++ from C#, or C# from C++ headers.

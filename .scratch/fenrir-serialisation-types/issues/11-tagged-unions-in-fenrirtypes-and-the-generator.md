# 11: Tagged unions in FenrirTypes and the registry generator

**What to build:** `TypeDescription` describes a `tagged-union` (the description's category since ADR-0042: one enum tag followed by one anonymous union, `SCHEMA.md` "tagged-union"), its visitor pushes the tag and then only the live alternative, and the registry generator writes both, so `CollisionShape`, `JointKind`, `VehicleKind` and the renderer's camera register in ticket 07. Ticket 05 found the gap: FenrirTypes (ticket 01) has no tag or alternatives, so a table of offsets cannot say which union member is live, and the format spec writes a tagged union as an object whose first member is `kind` (format spec, "Tree forms nobody decided"). Ticket 05 refuses a tagged union, and every type holding one, as "not yet described by FenrirTypes" and skips them; this ticket lifts that.

- **The description:** a `TypeDescription` of category tagged union carries its tag as a `FieldInfo` (kind `enum`, its enum's description, its offset), the union's offset, its alternatives in declaration order (each the member's name, the tag value that selects it, a `FieldInfo` of its kind keys at the union's offset, and its doc) and the default alternative. The kinds an alternative may have are those the description allows in a union today (fixed-layout only).
- **The visitor:** `Visit` pushes the tag first, then the live alternative's field alone, read at the union's offset; a const visit never reads a dead member. A caller cannot tell this from a struct's visit except by the description's category.
- **Choosing an alternative:** a mutable caller switches the live alternative through one `TypeDescription` door (for example `SelectAlternative(void* instance, tag value)`) that writes the tag and value-initialises the new alternative with that alternative's own defaults, so a reader sets the tag first and then fills the live member. Writing the tag field directly through `ValueRef` is refused (a diagnostic), since it would leave the union's bytes those of the old member.
- **The generator:** `tools/generate-type-registry` writes a tagged union's description, its alternatives and the select door from the `tag`, `unionOffset`, `kinds` and `defaultKind` keys; the ticket-05 refusal and skip of tagged unions, and of types holding them, go. `Construct` builds the default alternative as the default constructor does.
- **Tests:** in FenrirTypes, the describe-api fixture's `Fixture::Shape` (nested, held by the `Collider` component) and `Fixture::Sky` (an app resource) register; a recording visitor sees the tag, then `m_box` for a box and `m_sphere` alone for a sphere; selecting `Sphere` on a box writes the tag and a `SphereShape` with its default `radius`; writing the tag through `ValueRef` is a diagnostic. The FenrirTypes test package (ticket 06) gains a tagged union and its committed registration regenerates byte-identical. In the generator's xunit tests, the fixture's committed registration changes only by the tagged unions it now holds.

**Blocked by:** 06.

**Touches:** `packages/FenrirTypes/` (`include/FenrirTypes/`, `src/`, `Tests/`, `Tests/Package/`), `tools/generate-type-registry/src/` and `tests/`, `tools/describe-api/tests/Fixture/` (the fixture's `.api.toml` if the ticket-05 skip flag lives there)

**Status:** ready-for-agent

- [ ] `TypeDescription` holds a tagged union's tag, union offset, alternatives and default alternative; `PublicHeaders` passes.
- [ ] A recording `FieldVisitor` over `Fixture::Shape` sees the tag then only the live alternative, for each alternative.
- [ ] Selecting an alternative writes the tag and that alternative's defaults; writing the tag through `ValueRef` is a diagnostic, not a write.
- [ ] The generator writes `Fixture::Shape`, `Fixture::Collider` and `Fixture::Sky` instead of refusing them, and the ticket-05 refusal and skip are gone.
- [ ] The test package's tagged union registers, and its committed registration regenerates byte-identical.

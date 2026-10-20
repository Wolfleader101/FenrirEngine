# 03: `[TaggedUnion]` diagnostics

**What to build:** The `[TaggedUnion]` generator refuses every malformed union with its own error, before any generated code can fail with a worse message (ticket 01's Answer, decision 3). All are errors, following the existing generators' `GeneratorDiagnostics` pattern.

| ID | Refused |
|---|---|
| FENRIR0030 | The type is not a `partial struct`, or a type containing it is not partial (as FENRIR0022). |
| FENRIR0031 | The struct is `readonly`, generic, or a `ref struct`. |
| FENRIR0032 | There is no single enum-typed field at offset 0 to be the tag. |
| FENRIR0033 | A kind field is not a private instance field, or its name is not `_` plus a tag member, first letter lowered. |
| FENRIR0034 | A tag member has no kind field. |
| FENRIR0035 | The kind fields do not share one offset, or that offset is less than the tag's size. |
| FENRIR0036 | A kind is not unmanaged, is the union itself, or has the same type as another kind. |
| FENRIR0037 | The author wrote a member the generator emits: a parameterless constructor, `Type`, a view, a conversion or `Expect`. |
| FENRIR0038 | The tag enum has no member with value 0, or two members share a value. |

- When a diagnostic fires, the generator emits nothing for that union, so the build shows the FENRIR error alone.
- Each message names the union and the offending field, member or type, and says what to write instead.
- No diagnostic for a `bool` inside a kind, or for a missing `[StructLayout(LayoutKind.Explicit)]` (the compiler's CS0636 covers it).

**Blocked by:** 02 (`[TaggedUnion]` tracer bullet)

**Status:** resolved (built on branch `claude/tagged-union-diagnostics`, on top of 02's). Where the build settled something the ticket left open:
- `[TaggedUnion]` stays struct-only, as ticket 01 decided, so a class gets the compiler's CS0592 beside FENRIR0030; its case allows that one extra error, as `NativeFunctionTypeNotPartial` allows CS8795. A union that is its own kind likewise gets the compiler's CS0523 (a layout cycle) beside FENRIR0036.
- FENRIR0033 also refuses a `readonly` kind field, which a writable view cannot return.
- A misnamed kind field is reported once, as FENRIR0033 naming the field the unpaired tag member would have, and not also as FENRIR0034 for that member.

- [x] One `fenrir_expect_compile` case per ID in `Tests/CompileFail/`, each failing with exactly that ID and no other error. Where one ID covers several shapes (0030 a class and a non-partial container; 0031 readonly, generic and `ref struct`; 0035 mismatched offsets and an offset inside the tag; 0036 managed, self and duplicate kinds; 0037 each emitted member; 0038 no zero and a duplicate value), a case for each.
- [x] The OK cases from ticket 02 still pass.

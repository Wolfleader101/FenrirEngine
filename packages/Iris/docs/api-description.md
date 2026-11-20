# The API description: what Iris.Bindings reads

An **API description** (`<Package>.api.json`) is a language-neutral record of the plain-data types a C++ package shares
with bindings: each type's layout, field kinds, default values and doc comments, its enums and its tagged unions. A header
reader writes it from the package's headers (in FenrirEngine, `tools/describe-api`, whose `SCHEMA.md` this copies);
[`Tools/Iris.Bindings`](../Tools/Iris.Bindings/README.md) reads it and never the headers. This is the input format the
generator accepts, kept here so Iris does not depend on the tool that writes it. `Tools/Iris.Bindings/tests/Fixture/Fixture.api.json`
is a worked example of every part below.

The file is UTF-8 JSON. Every list is in a fixed order, so the same headers always give the same bytes, on any platform.

## The file

| Key | Value |
| --- | --- |
| `package` | The package's name: the selection's file name without `.api.toml`. |
| `pointerSize` | Always `8`. The tool refuses a target with other pointers. |
| `alignment` | Always `"natural"`: the layout rules MSVC, Clang and GCC share for standard-layout data on a 64-bit target. Sizes, offsets and alignments below are for it. |
| `types` | Every described type, dependencies first (a type comes after every type its fields name), then by name (ordinal). |

## Types

Every type has:

| Key | Value |
| --- | --- |
| `name` | The C++ qualified name: as the selection spells it for a selected type (an alias such as `Math::Vec3` included), else clang's canonical spelling (`Ns::Type`). |
| `category` | `struct`, `tagged-union`, `bound-type` or `enum`. |
| `role` | Not on enums. `component`, `event`, `scene-resource`, `app-resource`, `shared-struct`, `data-asset`, `settings` or `bound-type` for a selected type; `nested` for a type described only because a field uses it. Iris.Bindings skips `data-asset` and `settings` types (see "Owning kinds"). |
| `base` | For a `data-asset` extending another: the `name` of the data asset it extends. Absent otherwise. |
| `size`, `alignment` | In bytes. An empty struct is 1 byte. Absent for a struct reached by member (see "Owning kinds"). |
| `doc` | The doc comment, when there is one (see "Doc comments"). |
| `version` | The type's version from `@version`, when it is above 1; absent means 1 (see "Type and field doc commands"). |
| `formerly` | The type's former qualified names from `@formerly`, in the order written, when it has any. |
| `transient` | `true` when the type is marked `@transient`: plain run-time data a file never holds (absent otherwise). |
| `editorOnly` | `true` when the type is marked `@editor-only`: editor data a cooked build strips (absent otherwise). |

### `struct`

Plain data. `fields` lists every non-static data member in declaration order (which is storage order), private ones included when the header reader was told to share them. A field is:

| Key | Value |
| --- | --- |
| `name` | The C++ member name. |
| `offset` | Bytes from the start of the struct. |
| `kind` | See "Kinds". |
| `type` | For `enum` and `struct` kinds: the `name` of the described type. |
| `target` | For the `reference` kind: the canonical name of the type the handle names (`Fenrir::Mesh`), never described for it. |
| `elementKind`, `elementType`, `elementTarget`, `length` | For the `array` kind: the element's kind, the element's type for `enum` and `struct` elements, its target for `reference` elements, and the element count. For the `fixed-string` kind, `length` alone: its bytes. For the `list` and `optional` kinds, the element keys alone. |
| `element` | For a `list` or `optional` whose element has keys of its own (an `array`, a `fixed-string`, a `list` or an `optional`): every key of the element, `kind` first. |
| `default` | See "Defaults". Every field has one. |
| `doc` | The member's doc comment, when there is one. |
| `formerly` | The member's former names from `@formerly`, in the order written, when it has any. |
| `metadata` | What its metadata commands give, when it has any: an object with only the keys of the commands present (see "Type and field doc commands"). |

### `tagged-union`

A class whose only non-static data members are an enum tag followed by one anonymous union (its members may be private). It records:

| Key | Value |
| --- | --- |
| `tag` | The tag, as a field (above): kind `enum`, and as `default` the tag value of the default kind. |
| `unionOffset` | Where every kind's value starts. |
| `kinds` | The union's members in declaration order: `name` (the C++ member name), `tag` (the tag value that selects it), its kind keys (`kind`, `type`, ...) and `doc`. The kinds pair with the tag enum's values in declaration order; a constructor that pairs them otherwise fails the tool. |
| `defaultKind` | The kind the default constructor holds, which it holds with that kind's own defaults. |

### `bound-type`

A type every binding has its own of (a vector, a quaternion, an entity handle). Its `size` and `alignment` are recorded, and it need not be plain data. When every value it holds is a scalar, `components` lists them in storage order, each with `name`, `offset` and `kind` (a number kind, `bool` or `char16`): a member of an anonymous union is its first member's, and a nested struct's members and an array's elements (a `std::array`'s as its C array's) are named by path (`value[0].x`), so a vector is its three `f32` and a 4x4 matrix its sixteen. A bound type holding anything else (a pointer, an enum) has no `components`. A binding's mirror is its own, so Iris.Bindings does not read them.

### `enum`

Described when a field uses it, or when the selection names it with the role `enum` (one a native function takes that no field holds); either way it records no role. It records `underlying` (an integer kind) and `values`: each enumerator's `name`, `value` (a JSON integer) and `doc`, in declaration order.

## Kinds

| Kind | Bytes |
| --- | --- |
| `i8`, `i16`, `i32`, `i64` | Signed integers. |
| `u8`, `u16`, `u32`, `u64` | Unsigned integers. A C++ `char` is a `u8` (a byte of text) on every target (an array of them is a `fixed-string`), and a `char32_t` (a UTF-32 code point) a `u32`. |
| `f32`, `f64` | IEEE floats. |
| `bool` | One byte, 0 or 1. |
| `char16` | A UTF-16 code unit (`char16_t`). |
| `enum` | An enum of its underlying kind; `type` names it. An enum with no enumerators (`enum class Id : std::uint32_t {}`) is a distinct integer type rather than a set of values, and is recorded as its underlying integer kind. |
| `struct` | A described struct, tagged union or bound type, held by value; `type` names it. |
| `array` | `length` elements of `elementKind`, contiguous. An array's elements are never arrays or fixed strings. |
| `pointer` | 8 opaque bytes: any pointer, its target never described. |
| `entity` | 16 bytes: the engine's entity (`Fenrir::Entity`), recognised by its qualified name. It is never described for the kind: FenrirECS's description, which selects it as a `shared-struct`, records its layout. |
| `fixed-string` | `length` bytes of UTF-8 text, zero-terminated: a C array of `char` or `char8_t` (`char name[256]`), holding at most `length` - 1 bytes. A reader cuts longer text at `length` - 1 bytes on a UTF-8 boundary. An array of `signed char`, `unsigned char` or `std::uint8_t`, and a `std::array` of `char`, stay an `array` of `u8`. |
| `reference` | 8 bytes: an asset handle (`Fenrir::AssetHandle<T>`, recognised by its qualified name), a `u32` slot index then a `u32` generation; `target` names `T`. The handle is never described for the kind. |
| `string`, `list`, `optional`, `path` | The owning kinds: a `std::string`, a `std::vector<T>` (any element kind, a `list` included), a `std::optional<T>` (any but `optional`) and a `std::filesystem::path`, with the element keys for a list and an optional. Only a `data-asset` or `settings` type, or a nested struct only they reach, holds one. |

Anything else (a C++ reference, `T&`, `long double`, `wchar_t`, a bit-field, ...) fails the tool.

## Owning kinds

A `data-asset` or `settings` type is reached by member through a generated visitor, so it may hold the owning kinds. It, and every struct whose layout holds an owning kind, records no `size`, `alignment` or field `offset`: they are the standard library's. A `data-asset` may extend one other, recorded as `base`, and its `fields` are only those it declares. Iris.Bindings skips every `data-asset` and `settings` type and every nested type nothing else reaches, without failing or logging (the C# spec binds them), and fails naming the field on an owning kind or a missing layout in any type it binds.

## Defaults

Each field's `default` is the value its default member initialiser gives, recorded as a value:

| Kind | `default` |
| --- | --- |
| integers, `char16` | A JSON integer. |
| `f32`, `f64` | A JSON number, the shortest that reads back as the same float, or the string `"inf"` or `"-inf"`. |
| `bool` | `true` or `false`. |
| `enum` | The enumerator's name, as a string. |
| `pointer` | `null` (only a null pointer is recorded). |
| `struct` of a described struct or tagged union | An object of the members the initialiser sets, in storage order, each with its own `default` value; `{}` when it sets none, which is the type's own defaults (C++ `T{}`). |
| `struct` of a bound type | An object of every component's value, by component name, in storage order; `{}` for the bound type's own default. |
| `array` | A list of `length` element defaults. |
| `entity`, `reference` | `null`: the empty entity or handle, the only default recorded. |
| `fixed-string` | The text, as a string: `""` when empty. |
| `string`, `path` | The text, as a string: `""` when empty. |
| `list` | `[]`, the only default recorded. |
| `optional` | `null`, the only default recorded. |

The initialisers recorded are: for a number or `bool`, any constant expression libclang evaluates, recorded as its value: a literal (`1.0f`, `0.05f`, `true`, `u'A'`), a named constant (`inline constexpr bool DebugValidationByDefault = ...`), a `std::numbers` constant, `std::numeric_limits<T>::infinity()`, `max()` and `lowest()`, or operators applied to those (`-1.0f`, `~0u`, `1u << 3`, `2 * 4`, `std::numbers::pi_v<float> / 4.0f`); an enumerator; `{}` or `T{}` or no initialiser for a nested struct or bound type, and an aggregate initialiser setting some of a nested struct's members (`{.frequency = 2.0f}`); a constant of a nested struct's type that is const and defined with one of these initialisers (`Colour::Black`, declared `static const` in its class and defined `inline constexpr Colour Colour::Black{0.0f, 0.0f, 0.0f, 1.0f}`), recorded as the values its initialiser gives; a bound type's constructor with literal arguments; and for arrays, a braced list of those (missing elements are zero). A field of an enum with no enumerators has no enumerator to name, so its initialiser may be any constant the reader evaluates, recorded as the integer. An entity or reference records only the empty value. A fixed string's initialiser is a string literal, a braced list of character constants or `{}`, and no initialiser is the empty string. A scalar, enum, pointer or array of them with no initialiser fails, as does any other expression.

A bound type's constructor arguments are matched to its components by name: its components are its fields in storage order (of an anonymous union, the first member's), and each parameter names one, ignoring leading underscores (`Quat(float w, float x, float y, float z)` and `qua(T _w, T _x, T _y, T _z)` both set `w` first). A constructor with one argument sets every component to it (`Vec3(0.5f)`). Anything else fails.

## Doc comments

A doc comment (`/** ... */`, `/*! ... */`, `///`) is recorded as text without its markers and its `@brief` tag. The lines of a paragraph are joined with spaces; paragraphs are separated by a blank line (`\n\n`). A line that starts with `@` and a word is a doc command, never text: the commands below are split out, and any other (`@param` on a type, `@copydoc`, a misspelling) fails the tool, naming the type, member and command. Nothing else is rewritten: cross-references stay as written.

## Type and field doc commands

What a file and an inspector need from a type is written in its header as doc commands, as a native's `@param` is (`natives-description.md`, "Doc commands"): each on a line of its own in the doc comment, recorded as keys rather than text. A type's comment (a struct, tagged union, bound type or enum) takes:

| Command | Records |
| --- | --- |
| `@version N` | `version`: a whole number from 1, given once. A file records the version it was written at, and a reader upgrades an older one. |
| `@formerly Old::Name` | One of `formerly`: a qualified name the type had before, which a file may still spell. Repeatable. |
| `@transient` | `transient`: plain run-time data (a contact list, a cache) that a scene file never holds. |
| `@editor-only` | `editorOnly`: editor data (layers, folded state) that a cooked build strips. |

A field's comment (a struct's member, or a tagged union's tag) takes:

| Command | Records |
| --- | --- |
| `@formerly oldName` | One of `formerly`: a name the member had before. Repeatable. |
| `@range min max` | `metadata.range`, an object of `min` and `max`: two numbers, the maximum not below the minimum. |
| `@step s` | `metadata.step`: a number above 0, the increment an inspector's control moves by. |
| `@display text` | `metadata.display`: the name an inspector shows instead of the member's. |
| `@group text` | `metadata.group`: the heading an inspector shows the member under. |
| `@readonly` | `metadata.readonly`: `true`; an inspector shows the value but does not edit it. |
| `@hidden` | `metadata.hidden`: `true`; an inspector does not show it. |
| `@multiline` | `metadata.multiline`: `true`; an inspector edits its text over several lines. |
| `@units text` | `metadata.units`: the units an inspector shows beside the value (`m/s`, `percent`). |

A number is written as JSON's, the shortest that reads back as the same value. A text command's text is the rest of its line; a flag takes nothing after it. Every command but `@formerly` is given at most once. A type's command on a field, a field's on a type, any command on an enumerator or a tagged union's kind, a malformed one (`@version 0`, `@range 10`, `@formerly` with no name) or a second of one fails the tool, naming the type, member and command. Nothing checks a command against the member's kind: `@multiline` on a number is recorded as written.

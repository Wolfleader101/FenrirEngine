# The API description schema

`<Package>.api.json` is what `describe-api` writes from a package's selection (`README.md`). It is language-neutral: a binding's generator reads it, never the headers. `tests/Fixture/Fixture.api.json` is a worked example of every part below.

Iris keeps a copy of this schema as the input format its binding generator accepts (`packages/Iris/docs/api-description.md`), so a change to it changes that copy too.

The file is UTF-8 JSON with two-space indents, LF line ends and a final newline. Every list is in a fixed order, so the same headers always give the same bytes, on any platform.

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
| `role` | Not on enums. `component`, `event`, `scene-resource`, `app-resource`, `shared-struct`, `data-asset`, `settings` or `bound-type` for a selected type; `nested` for a type described only because a field uses it. |
| `base` | For a `data-asset` extending another: the `name` of the data asset it extends (see "Owning kinds"). Absent otherwise. |
| `size`, `alignment` | In bytes. An empty struct is 1 byte. Absent for a struct reached by member (see "Owning kinds"), whose layout is the standard library's. |
| `doc` | The doc comment, when there is one (see "Doc comments"). |
| `version` | The type's version from `@version`, when it is above 1; absent means 1 (see "Type and field doc commands"). |
| `formerly` | The type's former qualified names from `@formerly`, in the order written, when it has any. |
| `transient` | `true` when the type is marked `@transient`: plain run-time data a file never holds (absent otherwise). |
| `editorOnly` | `true` when the type is marked `@editor-only`: editor data a cooked build strips (absent otherwise). |

### `struct`

Plain data. `fields` lists every non-static data member in declaration order (which is storage order), private ones included when the selection's `private-members` names the type. A field is:

| Key | Value |
| --- | --- |
| `name` | The C++ member name. |
| `offset` | Bytes from the start of the struct. Absent in a struct reached by member. |
| `kind` | See "Kinds". |
| `type` | For `enum` and `struct` kinds: the `name` of the described type. |
| `target` | For the `reference` kind: the canonical name of the type the handle names (`Fenrir::Mesh`), never described for it. |
| `elementKind`, `elementType`, `elementTarget`, `length` | For the `array` kind: the element's kind, the element's type for `enum` and `struct` elements, its target for `reference` elements, and the element count. For the `fixed-string` kind, `length` alone: its bytes. For the `list` and `optional` kinds, the element keys alone, with no `length`. |
| `element` | For a `list` or `optional` whose element has keys of its own (an `array`, a `fixed-string`, a `list` or an `optional`): every key of the element, `kind` first, as a field's kind keys are (`{"kind": "list", "elementKind": "i32"}` for a list of lists of `i32`). |
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

A type every binding has its own of (a vector, a quaternion, an entity handle). Its `size` and `alignment` are recorded, and it need not be plain data. When every value it holds is a scalar, `components` lists them in storage order, each with `name`, `offset` and `kind` (a number kind, `bool` or `char16`): a member of an anonymous union is its first member's, and a nested struct's members and an array's elements (a `std::array`'s as its C array's) are named by path (`value[0].x`), so a vector is its three `f32` and a 4x4 matrix its sixteen. Private members are read too: they are the layout. A bound type holding anything else (a pointer, an enum) has no `components`. A bound type that is a class template's specialisation the headers never instantiate (glm's quaternion, when nothing in them holds one) is instantiated to read its layout.

### `enum`

Described when a field uses it, or when the selection names it with the role `enum` (one a native function takes that no field holds); either way it records no role. It records `underlying` (an integer kind) and `values`: each enumerator's `name`, `value` (a JSON integer) and `doc`, in declaration order.

## Kinds

| Kind | Bytes |
| --- | --- |
| `i8`, `i16`, `i32`, `i64` | Signed integers. |
| `u8`, `u16`, `u32`, `u64` | Unsigned integers. A `char` is a `u8` (a byte of text) on every target (an array of them is a `fixed-string`), and a `char32_t` (a UTF-32 code point) a `u32`. |
| `f32`, `f64` | IEEE floats. |
| `bool` | One byte, 0 or 1. |
| `char16` | A UTF-16 code unit (`char16_t`). |
| `enum` | An enum of its underlying kind; `type` names it. An enum with no enumerators (`enum class Id : std::uint32_t {}`, EnTT's `entt::entity`) is a distinct integer type rather than a set of values, and is recorded as its underlying integer kind. |
| `struct` | A described struct, tagged union or bound type, held by value; `type` names it. |
| `array` | `length` elements of `elementKind`, contiguous: a C array, or a `std::array`, which is recorded as the C array it holds (the same bytes). An array's elements are never arrays or fixed strings. |
| `pointer` | 8 opaque bytes: any pointer, its target never described. |
| `entity` | 16 bytes: the engine's entity (`Fenrir::Entity`), recognised by its qualified name. It is never described for the kind: FenrirECS's description, which selects it as a `shared-struct`, records its layout. |
| `fixed-string` | `length` bytes of UTF-8 text, zero-terminated: a C array of `char` or `char8_t` (`char name[256]`), holding at most `length` - 1 bytes. A reader cuts longer text at `length` - 1 bytes on a UTF-8 boundary. An array of `signed char`, `unsigned char` or `std::uint8_t`, and a `std::array` of `char`, stay an `array` of `u8`. |
| `reference` | 8 bytes: an asset handle (`Fenrir::AssetHandle<T>`, recognised by its qualified name), a `u32` slot index then a `u32` generation; `target` names `T`. The handle is never described for the kind. |
| `string` | Owned UTF-8 text: a `std::string`. A `std::basic_string` of another character type fails the tool. |
| `list` | A `std::vector<T>` of any kind, a `list` included, recorded with the element keys. A `std::vector<bool>`, whose elements are bits, fails the tool. |
| `optional` | A `std::optional<T>` of any kind but `optional`, recorded with the element keys. |
| `path` | A `std::filesystem::path`, written as its text. |

`string`, `list`, `optional` and `path` are the **owning kinds** (see below), recognised by their qualified names in `std`. Anything else (a C++ reference, `T&`, `long double`, `wchar_t`, a bit-field, an array of an owning kind, ...) fails the tool.

## Owning kinds

A `data-asset` (ADR-0075) or `settings` type (ADR-0084) is read and written through a generated visitor that reaches each field by member, never at an offset, so it may hold the owning kinds, whose bytes are the standard library's own. The rest of the roles keep their fixed layout:

- **Who may hold them.** A `data-asset` or `settings` type, and a nested struct that only those reach (by value, through any chain of fields, list elements and optionals included). An owning kind in a `component`, `event`, `scene-resource`, `app-resource` or `shared-struct`, or in a nested struct one of them reaches, fails the tool naming the type, member and role; a struct holding one that both a fixed-layout role and a data asset or settings type reach fails naming a path from each. A struct holding none may be reached from both: a data asset may hold a component type by value.
- **Reached by member.** A `data-asset` or `settings` type, and every struct whose layout holds an owning kind (itself, in a struct it holds by value, or in a data asset it extends), records no `size`, `alignment` or field `offset`: they differ between standard libraries, so a description that recorded them would not be the same bytes on every platform, and the code a generator writes for the type takes them from the compiler. A nested struct of fixed kinds that only data assets reach keeps its layout.
- **Bases.** A `data-asset` may extend one other type, which the selection selects as a `data-asset`, recorded as `base`; the base's fields are its own description's, and the type's `fields` are only those it declares. A second base, a virtual base, or a base the selection does not select as a `data-asset` fails the tool. No other role's base may have data members.
- **Plain data still.** A virtual function (a virtual destructor included) fails the tool in every role. A `data-asset` or `settings` type may declare its own copy, move, assignment or destructor; any other may not.

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
| `list` | `[]`: the empty list, the only default recorded. |
| `optional` | `null`: empty, the only default recorded. |

The initialisers recorded are: for a number or `bool`, any constant expression libclang evaluates, recorded as its value: a literal (`1.0f`, `0.05f`, `true`, `u'A'`), a named constant (`inline constexpr bool DebugValidationByDefault = ...`), a `std::numbers` constant, `std::numeric_limits<T>::infinity()`, `max()` and `lowest()`, or operators applied to those (`-1.0f`, `~0u`, `1u << 3`, `2 * 4`, `std::numbers::pi_v<float> / 4.0f`); an enumerator; `{}` or `T{}` or no initialiser for a nested struct or bound type, and an aggregate initialiser setting some of a nested struct's members (`{.frequency = 2.0f}`); a bound type's constructor with literal arguments; and for arrays, a braced list of those (missing elements are zero; a `std::array`'s with its braces elided or not). A field of an enum with no enumerators has no enumerator to name, so its initialiser may be any constant libclang evaluates (`entt::null`, which converts to all bits set), recorded as the integer. An entity or reference records only the empty value: no initialiser, `{}`, `Entity()` or `AssetHandle<T>()`. A fixed string's initialiser is a string literal, a braced list of character constants (its text ends at the first zero) or `{}`, and no initialiser is the empty string; text that fills every byte, leaving no terminator, or is not UTF-8 fails. A string's or path's initialiser is a string literal it is constructed from (`= "assets"`, `{"assets"}`), and `{}` or no initialiser is the empty text. A list's is `{}` or none, and an optional's `{}`, `std::nullopt` or none: anything else fails. A scalar, enum, pointer or array of them (a `std::array` too) with no initialiser fails, as does any other expression.

A bound type's constructor arguments are matched to its components by name: its components are its fields in storage order (of an anonymous union, the first member's), and each parameter names one, ignoring leading underscores (`Quat(float w, float x, float y, float z)` and `qua(T _w, T _x, T _y, T _z)` both set `w` first). A constructor with one argument sets every component to it (`Vec3(0.5f)`). Anything else fails.

## Doc comments

A doc comment (`/** ... */`, `/*! ... */`, `///`) is recorded as text without its markers and its `@brief` tag. The lines of a paragraph are joined with spaces; paragraphs are separated by a blank line (`\n\n`). A line that starts with `@` and a word is a doc command, never text: the commands below are split out, and any other (`@param` on a type, `@copydoc`, a misspelling) fails the tool, naming the type, member and command. Nothing else is rewritten: cross-references stay as written.

## Type and field doc commands

What a file and an inspector need from a type is written in its header as doc commands, as a native's `@param` is ("Doc commands" below): each on a line of its own in the doc comment, recorded as keys rather than text. A type's comment (a struct, tagged union, bound type or enum) takes:

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

# The natives description schema

`<Name>.natives.json` is what `describe-api --natives` writes beside a binding's configuration `<Name>.binding.toml` (`README.md`, "Native functions"). Unlike an API description it belongs to one binding: it records the C++ functions that binding's script language calls, so that the binding's generator can write their declarations and registration from them. `tests/Fixture/Fixture.natives.json` is a worked example of every part below. Iris keeps a copy of this section as the natives input its generator accepts (`packages/Iris/docs/natives-description.md`).

The format is the API description's: UTF-8 JSON, two-space indents, LF line ends, a final newline, the same bytes on any platform.

## The file

| Key | Value |
| --- | --- |
| `binding` | The configuration's file name without `.binding.toml`. |
| `context` | The qualified name of the call context every native takes first, as `[natives] context` gives it. |
| `functions` | Every native function, in the order of `[natives] sources` and, within a source, of their definitions. |

A **native function** is a free function defined in one of the sources whose first parameter is `const Context&`. Its second parameter is its **state** when it is a reference to a non-const class; every later parameter is described. Member functions, function templates and declarations without a definition are never natives. A native needs external linkage (the generated registration declares and names it from another file), named parameters, and a parameter list the kinds can record; anything else fails the tool, naming the function and parameter.

## Functions

| Key | Value |
| --- | --- |
| `name` | The C++ name, which is also the name the binding gives it. Two natives of one name fail the tool. |
| `namespace` | The namespaces (and classes) enclosing it, `::`-joined; empty at global scope. |
| `source` | The source defining it, relative to the description, with `/`. |
| `state` | The qualified name of its state's class, when it takes one. |
| `noexcept` | `true` when it is declared `noexcept` (absent otherwise). |
| `doc` | Its doc comment without the `@param`, `@return`, `@notrunning` and `@facade` commands (see below), when there is one. |
| `notRunning` | The C++ expression its `@notrunning` gives: the value a caller gets when the native cannot be called (absent otherwise). |
| `facade` | The `Class.Method` its `@facade` gives: the public static class, in the binding's namespace, and the method name the binding gives the native there, as a pass-through of its declaration (absent otherwise). |
| `facadeReturns` | The name of the single output its `@facade ... returns <parameter>` names: the binding's method gives that output as its value and drops the native's `bool` result (absent otherwise). |
| `returns` | Its result, as a type (below), with `doc` from `@return` (or `@returns`). |
| `parameters` | Every parameter after the call context and the state, in order: `name`, its type (below), `output`, `inOut` or `input`, and `doc`. |

## Types

A parameter or result is a value or one pointer to a value:

| Key | Value |
| --- | --- |
| `type` | The C++ spelling, usable in a declaration: `float`, `std::uint32_t`, `const Ns::Vec3*`, `Ns::Hit*`. A number is spelled by its kind (`std::int32_t`, never `int` or `long`), unless it is written as an alias outside `std` and the global namespace (`Ns::EntityId`), which is kept so a binding can map it. A struct or enum is its qualified name, or the alias it is written as. |
| `kind` | `void`, a number kind (`i8` ... `u64`, `f32`, `f64`), `bool`, `char16`, `string`, `struct`, `enum`, or `pointer`. `char` is `i8` or `u8` by the target's signedness, and `char32_t` is `u32`. `string` is a `std::string_view` by value, spelled `std::string_view`: UTF-8 text, which a binding passes as a pointer and a length. |
| `pointee`, `pointeeKind` | For a pointer: what it points at, spelled and classified as above. |
| `const` | `true` for a pointer to const (absent otherwise). |

A reference (after the context and state), a pointer to a pointer, a pointer to a `std::string_view`, and a type of no kind (`long double`, `wchar_t`) fail the tool.

## Doc commands

The doc comment is split: `@param name text` (or `\param`) gives that parameter's `doc`, `@param[out] name text` also marks it an `output`, `@param[in,out] name text` marks it `inOut` (a pointer the native reads and writes), `@param[in] name text` marks it `input` (a pointer to one value the native reads, which a binding never takes for a span, whatever parameter follows it), and `@return text` (or `@returns`) gives the result's `doc`; each command's text runs to the next command or blank line. `@notrunning expression` gives `notRunning`, the rest of its line, and `@facade Class.Method` (or `\facade`) gives `facade`: two identifiers joined by one `.`, both always written, followed at most by `returns <parameter>`, which gives `facadeReturns`. `@param[out]` or `@param[in,out]` on anything but a pointer to non-const data fails the tool, as do `@param[in]` on anything but a pointer to const data, `@param` naming no parameter, `@notrunning` on a native that returns nothing, a `@facade` that is not `Class.Method` (followed at most by `returns <parameter>`), a second `@facade` on one native, and a `returns` that names no parameter, names one that is not a single output (a pointer documented `@param[out]`), is on a native with more than one single output, or is on a native whose result is neither `bool` nor nothing (the method drops a `bool`; it cannot drop a count). The rest of the comment is the function's `doc`, as for a type ("Doc comments").

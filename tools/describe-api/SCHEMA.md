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
| `role` | Not on enums. `component`, `event`, `scene-resource`, `app-resource`, `shared-struct` or `bound-type` for a selected type; `nested` for a type described only because a field uses it. |
| `size`, `alignment` | In bytes. An empty struct is 1 byte. |
| `doc` | The doc comment, when there is one (see "Doc comments"). |

### `struct`

Plain data. `fields` lists every non-static data member in declaration order (which is storage order), private ones included when the selection's `private-members` names the type. A field is:

| Key | Value |
| --- | --- |
| `name` | The C++ member name. |
| `offset` | Bytes from the start of the struct. |
| `kind` | See "Kinds". |
| `type` | For `enum` and `struct` kinds: the `name` of the described type. |
| `elementKind`, `elementType`, `length` | For the `array` kind: the element's kind, the element's type for `enum` and `struct` elements, and the element count. |
| `default` | See "Defaults". Every field has one. |
| `doc` | The member's doc comment, when there is one. |

### `tagged-union`

A class whose only non-static data members are an enum tag followed by one anonymous union (its members may be private). It records:

| Key | Value |
| --- | --- |
| `tag` | The tag, as a field (above): kind `enum`, and as `default` the tag value of the default kind. |
| `unionOffset` | Where every kind's value starts. |
| `kinds` | The union's members in declaration order: `name` (the C++ member name), `tag` (the tag value that selects it), its kind keys (`kind`, `type`, ...) and `doc`. The kinds pair with the tag enum's values in declaration order; a constructor that pairs them otherwise fails the tool. |
| `defaultKind` | The kind the default constructor holds, which it holds with that kind's own defaults. |

### `bound-type`

A type every binding has its own of (a vector, a quaternion, an entity handle). Only `size` and `alignment` are recorded; its members are not read, and it need not be plain data. A bound type that is a class template's specialisation the headers never instantiate (glm's quaternion, when nothing in them holds one) is instantiated to read its layout.

### `enum`

Described when a field uses it; an enum is never selected. It records `underlying` (an integer kind) and `values`: each enumerator's `name`, `value` (a JSON integer) and `doc`, in declaration order.

## Kinds

| Kind | Bytes |
| --- | --- |
| `i8`, `i16`, `i32`, `i64` | Signed integers. |
| `u8`, `u16`, `u32`, `u64` | Unsigned integers. A `char` is a `u8` (a byte of text) on every target, and a `char32_t` (a UTF-32 code point) a `u32`. |
| `f32`, `f64` | IEEE floats. |
| `bool` | One byte, 0 or 1. |
| `char16` | A UTF-16 code unit (`char16_t`). |
| `enum` | An enum of its underlying kind; `type` names it. An enum with no enumerators (`enum class Id : std::uint32_t {}`, EnTT's `entt::entity`) is a distinct integer type rather than a set of values, and is recorded as its underlying integer kind. |
| `struct` | A described struct, tagged union or bound type, held by value; `type` names it. |
| `array` | `length` elements of `elementKind`, contiguous. An array's elements are never arrays. |
| `pointer` | 8 opaque bytes: any pointer, its target never described. |

Anything else (a reference, `long double`, `wchar_t`, a bit-field, ...) fails the tool.

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

The initialisers recorded are: a literal (`1.0f`, `0.05f`, `true`, `u'A'`), a `std::numbers` constant, or operators applied to those (`-1.0f`, `~0u`, `1u << 3`, `std::numbers::pi_v<float> / 4.0f`); an enumerator; `std::numeric_limits<T>::infinity()`, `max()` and `lowest()`; `{}` or `T{}` or no initialiser for a nested struct or bound type, and an aggregate initialiser setting some of a nested struct's members (`{.frequency = 2.0f}`); a bound type's constructor with literal arguments; and for arrays, a braced list of those (missing elements are zero). A field of an enum with no enumerators has no enumerator to name, so its initialiser may be any constant libclang evaluates (`entt::null`, which converts to all bits set), recorded as the integer. A scalar, enum, pointer or array of them with no initialiser fails, as does any other expression.

A bound type's constructor arguments are matched to its components by name: its components are its fields in storage order (of an anonymous union, the first member's), and each parameter names one, ignoring leading underscores (`Quat(float w, float x, float y, float z)` and `qua(T _w, T _x, T _y, T _z)` both set `w` first). A constructor with one argument sets every component to it (`Vec3(0.5f)`). Anything else fails.

## Doc comments

A doc comment (`/** ... */`, `/*! ... */`, `///`) is recorded as text without its markers and its `@brief` tag. The lines of a paragraph are joined with spaces; paragraphs are separated by a blank line (`\n\n`). Nothing else is rewritten: other tags (`@param`, `@copydoc`) and cross-references stay as written.

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
| `doc` | Its doc comment without the `@param`, `@return` and `@notrunning` commands (see below), when there is one. |
| `notRunning` | The C++ expression its `@notrunning` gives: the value a caller gets when the native cannot be called (absent otherwise). |
| `returns` | Its result, as a type (below), with `doc` from `@return` (or `@returns`). |
| `parameters` | Every parameter after the call context and the state, in order: `name`, its type (below), `output` or `inOut`, and `doc`. |

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

The doc comment is split: `@param name text` (or `\param`) gives that parameter's `doc`, `@param[out] name text` also marks it an `output`, `@param[in,out] name text` marks it `inOut` (a pointer the native reads and writes), and `@return text` (or `@returns`) gives the result's `doc`; each command's text runs to the next command or blank line. `@notrunning expression` gives `notRunning`, the rest of its line. `@param[out]` or `@param[in,out]` on anything but a pointer to non-const data fails the tool, as do `@param` naming no parameter and `@notrunning` on a native that returns nothing. The rest of the comment is the function's `doc`, as for a type ("Doc comments").

# The natives description: what Iris.Bindings reads for native functions

A **natives description** (`<Name>.natives.json`, beside the binding's configuration `<Name>.binding.toml`) records the
C++ functions a binding's script language calls: a source reader writes it from the extension's sources (in FenrirEngine,
`tools/describe-api --natives`, whose `SCHEMA.md` this copies), and [`Tools/Iris.Bindings`](../Tools/Iris.Bindings/README.md)
reads it when the configuration has `[natives]`, to write their C# declarations, their registration and a test.
`Tools/Iris.Bindings/tests/Fixture/Fixture.natives.json` is a worked example of every part below.

The format is the [API description](api-description.md)'s: UTF-8 JSON in a fixed order, the same bytes on any platform.

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
| `doc` | Its doc comment without the `@param` and `@return` commands (see below), when there is one. |
| `returns` | Its result, as a type (below), with `doc` from `@return` (or `@returns`). |
| `parameters` | Every parameter after the call context and the state, in order: `name`, its type (below), `output` and `doc`. |

## Types

A parameter or result is a value or one pointer to a value:

| Key | Value |
| --- | --- |
| `type` | The C++ spelling, usable in a declaration: `float`, `std::uint32_t`, `const Ns::Vec3*`, `Ns::Hit*`. A number is spelled by its kind (`std::int32_t`, never `int` or `long`), unless it is written as an alias outside `std` and the global namespace (`Ns::EntityId`), which is kept so a binding can map it. A struct or enum is its qualified name, or the alias it is written as. |
| `kind` | `void`, a number kind (`i8` ... `u64`, `f32`, `f64`), `bool`, `char16`, `struct`, `enum`, or `pointer`. `char` is `i8` or `u8` by the target's signedness. |
| `pointee`, `pointeeKind` | For a pointer: what it points at, spelled and classified as above. |
| `const` | `true` for a pointer to const (absent otherwise). |

A reference (after the context and state), a pointer to a pointer, and a type of no kind (`long double`, `wchar_t`) fail the tool.

## Doc commands

The doc comment is split: `@param name text` (or `\param`) gives that parameter's `doc`, `@param[out] name text` also marks it an `output`, and `@return text` (or `@returns`) gives the result's `doc`; each command's text runs to the next command or blank line. `@param[out]` on anything but a pointer to non-const data fails the tool, as does `@param` naming no parameter. The rest of the comment is the function's `doc`, as for a type ("Doc comments" in [the API description](api-description.md)).

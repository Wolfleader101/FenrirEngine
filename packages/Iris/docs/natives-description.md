# The natives description: what Iris.Bindings reads for native functions

A **natives description** (`<Name>.natives.json`, beside the binding's configuration `<Name>.binding.toml`) records the
C++ functions a binding's script language calls: a source reader writes it from the extension's sources (in FenrirEngine,
`tools/describe-api --natives`, whose `SCHEMA.md` this copies), and [`Tools/Iris.Bindings`](../Tools/Iris.Bindings/README.md)
reads it when the configuration has `[natives]`, to write their C# declarations and their registration, which zeroes
each `output` before the native runs (the configuration's `zero-outputs` names the host's template).
`Tools/Iris.Bindings/tests/Fixture/Fixture.natives.json` is a worked example of every part below.

The format is the [API description](api-description.md)'s: UTF-8 JSON in a fixed order, the same bytes on any platform.

## The file

| Key | Value |
| --- | --- |
| `binding` | The configuration's file name without `.binding.toml`. |
| `context` | The qualified name of the call context every native takes first, as `[natives] context` gives it, in the binding's configuration or in the defaults file its `defaults` names. |
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
| `kind` | `void`, a number kind (`i8` ... `u64`, `f32`, `f64`), `bool`, `char16`, `string`, `struct`, `enum`, or `pointer`. A `char` is `u8` (a byte of text) on every target, as `api-description.md` records a field, and `char32_t` is `u32`. `string` is a `std::string_view` by value, spelled `std::string_view`: UTF-8 text, which a binding passes as a pointer and a length. |
| `pointee`, `pointeeKind` | For a pointer: what it points at, spelled and classified as above. |
| `const` | `true` for a pointer to const (absent otherwise). |

A reference (after the context and state), a pointer to a pointer, a pointer to a `std::string_view`, and a type of no kind (`long double`, `wchar_t`) fail the tool.

## Doc commands

The doc comment is split: `@param name text` (or `\param`) gives that parameter's `doc`, `@param[out] name text` also marks it an `output`, `@param[in,out] name text` marks it `inOut` (a pointer the native reads and writes), `@param[in] name text` marks it `input` (a pointer to one value the native reads, which a binding never takes for a span, whatever parameter follows it), and `@return text` (or `@returns`) gives the result's `doc`; each command's text runs to the next command or blank line. `@notrunning expression` gives `notRunning`, the rest of its line, and `@facade Class.Method` (or `\facade`) gives `facade`: two identifiers joined by one `.`, both always written, followed at most by `returns <parameter>`, which gives `facadeReturns`. `@param[out]` or `@param[in,out]` on anything but a pointer to non-const data fails the tool, as do `@param[in]` on anything but a pointer to const data, `@param` naming no parameter, `@notrunning` on a native that returns nothing, a `@facade` that is not `Class.Method` (followed at most by `returns <parameter>`), a second `@facade` on one native, and a `returns` that names no parameter, names one that is not a single output (a pointer documented `@param[out]`), is on a native with more than one single output, or is on a native whose result is neither `bool` nor nothing (the method drops a `bool`; it cannot drop a count). The rest of the comment is the function's `doc`, as for a type ("Doc comments" in [the API description](api-description.md)).

using System.Text.Json;

namespace Iris.Bindings;

/// <summary>A native's parameter or result as the natives description records it: C++ spelling, kind, and pointee.</summary>
internal sealed record NativeType(string Spelling, string Kind, string? Pointee, string? PointeeKind, bool Const);

/// <summary>
/// A parameter after the call context and the extension state: an output, a pointer the native reads and writes
/// (<c>inOut</c>), a pointer to one value it reads that is never a span whatever follows it (<c>input</c>), or an
/// input.
/// </summary>
internal sealed record NativeParameter(string Name, NativeType Type, bool Output, bool InOut, bool Input, string? Doc);

/// <summary>
/// One described native function (<c>docs/natives-description.md</c>). <c>NotRunning</c> is the C++ expression of what a
/// caller gets when it cannot be called; <c>Facade</c> is its <c>@facade</c> mark, <c>Class.Method</c>, the public
/// static class and method name the binding gives it.
/// </summary>
internal sealed record NativeFunction(
    string Name,
    string Namespace,
    string Source,
    string? State,
    bool NoExcept,
    string? Doc,
    NativeType Returns,
    string? ReturnsDoc,
    IReadOnlyList<NativeParameter> Parameters,
    string? NotRunning,
    string? Facade)
{
    /// <summary>The C++ function's qualified name.</summary>
    public string QualifiedName => Namespace.Length == 0 ? Name : $"{Namespace}::{Name}";
}

/// <summary>A natives description: the call context every native takes first, and the natives in source order.</summary>
internal sealed record NativesDescription(string Path, string Context, IReadOnlyList<NativeFunction> Functions)
{
    /// <summary>Reads <c>&lt;Name&gt;.natives.json</c>, or throws <see cref="BindingException" /> saying what is wrong.</summary>
    public static NativesDescription Read(string path)
    {
        JsonDocument document;
        try
        {
            document = JsonDocument.Parse(File.ReadAllBytes(path));
        }
        catch (Exception exception) when (exception is JsonException or IOException)
        {
            throw new BindingException($"{path}: {exception.Message}");
        }

        var root = document.RootElement;
        var functions = Description.Array(root, "functions", path).EnumerateArray().Select(function =>
        {
            var name = Description.String(function, "name", path);
            var where = $"{path}: {name}";
            var returns = Description.Object(function, "returns", where);
            return new NativeFunction(
                name, Description.String(function, "namespace", where), Description.String(function, "source", where),
                Description.OptionalString(function, "state"), Flag(function, "noexcept"),
                Description.OptionalString(function, "doc"), ReadType(returns, $"{where} (result)"),
                Description.OptionalString(returns, "doc"),
                Description.Array(function, "parameters", where).EnumerateArray().Select(parameter =>
                {
                    var parameterName = Description.String(parameter, "name", where);
                    return new NativeParameter(parameterName, ReadType(parameter, $"{where}::{parameterName}"),
                                               Flag(parameter, "output"), Flag(parameter, "inOut"),
                                               Flag(parameter, "input"), Description.OptionalString(parameter, "doc"));
                }).ToList(), Description.OptionalString(function, "notRunning"), Description.OptionalString(function, "facade"));
        }).ToList();
        return new NativesDescription(path, Description.String(root, "context", path), functions);
    }

    private static NativeType ReadType(JsonElement element, string where) =>
        new(Description.String(element, "type", where), Description.String(element, "kind", where),
            Description.OptionalString(element, "pointee"), Description.OptionalString(element, "pointeeKind"),
            Flag(element, "const"));

    private static bool Flag(JsonElement element, string key) =>
        element.TryGetProperty(key, out var value) && value.ValueKind == JsonValueKind.True;
}

/// <summary>The public static class and method name a native's <c>@facade</c> mark gives it.</summary>
internal sealed record FacadeName(string Class, string Method)
{
    public override string ToString() => $"{Class}.{Method}";
}

/// <summary>
/// A native function resolved against a binding: its C# declaration, what is zeroed, and the public method its
/// <c>@facade</c> names.
/// </summary>
/// <param name="Result">The C# result type of the declaration.</param>
/// <param name="Parameters">The C# parameters of the declaration, each <c>[modifier] Type name</c>.</param>
/// <param name="Named">The described types its declaration names, by C++ name.</param>
/// <param name="Outputs">
/// The indices, into the C++ function's full parameter list (the call context and the state counted), of the single
/// outputs (<c>out T</c>) the registration zeroes before the native runs. An output span and an <c>inOut</c> pointer are
/// not among them.
/// </param>
/// <param name="Folded">
/// The C++ parameters the declaration folds into another: a span's count and a text's length, which are the span's
/// <c>Length</c>, so they get no parameter and no doc of their own.
/// </param>
/// <param name="Facade">The public class and method its <c>@facade</c> names; null for a native with no mark.</param>
/// <param name="FacadeParameters">
/// The parameters of its facade method: the declaration's with <c>in</c> dropped (<c>in Vector3 force</c> becomes
/// <c>Vector3 force</c>, passed as <c>in force</c>), everything else as declared. Empty with no mark.
/// </param>
internal sealed record BoundNative(NativeFunction Function, string Result, IReadOnlyList<string> Parameters,
                                   IReadOnlyList<string> Named, IReadOnlyList<int> Outputs, IReadOnlyList<string> Folded,
                                   FacadeName? Facade, IReadOnlyList<string> FacadeParameters)
{
    /// <summary>Whether its declaration takes a span (text, a <c>ReadOnlySpan&lt;char&gt;</c>, among them), which <c>System</c> declares.</summary>
    public bool NeedsSystem => Parameters.Any(parameter => parameter.StartsWith("Span<", StringComparison.Ordinal) ||
                                                           parameter.StartsWith("ReadOnlySpan<", StringComparison.Ordinal));

    /// <summary>
    /// The arguments the facade method passes to the declaration, one per parameter: <c>in</c> or <c>out</c> and the
    /// name where the declaration has the modifier, the name alone otherwise.
    /// </summary>
    public IEnumerable<string> FacadeArguments =>
        Parameters.Select(parameter => parameter.StartsWith("in ", StringComparison.Ordinal) ||
                                       parameter.StartsWith("out ", StringComparison.Ordinal)
                                           ? $"{parameter[..parameter.IndexOf(' ')]} {ParameterName(parameter)}"
                                           : ParameterName(parameter));

    /// <summary>The types of the facade method's parameters, which tell two methods of one name apart.</summary>
    public IEnumerable<string> FacadeParameterTypes =>
        FacadeParameters.Select(parameter => parameter[..parameter.LastIndexOf(' ')]);

    // The name of a `[modifier] Type name` parameter: a type has no spaces.
    private static string ParameterName(string parameter) => parameter[(parameter.LastIndexOf(' ') + 1)..];

    /// <summary>The C# type of a <c>std::string_view</c> parameter.</summary>
    public const string TextType = "ReadOnlySpan<char>";
}

/// <summary>
/// Maps natives to C#: the leading context and state are dropped, and each C++ parameter becomes what the hand-written
/// declarations used to say. A <c>std::string_view</c> is a <c>ReadOnlySpan&lt;char&gt;</c>, and a <c>const char*</c>
/// with a <c>std::int32_t</c> length after it a <c>string</c>, both crossing as bytes and a length; any other pointer
/// with a <c>std::int32_t</c> count after it is a span, <c>ReadOnlySpan&lt;T&gt;</c> for a const pointee and
/// <c>Span&lt;T&gt;</c> otherwise, with the count folded into it, unless its doc marks it <c>[in]</c>; a <c>T*</c> its
/// doc marks an output is <c>out T</c>, and one it marks <c>[in,out]</c> is <c>T*</c>; a pointer to <c>void</c> is
/// <c>void*</c>, an address C# passes as it is; a <c>const T*</c> is <c>in T</c>. Each <c>out T</c> is also an output the
/// registration zeroes before the native runs (<see cref="BoundNative.Outputs" />). Numbers and bool are themselves, an
/// alias in <c>[natives] types</c> is the C#
/// type it names, an enum is its C# enum, and a wrapper (a described struct of one field, or a bound type whose entry
/// names its <c>wrapper</c> kind) is its C# type, crossing as that field. A type a binding in <c>uses</c> describes
/// and this binding's descriptions do not is named as that binding names it. A result is any of those values, nothing,
/// or a <c>void*</c>. Anything else fails naming the function and parameter.
/// <para/>
/// A native's <c>@facade Class.Method</c> names the public method the facade writer generates over its declaration,
/// which takes <c>T</c> where the declaration takes <c>in T</c>. The mark is refused, naming the native and the mark,
/// on a native whose declaration cannot be public (it takes a type in <c>internal-structs</c>, or a pointer); when two
/// marks give one class and method the same parameter types; and when the class is named as a mirror this binding or
/// one it uses generates, as a bound type's C# type, or as the <c>[natives] class</c>.
/// </summary>
internal static class Natives
{
    public static IReadOnlyList<BoundNative> Bind(Binding binding, NativesDescription natives)
    {
        var config = binding.Config.Natives!;
        var errors = new List<string>();
        var bound = new List<BoundNative>();
        foreach (var function in natives.Functions)
        {
            try
            {
                bound.Add(Bind(binding, config, function, $"{natives.Path}: {function.Name}"));
            }
            catch (BindingException exception)
            {
                errors.Add(exception.Message);
            }
        }

        foreach (var name in config.Types.Keys.Where(name => natives.Functions.All(function =>
                     function.Parameters.All(parameter => parameter.Type.Spelling != name && parameter.Type.Pointee != name))))
        {
            errors.Add($"{binding.Config.Path}: [natives] types names {name}, which no native takes");
        }

        errors.AddRange(FacadeErrors(binding, config, natives, bound));
        if (errors.Count > 0)
        {
            throw new BindingException(string.Join('\n', errors));
        }

        return bound;
    }

    // What the marks say together, once each native is bound: two marks giving one class and method the same
    // parameter types, and a facade class named as a type C# already knows under that name.
    private static IEnumerable<string> FacadeErrors(Binding binding, NativesConfig config, NativesDescription natives,
                                                    IReadOnlyList<BoundNative> bound)
    {
        var marked = bound.Where(native => native.Facade is not null).ToList();
        foreach (var group in marked.GroupBy(native => $"{native.Facade}({string.Join(", ", native.FacadeParameterTypes)})")
                                    .Where(group => group.Count() > 1))
        {
            yield return $"{natives.Path}: {string.Join(" and ", group.Select(native => native.Function.Name))}: both are " +
                         $"@facade {group.Key}";
        }

        foreach (var native in marked)
        {
            var facade = native.Facade!;
            var taken = facade.Class == config.Class ? "the [natives] class" : NamedType(binding, facade.Class);
            if (taken is not null)
            {
                yield return $"{natives.Path}: {native.Function.Name}: @facade {facade.Class} is the name of {taken}";
            }
        }
    }

    // What a described type of that C# name is, in this binding or one it uses (transitively): a mirror, a bound type.
    private static string? NamedType(Binding binding, string name)
    {
        foreach (var type in binding.Types.Where(type => binding.CSharpName(type) == name))
        {
            return type.Category == Category.BoundType
                       ? $"the C# type of the bound type {type.Name}"
                       : binding.IsUsed(type) ? $"the mirror of {type.Name} in the binding it uses" : $"the mirror of {type.Name}";
        }

        return binding.Uses.Select(used => NamedType(used, name)).FirstOrDefault(taken => taken is not null);
    }

    private static BoundNative Bind(Binding binding, NativesConfig config, NativeFunction function, string where)
    {
        var named = new List<string>();
        string result;
        switch (function.Returns)
        {
            case { Kind: "void" }:
                result = "void";
                break;
            case { Kind: "pointer", PointeeKind: "void" }:
                result = "void*";
                break;
            case { Kind: "pointer" or "string" }:
                throw Unmappable(where + " (result)", function.Returns,
                                 "a native returns a number, a bool, an enum, a wrapper, nothing or a void*");
            case { Kind: "struct" or "enum" } type:
                result = ByValue(binding, type, where + " (result)", named);
                break;
            case var type:
                result = Scalar(type.Kind, where + " (result)", type);
                break;
        }

        var parameters = new List<string>();
        var outputs = new List<int>();
        var folded = new List<string>();
        // The described parameters follow the call context and, when the native takes one, its state.
        var leading = function.State == null ? 1 : 2;
        var all = function.Parameters;
        for (var index = 0; index < all.Count; index++)
        {
            var parameter = all[index];
            var subject = $"{where}::{parameter.Name}";
            var name = Identifier(parameter.Name);
            var type = parameter.Type;
            var countFollows = index + 1 < all.Count && all[index + 1].Type.Kind == "i32";
            if (type.Kind == "pointer")
            {
                if (type.PointeeKind == "pointer" || type.Pointee is null)
                {
                    throw Unmappable(subject, type, "a pointer to a pointer cannot cross");
                }

                if (type.Const && type.Pointee == "char" && countFollows)
                {
                    parameters.Add($"string {name}");
                    folded.Add(all[++index].Name);
                    continue;
                }

                var pointee = Pointee(binding, config, type, subject);
                if (type.PointeeKind is "struct" or "enum" && !config.Types.ContainsKey(type.Pointee!))
                {
                    named.Add(type.Pointee!);
                }

                if (parameter.Output)
                {
                    parameters.Add($"out {pointee} {name}");
                    outputs.Add(leading + index);
                }
                else if (countFollows && !parameter.Input && !parameter.InOut && type.PointeeKind != "void")
                {
                    // A span: the SDK's generator pins it and passes its first element and its length, so the count is
                    // the span's. A const char16_t* would read as ReadOnlySpan<char>, which is text (UTF-8 on the wire).
                    if (type.Const && type.Pointee == "char16_t")
                    {
                        throw Unmappable(subject, type,
                                         "a const char16_t* with a count would be declared ReadOnlySpan<char>, which is text: " +
                                         "take a std::string_view, or a non-const buffer to fill");
                    }

                    parameters.Add($"{(type.Const ? "ReadOnlySpan" : "Span")}<{pointee}> {name}");
                    folded.Add(all[++index].Name);
                }
                else if (parameter.InOut || type.PointeeKind == "void")
                {
                    parameters.Add($"{pointee}* {name}");
                }
                else if (type.Const)
                {
                    parameters.Add($"in {pointee} {name}");
                }
                else
                {
                    throw Unmappable(subject, type,
                                     "a pointer to non-const data is an output (its doc says [out]), read and written " +
                                     "(its doc says [in,out]) or a span (its std::int32_t count follows it)");
                }

                continue;
            }

            if (config.Types.TryGetValue(type.Spelling, out var mapped))
            {
                parameters.Add($"{mapped} {name}");
            }
            else if (type.Kind == "string")
            {
                parameters.Add($"{BoundNative.TextType} {name}");
            }
            else if (type.Kind is "struct" or "enum")
            {
                parameters.Add($"{ByValue(binding, type, subject, named)} {name}");
            }
            else
            {
                parameters.Add($"{Scalar(type.Kind, subject, type)} {name}");
            }
        }

        var facade = Facade(binding, function, parameters, result, named, where);
        var facadeParameters = facade is null
                                   ? []
                                   : parameters.Select(parameter => parameter.StartsWith("in ", StringComparison.Ordinal)
                                                                        ? parameter["in ".Length..]
                                                                        : parameter).ToList();
        return new BoundNative(function, result, parameters, named.Distinct().ToList(), outputs, folded, facade,
                               facadeParameters);
    }

    // The native's @facade as a class and method, or null without one. The mark is refused when the declaration cannot
    // be public: an internal mirror among the types it names, or a pointer among its parameters or as its result.
    private static FacadeName? Facade(Binding binding, NativeFunction function, IReadOnlyList<string> parameters,
                                      string result, IEnumerable<string> named, string where)
    {
        if (function.Facade is null)
        {
            return null;
        }

        var parts = function.Facade.Split('.');
        if (parts.Length != 2 || parts.Any(part => part.Length == 0))
        {
            throw new BindingException($"{where}: @facade '{function.Facade}' names a public method as Class.Method");
        }

        var facade = new FacadeName(parts[0], parts[1]);
        foreach (var name in named)
        {
            if (binding.Find(name) is { } type && binding.IsInternal(type))
            {
                throw new BindingException($"{where}: @facade {facade} cannot be public: it takes {binding.CSharpName(type)}");
            }
        }

        foreach (var parameter in parameters.Where(parameter => parameter[..parameter.LastIndexOf(' ')].EndsWith('*')))
        {
            throw new BindingException($"{where}: @facade {facade} cannot be public: it takes {parameter}");
        }

        if (result.EndsWith('*'))
        {
            throw new BindingException($"{where}: @facade {facade} cannot be public: it returns {result}");
        }

        return facade;
    }

    // An enum or wrapper a native takes or returns by value: its C# type. An enum crosses as its underlying integer, a
    // described struct of one field as that field, and a bound type as the kind its [bound-types] entry gives as its
    // wrapper.
    private static string ByValue(Binding binding, NativeType type, string subject, List<string> named)
    {
        if (binding.Find(type.Spelling) is not { } described)
        {
            throw Unmappable(subject, type, $"no description describes {type.Spelling}, and [natives] types does not name it");
        }

        named.Add(described.Name);
        switch (described.Category)
        {
            case Category.Enum:
            case Category.Struct when described.Fields is [{ Kind.Kind: not ("struct" or "array" or "pointer") }]:
            case Category.BoundType when binding.BoundTypeOf(described).Wrapper is not null:
                return binding.CSharpName(described);
            default:
                throw Unmappable(subject, type,
                                 "a struct crosses by value only as a wrapper (one field of a number, bool, char or enum, " +
                                 "or a bound type whose [bound-types] entry names its wrapper kind): take a const pointer");
        }
    }

    // The C# type a pointer points at: a number, a type a description describes, or an alias in [natives] types.
    private static string Pointee(Binding binding, NativesConfig config, NativeType type, string subject)
    {
        if (config.Types.TryGetValue(type.Pointee!, out var mapped))
        {
            return mapped;
        }

        switch (type.PointeeKind)
        {
            case "struct" or "enum":
                if (binding.Find(type.Pointee!) is { } described)
                {
                    return binding.CSharpName(described);
                }

                throw Unmappable(subject, type, $"no description describes {type.Pointee}, and [natives] types does not name it");
            case "void":
                return "void";
            default:
                return Scalar(type.PointeeKind!, subject, type);
        }
    }

    private static string Scalar(string kind, string subject, NativeType type)
    {
        try
        {
            return Binding.CSharpScalar(kind);
        }
        catch (BindingException)
        {
            throw Unmappable(subject, type, $"its kind '{kind}' is not one C# has");
        }
    }

    private static BindingException Unmappable(string subject, NativeType type, string reason) =>
        new($"{subject}: cannot map the C++ type '{type.Spelling}' to C#: {reason}");

    private static readonly HashSet<string> Keywords =
    [
        "abstract", "as", "base", "bool", "break", "byte", "case", "catch", "char", "checked", "class", "const",
        "continue", "decimal", "default", "delegate", "do", "double", "else", "enum", "event", "explicit", "extern",
        "false", "finally", "fixed", "float", "for", "foreach", "goto", "if", "implicit", "in", "int", "interface",
        "internal", "is", "lock", "long", "namespace", "new", "null", "object", "operator", "out", "override", "params",
        "private", "protected", "public", "readonly", "ref", "return", "sbyte", "sealed", "short", "sizeof",
        "stackalloc", "static", "string", "struct", "switch", "this", "throw", "true", "try", "typeof", "uint", "ulong",
        "unchecked", "unsafe", "ushort", "using", "virtual", "void", "volatile", "while",
    ];

    /// <summary>A C++ parameter name as a C# identifier: a C# keyword gets <c>@</c>.</summary>
    public static string Identifier(string name) => Keywords.Contains(name) ? "@" + name : name;
}

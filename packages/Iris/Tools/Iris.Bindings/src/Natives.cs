using System.Text.Json;

namespace Iris.Bindings;

/// <summary>A native's parameter or result as the natives description records it: C++ spelling, kind, and pointee.</summary>
internal sealed record NativeType(string Spelling, string Kind, string? Pointee, string? PointeeKind, bool Const);

/// <summary>A parameter after the call context and the extension state.</summary>
internal sealed record NativeParameter(string Name, NativeType Type, bool Output, string? Doc);

/// <summary>One described native function (<c>docs/natives-description.md</c>).</summary>
internal sealed record NativeFunction(
    string Name,
    string Namespace,
    string Source,
    string? State,
    bool NoExcept,
    string? Doc,
    NativeType Returns,
    string? ReturnsDoc,
    IReadOnlyList<NativeParameter> Parameters)
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
                                               Flag(parameter, "output"), Description.OptionalString(parameter, "doc"));
                }).ToList());
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

/// <summary>What crosses for one C++ parameter, as the natives test passes it to the bound function pointer.</summary>
/// <param name="Type">The C# type of the function pointer's parameter (<c>void*</c>, <c>byte</c> for bool, ...).</param>
/// <param name="Output">For an output, the C# type of the value it points at; null otherwise.</param>
internal sealed record WireParameter(string Type, string? Output);

/// <summary>A native function resolved against a binding: its C# declaration and what crosses.</summary>
/// <param name="Result">The C# result type of the declaration.</param>
/// <param name="Parameters">The C# parameters of the declaration, each <c>[modifier] Type name</c>.</param>
/// <param name="WireResult">The C# type of the bound function pointer's result.</param>
/// <param name="Wire">One per C++ parameter after the context and state, in order.</param>
internal sealed record BoundNative(NativeFunction Function, string Result, IReadOnlyList<string> Parameters,
                                   string WireResult, IReadOnlyList<WireParameter> Wire)
{
    /// <summary>Whether the native writes an output, which the natives test checks it writes with no world.</summary>
    public bool HasOutput => Wire.Any(parameter => parameter.Output != null);
}

/// <summary>
/// Maps natives to C#: the leading context and state are dropped, and each C++ parameter becomes what the hand-written
/// declarations used to say. A <c>const char*</c> with a <c>std::int32_t</c> length after it is a <c>string</c>; any
/// other pointer with a <c>std::int32_t</c> count after it is a <c>T*</c> span; a <c>T*</c> its doc marks an output is
/// <c>out T</c>; a <c>const T*</c> is <c>in T</c>. Numbers and bool are themselves, and an alias in <c>[natives] types</c>
/// is the C# type it names. Anything else fails naming the function and parameter.
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

        if (errors.Count > 0)
        {
            throw new BindingException(string.Join('\n', errors));
        }

        return bound;
    }

    private static BoundNative Bind(Binding binding, NativesConfig config, NativeFunction function, string where)
    {
        var result = function.Returns.Kind switch
        {
            "void" => "void",
            "pointer" or "struct" or "enum" => throw Unmappable(where + " (result)", function.Returns,
                                                                 "a native returns a number, a bool or nothing"),
            var kind => Scalar(kind, where + " (result)", function.Returns),
        };
        var wireResult = function.Returns.Kind switch
        {
            "void" => "void",
            "bool" => "byte",
            var kind => Binding.CSharpScalar(kind),
        };

        var parameters = new List<string>();
        var wire = new List<WireParameter>();
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
                    wire.Add(new WireParameter("void*", null));
                    wire.Add(new WireParameter("int", null));
                    index++;
                    continue;
                }

                var pointee = Pointee(binding, config, type, subject);
                if (parameter.Output)
                {
                    parameters.Add($"out {pointee} {name}");
                    wire.Add(new WireParameter($"{pointee}*", pointee));
                }
                else if (countFollows)
                {
                    parameters.Add($"{pointee}* {name}");
                    wire.Add(new WireParameter("void*", null));
                }
                else if (type.Const)
                {
                    parameters.Add($"in {pointee} {name}");
                    wire.Add(new WireParameter("void*", null));
                }
                else
                {
                    throw Unmappable(subject, type,
                                     "a pointer to non-const data is an output (its doc says [out]) or a span (its " +
                                     "std::int32_t count follows it)");
                }

                continue;
            }

            if (config.Types.TryGetValue(type.Spelling, out var mapped))
            {
                parameters.Add($"{mapped} {name}");
            }
            else if (type.Kind is "struct" or "enum")
            {
                throw Unmappable(subject, type, "Iris passes no struct or enum by value: take a const pointer");
            }
            else
            {
                parameters.Add($"{Scalar(type.Kind, subject, type)} {name}");
            }

            wire.Add(new WireParameter(type.Kind == "bool" ? "byte" : Binding.CSharpScalar(type.Kind), null));
        }

        return new BoundNative(function, result, parameters, wireResult, wire);
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

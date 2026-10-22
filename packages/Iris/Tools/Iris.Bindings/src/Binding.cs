using System.Globalization;
using System.Text.Json;

namespace Iris.Bindings;

/// <summary>
/// The descriptions read against a binding's configuration: every C# name derived and every type a field names resolved,
/// so the writers only format. Building it checks everything the writers rely on and throws
/// <see cref="BindingException" /> naming the type and field.
/// </summary>
internal sealed class Binding
{
    private readonly Dictionary<string, DescribedType> types;

    public Binding(BindingConfig config, IReadOnlyList<DescribedType> described)
    {
        Config = config;
        Types = described;
        types = described.ToDictionary(type => type.Name, StringComparer.Ordinal);
        Check();
    }

    public BindingConfig Config { get; }

    /// <summary>Every described type, dependencies first.</summary>
    public IReadOnlyList<DescribedType> Types { get; }

    /// <summary>The types that get a C# mirror: every struct, tagged union and enum. Bound types have their own.</summary>
    public IEnumerable<DescribedType> Mirrored => Types.Where(type => type.Category != Category.BoundType);

    /// <summary>The types an extension registers, and the defaults test covers: components, events and shared structs.</summary>
    public IEnumerable<DescribedType> Registered =>
        Types.Where(type => type.Role is Role.Component or Role.Event or Role.SharedStruct);

    public DescribedType Type(string name) =>
        types.TryGetValue(name, out var type)
            ? type
            : throw new BindingException($"{name} is named by a field but described by none of the descriptions");

    /// <summary>The C# name of a type: its configured bound type, a configured name, or its last C++ name segment.</summary>
    public string CSharpName(DescribedType type)
    {
        if (type.Category == Category.BoundType)
        {
            return Config.BoundTypes[type.Name].Type;
        }

        return Config.Names.TryGetValue(type.Name, out var name) ? name : type.Name[(type.Name.LastIndexOf(':') + 1)..];
    }

    /// <summary>The name C# knows a mirrored type by, and the name it is registered under: <c>Namespace.Type</c>.</summary>
    public string QualifiedName(DescribedType type) => $"{Config.Namespace}.{CSharpName(type)}";

    /// <summary>A field's or enumerator's C# name: <c>Type::member</c> or <c>member</c> in [names], else PascalCase.</summary>
    public string MemberName(DescribedType owner, string member) =>
        Config.Names.TryGetValue($"{owner.Name}::{member}", out var qualified) ? qualified
        : Config.Names.TryGetValue(member, out var bare) ? bare
        : char.ToUpperInvariant(member[0]) + member[1..];

    /// <summary>The private field that holds a tagged union's kind: <c>_</c> and its tag value's C# name, lowered first.</summary>
    public string KindFieldName(DescribedType union, UnionKind kind)
    {
        var tag = MemberName(Type(union.Tag!.Kind.Type!), kind.Tag);
        return "_" + char.ToLowerInvariant(tag[0]) + tag[1..];
    }

    /// <summary>The C# field holding a tagged union's tag, which the SDK's generator finds by offset.</summary>
    public const string TagFieldName = "_type";

    /// <summary>The C# type of a scalar kind.</summary>
    public static string CSharpScalar(string kind) => kind switch
    {
        "i8" => "sbyte",
        "u8" => "byte",
        "i16" => "short",
        "u16" => "ushort",
        "i32" => "int",
        "u32" => "uint",
        "i64" => "long",
        "u64" => "ulong",
        "f32" => "float",
        "f64" => "double",
        "bool" => "bool",
        "char16" => "char",
        _ => throw new BindingException($"'{kind}' is not a scalar kind"),
    };

    /// <summary>The C++ type of a scalar kind.</summary>
    public static string CppScalar(string kind) => kind switch
    {
        "i8" => "std::int8_t",
        "u8" => "std::uint8_t",
        "i16" => "std::int16_t",
        "u16" => "std::uint16_t",
        "i32" => "std::int32_t",
        "u32" => "std::uint32_t",
        "i64" => "std::int64_t",
        "u64" => "std::uint64_t",
        "f32" => "float",
        "f64" => "double",
        "bool" => "bool",
        "char16" => "char16_t",
        _ => throw new BindingException($"'{kind}' is not a scalar kind"),
    };

    /// <summary>The C# type of a field that is not an array.</summary>
    public string CSharpType(string kind, string? type) => kind switch
    {
        "enum" or "struct" => CSharpName(Type(type!)),
        "pointer" => Config.CSharp.PointerType,
        _ => CSharpScalar(kind),
    };

    /// <summary>The C++ type <c>Iris::Field&lt;T&gt;</c> is given for a field: a pointer is <c>void*</c>, opaque.</summary>
    public string CppType(FieldKind kind) => kind.Kind switch
    {
        "array" => $"{CppType(new FieldKind(kind.ElementKind!, kind.ElementType, null, null, 0))}[{kind.Length}]",
        "enum" or "struct" => kind.Type!,
        "pointer" => "void*",
        _ => CppScalar(kind.Kind),
    };

    /// <summary>The C# name of an array field's storage type, nested in its struct.</summary>
    public static string ArrayTypeName(string fieldName) => fieldName + "Array";

    /// <summary>
    /// The C# expression for a recorded default of a field of this kind and type, or null when the field needs none (an
    /// array, which the constructor fills). <paramref name="where" /> names the field in diagnostics.
    /// </summary>
    public string? Default(string kind, string? type, JsonElement value, string where)
    {
        switch (kind)
        {
            case "array":
                return null;
            case "pointer":
                return value.ValueKind == JsonValueKind.Null
                           ? "0"
                           : throw new BindingException($"{where}: a pointer's default is null, not {value}");
            case "bool":
                return value.ValueKind switch
                {
                    JsonValueKind.True => "true",
                    JsonValueKind.False => "false",
                    _ => throw new BindingException($"{where}: a bool's default is true or false, not {value}"),
                };
            case "char16":
                return Char(value, where);
            case "f32":
            case "f64":
                return Float(value, kind == "f32", where);
            case "enum":
            {
                var described = Type(type!);
                var name = value.ValueKind == JsonValueKind.String ? value.GetString()! : "";
                if (described.Values.All(enumerator => enumerator.Name != name))
                {
                    throw new BindingException($"{where}: the default {value} is not an enumerator of {described.Name}");
                }

                return $"{CSharpName(described)}.{MemberName(described, name)}";
            }
            case "struct":
                return StructDefault(Type(type!), value, where);
            default:
                return Integer(value, kind, where);
        }
    }

    private string StructDefault(DescribedType type, JsonElement value, string where)
    {
        if (value.ValueKind != JsonValueKind.Object)
        {
            throw new BindingException($"{where}: a struct's default is an object, not {value}");
        }

        var members = value.EnumerateObject().ToList();
        if (members.Count == 0)
        {
            return "new()";
        }

        if (type.Category == Category.BoundType)
        {
            // A bound type's components, by name in storage order: through its configured constructor, else by name.
            var bound = Config.BoundTypes[type.Name];
            var components = members.ToDictionary(member => member.Name, member => Float(member.Value, true, where));
            if (bound.Constructor is { } parameters)
            {
                var missing = parameters.Where(parameter => !components.ContainsKey(parameter))
                                  .Concat(components.Keys.Where(component => !parameters.Contains(component)))
                                  .ToList();
                if (missing.Count > 0)
                {
                    throw new BindingException(
                        $"{where}: the default names components {string.Join(", ", components.Keys)}, but " +
                        $"{type.Name}'s constructor in [bound-types] takes {string.Join(", ", parameters)}");
                }

                return $"new({string.Join(", ", parameters.Select(parameter => components[parameter]))})";
            }

            return "new() { " +
                   string.Join(", ", members.Select(member => $"{MemberName(type, member.Name)} = {components[member.Name]}")) +
                   " }";
        }

        if (type.Category != Category.Struct)
        {
            throw new BindingException($"{where}: a default may set members only of a struct, not of {type.Name}");
        }

        var assignments = new List<string>();
        foreach (var member in members)
        {
            var field = type.Fields.FirstOrDefault(field => field.Name == member.Name) ??
                        throw new BindingException($"{where}: the default sets '{member.Name}', which {type.Name} lacks");
            var expression = Default(field.Kind.Kind, field.Kind.Type, member.Value, $"{where}.{member.Name}") ??
                             throw new BindingException($"{where}: a default cannot set the array '{member.Name}'");
            assignments.Add($"{MemberName(type, field.Name)} = {expression}");
        }

        return "new() { " + string.Join(", ", assignments) + " }";
    }

    /// <summary>The C# expression of one array element's default.</summary>
    public string ElementDefault(FieldKind array, JsonElement value, string where) =>
        Default(array.ElementKind!, array.ElementType, value, where)!;

    private static string Integer(JsonElement value, string kind, string where)
    {
        if (value.ValueKind != JsonValueKind.Number || value.GetRawText().Any(c => c is '.' or 'e' or 'E'))
        {
            throw new BindingException($"{where}: an integer's default is a JSON integer, not {value}");
        }

        _ = CSharpScalar(kind);
        return value.GetRawText();
    }

    private static string Char(JsonElement value, string where)
    {
        if (!value.TryGetInt32(out var code) || code is < 0 or > 0xFFFF)
        {
            throw new BindingException($"{where}: a char16's default is a UTF-16 code unit, not {value}");
        }

        return code is >= 0x20 and < 0x7F and not ('\'' or '\\')
                   ? $"'{(char)code}'"
                   : $"'\\u{code:X4}'";
    }

    /// <summary>
    /// A float default: an integral value as an integer literal (<c>1</c>), anything else as the shortest text that reads
    /// back the same, <c>f</c>-suffixed for <c>float</c>, and the infinities by name.
    /// </summary>
    private static string Float(JsonElement value, bool single, string where)
    {
        var type = single ? "float" : "double";
        if (value.ValueKind == JsonValueKind.String)
        {
            return value.GetString() switch
            {
                "inf" => $"{type}.PositiveInfinity",
                "-inf" => $"{type}.NegativeInfinity",
                _ => throw new BindingException($"{where}: a float's default is a number, \"inf\" or \"-inf\", not {value}"),
            };
        }

        if (value.ValueKind != JsonValueKind.Number)
        {
            throw new BindingException($"{where}: a float's default is a number, not {value}");
        }

        var number = value.GetDouble();
        var text = single
                       ? ((float)number).ToString("R", CultureInfo.InvariantCulture)
                       : number.ToString("R", CultureInfo.InvariantCulture);
        if (double.IsNegative(number) && number == 0)
        {
            return single ? "-0f" : "-0d";
        }

        if (text.All(c => char.IsAsciiDigit(c) || c == '-'))
        {
            return text;
        }

        return single ? text + "f" : text.Contains('.') || text.Contains('E') ? text : text + "d";
    }

    private void Check()
    {
        foreach (var type in Types.Where(type => type.Category == Category.BoundType && !Config.BoundTypes.ContainsKey(type.Name)))
        {
            throw new BindingException(
                $"{Config.Path}: {type.Name} is a bound type ({type.Source}) with no C# type in [bound-types]");
        }

        foreach (var name in Config.BoundTypes.Keys.Where(name => !types.ContainsKey(name)))
        {
            throw new BindingException($"{Config.Path}: [bound-types] names {name}, which no description describes");
        }

        foreach (var group in Mirrored.GroupBy(CSharpName).Where(group => group.Count() > 1))
        {
            throw new BindingException(
                $"{Config.Path}: {string.Join(" and ", group.Select(type => type.Name))} would all be C# " +
                $"{Config.Namespace}.{group.Key}: give one another name in [names]");
        }

        foreach (var (name, _) in Config.Nouns)
        {
            if (!types.TryGetValue(name, out var union) || union.Category != Category.TaggedUnion)
            {
                throw new BindingException($"{Config.Path}: [nouns] names {name}, which is not a described tagged union");
            }
        }

        foreach (var name in Config.Registration?.ReadOnly ?? new HashSet<string>())
        {
            if (!types.TryGetValue(name, out var component) || component.Role != Role.Component)
            {
                throw new BindingException(
                    $"{Config.Path}: [registration] read-only names {name}, which is not a described component");
            }
        }

        foreach (var type in Mirrored)
        {
            CheckType(type);
        }
    }

    private void CheckType(DescribedType type)
    {
        var name = CSharpName(type);
        if (type.Category == Category.Enum)
        {
            _ = CSharpScalar(type.Underlying!);
            return;
        }

        if (type.Category == Category.TaggedUnion)
        {
            var tag = Type(type.Tag!.Kind.Type!);
            var defaultKind = type.Kinds.FirstOrDefault(kind => kind.Name == type.DefaultKind);
            var zero = tag.Values.FirstOrDefault(value => value.Value == 0);
            if (defaultKind == null || zero == null || defaultKind.Tag != zero.Name)
            {
                // The SDK's generator writes the constructor, and it makes the kind whose tag value is 0.
                throw new BindingException(
                    $"{type.Source}: {type.Name}'s default kind is {type.DefaultKind}, but a tagged union's C# new() " +
                    $"makes the kind whose tag value is 0{(zero == null ? $", and {tag.Name} has none" : $" ({zero.Name})")}");
            }

            foreach (var kind in type.Kinds.Where(kind => kind.Kind.Kind != "struct"))
            {
                throw new BindingException(
                    $"{type.Source}: {type.Name}::{kind.Name} holds a {kind.Kind.Kind}; a tagged union's kinds are structs");
            }

            foreach (var kind in type.Kinds)
            {
                _ = Type(kind.Kind.Type!);
            }

            return;
        }

        foreach (var field in type.Fields)
        {
            var where = $"{type.Source}: {type.Name}::{field.Name}";
            if (MemberName(type, field.Name) == name)
            {
                throw new BindingException(
                    $"{where}: C# cannot name a field {name} inside {name}; give it another name in [names] " +
                    $"(\"{type.Name}::{field.Name}\" = \"...\")");
            }

            if (field.Kind.IsArray)
            {
                if (field.Default.ValueKind != JsonValueKind.Array || field.Default.GetArrayLength() != field.Kind.Length)
                {
                    throw new BindingException($"{where}: an array's default is a list of {field.Kind.Length} elements");
                }

                var index = 0;
                foreach (var element in field.Default.EnumerateArray())
                {
                    _ = ElementDefault(field.Kind, element, $"{where}[{index++}]");
                }
            }
            else
            {
                _ = Default(field.Kind.Kind, field.Kind.Type, field.Default, where);
            }
        }
    }
}

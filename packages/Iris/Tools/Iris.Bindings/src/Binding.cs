using System.Globalization;
using System.Text.Json;

namespace Iris.Bindings;

/// <summary>
/// The descriptions read against a binding's configuration: every C# name derived and every type a field names resolved,
/// so the writers only format. Building it checks everything the writers rely on and throws
/// <see cref="BindingException" /> naming the type and field.
/// </summary>
/// <remarks>
/// A type that one of the bindings in <c>uses</c> mirrors is that binding's: it is named by its C# name there, and gets
/// no mirror, mapping, registration or test here.
/// </remarks>
internal sealed class Binding
{
    private readonly Dictionary<string, DescribedType> types;

    // Each type a binding in `uses` mirrors, by C++ name, with that binding.
    private readonly Dictionary<string, Binding> owners = new(StringComparer.Ordinal);

    public Binding(BindingConfig config, IReadOnlyList<DescribedType> described, IReadOnlyList<Binding>? uses = null)
    {
        Config = config;
        Uses = uses ?? [];
        CheckUses(described);
        CheckExcluded(described);
        Types = described.Where(type => !config.Exclude.Contains(type.Name)).ToList();
        types = Types.ToDictionary(type => type.Name, StringComparer.Ordinal);
        Check();
    }

    public BindingConfig Config { get; }

    /// <summary>The bindings whose mirrors this one names (<c>uses</c>).</summary>
    public IReadOnlyList<Binding> Uses { get; }

    /// <summary>Every described type the binding binds (all but those it excludes), dependencies first.</summary>
    public IReadOnlyList<DescribedType> Types { get; }

    /// <summary>
    /// The types that get a C# mirror here: every struct, tagged union and enum that no binding in <c>uses</c> mirrors.
    /// Bound types have their own.
    /// </summary>
    public IEnumerable<DescribedType> Mirrored => Types.Where(type => type.Category != Category.BoundType && !IsUsed(type));

    /// <summary>
    /// The types the defaults test covers: components, events, resources and shared structs mirrored here, but for an
    /// <c>internal</c> mirror, which a test's public method cannot return.
    /// </summary>
    public IEnumerable<DescribedType> Registered => Types.Where(type => IsRegisteredRole(type) && !IsUsed(type) && !IsInternal(type));

    /// <summary>The types the generated registration lists: those mirrored here with a registered role, but for <c>by-hand</c>.</summary>
    public IEnumerable<DescribedType> Listed =>
        Types.Where(type => IsRegisteredRole(type) && !IsUsed(type) && Config.Registration?.ByHand.Contains(type.Name) != true);

    private static bool IsRegisteredRole(DescribedType type) =>
        type.Role is Role.Component or Role.Event or Role.SharedStruct or Role.SceneResource or Role.AppResource;

    /// <summary>Whether a binding in <c>uses</c> mirrors the type, so this one names it and generates nothing for it.</summary>
    public bool IsUsed(DescribedType type) => owners.ContainsKey(type.Name);

    /// <summary>
    /// The C# namespaces of the bindings in <c>uses</c> that mirror any of these types (by C++ name), other than this
    /// binding's own, in ordinal order: the <c>using</c>s a file naming them needs.
    /// </summary>
    public IReadOnlyList<string> UsedNamespaces(IEnumerable<string?> names) =>
        names.Where(name => name != null && owners.ContainsKey(name))
             .Select(name => owners[name!].Config.Namespace)
             .Where(space => space != Config.Namespace)
             .Distinct()
             .Order(StringComparer.Ordinal)
             .ToList();

    /// <summary>The described types a type's fields, tag and kinds name, by C++ name.</summary>
    public static IEnumerable<string?> Named(DescribedType type) =>
        type.Fields.SelectMany(field => new[] { field.Kind.Type, field.Kind.ElementType })
            .Concat(type.Kinds.Select(kind => kind.Kind.Type))
            .Append(type.Tag?.Kind.Type);

    /// <summary>Whether the type is a scene or app resource.</summary>
    public static bool IsResource(DescribedType type) => type.Role is Role.SceneResource or Role.AppResource;

    /// <summary>Whether a mirror is <c>internal</c> (<c>[csharp] internal-structs</c>).</summary>
    public bool IsInternal(DescribedType type) => Config.CSharp.InternalStructs.Contains(type.Name);

    /// <summary>Whether a component or resource is registered, and mirrored, read-only (<c>[registration] read-only</c>).</summary>
    public bool IsRegisteredReadOnly(DescribedType type) => Config.Registration?.ReadOnly.Contains(type.Name) == true;

    /// <summary>The described type of that name, or null.</summary>
    public DescribedType? Find(string name) => types.GetValueOrDefault(name);

    public DescribedType Type(string name) =>
        types.TryGetValue(name, out var type)
            ? type
            : throw new BindingException($"{name} is named by a field but described by none of the descriptions");

    /// <summary>
    /// The C# name of a type: the name the binding in <c>uses</c> that mirrors it gives it, its configured bound type, a
    /// configured name, or its last C++ name segment.
    /// </summary>
    public string CSharpName(DescribedType type)
    {
        if (owners.TryGetValue(type.Name, out var owner))
        {
            return owner.CSharpName(owner.Type(type.Name));
        }

        if (type.Category == Category.BoundType)
        {
            return Config.BoundTypes[type.Name].Type;
        }

        return Config.Names.TryGetValue(type.Name, out var name) ? name : type.Name[(type.Name.LastIndexOf(':') + 1)..];
    }

    /// <summary>The name C# knows a mirrored type by, which shared structs are registered under: <c>Namespace.Type</c>.</summary>
    public string QualifiedName(DescribedType type) =>
        owners.TryGetValue(type.Name, out var owner) ? owner.QualifiedName(owner.Type(type.Name)) : $"{Config.Namespace}.{CSharpName(type)}";

    /// <summary>
    /// The name a component or event is registered under, which its C# attribute names:
    /// <c>RegisteredNamespace.Type</c> (<c>registered-namespace</c>, the C# namespace unless it says otherwise).
    /// </summary>
    public string RegisteredName(DescribedType type) => $"{Config.RegisteredNamespace}.{CSharpName(type)}";

    /// <summary>Whether a field's mirror is private (<c>[csharp] private-fields</c>): C# reaches it through hand-written members.</summary>
    public bool IsPrivate(DescribedType owner, Field field) =>
        Config.CSharp.PrivateFields.Contains($"{owner.Name}::{field.Name}");

    /// <summary>Whether an array field is a fixed buffer (<c>[csharp] fixed-buffers</c>) rather than an <c>[InlineArray]</c> struct.</summary>
    public bool IsFixedBuffer(DescribedType owner, Field field) =>
        Config.CSharp.FixedBuffers.Contains($"{owner.Name}::{field.Name}");

    /// <summary>A field's or enumerator's C# name: <c>Type::member</c> or <c>member</c> in [names], else PascalCase.</summary>
    public string MemberName(DescribedType owner, string member) =>
        owners.TryGetValue(owner.Name, out var binding) ? binding.MemberName(binding.Type(owner.Name), member)
        : Config.Names.TryGetValue($"{owner.Name}::{member}", out var qualified) ? qualified
        : Config.Names.TryGetValue(member, out var bare) ? bare
        : char.ToUpperInvariant(member[0]) + member[1..];

    /// <summary>
    /// Whether a struct's mirror is a <c>readonly struct</c> of <c>readonly</c> fields: an event under
    /// <c>readonly-events</c>, a struct <c>readonly-structs</c> names, or a resource registered read-only. A type a binding
    /// in <c>uses</c> mirrors is as that binding mirrors it.
    /// </summary>
    public bool IsReadOnly(DescribedType type) =>
        owners.TryGetValue(type.Name, out var owner)
            ? owner.IsReadOnly(owner.Type(type.Name))
            : (type.Role == Role.Event && Config.CSharp.ReadOnlyEvents) || Config.CSharp.ReadOnlyStructs.Contains(type.Name) ||
              (IsResource(type) && type.Category == Category.Struct && IsRegisteredReadOnly(type));

    /// <summary>
    /// Whether a struct's mirror gets field initialisers and an explicit <c>public T() {}</c> to run them: when some
    /// field's default is not all zero bytes, which is what C#'s <c>default</c> is. A read-only mirror always gets
    /// them: C# never writes its fields, so <c>new T()</c> is the one way it names a value, and the constructor stays
    /// whatever its defaults become.
    /// </summary>
    public bool NeedsConstructor(DescribedType type) => IsReadOnly(type) || !HasZeroDefaults(type);

    private bool HasZeroDefaults(DescribedType type) => type.Fields.All(field => IsZero(field.Kind, field.Default));

    /// <summary>Whether a recorded default is all zero bytes, so C#'s <c>default</c> already holds it.</summary>
    private bool IsZero(FieldKind kind, JsonElement value)
    {
        switch (kind.Kind)
        {
            case "array":
                var element = new FieldKind(kind.ElementKind!, kind.ElementType, null, null, 0);
                return value.EnumerateArray().All(item => IsZero(element, item));
            case "pointer":
                return value.ValueKind == JsonValueKind.Null;
            case "bool":
                return value.ValueKind == JsonValueKind.False;
            case "f32":
            case "f64":
                // -0 is not zero bytes.
                return value.ValueKind == JsonValueKind.Number && value.GetDouble() is var number && number == 0 &&
                       !double.IsNegative(number);
            case "enum":
                return Type(kind.Type!).Values.First(enumerator => enumerator.Name == value.GetString()).Value == 0;
            case "struct":
                return IsZero(Type(kind.Type!), value);
            default:
                return value.ValueKind == JsonValueKind.Number && value.GetRawText() == "0";
        }
    }

    private bool IsZero(DescribedType type, JsonElement value)
    {
        var members = value.EnumerateObject().ToDictionary(member => member.Name, member => member.Value, StringComparer.Ordinal);
        switch (type.Category)
        {
            case Category.BoundType:
                // Its own default is zero unless [bound-types] gives it another; the components a default sets are numbers.
                return members.Count == 0
                           ? Config.BoundTypes[type.Name].Default == null
                           : members.Values.All(component => IsZero(new FieldKind("f64", null, null, null, 0), component));
            case Category.TaggedUnion:
                // Its tag value is 0 (checked), so its default kind, with that kind's own defaults, decides.
                var kind = type.Kinds.First(kind => kind.Name == type.DefaultKind);
                return HasZeroDefaults(Type(kind.Kind.Type!));
            default:
                return type.Fields.All(field => IsZero(field.Kind, members.TryGetValue(field.Name, out var set) ? set : field.Default));
        }
    }

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
            return type.Category == Category.BoundType && Config.BoundTypes[type.Name].Default is { } own ? own : "new()";
        }

        if (type.Category == Category.BoundType)
        {
            // A bound type's components, by name in storage order: through its configured constructor, else by name.
            // Their kinds are not described, so an integral value is written as the integer it is (as a float a large
            // one would round), which C# converts to the component's type as C++ does.
            var bound = Config.BoundTypes[type.Name];
            var components = members.ToDictionary(member => member.Name,
                                                  member => member.Value.ValueKind == JsonValueKind.Number &&
                                                            !member.Value.GetRawText().Any(c => c is '.' or 'e' or 'E')
                                                                ? member.Value.GetRawText()
                                                                : Float(member.Value, true, where));
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

    // Finds the type each binding in `uses` mirrors among the described ones: one binding's at most, with the layout that
    // binding describes, never one this binding selects, excludes or binds as a bound type.
    private void CheckUses(IReadOnlyList<DescribedType> described)
    {
        foreach (var used in Uses)
        {
            foreach (var mirrored in used.Mirrored)
            {
                if (described.FirstOrDefault(type => type.Name == mirrored.Name) is not { } type)
                {
                    continue;
                }

                var where = $"{Config.Path}: {type.Name} is mirrored by the binding it uses, {used.Config.Path}";
                if (owners.TryGetValue(type.Name, out var other))
                {
                    throw new BindingException($"{where}, and by {other.Config.Path}: use the type from one of them");
                }

                if (Config.Exclude.Contains(type.Name) || Config.BoundTypes.ContainsKey(type.Name))
                {
                    throw new BindingException(
                        $"{where}, but {(Config.Exclude.Contains(type.Name) ? "exclude" : "[bound-types]")} names it too: " +
                        "a used type is that binding's, so name it in neither");
                }

                if (type.Role is not (Role.Nested or Role.None))
                {
                    throw new BindingException(
                        $"{where}, but {Path.GetFileName(type.Source)} selects it as its own: select it in one package");
                }

                if (type.Category != mirrored.Category || type.Size != mirrored.Size || type.Alignment != mirrored.Alignment)
                {
                    throw new BindingException(
                        $"{where}, which describes it with another layout ({Path.GetFileName(mirrored.Source)}): " +
                        "regenerate both descriptions");
                }

                owners.Add(type.Name, used);
            }
        }
    }

    // Every excluded type is described, and no type the binding keeps holds one.
    private void CheckExcluded(IReadOnlyList<DescribedType> described)
    {
        foreach (var name in Config.Exclude.Where(name => described.All(type => type.Name != name)))
        {
            throw new BindingException($"{Config.Path}: exclude names {name}, which no description describes");
        }

        foreach (var type in described.Where(type => !Config.Exclude.Contains(type.Name)))
        {
            var held = new List<(string Member, string? Type)>();
            held.AddRange(type.Fields.Select(field => (field.Name, field.Kind.Type ?? field.Kind.ElementType)));
            held.AddRange(type.Kinds.Select(kind => (kind.Name, kind.Kind.Type)));
            if (type.Tag is { } tag)
            {
                held.Add((tag.Name, tag.Kind.Type));
            }

            foreach (var (member, name) in held.Where(member => member.Type != null && Config.Exclude.Contains(member.Type)))
            {
                throw new BindingException(
                    $"{Config.Path}: exclude names {name}, but {type.Name}::{member} holds one: exclude {type.Name} too");
            }
        }
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

        foreach (var name in Config.CSharp.ReadOnlyStructs)
        {
            if (!types.TryGetValue(name, out var mirrored) || mirrored.Category != Category.Struct)
            {
                throw new BindingException($"{Config.Path}: readonly-structs names {name}, which is not a described struct");
            }
        }

        foreach (var (key, list) in new[] { ("private-fields", Config.CSharp.PrivateFields), ("fixed-buffers", Config.CSharp.FixedBuffers) })
        {
            foreach (var name in list)
            {
                var separator = name.LastIndexOf("::", StringComparison.Ordinal);
                var owner = separator < 0 ? null : types.GetValueOrDefault(name[..separator]);
                var field = owner?.Category == Category.Struct
                                ? owner.Fields.FirstOrDefault(field => field.Name == name[(separator + 2)..])
                                : null;
                if (field == null)
                {
                    throw new BindingException($"{Config.Path}: {key} names {name}, which is not a field of a described struct");
                }

                // A fixed buffer holds numbers only, and is never readonly: C# allows neither.
                if (key == "fixed-buffers" && (!field.Kind.IsArray || field.Kind.ElementKind is "struct" or "enum" or "pointer"))
                {
                    throw new BindingException($"{Config.Path}: fixed-buffers names {name}, which is not an array of numbers");
                }

                if (key == "fixed-buffers" && IsReadOnly(owner!))
                {
                    throw new BindingException(
                        $"{Config.Path}: fixed-buffers names {name}, but {owner!.Name}'s mirror is read-only and a fixed buffer cannot be");
                }
            }
        }

        foreach (var name in Config.Registration?.ReadOnly ?? new HashSet<string>())
        {
            if (!types.TryGetValue(name, out var registered) || !(registered.Role == Role.Component || IsResource(registered)))
            {
                throw new BindingException(
                    $"{Config.Path}: [registration] read-only names {name}, which is not a described component or resource");
            }
        }

        foreach (var name in Config.Registration?.ByHand ?? new HashSet<string>())
        {
            if (!types.TryGetValue(name, out var registered) || !IsRegisteredRole(registered) || IsUsed(registered))
            {
                throw new BindingException(
                    $"{Config.Path}: [registration] by-hand names {name}, which is not a described component, event, " +
                    "resource or shared struct this binding mirrors");
            }
        }

        CheckResources();
        CheckInternal();
        foreach (var (name, bound) in Config.BoundTypes.Where(entry => entry.Value.Wrapper != null))
        {
            if (bound.Wrapper is "bool" || !IsScalar(bound.Wrapper!))
            {
                throw new BindingException(
                    $"{Config.Path}: [bound-types] {name}'s wrapper is '{bound.Wrapper}', which is not a number kind");
            }
        }

        foreach (var type in Mirrored)
        {
            CheckType(type);
        }
    }

    private static bool IsScalar(string kind)
    {
        try
        {
            _ = CSharpScalar(kind);
            return true;
        }
        catch (BindingException)
        {
            return false;
        }
    }

    // A resource mirrored here needs the SDK's resource names, and with a registration its factory.
    private void CheckResources()
    {
        foreach (var type in Mirrored.Where(IsResource))
        {
            var readOnly = IsRegisteredReadOnly(type);
            var scope = type.Role == Role.SceneResource ? "scene" : "app";
            var interfaceKey = readOnly ? "read-only-resource-interface" : "resource-interface";
            var missing = Config.CSharp.ResourceAttribute == null ? "resource-attribute"
                          : (readOnly ? Config.CSharp.ReadOnlyResourceInterface : Config.CSharp.ResourceInterface) == null ? interfaceKey
                          : null;
            if (missing != null)
            {
                throw new BindingException($"{Config.Path}: {type.Name} is a{(readOnly ? " read-only" : "")} {scope} " +
                                           $"resource, but [csharp] has no {missing}");
            }

            if (Config.Registration is { } registration && !registration.ByHand.Contains(type.Name) &&
                ResourceFactory(registration, type) == null)
            {
                throw new BindingException(
                    $"{Config.Path}: {type.Name} is a{(readOnly ? " read-only" : "")} {scope} resource, but [registration] " +
                    $"has no make-{(readOnly ? "read-only-" : "")}{scope}-resource");
            }
        }
    }

    /// <summary>The factory a resource is registered by, or null when the configuration names none.</summary>
    public string? ResourceFactory(RegistrationConfig registration, DescribedType type) =>
        (type.Role == Role.SceneResource, IsRegisteredReadOnly(type)) switch
        {
            (true, false) => registration.MakeSceneResource,
            (true, true) => registration.MakeReadOnlySceneResource,
            (false, false) => registration.MakeAppResource,
            (false, true) => registration.MakeReadOnlyAppResource,
        };

    // Each internal mirror is a struct mirrored here, and no public mirror holds one (C# refuses a public field of an
    // internal type).
    private void CheckInternal()
    {
        foreach (var name in Config.CSharp.InternalStructs)
        {
            if (!types.TryGetValue(name, out var type) || type.Category is not (Category.Struct or Category.TaggedUnion) ||
                IsUsed(type))
            {
                throw new BindingException(
                    $"{Config.Path}: internal-structs names {name}, which is not a described struct this binding mirrors");
            }
        }

        foreach (var type in Mirrored.Where(type => !IsInternal(type)))
        {
            foreach (var held in Named(type).Where(held => held != null && Config.CSharp.InternalStructs.Contains(held)).Distinct())
            {
                throw new BindingException(
                    $"{Config.Path}: internal-structs names {held}, but the public {type.Name} holds one: list it too");
            }
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

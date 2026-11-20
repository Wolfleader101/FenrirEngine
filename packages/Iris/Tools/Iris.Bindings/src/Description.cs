using System.Text.Json;

namespace Iris.Bindings;

/// <summary>What a type is: the description's <c>category</c>.</summary>
internal enum Category
{
    Struct,
    TaggedUnion,
    BoundType,
    Enum,
}

/// <summary>Why a type is described: the description's <c>role</c>. Enums have none.</summary>
internal enum Role
{
    None,
    Component,
    Event,
    SharedStruct,
    BoundType,
    SceneResource,
    AppResource,
    Nested,
}

/// <summary>
/// A field's kind and what it names: <c>kind</c>, <c>type</c> for an enum or struct, and for an array the element's kind,
/// type and count.
/// </summary>
internal sealed record FieldKind(string Kind, string? Type, string? ElementKind, string? ElementType, int Length)
{
    public bool IsArray => Kind == "array";

    /// <summary>
    /// The type a <c>reference</c> (or an array of them) names, which is read as the bound handle of it: kept so a
    /// handle no description describes is diagnosed as the reference's.
    /// </summary>
    public string? Target { get; init; }
}

/// <summary>A struct field, or a tagged union's tag (its default is the tag value of the default kind).</summary>
internal sealed record Field(string Name, int Offset, FieldKind Kind, JsonElement Default, string? Doc);

/// <summary>One kind of a tagged union: the union member, the tag value that selects it, and what it holds.</summary>
internal sealed record UnionKind(string Name, string Tag, FieldKind Kind, string? Doc);

/// <summary>An enumerator.</summary>
internal sealed record EnumValue(string Name, long Value, string? Doc);

/// <summary>
/// A described type, as <c>SCHEMA.md</c> of the input format records it. Which members are set depends on the category:
/// <see cref="Fields" /> for a struct, the tag, union offset and kinds for a tagged union, the underlying kind and values
/// for an enum, and only the layout for a bound type.
/// </summary>
internal sealed record DescribedType(
    string Name,
    Category Category,
    Role Role,
    int Size,
    int Alignment,
    string? Doc,
    string Source,
    IReadOnlyList<Field> Fields,
    Field? Tag,
    int UnionOffset,
    IReadOnlyList<UnionKind> Kinds,
    string? DefaultKind,
    string? Underlying,
    IReadOnlyList<EnumValue> Values);

/// <summary>Reads API descriptions (<c>docs/api-description.md</c>): the input the generator accepts.</summary>
internal static class Description
{
    /// <summary>
    /// Reads every description and returns their types in input order. A type in two descriptions must be described
    /// alike in both: a bound type of one layout, or another type with the same fields, kinds or values, which a
    /// binding's own description holds as <c>nested</c> where the package's selects it (a package type inside a
    /// binding-owned copy). It is kept once, with the role it is selected with.
    /// </summary>
    public static IReadOnlyList<DescribedType> Read(IEnumerable<string> paths)
    {
        var types = new List<DescribedType>();
        var byName = new Dictionary<string, DescribedType>(StringComparer.Ordinal);
        var skipped = new HashSet<string>(StringComparer.Ordinal);
        foreach (var path in paths)
        {
            foreach (var type in ReadOne(Path.GetFullPath(path), skipped))
            {
                if (byName.TryGetValue(type.Name, out var earlier))
                {
                    if (earlier.Category != type.Category || earlier.Size != type.Size || earlier.Alignment != type.Alignment ||
                        (type.Category != Category.BoundType && !Alike(earlier, type)))
                    {
                        throw new BindingException(
                            $"{type.Source}: {type.Name} is also described by {earlier.Source}, differently; a type " +
                            "described twice must be described alike");
                    }

                    if (earlier.Role != type.Role && earlier.Role is not Role.Nested && type.Role is not Role.Nested)
                    {
                        throw new BindingException(
                            $"{type.Source}: {type.Name} is also described by {earlier.Source}, with another role; a type " +
                            "described twice is selected in one description at most");
                    }

                    if (earlier.Role == Role.Nested && type.Role != Role.Nested)
                    {
                        // Selected here, nested there: kept where it was first described, with the role it is selected with.
                        types[types.IndexOf(earlier)] = type;
                        byName[type.Name] = type;
                    }

                    continue;
                }

                byName.Add(type.Name, type);
                types.Add(type);
            }
        }

        return Bound(types, skipped);
    }

    // The data asset and settings roles, which the C# spec binds; until then a binding skips them and what only they
    // hold.
    private static bool IsSkippedRole(string role) => role is "data-asset" or "settings";

    // describe-api's owning kinds (SCHEMA.md of the input format, "Owning kinds"), which only skipped types hold.
    private static bool IsOwningKind(string kind) => kind is "string" or "list" or "optional" or "path";

    // The types a binding binds: all but the nested types and enums that a skipped type reaches and no selected type
    // does (a data asset's table row, a settings type's mode), each checked to hold no owning kind and to have a layout.
    // An enum a selection names for a native and only a skipped type holds is dropped too: a description does not say
    // which enums were selected.
    private static List<DescribedType> Bound(List<DescribedType> types, IEnumerable<string> namedBySkipped)
    {
        var byName = types.ToDictionary(type => type.Name, StringComparer.Ordinal);
        var fromSkipped = Reached(namedBySkipped);
        var fromSelected = Reached(types.Where(type => type.Role is not (Role.Nested or Role.None))
                                        .SelectMany(Binding.Named)
                                        .OfType<string>());
        var bound = types.Where(type => type.Role is not (Role.Nested or Role.None) || fromSelected.Contains(type.Name) ||
                                        !fromSkipped.Contains(type.Name))
                         .ToList();
        foreach (var type in bound)
        {
            foreach (var field in type.Fields.Where(field => IsOwningKind(field.Kind.Kind)))
            {
                throw new BindingException(
                    $"{type.Source}: {type.Name}::{field.Name}: a {field.Kind.Kind}, an owning kind, which a binding skips " +
                    "only in a data asset or settings type and what only they hold");
            }

            if (type.Size < 0)
            {
                throw new BindingException($"{type.Source}: {type.Name}: the description has no 'size'");
            }

            foreach (var field in type.Fields.Where(field => field.Offset < 0))
            {
                throw new BindingException($"{type.Source}: {type.Name}::{field.Name}: the description has no 'offset'");
            }
        }

        return bound;

        // These names and every described type they reach through fields, tags and kinds.
        HashSet<string> Reached(IEnumerable<string> names)
        {
            var reached = new HashSet<string>(names, StringComparer.Ordinal);
            var next = new Queue<string>(reached);
            while (next.Count > 0)
            {
                if (byName.TryGetValue(next.Dequeue(), out var held))
                {
                    foreach (var name in Binding.Named(held).OfType<string>().Where(reached.Add))
                    {
                        next.Enqueue(name);
                    }
                }
            }

            return reached;
        }
    }

    // Whether two descriptions of a type that is not a bound type record the same fields, tag, kinds and values. Docs may
    // differ.
    private static bool Alike(DescribedType a, DescribedType b)
    {
        static bool SameField(Field x, Field y) =>
            x.Name == y.Name && x.Offset == y.Offset && x.Kind == y.Kind && x.Default.GetRawText() == y.Default.GetRawText();

        return a.Fields.Count == b.Fields.Count && a.Fields.Zip(b.Fields).All(pair => SameField(pair.First, pair.Second)) &&
               (a.Tag == null) == (b.Tag == null) && (a.Tag == null || SameField(a.Tag, b.Tag!)) &&
               a.UnionOffset == b.UnionOffset && a.DefaultKind == b.DefaultKind && a.Underlying == b.Underlying &&
               a.Kinds.Select(kind => (kind.Name, kind.Tag, kind.Kind)).SequenceEqual(b.Kinds.Select(kind => (kind.Name, kind.Tag, kind.Kind))) &&
               a.Values.Select(value => (value.Name, value.Value)).SequenceEqual(b.Values.Select(value => (value.Name, value.Value)));
    }

    // A description's types but the skipped ones, whose names and the types they name go to `skipped`.
    private static IEnumerable<DescribedType> ReadOne(string path, HashSet<string> skipped)
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
        if (Integer(root, "pointerSize", path) != 8 || String(root, "alignment", path) != "natural")
        {
            throw new BindingException(
                $"{path}: the description is for another target; the generator reads only pointerSize 8 and natural alignment");
        }

        foreach (var type in Array(root, "types", path).EnumerateArray())
        {
            if (OptionalString(type, "role") is { } role && IsSkippedRole(role))
            {
                skipped.UnionWith(NamesIn(type));
                continue;
            }

            yield return ReadType(type, path);
        }
    }

    // Every type a skipped type names: its base, and its fields' types and elements' types at any depth.
    private static IEnumerable<string> NamesIn(JsonElement element)
    {
        switch (element.ValueKind)
        {
            case JsonValueKind.Object:
                foreach (var property in element.EnumerateObject())
                {
                    if (property.Name is "type" or "elementType" or "base" && property.Value.ValueKind == JsonValueKind.String)
                    {
                        yield return property.Value.GetString()!;
                    }
                    else if (property.Name is "fields" or "element")
                    {
                        foreach (var name in NamesIn(property.Value))
                        {
                            yield return name;
                        }
                    }
                }

                break;
            case JsonValueKind.Array:
                foreach (var name in element.EnumerateArray().SelectMany(NamesIn))
                {
                    yield return name;
                }

                break;
        }
    }

    private static DescribedType ReadType(JsonElement type, string path)
    {
        var name = String(type, "name", path);
        var where = $"{path}: {name}";
        var category = String(type, "category", where) switch
        {
            "struct" => Category.Struct,
            "tagged-union" => Category.TaggedUnion,
            "bound-type" => Category.BoundType,
            "enum" => Category.Enum,
            var other => throw new BindingException($"{where}: unknown category '{other}'"),
        };
        var role = category == Category.Enum
                       ? Role.None
                       : String(type, "role", where) switch
                       {
                           "component" => Role.Component,
                           "event" => Role.Event,
                           "shared-struct" => Role.SharedStruct,
                           "bound-type" => Role.BoundType,
                           "scene-resource" => Role.SceneResource,
                           "app-resource" => Role.AppResource,
                           "nested" => Role.Nested,
                           var other => throw new BindingException($"{where}: unknown role '{other}'"),
                       };

        var fields = category == Category.Struct
                         ? Array(type, "fields", where).EnumerateArray().Select(field => ReadField(field, where)).ToList()
                         : [];
        Field? tag = null;
        var unionOffset = 0;
        var kinds = new List<UnionKind>();
        string? defaultKind = null;
        if (category == Category.TaggedUnion)
        {
            tag = ReadField(Object(type, "tag", where), where);
            unionOffset = Integer(type, "unionOffset", where);
            kinds = Array(type, "kinds", where)
                        .EnumerateArray()
                        .Select(kind => new UnionKind(String(kind, "name", where), String(kind, "tag", where),
                                                      ReadKind(kind, where), OptionalString(kind, "doc")))
                        .ToList();
            defaultKind = String(type, "defaultKind", where);
        }

        string? underlying = null;
        var values = new List<EnumValue>();
        if (category == Category.Enum)
        {
            underlying = String(type, "underlying", where);
            values = Array(type, "values", where)
                         .EnumerateArray()
                         .Select(value => new EnumValue(String(value, "name", where),
                                                        Property(value, "value", where).GetInt64(),
                                                        OptionalString(value, "doc")))
                         .ToList();
        }

        // A type reached by member records no layout; Bound refuses one a binding keeps.
        var reachedByMember = !type.TryGetProperty("size", out _);
        return new DescribedType(name, category, role, reachedByMember ? -1 : Integer(type, "size", where),
                                 reachedByMember ? -1 : Integer(type, "alignment", where),
                                 OptionalString(type, "doc"), path, fields, tag, unionOffset, kinds, defaultKind,
                                 underlying, values);
    }

    private static Field ReadField(JsonElement field, string where)
    {
        var name = String(field, "name", where);
        var subject = $"{where}::{name}";
        var described = Property(field, "default", subject);
        var kind = String(field, "kind", subject);
        var element = kind == "array" ? String(field, "elementKind", subject) : null;
        var value = (kind, element) switch
        {
            ("entity" or "reference", _) => Empty(described, kind, subject),
            ("fixed-string", _) => TextBytes(described, Integer(field, "length", subject), subject),
            ("array", "entity" or "reference") =>
                Json(described.ValueKind == JsonValueKind.Array
                         ? $"[{string.Join(",", described.EnumerateArray().Select(item => Empty(item, element!, subject).GetRawText()))}]"
                         : throw new BindingException($"{subject}: 'default' is not a list")),
            _ => described.Clone(),
        };
        // A field of a type reached by member records no offset; Bound refuses one a binding keeps.
        var offset = field.TryGetProperty("offset", out _) ? Integer(field, "offset", where) : -1;
        return new Field(name, offset, ReadKind(field, subject), value, OptionalString(field, "doc"));
    }

    // describe-api's fixed-layout kinds are read as the shapes they bind as (SCHEMA.md of the input format, "Kinds"): an
    // `entity` as the engine's entity, the built-in binding's mirror of FenrirECS's `Fenrir::Entity`; a `fixed-string` as
    // the array of `u8` its bytes are; a `reference` as the asset handle of its target, a bound type the assets binding
    // binds. So a binding generates what it did when the description recorded them by those shapes.

    /// <summary>The engine's entity, which an <c>entity</c> kind holds.</summary>
    internal const string EntityType = "Fenrir::Entity";

    /// <summary>The bound handle a <c>reference</c> to <paramref name="target" /> holds.</summary>
    internal static string HandleType(string target) => $"Fenrir::AssetHandle<{target}>";

    private static FieldKind ReadKind(JsonElement field, string where)
    {
        var kind = String(field, "kind", where);
        switch (kind)
        {
            case "array":
                var described = String(field, "elementKind", where);
                var elementTarget = described == "reference" ? String(field, "elementTarget", where) : null;
                var (elementKind, elementType) = described switch
                {
                    "entity" => ("struct", EntityType),
                    "reference" => ("struct", HandleType(elementTarget!)),
                    _ => (described, OptionalString(field, "elementType")),
                };
                return new FieldKind(kind, null, elementKind, elementType, Integer(field, "length", where))
                {
                    Target = elementTarget,
                };
            case "entity":
                return new FieldKind("struct", EntityType, null, null, 0);
            case "reference":
                var target = String(field, "target", where);
                return new FieldKind("struct", HandleType(target), null, null, 0) { Target = target };
            case "fixed-string":
                return new FieldKind("array", null, "u8", null, Integer(field, "length", where));
            case var owning when IsOwningKind(owning):
                // Read only to be dropped or refused (Bound). A list or optional names one type at most, its innermost
                // element's, kept as the type Binding.Named reaches through it.
                return new FieldKind(kind, NamesIn(field).SingleOrDefault(), null, null, 0);
            default:
                return new FieldKind(kind, OptionalString(field, "type"), null, null, 0);
        }
    }

    // An entity's or reference's default, which names nothing (null), as its struct's own defaults ({}).
    private static JsonElement Empty(JsonElement value, string kind, string where) =>
        value.ValueKind == JsonValueKind.Null
            ? Json("{}")
            : throw new BindingException($"{where}: an {kind}'s 'default' is not null");

    // A fixed string's default text as its bytes: UTF-8, then zeros to the length.
    private static JsonElement TextBytes(JsonElement value, int length, string where)
    {
        if (value.ValueKind != JsonValueKind.String)
        {
            throw new BindingException($"{where}: a fixed string's 'default' is not a string");
        }

        var bytes = System.Text.Encoding.UTF8.GetBytes(value.GetString()!);
        if (bytes.Length >= length)
        {
            throw new BindingException($"{where}: the default leaves no room in its {length} bytes for the terminator");
        }

        return Json($"[{string.Join(",", bytes.Concat(new byte[length - bytes.Length]))}]");
    }

    private static JsonElement Json(string text)
    {
        using var document = JsonDocument.Parse(text);
        return document.RootElement.Clone();
    }

    internal static JsonElement Property(JsonElement element, string key, string where) =>
        element.TryGetProperty(key, out var value)
            ? value
            : throw new BindingException($"{where}: the description has no '{key}'");

    internal static string String(JsonElement element, string key, string where) =>
        Property(element, key, where) is { ValueKind: JsonValueKind.String } value
            ? value.GetString()!
            : throw new BindingException($"{where}: '{key}' is not a string");

    internal static string? OptionalString(JsonElement element, string key) =>
        element.TryGetProperty(key, out var value) && value.ValueKind == JsonValueKind.String ? value.GetString() : null;

    private static int Integer(JsonElement element, string key, string where) =>
        Property(element, key, where) is { ValueKind: JsonValueKind.Number } value && value.TryGetInt32(out var number)
            ? number
            : throw new BindingException($"{where}: '{key}' is not an integer");

    internal static JsonElement Array(JsonElement element, string key, string where) =>
        Property(element, key, where) is { ValueKind: JsonValueKind.Array } value
            ? value
            : throw new BindingException($"{where}: '{key}' is not a list");

    internal static JsonElement Object(JsonElement element, string key, string where) =>
        Property(element, key, where) is { ValueKind: JsonValueKind.Object } value
            ? value
            : throw new BindingException($"{where}: '{key}' is not an object");
}

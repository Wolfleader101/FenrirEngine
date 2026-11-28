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
    DataAsset,
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

    /// <summary>
    /// What a <c>list</c> or <c>optional</c> holds: its element's kind, read from the element keys or the
    /// <c>element</c> object. Null for every other kind.
    /// </summary>
    public FieldKind? Element { get; init; }
}

/// <summary>A struct field, or a tagged union's tag (its default is the tag value of the default kind).</summary>
internal sealed record Field(string Name, int Offset, FieldKind Kind, JsonElement Default, string? Doc)
{
    /// <summary>The names the field had before (<c>@formerly</c>), which a file may still spell.</summary>
    public IReadOnlyList<string> Formerly { get; init; } = [];

    /// <summary>Whether the field is <c>@transient</c>: read-back state in the layout that no file holds.</summary>
    public bool Transient { get; init; }

    /// <summary>The inspector's metadata (<c>@range</c>, <c>@display</c>, ...) as the description records it; null for none.</summary>
    public JsonElement? Metadata { get; init; }
}

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
    IReadOnlyList<EnumValue> Values)
{
    /// <summary>For a data asset extending another: the C++ name of the data asset it extends.</summary>
    public string? Base { get; init; }

    /// <summary>The version a file writes it at (<c>@version</c>): 1 unless the description says otherwise.</summary>
    public int Version { get; init; } = 1;

    /// <summary>The C++ names the type had before (<c>@formerly</c>).</summary>
    public IReadOnlyList<string> Formerly { get; init; } = [];

    /// <summary>Whether the type is <c>@transient</c>.</summary>
    public bool Transient { get; init; }

    /// <summary>Whether the type is <c>@editor-only</c>.</summary>
    public bool EditorOnly { get; init; }

    /// <summary>
    /// Whether the type is a nested struct that only data assets reach: C# holds it in data asset classes and in no
    /// mirror with a layout, so it gets a mirror with no mapping, and may hold the owning kinds.
    /// </summary>
    public bool DataOnly { get; init; }

    /// <summary>Whether the type is a data asset or a nested struct only data assets reach: neither has a layout C# mirrors.</summary>
    public bool ByMember => Role == Role.DataAsset || DataOnly;
}

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

    // The settings role, which the settings spec binds; until then a binding skips it and what only it holds.
    private static bool IsSkippedRole(string role) => role is "settings";

    // describe-api's owning kinds (SCHEMA.md of the input format, "Owning kinds"), which only data assets, skipped types
    // and the nested structs only they reach hold.
    internal static bool IsOwningKind(string kind) => kind is "string" or "list" or "optional" or "path";

    // The types a binding binds: all but the nested types and enums that a skipped type reaches and no selected type
    // does (a settings type's mode). An enum a selection names for a native and only a skipped type holds is dropped too:
    // a description does not say which enums were selected. A nested struct that data assets reach and no type with a
    // layout does is marked data-only (DescribedType.DataOnly): it and the data assets are reached by member, so they may
    // hold the owning kinds and record no layout. Every other type is checked to hold no owning kind and to have one.
    private static List<DescribedType> Bound(List<DescribedType> types, IEnumerable<string> namedBySkipped)
    {
        var byName = types.ToDictionary(type => type.Name, StringComparer.Ordinal);
        var fromSkipped = Reached(namedBySkipped);
        var fromFixed = Reached(types.Where(type => type.Role is not (Role.Nested or Role.None or Role.DataAsset))
                                     .SelectMany(Binding.Named)
                                     .OfType<string>());
        var fromData = Reached(types.Where(type => type.Role == Role.DataAsset).SelectMany(Binding.Named).OfType<string>());
        var bound = types.Where(type => type.Role is not (Role.Nested or Role.None) || fromFixed.Contains(type.Name) ||
                                        fromData.Contains(type.Name) || !fromSkipped.Contains(type.Name))
                         .Select(type => type.Role == Role.Nested && type.Category != Category.BoundType &&
                                         fromData.Contains(type.Name) && !fromFixed.Contains(type.Name)
                                             ? type with { DataOnly = true }
                                             : type)
                         .ToList();
        foreach (var type in bound.Where(type => !type.ByMember))
        {
            foreach (var field in type.Fields.Where(field => IsOwningKind(field.Kind.Kind)))
            {
                throw new BindingException(
                    $"{type.Source}: {type.Name}::{field.Name}: a {field.Kind.Kind}, an owning kind, which a binding binds " +
                    "only in a data asset and the nested structs only data assets reach");
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
                           "data-asset" => Role.DataAsset,
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

        // A type reached by member records no layout; Bound refuses one a binding keeps, but for a data asset and what
        // only data assets reach.
        var reachedByMember = !type.TryGetProperty("size", out _);
        return new DescribedType(name, category, role, reachedByMember ? -1 : Integer(type, "size", where),
                                 reachedByMember ? -1 : Integer(type, "alignment", where),
                                 OptionalString(type, "doc"), path, fields, tag, unionOffset, kinds, defaultKind,
                                 underlying, values)
        {
            Base = OptionalString(type, "base"),
            Version = type.TryGetProperty("version", out _) ? Integer(type, "version", where) : 1,
            Formerly = Names(type, "formerly", where),
            Transient = Flag(type, "transient"),
            EditorOnly = Flag(type, "editorOnly"),
        };
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
        return new Field(name, offset, ReadKind(field, subject), value, OptionalString(field, "doc"))
        {
            Formerly = Names(field, "formerly", subject),
            Transient = Flag(field, "transient"),
            Metadata = field.TryGetProperty("metadata", out var metadata) && metadata.ValueKind == JsonValueKind.Object
                           ? metadata.Clone()
                           : null,
        };
    }

    // A list of names (`formerly`); none when the key is absent.
    private static IReadOnlyList<string> Names(JsonElement element, string key, string where) =>
        !element.TryGetProperty(key, out var names) ? []
        : names.ValueKind == JsonValueKind.Array && names.EnumerateArray().All(name => name.ValueKind == JsonValueKind.String)
            ? names.EnumerateArray().Select(name => name.GetString()!).ToList()
            : throw new BindingException($"{where}: '{key}' is not a list of strings");

    private static bool Flag(JsonElement element, string key) =>
        element.TryGetProperty(key, out var flag) && flag.ValueKind == JsonValueKind.True;

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
            case "list" or "optional":
                // A list or optional names one type at most, its innermost element's, kept as the type Binding.Named
                // reaches through it; its element is read whole for a data asset's codecs.
                return new FieldKind(kind, NamesIn(field).SingleOrDefault(), null, null, 0) { Element = ElementOf(field, where) };
            case var owning when IsOwningKind(owning):
                return new FieldKind(kind, null, null, null, 0);
            default:
                return new FieldKind(kind, OptionalString(field, "type"), null, null, 0);
        }
    }

    // A list's or optional's element: the `element` object when the element has keys of its own, else the element keys.
    private static FieldKind ElementOf(JsonElement field, string where)
    {
        if (field.TryGetProperty("element", out var element) && element.ValueKind == JsonValueKind.Object)
        {
            return ReadKind(element, where);
        }

        var kind = String(field, "elementKind", where);
        switch (kind)
        {
            case "entity":
                return new FieldKind("struct", EntityType, null, null, 0);
            case "reference":
                var target = String(field, "elementTarget", where);
                return new FieldKind("struct", HandleType(target), null, null, 0) { Target = target };
            default:
                return new FieldKind(kind, OptionalString(field, "elementType"), null, null, 0);
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

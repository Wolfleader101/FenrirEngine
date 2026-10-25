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
        foreach (var path in paths)
        {
            foreach (var type in ReadOne(Path.GetFullPath(path)))
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

        return types;
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

    private static IEnumerable<DescribedType> ReadOne(string path)
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
            yield return ReadType(type, path);
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

        return new DescribedType(name, category, role, Integer(type, "size", where), Integer(type, "alignment", where),
                                 OptionalString(type, "doc"), path, fields, tag, unionOffset, kinds, defaultKind,
                                 underlying, values);
    }

    private static Field ReadField(JsonElement field, string where)
    {
        var name = String(field, "name", where);
        return new Field(name, Integer(field, "offset", where), ReadKind(field, $"{where}::{name}"),
                         Property(field, "default", $"{where}::{name}").Clone(), OptionalString(field, "doc"));
    }

    private static FieldKind ReadKind(JsonElement field, string where)
    {
        var kind = String(field, "kind", where);
        return kind == "array"
                   ? new FieldKind(kind, null, String(field, "elementKind", where), OptionalString(field, "elementType"),
                                   Integer(field, "length", where))
                   : new FieldKind(kind, OptionalString(field, "type"), null, null, 0);
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

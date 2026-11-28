using System.Globalization;
using System.Text.Json;

namespace Iris.Bindings;

/// <summary>What a data asset's field holds, as its codecs write and read it.</summary>
internal abstract record DataShape;

/// <summary>A number, <c>bool</c> or <c>char16</c>, by its kind.</summary>
internal sealed record PlainShape(string Kind) : DataShape;

/// <summary>An enum, written by enumerator name.</summary>
internal sealed record EnumShape(DescribedType Type) : DataShape;

/// <summary>A <c>string</c>, or a <c>path</c> held as one.</summary>
internal sealed record TextShape(bool Path) : DataShape;

/// <summary>A <c>list</c>: C#'s <c>List&lt;T&gt;</c>.</summary>
internal sealed record ListShape(DataShape Element) : DataShape;

/// <summary>An <c>optional</c> of a value type: C#'s <c>T?</c>.</summary>
internal sealed record OptionalShape(DataShape Held) : DataShape;

/// <summary>A <c>reference</c>: an asset handle of the target, written as the path the resolver gives it.</summary>
internal sealed record ReferenceShape(string Target, string CSharpType) : DataShape;

/// <summary>A nested struct held by value, through its mirror.</summary>
internal sealed record StructShape(DescribedType Type) : DataShape;

/// <summary>
/// Writes what C# has of a binding's C++ data assets (ADR-0083 §3): a class per data asset, a mirror per nested struct only
/// data assets reach, and one registration class whose module initialiser hands their codecs to the SDK's registry. The
/// codecs write and read the store's tree as the C++ bridge does (ADR-0079): base fields first, a field at its default
/// left out, an absent one defaulted and an unknown one dropped, so C# never reads a data asset's bytes.
/// </summary>
internal static class DataAssetWriter
{
    /// <summary>The generated class whose module initialiser registers the codecs.</summary>
    public const string RegistrationClass = "NativeDataAssets";

    /// <summary>
    /// The shape of a field of <paramref name="holder" /> (a data asset or a struct one holds) of this kind, or a
    /// <see cref="BindingException" /> naming <paramref name="where" /> for what the codecs do not write.
    /// </summary>
    public static DataShape Shape(Binding binding, DescribedType holder, FieldKind kind, string where)
    {
        switch (kind.Kind)
        {
            case "enum":
                return new EnumShape(binding.Type(kind.Type!));
            case "string" or "path":
                return new TextShape(kind.Kind == "path");
            case "list":
                return new ListShape(Shape(binding, holder, kind.Element!, where + "[]"));
            case "optional":
                var held = Shape(binding, holder, kind.Element!, where);
                return held is TextShape or ListShape or OptionalShape
                           ? throw new BindingException(
                                 $"{where}: an optional {Spell(kind.Element!)}, but C# holds an optional only of a value type " +
                                 "(a number, enum, struct or handle)")
                           : new OptionalShape(held);
            case "struct" when kind.Target is { } target:
                return new ReferenceShape(target, binding.IsDataAssetTarget(target)
                                                      ? $"{binding.AssetHandle(holder)}<{binding.CSharpName(binding.Type(target))}>"
                                                      : binding.CSharpName(binding.Type(kind.Type!)));
            case "struct" when kind.Type == Description.EntityType:
                throw new BindingException($"{where}: an entity, which a data asset never holds (ADR-0075)");
            case "struct":
                var type = binding.Type(kind.Type!);
                return type.Category switch
                {
                    Category.Struct when type.Role == Role.DataAsset => throw new BindingException(
                        $"{where}: the data asset {type.Name} by value; a data asset names another by a reference"),
                    Category.Struct => new StructShape(type),
                    Category.BoundType => throw new BindingException(
                        $"{where}: the bound type {type.Name}, which a data asset's codecs do not write yet"),
                    _ => throw new BindingException(
                        $"{where}: the tagged union {type.Name}, which a data asset's codecs do not write yet"),
                };
            case "array":
                throw new BindingException(
                    $"{where}: a fixed-size array or fixed string, which a data asset's codecs do not write yet; hold a list or a string");
            case "pointer":
                throw new BindingException($"{where}: a pointer, which no file holds");
            default:
                _ = Binding.CSharpScalar(kind.Kind);
                return new PlainShape(kind.Kind);
        }
    }

    private static string Spell(FieldKind kind) => kind.Kind switch
    {
        "list" => "of a list",
        "optional" => "of an optional",
        _ => "of a " + kind.Kind,
    };

    /// <summary>
    /// The nested structs a binding's data asset classes hold, at any depth, each once in the order first reached, each
    /// checked to be one the codecs can fill: every field public and writable, and of a shape they write.
    /// </summary>
    public static IReadOnlyList<DescribedType> Structs(Binding binding, IEnumerable<DescribedType> holders)
    {
        var structs = new List<DescribedType>();
        var seen = new HashSet<string>(StringComparer.Ordinal);
        foreach (var holder in holders)
        {
            foreach (var field in holder.Fields)
            {
                Walk(Shape(binding, holder, field.Kind, $"{holder.Source}: {holder.Name}::{field.Name}"));
            }
        }

        return structs;

        void Walk(DataShape shape)
        {
            switch (shape)
            {
                case ListShape list:
                    Walk(list.Element);
                    break;
                case OptionalShape optional:
                    Walk(optional.Held);
                    break;
                case StructShape held when seen.Add(held.Type.Name):
                    var type = held.Type;
                    if (binding.IsReadOnly(type) || type.Fields.Any(field => binding.IsPrivate(type, field)))
                    {
                        throw new BindingException(
                            $"{type.Source}: {type.Name} is held by a data asset, but its mirror is read-only or has private " +
                            "fields, which the codecs cannot fill");
                    }

                    structs.Add(type);
                    foreach (var field in type.Fields)
                    {
                        Walk(Shape(binding, type, field.Kind, $"{type.Source}: {type.Name}::{field.Name}"));
                    }

                    break;
            }
        }
    }

    /// <summary>
    /// Each file this writes, by name: a class per data asset, a mirror per nested struct only data assets reach, and the
    /// registration class. Empty when the binding mirrors no data asset.
    /// </summary>
    public static IEnumerable<KeyValuePair<string, string>> Files(Binding binding)
    {
        var assets = binding.DataAssets.ToList();
        if (assets.Count == 0)
        {
            yield break;
        }

        var structs = Structs(binding, assets);
        foreach (var type in assets)
        {
            yield return KeyValuePair.Create(binding.CSharpName(type) + ".cs", Class(binding, type));
        }

        foreach (var type in binding.Mirrored.Where(type => type.DataOnly))
        {
            yield return KeyValuePair.Create(binding.CSharpName(type) + ".cs", DataOnlyMirror(binding, type));
        }

        yield return KeyValuePair.Create(RegistrationClass + ".cs", Registration(binding, assets, structs));
    }

    // ---- The class of a data asset ----

    private static string Class(Binding binding, DescribedType type)
    {
        var name = binding.CSharpName(type);
        var shapes = type.Fields.Select(field => (Field: field, Shape: FieldShape(binding, type, field))).ToList();
        var text = Header(binding, type, shapes.Select(field => field.Shape), type.Base);
        CSharpWriter.Doc(text, type.Doc);
        text.Line($"[{binding.DataAssetAttribute(type)}(\"{type.Name}\")]");
        var baseType = type.Base is { } baseName ? binding.CSharpName(binding.Type(baseName)) : binding.DataAssetInterface(type);
        text.Line($"public partial class {name} : {baseType}");
        text.Line("{");
        text.Indent();
        foreach (var (field, shape) in shapes)
        {
            CSharpWriter.Doc(text, field.Doc);
            var initialiser = Initialiser(binding, field, shape, $"{type.Source}: {type.Name}::{field.Name}");
            text.Line($"public {TypeOf(binding, shape)} {binding.MemberName(type, field.Name)} {{ get; set; }}" +
                      (initialiser == null ? "" : $" = {initialiser};"));
            text.Line();
        }

        text.Line($"/// <summary>The codecs that carry a <see cref=\"{name}\"/> to and from the store's tree, as C++'s bridge writes {type.Name}.</summary>");
        text.Line("[global::System.ComponentModel.EditorBrowsable(global::System.ComponentModel.EditorBrowsableState.Never)]");
        var baseLiteral = type.Base == null ? "null" : $"\"{type.Base}\"";
        text.Line($"public static {(type.Base == null ? "" : "new ")}DataAssetCodec<{name}> Codec {{ get; }} = new(\"{type.Name}\", {type.Version}, {baseLiteral},");
        text.Line($"    Entry, WriteFields, ReadFields, static () => new {name}(), null);");
        text.Line();
        Entry(binding, type, shapes, text);
        text.Line();
        text.Line($"private static void WriteFields(TreeNode tree, {name} value, {name}? defaults)");
        text.Line("{");
        text.Indent();
        if (type.Base is { } extended)
        {
            text.Line($"{binding.CSharpName(binding.Type(extended))}.Codec.WriteFields(tree, value, defaults);");
        }

        text.Line("bool full = defaults is null;");
        foreach (var (field, shape) in shapes)
        {
            WriteField(binding, text, field.Name, binding.MemberName(type, field.Name), shape, "defaults!");
        }

        text.Outdent();
        text.Line("}");
        text.Line();
        text.Line($"private static void ReadFields(TreeNode tree, {name} value)");
        text.Line("{");
        text.Indent();
        if (type.Base is { } read)
        {
            text.Line($"{binding.CSharpName(binding.Type(read))}.Codec.ReadFields(tree, value);");
        }

        foreach (var (field, shape) in shapes)
        {
            ReadField(binding, text, field, binding.MemberName(type, field.Name), shape);
        }

        text.Outdent();
        text.Line("}");
        text.Outdent();
        text.Line("}");
        return text.ToString();
    }

    private static DataShape FieldShape(Binding binding, DescribedType holder, Field field) =>
        Shape(binding, holder, field.Kind, $"{holder.Source}: {holder.Name}::{field.Name}");

    // The initialiser of a member that C#'s default does not hold: its recorded default.
    private static string? Initialiser(Binding binding, Field field, DataShape shape, string where) => shape switch
    {
        TextShape => CSharpLiteral.String(field.Default, where),
        ListShape => field.Default is { ValueKind: JsonValueKind.Array } list && list.GetArrayLength() == 0
                         ? "new()"
                         : throw new BindingException($"{where}: a list's default is []"),
        OptionalShape => field.Default.ValueKind == JsonValueKind.Null
                             ? null
                             : throw new BindingException($"{where}: an optional's default is null, its only recorded default"),
        // A reference's null default is read as its handle struct's own defaults ({}): the empty handle.
        ReferenceShape => null,
        _ => binding.IsZeroDefault(field) && shape is not StructShape
                 ? null
                 : CSharpWriter.Default(binding, field.Kind.Kind, field.Kind.Type, field.Default, where),
    };

    // The file's comment, usings and namespace: the configuration's, what lists need, and the namespaces of the used
    // bindings whose types it names.
    private static Writer Header(Binding binding, DescribedType type, IEnumerable<DataShape> shapes, string? baseName)
    {
        var text = new Writer();
        text.Line($"// Generated by Iris.Bindings from {type.Name} ({Path.GetFileName(type.Source)}): do not edit.");
        text.Line("// Change the C++ header, then regenerate its description and this binding.");
        text.Line();
        var held = shapes.ToList();
        var usings = new List<string>();
        if (held.Any(HoldsList))
        {
            usings.Add("System.Collections.Generic");
        }

        var named = held.SelectMany(Named).Append(baseName);
        usings.AddRange(binding.Config.CSharp.Usings.Concat(binding.UsedNamespaces(named))
                               .Where(space => space != binding.Config.Namespace)
                               .Distinct()
                               .Order(StringComparer.Ordinal));
        foreach (var space in usings)
        {
            text.Line($"using {space};");
        }

        text.Line();
        text.Line("#nullable enable");
        text.Line();
        text.Line($"namespace {binding.Config.Namespace};");
        text.Line();
        return text;
    }

    private static bool HoldsList(DataShape shape) => shape switch
    {
        ListShape => true,
        OptionalShape optional => HoldsList(optional.Held),
        _ => false,
    };

    // The C++ names of the types a shape names, for the usings: its enums, structs and handles.
    private static IEnumerable<string?> Named(DataShape shape) => shape switch
    {
        EnumShape enumShape => [enumShape.Type.Name],
        StructShape structShape => [structShape.Type.Name],
        ReferenceShape reference => [reference.Target, Description.HandleType(reference.Target)],
        ListShape list => Named(list.Element),
        OptionalShape optional => Named(optional.Held),
        _ => [],
    };

    // ---- The mirror of a nested struct only data assets reach ----

    private static string DataOnlyMirror(Binding binding, DescribedType type)
    {
        var name = binding.CSharpName(type);
        var shapes = type.Fields.Select(field => (Field: field, Shape: FieldShape(binding, type, field))).ToList();
        var text = Header(binding, type, shapes.Select(field => field.Shape), null);
        CSharpWriter.Doc(text, type.Doc);
        text.Line($"public partial struct {name}");
        text.Line("{");
        text.Indent();
        var initialised = binding.NeedsConstructor(type);
        foreach (var (field, shape) in shapes)
        {
            CSharpWriter.Doc(text, field.Doc);
            var initialiser = initialised ? Initialiser(binding, field, shape, $"{type.Source}: {type.Name}::{field.Name}") : null;
            text.Line($"public {TypeOf(binding, shape)} {binding.MemberName(type, field.Name)}" +
                      (initialiser == null ? ";" : $" = {initialiser};"));
        }

        if (initialised)
        {
            // Explicit, so `new T()` runs the initialisers.
            text.Line();
            text.Line($"public {name}()");
            text.Line("{");
            text.Line("}");
        }

        text.Outdent();
        text.Line("}");
        return text.ToString();
    }

    // ---- The registration class and the structs' codecs ----

    private static string Registration(Binding binding, IReadOnlyList<DescribedType> assets, IReadOnlyList<DescribedType> structs)
    {
        var text = new Writer();
        text.Line($"// Generated by Iris.Bindings from {Path.GetFileName(binding.Config.Path)}: do not edit.");
        text.Line("// Change the C++ headers, then regenerate their descriptions and this binding.");
        text.Line();
        var shapes = structs.SelectMany(type => type.Fields.Select(field => FieldShape(binding, type, field))).ToList();
        var usings = new List<string>();
        if (shapes.Any(HoldsList))
        {
            usings.Add("System.Collections.Generic");
        }

        usings.AddRange(binding.Config.CSharp.Usings
                               .Concat(binding.UsedNamespaces(shapes.SelectMany(Named).Concat(structs.Select(type => type.Name))))
                               .Where(space => space != binding.Config.Namespace)
                               .Distinct()
                               .Order(StringComparer.Ordinal));
        foreach (var space in usings)
        {
            text.Line($"using {space};");
        }

        text.Line();
        text.Line("#nullable enable");
        text.Line();
        text.Line($"namespace {binding.Config.Namespace};");
        text.Line();
        text.Line("/// <summary>");
        text.Line("/// Registers the codecs of the data asset classes this binding generates, and of the structs they hold, with");
        text.Line("/// DataAssetCodecs as the assembly loads, so a script finds them by type or C++ name before it asks for one.");
        text.Line("/// </summary>");
        text.Line("[global::System.ComponentModel.EditorBrowsable(global::System.ComponentModel.EditorBrowsableState.Never)]");
        text.Line($"internal static class {RegistrationClass}");
        text.Line("{");
        text.Indent();
        text.Line("// A contract registers its own classes' codecs as it loads: nothing else knows they exist.");
        text.Line("#pragma warning disable CA2255");
        text.Line("[global::System.Runtime.CompilerServices.ModuleInitializer]");
        text.Line("#pragma warning restore CA2255");
        text.Line("internal static void Register()");
        text.Line("{");
        text.Indent();
        foreach (var type in structs)
        {
            var mangled = Mangle(type.Name);
            text.Line($"DataAssetCodecs.RegisterValue(new ValueCodec<{binding.CSharpName(type)}>(\"{type.Name}\", {mangled}ToTree, {mangled}FromTree));");
        }

        foreach (var type in assets)
        {
            text.Line($"DataAssetCodecs.Register({binding.CSharpName(type)}.Codec);");
        }

        text.Outdent();
        text.Line("}");
        foreach (var type in structs)
        {
            text.Line();
            StructCodecs(binding, type, text);
        }

        text.Outdent();
        text.Line("}");
        return text.ToString();
    }

    private static void StructCodecs(Binding binding, DescribedType type, Writer text)
    {
        var name = binding.CSharpName(type);
        var mangled = Mangle(type.Name);
        var shapes = type.Fields.Select(field => (Field: field, Shape: FieldShape(binding, type, field))).ToList();
        text.Line($"/// <summary>{CSharpWriter.Escape(type.Name)}'s entry in a data asset's description: its name and fields.</summary>");
        text.Line($"internal static TreeNode {mangled}Entry()");
        text.Line("{");
        text.Indent();
        text.Line("var fields = TreeNode.NewArray();");
        foreach (var (field, shape) in shapes)
        {
            FieldEntry(binding, text, field, shape);
        }

        text.Line($"return TreeNode.NewObject().Add(\"name\", TreeNode.From(\"{type.Name}\")).Add(\"fields\", fields);");
        text.Outdent();
        text.Line("}");
        text.Line();
        text.Line($"/// <summary>The tree of a {CSharpWriter.Escape(type.Name)}: its fields that differ from the defaults, or every one when full.</summary>");
        text.Line($"internal static TreeNode {mangled}ToTree({name} value, {name} defaults, bool full)");
        text.Line("{");
        text.Indent();
        text.Line("var tree = TreeNode.NewObject();");
        foreach (var (field, shape) in shapes)
        {
            WriteField(binding, text, field.Name, binding.MemberName(type, field.Name), shape, "defaults");
        }

        text.Line("return tree;");
        text.Outdent();
        text.Line("}");
        text.Line();
        text.Line($"/// <summary>The {CSharpWriter.Escape(type.Name)} a tree holds, what it lacks or cannot be read kept from the fallback.</summary>");
        text.Line($"internal static {name} {mangled}FromTree(TreeNode tree, {name} fallback)");
        text.Line("{");
        text.Indent();
        text.Line("if (tree.Kind != TreeKind.Object)");
        text.Line("    return fallback;");
        text.Line("var value = fallback;");
        foreach (var (field, shape) in shapes)
        {
            ReadField(binding, text, field, binding.MemberName(type, field.Name), shape);
        }

        text.Line("return value;");
        text.Outdent();
        text.Line("}");
    }

    // A C++ name as a C# identifier part: `::` as `_`, a `_` doubled and anything else not a letter or digit as `_`, so no
    // two names share one.
    private static string Mangle(string name) =>
        string.Concat(name.Replace("_", "__").Replace("::", "_").Select(c => char.IsAsciiLetterOrDigit(c) || c == '_' ? c : '_'));

    // ---- The entry ----

    private static void Entry(Binding binding, DescribedType type, IReadOnlyList<(Field Field, DataShape Shape)> shapes, Writer text)
    {
        var name = binding.CSharpName(type);
        text.Line("private static TreeNode Entry()");
        text.Line("{");
        text.Indent();
        text.Line("var fields = TreeNode.NewArray();");
        foreach (var (field, shape) in shapes)
        {
            FieldEntry(binding, text, field, shape);
        }

        // Its own fields' structs and enums, each once: its base's are its base's entry.
        text.Line("var structs = TreeNode.NewArray();");
        foreach (var held in Structs(binding, [type]))
        {
            text.Line($"structs.Add({RegistrationClass}.{Mangle(held.Name)}Entry());");
        }

        text.Line("var enums = TreeNode.NewArray();");
        foreach (var held in Enums(binding, type))
        {
            var unsigned = held.Underlying!.StartsWith('u');
            text.Line($"enums.Add(TreeNode.NewObject().Add(\"name\", TreeNode.From(\"{held.Name}\")).Add(\"kind\", TreeNode.From(\"{held.Underlying}\"))");
            text.Line("    .Add(\"enumerators\", TreeNode.NewArray()" +
                      string.Concat(held.Values.Select(value =>
                          $".Add(TreeNode.NewObject().Add(\"name\", TreeNode.From(\"{value.Name}\")).Add(\"value\", " +
                          $"TreeNode.From({(unsigned ? $"{(ulong)value.Value}UL" : $"{value.Value}L")})))")) + "));");
        }

        text.Line("return TreeNode.NewObject()");
        text.Indent();
        text.Line($".Add(\"name\", TreeNode.From(\"{type.Name}\"))");
        text.Line($".Add(\"version\", TreeNode.From({type.Version}L))");
        text.Line($".Add(\"formerly\", TreeCodec.Strings({string.Join(", ", type.Formerly.Select(CSharpLiteral.Text))}))");
        text.Line($".Add(\"base\", {(type.Base == null ? "TreeNode.Null" : $"TreeNode.From(\"{type.Base}\")")})");
        text.Line($".Add(\"transient\", TreeNode.From({(type.Transient ? "true" : "false")}))");
        text.Line($".Add(\"editorOnly\", TreeNode.From({(type.EditorOnly ? "true" : "false")}))");
        text.Line(".Add(\"fields\", fields)");
        text.Line(".Add(\"structs\", structs)");
        text.Line(".Add(\"enums\", enums)");
        text.Line(".Add(\"upgraders\", TreeNode.NewArray())");
        text.Line($".Add(\"default\", Codec.ToTree(new {name}(), null));");
        text.Outdent();
        text.Outdent();
        text.Line("}");
    }

    // The enums a type's own fields reach, through its structs too, each once.
    private static IEnumerable<DescribedType> Enums(Binding binding, DescribedType type)
    {
        var seen = new HashSet<string>(StringComparer.Ordinal);
        var enums = new List<DescribedType>();
        foreach (var holder in Structs(binding, [type]).Prepend(type))
        {
            foreach (var field in holder.Fields)
            {
                Walk(FieldShape(binding, holder, field));
            }
        }

        return enums;

        void Walk(DataShape shape)
        {
            switch (shape)
            {
                case EnumShape held when seen.Add(held.Type.Name):
                    enums.Add(held.Type);
                    break;
                case ListShape list:
                    Walk(list.Element);
                    break;
                case OptionalShape optional:
                    Walk(optional.Held);
                    break;
            }
        }
    }

    private static void FieldEntry(Binding binding, Writer text, Field field, DataShape shape)
    {
        text.Line("{");
        text.Indent();
        text.Line($"var field = TreeNode.NewObject().Add(\"name\", TreeNode.From(\"{field.Name}\")).Add(\"type\", {TypeEntry(shape)});");
        if (field.Formerly.Count > 0)
        {
            text.Line($"field.Add(\"formerly\", TreeCodec.Strings({string.Join(", ", field.Formerly.Select(CSharpLiteral.Text))}));");
        }

        if (field.Metadata is { } metadata)
        {
            foreach (var entry in metadata.EnumerateObject())
            {
                text.Line(entry.Name switch
                {
                    "range" => $"field.Add(\"range\", TreeNode.NewArray().Add(TreeNode.From({Number(entry.Value.GetProperty("min"))})).Add(TreeNode.From({Number(entry.Value.GetProperty("max"))})));",
                    "step" => $"field.Add(\"step\", TreeNode.From({Number(entry.Value)}));",
                    "display" or "group" or "units" => $"field.Add(\"{entry.Name}\", TreeNode.From({CSharpLiteral.String(entry.Value, field.Name)}));",
                    "readonly" or "hidden" or "multiline" => $"field.Add(\"{entry.Name}\", TreeNode.From(true));",
                    var other => throw new BindingException($"{field.Name}: unknown metadata '{other}'"),
                });
            }
        }

        text.Line("fields.Add(field);");
        text.Outdent();
        text.Line("}");
    }

    // A metadata number as a double literal; describe-api records only finite numbers there.
    private static string Number(JsonElement value) =>
        value.ValueKind == JsonValueKind.Number && double.IsFinite(value.GetDouble())
            ? value.GetDouble().ToString("R", CultureInfo.InvariantCulture) + "d"
            : throw new BindingException($"a metadata number is a finite JSON number, not {value}");

    private static string TypeEntry(DataShape shape) => shape switch
    {
        PlainShape plain => $"TreeCodec.Type(\"{plain.Kind}\")",
        EnumShape enumShape => $"TreeCodec.Type(\"enum\", \"{enumShape.Type.Name}\")",
        TextShape text => $"TreeCodec.Type(\"{(text.Path ? "path" : "string")}\")",
        ReferenceShape reference => $"TreeCodec.Type(\"reference\", \"{reference.Target}\")",
        ListShape list => $"TreeCodec.Type(\"list\", null, {TypeEntry(list.Element)})",
        OptionalShape optional => $"TreeCodec.Type(\"optional\", null, {TypeEntry(optional.Held)})",
        StructShape structShape => $"TreeCodec.Type(\"struct\", \"{structShape.Type.Name}\")",
        _ => throw new InvalidOperationException("unknown shape"),
    };

    // ---- The codecs, field by field ----

    /// <summary>The C# type of a member of this shape.</summary>
    public static string TypeOf(Binding binding, DataShape shape) => shape switch
    {
        PlainShape plain => Binding.CSharpScalar(plain.Kind),
        EnumShape enumShape => binding.CSharpName(enumShape.Type),
        TextShape => "string",
        ListShape list => $"List<{TypeOf(binding, list.Element)}>",
        OptionalShape optional => TypeOf(binding, optional.Held) + "?",
        ReferenceShape reference => reference.CSharpType,
        StructShape structShape => binding.CSharpName(structShape.Type),
        _ => throw new InvalidOperationException("unknown shape"),
    };

    // The type a value of the shape is passed as in a codec: a text or list may be null there.
    private static string Passed(Binding binding, DataShape shape) =>
        shape is TextShape or ListShape ? TypeOf(binding, shape) + "?" : TypeOf(binding, shape);

    // Adds one field to `tree`: every field when `full`, else one whose tree differs from its default's, written against
    // that default. A disengaged optional is left out, but for null when its default holds a value, as C++ writes it.
    private static void WriteField(Binding binding, Writer text, string key, string member, DataShape shape, string defaults)
    {
        var value = $"value.{member}";
        var fallback = $"{defaults}.{member}";
        var inner = shape is OptionalShape optional ? optional.Held : shape;
        var type = Passed(binding, inner);
        var fresh = New(binding, inner);
        text.Line("{");
        text.Indent();
        text.Line($"static TreeNode Write({type} v, {type} d, bool f) => {Write(binding, inner, "v", "d", "f", 0)};");
        if (shape is OptionalShape)
        {
            text.Line($"if ({value} is {{ }} held)");
            text.Line("{");
            text.Line("    if (full)");
            text.Line($"        tree.Add(\"{key}\", Write(held, {fresh}, true));");
            text.Line($"    else if ({fallback} is not {{ }} heldDefault)");
            text.Line($"        tree.Add(\"{key}\", Write(held, {fresh}, false));");
            text.Line($"    else if (!{Same(inner, "held", "heldDefault", fresh)})");
            text.Line($"        tree.Add(\"{key}\", Write(held, heldDefault, false));");
            text.Line("}");
            text.Line($"else if (full || {fallback} is not null)");
            text.Line("{");
            text.Line($"    tree.Add(\"{key}\", TreeNode.Null);");
            text.Line("}");
        }
        else
        {
            text.Line("if (full)");
            text.Line($"    tree.Add(\"{key}\", Write({value}, {fresh}, true));");
            text.Line($"else if (!{Same(shape, value, fallback, fresh)})");
            text.Line($"    tree.Add(\"{key}\", Write({value}, {fallback}, false));");
        }

        text.Outdent();
        text.Line("}");
    }

    // Whether two values of a shape are the same, as the bridge compares them: a reference by its handle, anything else by
    // its whole tree, a float by its bits.
    private static string Same(DataShape shape, string a, string b, string fresh) => shape is ReferenceShape
        ? $"({a}.Index == {b}.Index && {a}.Generation == {b}.Generation)"
        : $"Write({a}, {fresh}, true).SameAs(Write({b}, {fresh}, true))";

    private static void ReadField(Binding binding, Writer text, Field field, string member, DataShape shape)
    {
        var value = $"value.{member}";
        var formerly = string.Concat(field.Formerly.Select(name => ", " + CSharpLiteral.Text(name)));
        text.Line("{");
        text.Indent();
        text.Line($"if (TreeCodec.Find(tree, \"{field.Name}\"{formerly}) is {{ }} node)");
        text.Line($"    {value} = {Read(binding, shape, "node", value, 0)};");
        text.Outdent();
        text.Line("}");
    }

    // The tree of `value`, of `shape`, written against `defaults` (an expression of its type) unless `full`.
    private static string Write(Binding binding, DataShape shape, string value, string defaults, string full, int depth)
    {
        switch (shape)
        {
            case PlainShape plain:
                return $"TreeNode.From(({Cast(plain.Kind)})({value}))";
            case TextShape:
                return $"TreeNode.From((string?)({value}))";
            case EnumShape:
                return $"TreeCodec.WriteEnum({value})";
            case ReferenceShape reference:
                return $"TreeCodec.WriteReference(({value}).Index, ({value}).Generation, \"{reference.Target}\")";
            case ListShape list:
                var element = $"e{depth}";
                var elementFull = $"f{depth}";
                return $"TreeCodec.WriteList({value}, {full}, static ({element}, {elementFull}) => " +
                       $"{Write(binding, list.Element, element, New(binding, list.Element), elementFull, depth + 1)})";
            case OptionalShape optional:
                var held = $"h{depth}";
                return $"(({value}) is {{ }} {held} ? {Write(binding, optional.Held, held, New(binding, optional.Held), full, depth + 1)} : TreeNode.Null)";
            case StructShape structShape:
                return $"{RegistrationClass}.{Mangle(structShape.Type.Name)}ToTree({value}, {defaults}, {full})";
            default:
                throw new InvalidOperationException("unknown shape");
        }
    }

    // The value of `shape` that `node` holds, or `fallback` when it holds none the shape can take.
    private static string Read(Binding binding, DataShape shape, string node, string fallback, int depth)
    {
        switch (shape)
        {
            case PlainShape plain:
                var type = Binding.CSharpScalar(plain.Kind);
                return plain.Kind switch
                {
                    "f32" => $"TreeCodec.ReadSingle({node}, {fallback})",
                    "f64" => $"TreeCodec.ReadDouble({node}, {fallback})",
                    "bool" => $"TreeCodec.ReadBool({node}, {fallback})",
                    "u8" or "u16" or "u32" or "u64" or "char16" => $"({type})TreeCodec.ReadUnsigned({node}, {type}.MaxValue, {fallback})",
                    _ => $"({type})TreeCodec.ReadSigned({node}, {type}.MinValue, {type}.MaxValue, {fallback})",
                };
            case TextShape:
                return $"TreeCodec.ReadString({node}, {fallback} ?? \"\")";
            case EnumShape:
                return $"TreeCodec.ReadEnum({node}, {fallback})";
            case ReferenceShape reference:
                return $"TreeCodec.ReadReference({node}, \"{reference.Target}\", {fallback}, static (i, g) => new {reference.CSharpType}(i, g))";
            case ListShape list:
                var element = $"e{depth}";
                var elementFallback = $"f{depth}";
                // A list read over a null fallback from a node that is no array stays null, as it was.
                return $"TreeCodec.ReadList({node}, {fallback}, {New(binding, list.Element)}, static ({element}, {elementFallback}) => " +
                       $"{Read(binding, list.Element, element, elementFallback, depth + 1)})!";
            case OptionalShape optional:
                var held = $"n{depth}";
                var heldFallback = $"f{depth}";
                return $"TreeCodec.ReadOptional({node}, {fallback}, {New(binding, optional.Held)}, static ({held}, {heldFallback}) => " +
                       $"{Read(binding, optional.Held, held, heldFallback, depth + 1)})";
            case StructShape structShape:
                return $"{RegistrationClass}.{Mangle(structShape.Type.Name)}FromTree({node}, {fallback})";
            default:
                throw new InvalidOperationException("unknown shape");
        }
    }

    // The C# type a plain kind is written through.
    private static string Cast(string kind) => kind switch
    {
        "f32" => "float",
        "f64" => "double",
        "bool" => "bool",
        "u8" or "u16" or "u32" or "u64" or "char16" => "ulong",
        _ => "long",
    };

    // A new value of the shape: what an element is compared with and read over, as C++ constructs one.
    private static string New(Binding binding, DataShape shape) => shape switch
    {
        TextShape => "\"\"",
        ListShape list => $"new List<{TypeOf(binding, list.Element)}>()",
        OptionalShape optional => $"({TypeOf(binding, optional.Held)}?)null",
        StructShape structShape => $"new {binding.CSharpName(structShape.Type)}()",
        _ => $"default({TypeOf(binding, shape)})",
    };
}

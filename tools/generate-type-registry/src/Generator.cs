using System.Globalization;
using System.Text;
using System.Text.Json;

namespace GenerateTypeRegistry;

/// <summary>The two files a package's registration is: its public header and its source.</summary>
internal sealed record GeneratedFiles(string Header, string Source);

/// <summary>
/// Writes a package's type registration (ADR-0074) from its API description: one <c>TypeDescription</c> per described
/// type as constant data, a fixed-layout type's fields with the offsets its description records (the table a
/// <c>TypeDescription::Visit</c> walks), an enum's enumerators, each type's <c>Describe&lt;T&gt;</c> and the function
/// registering them all. The text is the same bytes on every platform: LF line ends, and only ASCII, any other text
/// escaped.
/// </summary>
internal static class Generator
{
    private const string Tool = "tools/generate-type-registry";

    /// <summary>
    /// Generates the registration's header and source from the description's JSON text. Throws
    /// <see cref="GenerateException" /> naming the type (and field) when the description holds a category, role or kind
    /// the tool does not know, or a type it cannot generate yet; with <paramref name="skipUnsupported" />, a type of the
    /// last sort is left out, as is every type that holds one, instead.
    /// </summary>
    public static GeneratedFiles Generate(Registration registration, string descriptionJson, bool skipUnsupported)
    {
        using var document = ParseDescription(registration, descriptionJson);
        var root = document.RootElement;
        var package = root.TryGetProperty("package", out var packageValue) ? packageValue.GetString() : null;
        if (package != registration.Package)
        {
            throw new GenerateException(
                $"{registration.DescriptionPath}: describes package '{package}', not the selection's '{registration.Package}'");
        }

        var identifiers = new Identifiers();
        var types = Read(root.GetProperty("types"), skipUnsupported, identifiers);
        return new GeneratedFiles(WriteHeader(registration, types), WriteSource(registration, types, identifiers));
    }

    private static JsonDocument ParseDescription(Registration registration, string json)
    {
        try
        {
            return JsonDocument.Parse(json);
        }
        catch (JsonException exception)
        {
            throw new GenerateException($"{registration.DescriptionPath}: {exception.Message}");
        }
    }

    // One described type the registration describes: its C++ name, the identifier its data is named by in the source,
    // and its description.
    private sealed record Described(string Name, string Identifier, JsonElement Json);

    // The types to describe, in the description's order (dependencies first), each checked and given an identifier.
    private static List<Described> Read(JsonElement types, bool skipUnsupported, Identifiers identifiers)
    {
        var described = new List<Described>();
        var skipped = new HashSet<string>(StringComparer.Ordinal);
        foreach (var type in types.EnumerateArray())
        {
            var name = type.GetProperty("name").GetString()!;
            // Whatever the skip flag, so a skipped type holding something the tool does not know still fails it.
            CheckKnown(name, type);
            var unsupported = Unsupported(name, type) ?? HeldSkipped(name, type, skipped);
            if (unsupported is not null)
            {
                if (!skipUnsupported)
                {
                    throw new GenerateException(unsupported);
                }

                skipped.Add(name);
                continue;
            }

            CheckFixedLayout(name, type, described.Select(d => d.Name).ToHashSet(StringComparer.Ordinal));
            described.Add(new Described(name, identifiers.Unique(Identifier(name)), type));
        }

        return described;
    }

    // Why the tool cannot generate this type yet, naming it; null when it can.
    private static string? Unsupported(string name, JsonElement type)
    {
        var category = type.GetProperty("category").GetString();
        if (category == "tagged-union")
        {
            return $"{name}: a tagged union, which is not yet described by FenrirTypes";
        }

        var role = type.TryGetProperty("role", out var roleValue) ? roleValue.GetString() : null;
        if (role == "data-asset")
        {
            return $"{name}: a data asset is visited by member, by a generated function ticket 06 writes";
        }

        if (role == "settings")
        {
            return $"{name}: a settings type is visited by member, by a generated function ticket 06 writes";
        }

        if (category == "struct" && !type.TryGetProperty("size", out _))
        {
            return $"{name}: a struct holding an owning kind is visited by member, by a generated function ticket 06 writes";
        }

        return null;
    }

    // Why a type holding a type left out is left out too, naming the field; null when it holds none.
    private static string? HeldSkipped(string name, JsonElement type, HashSet<string> skipped)
    {
        foreach (var field in Fields(type))
        {
            foreach (var held in HeldTypes(field).Where(skipped.Contains))
            {
                return $"{name}::{field.GetProperty("name").GetString()}: holds {held}, which is left out";
            }
        }

        return null;
    }

    // The described types a field's value or its elements are, by name.
    private static IEnumerable<string> HeldTypes(JsonElement field)
    {
        foreach (var key in new[] { "type", "elementType" })
        {
            if (field.TryGetProperty(key, out var held))
            {
                yield return held.GetString()!;
            }
        }
    }

    private static IEnumerable<JsonElement> Fields(JsonElement type)
    {
        foreach (var key in new[] { "fields", "components" })
        {
            if (type.TryGetProperty(key, out var fields))
            {
                foreach (var field in fields.EnumerateArray())
                {
                    yield return field;
                }
            }
        }
    }

    // Fails naming the type, or the type and field, when the description holds a category, role or kind the tool does
    // not know.
    private static void CheckKnown(string name, JsonElement type)
    {
        var category = type.GetProperty("category").GetString();
        if (category is not ("struct" or "bound-type" or "enum" or "tagged-union"))
        {
            throw new GenerateException($"{name}: its category '{category}' is not one generate-type-registry knows");
        }

        if (category == "enum")
        {
            var underlying = type.GetProperty("underlying").GetString()!;
            if (!IsInteger(underlying))
            {
                throw new GenerateException($"{name}: its underlying kind '{underlying}' is not an integer kind");
            }

            return;
        }

        var role = type.TryGetProperty("role", out var roleValue) ? roleValue.GetString() : null;
        if (role is null || RoleName(role) is null)
        {
            throw new GenerateException($"{name}: its role '{role}' is not one generate-type-registry knows");
        }

        foreach (var field in Fields(type))
        {
            var fieldName = $"{name}::{field.GetProperty("name").GetString()}";
            foreach (var (key, what) in new[] { ("kind", "kind"), ("elementKind", "element kind") })
            {
                if (field.TryGetProperty(key, out var kind) && KindName(kind.GetString()!) is null)
                {
                    throw new GenerateException(
                        $"{fieldName}: its {what} '{kind.GetString()}' is not one generate-type-registry knows");
                }
            }
        }
    }

    // Fails naming the type and field when a type the tool writes as a table of offsets holds what a table cannot
    // reach, or a type the description has not described before it.
    private static void CheckFixedLayout(string name, JsonElement type, HashSet<string> earlier)
    {
        foreach (var field in Fields(type))
        {
            var fieldName = $"{name}::{field.GetProperty("name").GetString()}";
            var kind = field.GetProperty("kind").GetString()!;
            CheckOwning(fieldName, kind);
            if (kind == "array")
            {
                var element = field.GetProperty("elementKind").GetString()!;
                CheckOwning(fieldName, element);
                if (element is "array" or "fixed-string")
                {
                    throw new GenerateException($"{fieldName}: an array of {element}s, which the description never records");
                }
            }

            foreach (var held in HeldTypes(field).Where(held => !earlier.Contains(held)))
            {
                throw new GenerateException($"{fieldName}: holds {held}, which the description does not describe before it");
            }
        }
    }

    private static void CheckOwning(string fieldName, string kind)
    {
        if (kind is "string" or "list" or "optional" or "path")
        {
            throw new GenerateException(
                $"{fieldName}: a{(kind == "optional" ? "n" : "")} {kind} in a fixed-layout type, which is visited by " +
                "offset (ticket 06 generates visitors for the owning kinds)");
        }
    }

    private static string WriteHeader(Registration registration, List<Described> types)
    {
        var text = new Text();
        text.Line($"// Generated by {Tool} from {registration.DescriptionName}. Do not edit.");
        text.Line("// clang-format off");
        text.Line($"// The type registration of {registration.Package} (ADR-0074): each described type's Describe<T> and");
        text.Line("// the function adding every description to a type registry.");
        text.Line();
        text.Line("#pragma once");
        text.Line();
        foreach (var include in registration.Includes)
        {
            text.Line($"#include \"{include}\"");
        }

        if (registration.Includes.Count > 0)
        {
            text.Line();
        }

        text.Line("#include \"FenrirTypes/Describe.hpp\"");
        text.Line("#include \"FenrirTypes/TypeDescription.hpp\"");
        text.Line();
        text.Line("namespace Fenrir");
        text.Line("{");
        text.Line("    class TypeRegistry;");
        text.Line("} // namespace Fenrir");
        text.Line();

        var indent = registration.FunctionNamespace.Length > 0 ? "    " : "";
        if (indent.Length > 0)
        {
            text.Line($"namespace {registration.FunctionNamespace}");
            text.Line("{");
        }

        text.Line($"{indent}/**");
        text.Line($"{indent} * @brief Adds every type {registration.Package} describes to `registry`, dependencies first.");
        text.Line($"{indent} *");
        text.Line($"{indent} * @return Whether every one was added; the registry reports each it refused.");
        text.Line($"{indent} */");
        text.Line($"{indent}bool {registration.FunctionName}(Fenrir::TypeRegistry& registry);");
        if (indent.Length > 0)
        {
            text.Line($"}} // namespace {registration.FunctionNamespace}");
        }

        foreach (var type in types)
        {
            text.Line();
            text.Line($"/** @brief The description of {type.Name} that {registration.FunctionName} adds. */");
            text.Line("template <>");
            text.Line($"struct Fenrir::Describe<{type.Name}>");
            text.Line("{");
            text.Line("    static const Fenrir::TypeDescription& Description() noexcept;");
            text.Line("};");
        }

        text.Line();
        text.Line("// clang-format on");
        return text.ToString();
    }

    private static string WriteSource(Registration registration, List<Described> types, Identifiers identifiers)
    {
        var text = new Text();
        text.Line($"// Generated by {Tool} from {registration.DescriptionName}. Do not edit.");
        text.Line("// clang-format off");
        text.Line();
        var header = Path.GetRelativePath(Path.GetDirectoryName(registration.SourcePath)!, registration.HeaderPath)
                         .Replace('\\', '/');
        text.Line($"#include \"{header}\"");
        text.Line();
        text.Line("#include \"FenrirTypes/FieldInfo.hpp\"");
        text.Line("#include \"FenrirTypes/Kind.hpp\"");
        text.Line("#include \"FenrirTypes/TypeDescription.hpp\"");
        text.Line("#include \"FenrirTypes/TypeRegistry.hpp\"");
        text.Line();
        text.Line("#include <cstdint>");
        text.Line("#include <string_view>");
        text.Line();
        text.Line("namespace");
        text.Line("{");
        var typeIds = types.ToDictionary(type => type.Name, type => type.Identifier, StringComparer.Ordinal);
        var first = true;
        foreach (var type in types)
        {
            if (!first)
            {
                text.Line();
            }

            first = false;
            WriteDescription(text, registration, type, typeIds, identifiers);
        }

        text.Line("} // namespace");

        foreach (var type in types)
        {
            text.Line();
            text.Line($"const Fenrir::TypeDescription& Fenrir::Describe<{type.Name}>::Description() noexcept");
            text.Line("{");
            text.Line($"    return {type.Identifier};");
            text.Line("}");
        }

        var function = registration.FunctionNamespace.Length > 0
                           ? $"{registration.FunctionNamespace}::{registration.FunctionName}"
                           : registration.FunctionName;
        text.Line();
        text.Line($"bool {function}(Fenrir::TypeRegistry& registry)");
        text.Line("{");
        if (types.Count == 0)
        {
            text.Line("    static_cast<void>(registry);");
            text.Line("    return true;");
        }
        else
        {
            text.Line("    // As the description lists them: a type after every type its fields name.");
            text.Line("    constexpr const Fenrir::TypeDescription* descriptions[] = {");
            foreach (var type in types)
            {
                text.Line($"        &{type.Identifier},");
            }

            text.Line("    };");
            text.Line("    bool registered = true;");
            text.Line("    for (const Fenrir::TypeDescription* description : descriptions)");
            text.Line("        registered = registry.Register(*description) && registered;");
            text.Line("    return registered;");
        }

        text.Line("}");
        text.Line();
        text.Line("// clang-format on");
        return text.ToString();
    }

    private static void WriteDescription(Text text, Registration registration, Described type,
                                         Dictionary<string, string> typeIds, Identifiers identifiers)
    {
        var json = type.Json;
        var name = type.Name;
        var id = type.Identifier;
        var isEnum = json.GetProperty("category").GetString() == "enum";
        var size = json.GetProperty("size").GetInt64();
        var alignment = json.GetProperty("alignment").GetInt64();

        text.Line($"    // {name}");
        text.Line($"    static_assert(sizeof({name}) == {size} && alignof({name}) == {alignment},");
        text.Line($"                  {Literal($"{name} is not the layout {registration.DescriptionName} describes: regenerate the description, then this registration")});");

        string? formerly = null;
        if (json.TryGetProperty("formerly", out var formerlyNames))
        {
            formerly = identifiers.Unique($"{id}_Formerly");
            text.Line($"    constexpr std::string_view {formerly}[] = {{{Strings(formerlyNames)}}};");
        }

        string? fields = null;
        string? enumerators = null;
        if (isEnum)
        {
            if (json.GetProperty("values").GetArrayLength() > 0)
            {
                enumerators = identifiers.Unique($"{id}_Enumerators");
                text.Line($"    constexpr Fenrir::Enumerator {enumerators}[] = {{");
                foreach (var value in json.GetProperty("values").EnumerateArray())
                {
                    var doc = value.TryGetProperty("doc", out var docValue) ? $", .doc = {Literal(docValue.GetString()!)}" : "";
                    text.Line($"        {{.name = {Literal(value.GetProperty("name").GetString()!)}, " +
                              $".value = {Integer(value.GetProperty("value"))}{doc}}},");
                }

                text.Line("    };");
            }
        }
        else if (Fields(json).Any())
        {
            var entries = new List<string>();
            foreach (var field in Fields(json))
            {
                entries.Add(FieldEntry(text, id, field, typeIds, identifiers));
            }

            fields = identifiers.Unique($"{id}_Fields");
            text.Line($"    constexpr Fenrir::FieldInfo {fields}[] = {{");
            foreach (var entry in entries)
            {
                text.Line($"        {entry},");
            }

            text.Line("    };");
        }

        var role = isEnum ? "Enum" : RoleName(json.GetProperty("role").GetString()!)!;
        text.Line($"    constexpr Fenrir::TypeDescription {id}{{");
        text.Line($"        .name = {Literal(name)},");
        text.Line($"        .package = {Literal(registration.Package)},");
        text.Line($"        .role = Fenrir::Role::{role},");
        if (json.TryGetProperty("version", out var version))
        {
            text.Line($"        .version = {version.GetUInt32()},");
        }

        if (formerly is not null)
        {
            text.Line($"        .formerly = {formerly},");
        }

        if (json.TryGetProperty("transient", out var transient) && transient.GetBoolean())
        {
            text.Line("        .transient = true,");
        }

        if (json.TryGetProperty("editorOnly", out var editorOnly) && editorOnly.GetBoolean())
        {
            text.Line("        .editorOnly = true,");
        }

        text.Line($"        .size = sizeof({name}),");
        text.Line($"        .alignment = alignof({name}),");
        text.Line($"        .construct = &Fenrir::ConstructValue<{name}>,");
        text.Line($"        .destroy = &Fenrir::DestroyValue<{name}>,");
        if (json.TryGetProperty("doc", out var typeDoc))
        {
            text.Line($"        .doc = {Literal(typeDoc.GetString()!)},");
        }

        if (fields is not null)
        {
            text.Line($"        .fields = {fields},");
        }

        if (enumerators is not null)
        {
            text.Line($"        .enumerators = {enumerators},");
        }

        if (isEnum)
        {
            text.Line($"        .underlying = Fenrir::Kind::{KindName(json.GetProperty("underlying").GetString()!)},");
        }

        text.Line("    };");
    }

    // One FieldInfo initialiser; writes the arrays it points at (an array's element type, former names) first.
    private static string FieldEntry(Text text, string id, JsonElement field, Dictionary<string, string> typeIds,
                                     Identifiers identifiers)
    {
        var name = field.GetProperty("name").GetString()!;
        var fieldId = $"{id}_{Identifier(name)}";
        var kind = field.GetProperty("kind").GetString()!;

        var valueType = new List<string> { $".kind = Fenrir::Kind::{KindName(kind)}" };
        switch (kind)
        {
            case "enum" or "struct":
                valueType.Add($".type = &{typeIds[field.GetProperty("type").GetString()!]}");
                break;
            case "array":
                var element = new List<string> { $".kind = Fenrir::Kind::{KindName(field.GetProperty("elementKind").GetString()!)}" };
                if (field.TryGetProperty("elementType", out var elementType))
                {
                    element.Add($".type = &{typeIds[elementType.GetString()!]}");
                }

                if (field.TryGetProperty("elementTarget", out var elementTarget))
                {
                    element.Add($".target = {Literal(elementTarget.GetString()!)}");
                }

                var elementId = identifiers.Unique($"{fieldId}_Element");
                text.Line($"    constexpr Fenrir::ValueType {elementId}{{{string.Join(", ", element)}}};");
                valueType.Add($".element = &{elementId}");
                valueType.Add($".count = {field.GetProperty("length").GetInt64()}");
                break;
            case "fixed-string":
                valueType.Add($".count = {field.GetProperty("length").GetInt64()}");
                break;
            case "reference":
                valueType.Add($".target = {Literal(field.GetProperty("target").GetString()!)}");
                break;
        }

        var entry = new List<string>
        {
            $".name = {Literal(name)}",
            $".type = {{{string.Join(", ", valueType)}}}",
            $".offset = {field.GetProperty("offset").GetInt64()}",
        };

        if (field.TryGetProperty("formerly", out var formerly))
        {
            var formerlyId = identifiers.Unique($"{fieldId}_Formerly");
            text.Line($"    constexpr std::string_view {formerlyId}[] = {{{Strings(formerly)}}};");
            entry.Add($".formerly = {formerlyId}");
        }

        if (field.TryGetProperty("doc", out var doc))
        {
            entry.Add($".doc = {Literal(doc.GetString()!)}");
        }

        if (field.TryGetProperty("metadata", out var metadata))
        {
            entry.Add($".metadata = {{{Metadata(metadata)}}}");
        }

        return "{" + string.Join(", ", entry) + "}";
    }

    // A FieldMetadata's designated initialisers, in its members' order.
    private static string Metadata(JsonElement metadata)
    {
        var members = new List<string>();
        if (metadata.TryGetProperty("range", out var range))
        {
            members.Add($".range = Fenrir::FieldRange{{.min = {Number(range.GetProperty("min"))}, " +
                        $".max = {Number(range.GetProperty("max"))}}}");
        }

        if (metadata.TryGetProperty("step", out var step))
        {
            members.Add($".step = {Number(step)}");
        }

        foreach (var key in new[] { "display", "group", "units" })
        {
            if (metadata.TryGetProperty(key, out var value))
            {
                members.Add($".{key} = {Literal(value.GetString()!)}");
            }
        }

        foreach (var key in new[] { "readonly", "hidden", "multiline" })
        {
            if (metadata.TryGetProperty(key, out var value) && value.GetBoolean())
            {
                members.Add($".{key} = true");
            }
        }

        return string.Join(", ", members);
    }

    private static string Strings(JsonElement names) =>
        string.Join(", ", names.EnumerateArray().Select(name => Literal(name.GetString()!)));

    // A JSON number as a C++ double literal: the description's own digits, which are the shortest that read back.
    private static string Number(JsonElement number)
    {
        var raw = number.GetRawText();
        return raw.IndexOfAny(['.', 'e', 'E']) >= 0 ? raw : raw + ".0";
    }

    // A JSON integer as a C++ std::int64_t expression, an unsigned one above its range wrapping as the cast does.
    private static string Integer(JsonElement number)
    {
        if (number.TryGetInt64(out var value))
        {
            return value == long.MinValue ? "(-9223372036854775807 - 1)" : value.ToString(CultureInfo.InvariantCulture);
        }

        return $"static_cast<std::int64_t>({number.GetUInt64().ToString(CultureInfo.InvariantCulture)}ull)";
    }

    // A C++ narrow string literal of the UTF-8 text: anything outside printable ASCII as an octal escape, so the bytes
    // are the same whatever the compiler's source character set.
    private static string Literal(string value)
    {
        var literal = new StringBuilder("\"");
        foreach (var b in Encoding.UTF8.GetBytes(value))
        {
            switch (b)
            {
                case (byte)'"':
                    literal.Append("\\\"");
                    break;
                case (byte)'\\':
                    literal.Append("\\\\");
                    break;
                case (byte)'\n':
                    literal.Append("\\n");
                    break;
                case >= 0x20 and < 0x7F:
                    literal.Append((char)b);
                    break;
                default:
                    literal.Append('\\').Append(Convert.ToString(b, 8).PadLeft(3, '0'));
                    break;
            }
        }

        return literal.Append('"').ToString();
    }

    // A C++ identifier from a qualified name or a component path: each run of other characters becomes one '_'.
    private static string Identifier(string name)
    {
        var identifier = new StringBuilder();
        foreach (var c in name)
        {
            if (char.IsAsciiLetterOrDigit(c))
            {
                identifier.Append(c);
            }
            else if (identifier.Length > 0 && identifier[^1] != '_')
            {
                identifier.Append('_');
            }
        }

        return identifier.ToString().TrimEnd('_');
    }

    private static bool IsInteger(string kind) => kind is "i8" or "i16" or "i32" or "i64" or "u8" or "u16" or "u32" or "u64";

    // The Fenrir::Kind enumerator of a description's kind; null for one the tool does not know.
    private static string? KindName(string kind) => kind switch
    {
        "i8" => "I8",
        "i16" => "I16",
        "i32" => "I32",
        "i64" => "I64",
        "u8" => "U8",
        "u16" => "U16",
        "u32" => "U32",
        "u64" => "U64",
        "f32" => "F32",
        "f64" => "F64",
        "bool" => "Bool",
        "char16" => "Char16",
        "enum" => "Enum",
        "struct" => "Struct",
        "array" => "Array",
        "pointer" => "Pointer",
        "string" => "String",
        "list" => "List",
        "optional" => "Optional",
        "reference" => "Reference",
        "entity" => "Entity",
        "fixed-string" => "FixedString",
        "path" => "Path",
        _ => null,
    };

    // The Fenrir::Role enumerator of a description's role; null for one the tool does not know.
    private static string? RoleName(string role) => role switch
    {
        "component" => "Component",
        "event" => "Event",
        "scene-resource" => "SceneResource",
        "app-resource" => "AppResource",
        "shared-struct" => "SharedStruct",
        "bound-type" => "BoundType",
        "nested" => "Nested",
        "data-asset" => "DataAsset",
        "settings" => "Settings",
        _ => null,
    };

    // The names the source gives its data, each unique: a name wanted twice gets the next free numbered suffix, so a
    // type Foo's field Bar and a type Foo::Bar never name one array.
    private sealed class Identifiers
    {
        private readonly HashSet<string> m_taken = new(StringComparer.Ordinal);

        public string Unique(string wanted)
        {
            var identifier = wanted;
            for (var suffix = 2; !m_taken.Add(identifier); ++suffix)
            {
                identifier = $"{wanted}_{suffix}";
            }

            return identifier;
        }
    }

    // Lines joined with LF, whatever the platform's newline.
    private sealed class Text
    {
        private readonly StringBuilder m_text = new();

        public void Line(string line = "") => m_text.Append(line).Append('\n');

        public override string ToString() => m_text.ToString();
    }
}

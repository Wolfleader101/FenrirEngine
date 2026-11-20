using System.Globalization;
using System.Text;
using System.Text.Json;

namespace GenerateTypeRegistry;

/// <summary>
/// The files a package's registration is: its public header and its source, and its defaults test when its
/// <c>[registration]</c> names one.
/// </summary>
internal sealed record GeneratedFiles(string Header, string Source, string? Test = null);

/// <summary>
/// Writes a package's type registration (ADR-0074) from its API description: one <c>TypeDescription</c> per described
/// type as constant data, a fixed-layout type's fields with the offsets its description records (the table a
/// <c>TypeDescription::Visit</c> walks), an enum's enumerators, a tagged union's tag and alternatives, each type's
/// <c>Describe&lt;T&gt;</c> and the function registering them all. The text is the same bytes on every platform: LF line
/// ends, and only ASCII, any other text escaped.
/// </summary>
internal static class Generator
{
    private const string Tool = "tools/generate-type-registry";

    /// <summary>
    /// Generates the registration's header and source from the description's JSON text. Throws
    /// <see cref="GenerateException" /> naming the type (and field) when the description holds a category, role or kind
    /// the tool does not know, or a type it cannot write.
    /// </summary>
    public static GeneratedFiles Generate(Registration registration, string descriptionJson)
    {
        using var document = ParseDescription(registration, descriptionJson);
        var root = document.RootElement;
        var package = root.TryGetProperty("package", out var packageValue) ? packageValue.GetString() : null;
        if (package != registration.Package)
        {
            throw new GenerateException(
                $"{registration.DescriptionPath}: describes package '{package}', not the selection's '{registration.Package}'");
        }

        var used = new Dictionary<string, UsedType>(StringComparer.Ordinal);
        var uses = new List<Registration>();
        foreach (var selection in registration.Uses)
        {
            var (use, registers) = ReadUsed(selection, [registration.SelectionPath]);
            uses.Add(use);
            foreach (var (name, type) in registers)
            {
                if (used.TryGetValue(name, out var earlier) && earlier.Reference != type.Reference)
                {
                    throw new GenerateException(
                        $"{registration.SelectionPath}: two registrations it uses both describe {name} " +
                        $"({earlier.Reference} and {type.Reference}); one of them should use the other");
                }

                used.TryAdd(name, type);
            }
        }

        var identifiers = new Identifiers();
        var types = Read(root.GetProperty("types"), identifiers, used);
        return new GeneratedFiles(WriteHeader(registration, types, uses),
                                  WriteSource(registration, types, identifiers, used, uses),
                                  registration.TestPath is null ? null : WriteTest(registration, types, used));
    }

    // A type another package's registration describes and this one uses: the qualified name of its description object,
    // which that package's generated header declares, and its description.
    private sealed record UsedType(string Reference, JsonElement Json);

    // Reads a used package's registration and description, and through its own uses theirs: returns the registration
    // and every type its registration function registers (its own and, through the registrations it calls first, its
    // uses'), each under the name of its description object. `usingSelections` holds the selections on the way here, so
    // a cycle of uses fails rather than recursing.
    private static (Registration Registration, Dictionary<string, UsedType> Registers) ReadUsed(
        string selection, List<string> usingSelections)
    {
        if (usingSelections.Contains(selection, StringComparer.Ordinal))
        {
            throw new GenerateException(
                $"{usingSelections[^1]}: uses {selection}, which uses it in turn ({string.Join(" -> ", usingSelections)})");
        }

        if (!File.Exists(selection))
        {
            throw new GenerateException($"{usingSelections[^1]}: uses {selection}, which does not exist");
        }

        var registration = Registration.Read(selection) ??
                           throw new GenerateException(
                               $"{usingSelections[^1]}: uses {selection}, which has no [registration] to use");
        if (!File.Exists(registration.DescriptionPath))
        {
            throw new GenerateException($"{registration.DescriptionPath}: no description: run fenrir-describe-api first");
        }

        var registers = new Dictionary<string, UsedType>(StringComparer.Ordinal);
        foreach (var deeper in registration.Uses)
        {
            foreach (var (name, type) in ReadUsed(deeper, [.. usingSelections, selection]).Registers)
            {
                registers.TryAdd(name, type);
            }
        }

        // Its header, which this one's includes, is spelled from below an include directory.
        _ = registration.HeaderInclude;

        // Its own types named as its own generation names them: by its types alone, in its description's order, skipping
        // those its uses register.
        using var document = ParseDescription(registration, File.ReadAllText(registration.DescriptionPath));
        var identifiers = new Identifiers();
        foreach (var type in document.RootElement.GetProperty("types").EnumerateArray())
        {
            var name = type.GetProperty("name").GetString()!;
            if (!registers.ContainsKey(name))
            {
                registers.Add(name, new UsedType(
                                        $"{registration.DescriptionNamespace}::{identifiers.Unique(Identifier(name))}",
                                        type.Clone()));
            }
        }

        return (registration, registers);
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
    // A type a used registration describes is that package's: skipped here, and described before every type here.
    private static List<Described> Read(JsonElement types, Identifiers identifiers, Dictionary<string, UsedType> used)
    {
        var described = new List<Described>();
        var earlier = used.ToDictionary(type => type.Key, type => type.Value.Json, StringComparer.Ordinal);
        foreach (var type in types.EnumerateArray())
        {
            var name = type.GetProperty("name").GetString()!;
            if (used.ContainsKey(name))
            {
                continue;
            }

            CheckKnown(name, type);
            CheckFields(name, type, earlier.Keys.ToHashSet(StringComparer.Ordinal));
            if (IsTaggedUnion(type))
            {
                CheckTaggedUnion(name, type, earlier);
            }

            described.Add(new Described(name, identifiers.Unique(Identifier(name)), type));
            earlier.Add(name, type);
        }

        return described;
    }

    private static bool IsTaggedUnion(JsonElement type) => type.GetProperty("category").GetString() == "tagged-union";

    // Whether the type is reached by member through a generated visit function rather than by offset: a struct whose
    // description records no layout (a data asset, a settings type, a struct holding an owning kind).
    private static bool ByMember(JsonElement type) =>
        type.GetProperty("category").GetString() == "struct" && !type.TryGetProperty("size", out _);

    // The data asset the type extends; null when it extends none.
    private static string? Base(JsonElement type) =>
        type.TryGetProperty("base", out var baseName) ? baseName.GetString() : null;

    // The described types a field's value or its elements are, by name, through elements recorded whole too.
    private static IEnumerable<string> HeldTypes(JsonElement field)
    {
        foreach (var key in new[] { "type", "elementType" })
        {
            if (field.TryGetProperty(key, out var held))
            {
                yield return held.GetString()!;
            }
        }

        if (field.TryGetProperty("element", out var element))
        {
            foreach (var held in HeldTypes(element))
            {
                yield return held;
            }
        }
    }

    // The FenrirTypes hook the registration hands a type of this role to once it registers, which hangs the role's
    // attachments (ADR-0074); null for a role with none.
    private static string? Hook(JsonElement type) =>
        Role(type) switch
        {
            "component" => "AttachComponent",
            "scene-resource" => "AttachSceneResource",
            "app-resource" => "AttachAppResource",
            _ => null,
        };

    // The type's role; null for an enum, which has none.
    private static string? Role(JsonElement type) =>
        type.TryGetProperty("role", out var role) ? role.GetString() : null;

    // "a string", "an optional": the kind with its article, as a diagnostic names it.
    private static string WithArticle(string kind) => $"{(kind is "optional" or "array" or "enum" or "entity" ? "an" : "a")} {kind}";

    // A name in macro case: AttachComponent is ATTACH_COMPONENT.
    private static string MacroCase(string name)
    {
        var macro = new StringBuilder();
        foreach (var c in name)
        {
            if (char.IsAsciiLetterUpper(c) && macro.Length > 0)
            {
                macro.Append('_');
            }

            macro.Append(char.ToUpperInvariant(c));
        }

        return macro.ToString();
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

    // Every member a type holds, each checked as a field is: its fields, and a tagged union's tag and alternatives.
    private static IEnumerable<JsonElement> Members(JsonElement type)
    {
        foreach (var field in Fields(type))
        {
            yield return field;
        }

        if (!IsTaggedUnion(type))
        {
            yield break;
        }

        yield return type.GetProperty("tag");
        foreach (var alternative in type.GetProperty("kinds").EnumerateArray())
        {
            yield return alternative;
        }
    }

    // Fails naming the tag, an alternative or the type when the tag is not an enum, an alternative's tag is none of the
    // enum's enumerators, or the default kind is none of the alternatives. The tag's enum is described before it, as
    // CheckFields has checked.
    private static void CheckTaggedUnion(string name, JsonElement type, Dictionary<string, JsonElement> earlier)
    {
        var tag = type.GetProperty("tag");
        var tagKind = tag.GetProperty("kind").GetString()!;
        if (tagKind != "enum")
        {
            throw new GenerateException(
                $"{name}::{tag.GetProperty("name").GetString()}: a tagged union's tag is an enum, not {WithArticle(tagKind)}");
        }

        var tagEnum = earlier[tag.GetProperty("type").GetString()!];
        foreach (var alternative in type.GetProperty("kinds").EnumerateArray())
        {
            TagValue(name, alternative, tagEnum);
        }

        DefaultAlternative(name, type);
    }

    // The value of the enumerator an alternative's tag names, as the tag's enum describes it.
    private static JsonElement TagValue(string name, JsonElement alternative, JsonElement tagEnum)
    {
        var enumerator = alternative.GetProperty("tag").GetString()!;
        foreach (var value in tagEnum.GetProperty("values").EnumerateArray())
        {
            if (value.GetProperty("name").GetString() == enumerator)
            {
                return value.GetProperty("value");
            }
        }

        throw new GenerateException($"{name}::{alternative.GetProperty("name").GetString()}: its tag '{enumerator}' is not " +
                                    $"an enumerator of {tagEnum.GetProperty("name").GetString()}");
    }

    // The index among a tagged union's alternatives of the one its default constructor holds.
    private static int DefaultAlternative(string name, JsonElement type)
    {
        var defaultKind = type.GetProperty("defaultKind").GetString()!;
        var index = 0;
        foreach (var alternative in type.GetProperty("kinds").EnumerateArray())
        {
            if (alternative.GetProperty("name").GetString() == defaultKind)
            {
                return index;
            }

            ++index;
        }

        throw new GenerateException($"{name}: its default kind '{defaultKind}' is none of its alternatives");
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

        var role = Role(type);
        if (role is null || RoleName(role) is null)
        {
            throw new GenerateException($"{name}: its role '{role}' is not one generate-type-registry knows");
        }

        foreach (var field in Members(type))
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

    // Fails naming the type, and the field, when a type the tool writes as a table of offsets holds what a table cannot
    // reach, or the type holds or extends a type the description has not described before it.
    private static void CheckFields(string name, JsonElement type, HashSet<string> earlier)
    {
        var byMember = ByMember(type);
        if (Base(type) is { } baseName && !earlier.Contains(baseName))
        {
            throw new GenerateException($"{name}: extends {baseName}, which the description does not describe before it");
        }

        foreach (var field in Members(type))
        {
            var fieldName = $"{name}::{field.GetProperty("name").GetString()}";
            var kind = field.GetProperty("kind").GetString()!;
            if (!byMember)
            {
                CheckOwning(fieldName, kind);
            }

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
                $"{fieldName}: {WithArticle(kind)} in a type the description lays out, which is " +
                "visited by offset; only a type visited by member, which records no size, holds the owning kinds");
        }
    }

    private static string WriteHeader(Registration registration, List<Described> types, List<Registration> uses)
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

        // The registrations this one uses: the Describe<T> and description objects of the types they register.
        foreach (var use in uses)
        {
            text.Line($"#include \"{use.HeaderInclude}\"");
        }

        if (uses.Count > 0)
        {
            text.Line();
        }

        var hooks = types.Select(type => Hook(type.Json)).OfType<string>().Distinct().Order(StringComparer.Ordinal).ToList();
        text.Line("#include \"FenrirTypes/Describe.hpp\"");
        if (hooks.Count > 0)
        {
            text.Line("#include \"FenrirTypes/RoleHooks.hpp\"");
        }

        text.Line("#include \"FenrirTypes/TypeDescription.hpp\"");
        text.Line();
        text.Line("namespace Fenrir");
        text.Line("{");
        text.Line("    class TypeRegistry;");
        text.Line("} // namespace Fenrir");
        text.Line();
        foreach (var hook in hooks)
        {
            // Skipped where the package hanging the role's attachments has defined the hook (FenrirTypes/RoleHooks.hpp),
            // and guarded, so the headers of several registrations compile together.
            var guard = $"FENRIR_TYPES_{MacroCase(hook)}_DEFINED";
            text.Line($"// A no-op {hook}, unless a header this registration includes defines it ({guard}).");
            text.Line($"#ifndef {guard}");
            text.Line($"#define {guard}");
            text.Line("template <typename T>");
            text.Line($"inline bool Fenrir::{hook}(Fenrir::TypeRegistry&, const Fenrir::TypeDescription&)");
            text.Line("{");
            text.Line("    return true;");
            text.Line("}");
            text.Line("#endif");
            text.Line();
        }

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

        if (types.Count > 0)
        {
            text.Line();
            text.Line("// The description objects Describe<T> yields, declared so a registration using this one ([registration]");
            text.Line("// 'uses') points its fields at them rather than describing the types again.");
            text.Line($"namespace {registration.DescriptionNamespace}");
            text.Line("{");
            foreach (var type in types)
            {
                text.Line($"    extern const Fenrir::TypeDescription {type.Identifier};");
            }

            text.Line($"}} // namespace {registration.DescriptionNamespace}");
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

    private static string WriteSource(Registration registration, List<Described> types, Identifiers identifiers,
                                      Dictionary<string, UsedType> used, List<Registration> uses)
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
        if (types.Any(type => ByMember(type.Json)))
        {
            text.Line("#include \"FenrirTypes/FieldVisitor.hpp\"");
        }

        text.Line("#include \"FenrirTypes/Kind.hpp\"");
        text.Line("#include \"FenrirTypes/TypeDescription.hpp\"");
        text.Line("#include \"FenrirTypes/TypeRegistry.hpp\"");
        text.Line();
        text.Line("#include <cstdint>");
        text.Line("#include <string_view>");
        text.Line();
        text.Line($"namespace {registration.DescriptionNamespace}");
        text.Line("{");
        var typeIds = types.ToDictionary(type => type.Name, type => type.Identifier, StringComparer.Ordinal);
        var jsons = types.ToDictionary(type => type.Name, type => type.Json, StringComparer.Ordinal);
        foreach (var (name, type) in used)
        {
            typeIds.Add(name, type.Reference);
            jsons.Add(name, type.Json);
        }

        var visits = new Dictionary<string, string>(StringComparer.Ordinal);
        var first = true;
        foreach (var type in types)
        {
            if (!first)
            {
                text.Line();
            }

            first = false;
            WriteDescription(text, registration, type, typeIds, jsons, visits, identifiers);
        }

        text.Line($"}} // namespace {registration.DescriptionNamespace}");

        var ns = registration.DescriptionNamespace;
        foreach (var type in types)
        {
            text.Line();
            text.Line($"const Fenrir::TypeDescription& Fenrir::Describe<{type.Name}>::Description() noexcept");
            text.Line("{");
            text.Line($"    return {ns}::{type.Identifier};");
            text.Line("}");
        }

        text.Line();
        text.Line($"bool {registration.Function}(Fenrir::TypeRegistry& registry)");
        text.Line("{");
        if (types.Count == 0 && uses.Count == 0)
        {
            text.Line("    static_cast<void>(registry);");
            text.Line("    return true;");
        }
        else
        {
            text.Line("    bool registered = true;");
            if (uses.Count > 0)
            {
                text.Line("    // The registrations this one uses first: they register the types they describe that this one holds.");
                foreach (var use in uses)
                {
                    text.Line($"    registered = {use.Function}(registry) && registered;");
                }
            }

            if (types.Count > 0)
            {
                text.Line("    // As the description lists them: a type after every type its fields name and the data asset it");
                text.Line("    // extends. A type registered as this same description already is left alone, so a second run");
                text.Line("    // registers nothing; one that registers goes to its role's hook, which hangs its attachments.");
                foreach (var type in types)
                {
                    // Named from the function's own namespace, which the description namespace is inside.
                    var id = $"{registration.Package}Types::{type.Identifier}";
                    var hook = Hook(type.Json) is { } name ? $"Fenrir::{name}<{type.Name}>(registry, {id}) && " : "";
                    text.Line($"    if (registry.Find({id}.name) != &{id})");
                    text.Line($"        registered = registry.Register({id}) && {hook}registered;");
                }
            }

            text.Line("    return registered;");
        }

        text.Line("}");
        text.Line();
        text.Line("// clang-format on");
        return text.ToString();
    }

    private static void WriteDescription(Text text, Registration registration, Described type,
                                         Dictionary<string, string> typeIds, Dictionary<string, JsonElement> jsons,
                                         Dictionary<string, string> visits, Identifiers identifiers)
    {
        var json = type.Json;
        var name = type.Name;
        var id = type.Identifier;
        var isEnum = json.GetProperty("category").GetString() == "enum";
        var byMember = ByMember(json);

        text.Line($"    // {name}");
        if (!byMember)
        {
            var size = json.GetProperty("size").GetInt64();
            var alignment = json.GetProperty("alignment").GetInt64();
            text.Line($"    static_assert(sizeof({name}) == {size} && alignof({name}) == {alignment},");
            text.Line($"                  {Literal($"{name} is not the layout {registration.DescriptionName} describes: regenerate the description, then this registration")});");
        }

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
                entries.Add(FieldEntry(text, name, id, field, byMember, typeIds, identifiers));
            }

            fields = identifiers.Unique($"{id}_Fields");
            text.Line($"    constexpr Fenrir::FieldInfo {fields}[] = {{");
            foreach (var entry in entries)
            {
                text.Line($"        {entry},");
            }

            text.Line("    };");
        }

        string? tag = null;
        string? alternatives = null;
        if (IsTaggedUnion(json))
        {
            (tag, alternatives) = WriteAlternatives(text, type, typeIds, jsons, identifiers);
        }

        var visit = byMember ? WriteVisit(text, type, fields, visits, typeIds, identifiers) : null;

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
        if (Base(json) is { } baseName)
        {
            text.Line($"        .base = {Literal(baseName)},");
        }

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

        if (tag is not null)
        {
            text.Line($"        .tag = {tag},");
            text.Line($"        .unionOffset = {json.GetProperty("unionOffset").GetInt64()},");
            text.Line($"        .alternatives = {alternatives},");
            text.Line($"        .defaultAlternative = {DefaultAlternative(name, json)},");
        }

        if (visit is not null)
        {
            text.Line($"        .visit = &{visit},");
        }

        text.Line("    };");
    }

    // Writes a tagged union's alternatives, each its tag value and its member as a field at the union's offset; returns
    // the tag's FieldInfo initialiser, its type refusing writes, and the alternatives' name.
    private static (string Tag, string Alternatives) WriteAlternatives(Text text, Described type,
                                                                     Dictionary<string, string> typeIds,
                                                                     Dictionary<string, JsonElement> jsons,
                                                                     Identifiers identifiers)
    {
        var json = type.Json;
        var tagJson = json.GetProperty("tag");
        var tag = FieldEntry(text, type.Name, type.Identifier, tagJson, false, typeIds, identifiers, unionTag: true);
        var tagEnum = jsons[tagJson.GetProperty("type").GetString()!];
        var unionOffset = json.GetProperty("unionOffset").GetInt64();
        var entries = json.GetProperty("kinds").EnumerateArray()
                          .Select(alternative =>
                                      $"{{.tag = {Integer(TagValue(type.Name, alternative, tagEnum))}, .field = " +
                                      FieldEntry(text, type.Name, type.Identifier, alternative, false, typeIds, identifiers,
                                                 offset: unionOffset) + "}")
                          .ToList();

        var alternatives = identifiers.Unique($"{type.Identifier}_Alternatives");
        text.Line($"    constexpr Fenrir::Alternative {alternatives}[] = {{");
        foreach (var entry in entries)
        {
            text.Line($"        {entry},");
        }

        text.Line("    };");
        return (tag, alternatives);
    }

    // Writes a by-member type's visit function, which pushes its base's fields (through the base's own visit function),
    // then each of its own, reached by member; returns its name.
    private static string WriteVisit(Text text, Described type, string? fields, Dictionary<string, string> visits,
                                     Dictionary<string, string> typeIds, Identifiers identifiers)
    {
        var visit = identifiers.Unique($"{type.Identifier}_Visit");
        visits.Add(type.Name, visit);
        var baseName = Base(type.Json);
        var own = Fields(type.Json).Select(field => field.GetProperty("name").GetString()!).ToList();
        text.Line($"    void {visit}(void* instance, Fenrir::FieldVisitor& visitor)");
        text.Line("    {");
        if (baseName is null && own.Count == 0)
        {
            text.Line("        static_cast<void>(instance);");
            text.Line("        static_cast<void>(visitor);");
        }
        else
        {
            text.Line($"        auto& value = *static_cast<{type.Name}*>(instance);");
        }

        if (baseName is not null)
        {
            // A base a used registration describes is visited through its description object.
            text.Line("        // Its base's fields first (ADR-0075).");
            text.Line(visits.TryGetValue(baseName, out var baseVisit)
                          ? $"        {baseVisit}(static_cast<{baseName}*>(&value), visitor);"
                          : $"        {typeIds[baseName]}.visit(static_cast<{baseName}*>(&value), visitor);");
        }

        for (var i = 0; i < own.Count; ++i)
        {
            text.Line($"        visitor.Field({fields}[{i}], Fenrir::ValueRef(&value.{own[i]}, {fields}[{i}].type));");
        }

        text.Line("    }");
        return visit;
    }

    // One FieldInfo initialiser; writes the data it points at (element types, former names) first. A by-member type's
    // field has no offset, and its lists and optionals reach their elements through operations on the member's own
    // C++ type, so the generator never spells a standard library type. A tagged union's alternative records no offset
    // of its own and is given the union's; its tag's type refuses writes.
    private static string FieldEntry(Text text, string typeName, string id, JsonElement field, bool byMember,
                                     Dictionary<string, string> typeIds, Identifiers identifiers, long? offset = null,
                                     bool unionTag = false)
    {
        var name = field.GetProperty("name").GetString()!;
        var fieldId = $"{id}_{Identifier(name)}";
        var member = byMember ? $"decltype({typeName}::{name})" : null;

        var entry = new List<string>
        {
            $".name = {Literal(name)}",
            $".type = {{{ValueTypeOf(text, $"{typeName}::{name}", field, member, fieldId, typeIds, identifiers)}" +
            $"{(unionTag ? ", .unionTag = true" : "")}}}",
        };
        if (!byMember)
        {
            entry.Add($".offset = {offset ?? field.GetProperty("offset").GetInt64()}");
        }

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

    // A ValueType's designated initialisers for the value `value` describes (a field, or an element recorded whole);
    // writes its element's ValueType, named `<id>_Element`, first. `memberType` is the value's C++ type, which a list's
    // or optional's operations are instantiated over; null where the value is not reached by member.
    private static string ValueTypeOf(Text text, string fieldName, JsonElement value, string? memberType, string id,
                                      Dictionary<string, string> typeIds, Identifiers identifiers)
    {
        var kind = value.GetProperty("kind").GetString()!;
        var valueType = new List<string> { $".kind = Fenrir::Kind::{KindName(kind)}" };
        switch (kind)
        {
            case "enum" or "struct":
                valueType.Add($".type = &{typeIds[value.GetProperty("type").GetString()!]}");
                break;
            case "array" or "list" or "optional":
                var elementType = kind == "array" || memberType is null ? null : $"{memberType}::value_type";
                var elementId = identifiers.Unique($"{id}_Element");
                var element = ValueTypeOf(text, fieldName, ElementOf(value), elementType, elementId, typeIds, identifiers);
                text.Line($"    constexpr Fenrir::ValueType {elementId}{{{element}}};");
                valueType.Add($".element = &{elementId}");
                if (kind == "array")
                {
                    valueType.Add($".count = {value.GetProperty("length").GetInt64()}");
                }
                else if (elementType is null)
                {
                    throw new GenerateException(
                        $"{fieldName}: {WithArticle(kind)} in an array, which the description never records");
                }
                else
                {
                    valueType.Add(kind == "list"
                                      ? $".list = &Fenrir::ListOperationsOf<{elementType}>"
                                      : $".optional = &Fenrir::OptionalOperationsOf<{elementType}>");
                }

                break;
            case "fixed-string":
                valueType.Add($".count = {value.GetProperty("length").GetInt64()}");
                break;
            case "reference":
                valueType.Add($".target = {Literal(value.GetProperty("target").GetString()!)}");
                break;
        }

        return string.Join(", ", valueType);
    }

    // The description of an array's, list's or optional's element: recorded whole in `element` when it has keys of its
    // own, else as `elementKind` with `elementType` or `elementTarget`, which this gathers as an element's keys.
    private static JsonElement ElementOf(JsonElement value)
    {
        if (value.TryGetProperty("element", out var element))
        {
            return element;
        }

        var flat = new Dictionary<string, string> { ["kind"] = value.GetProperty("elementKind").GetString()! };
        foreach (var (from, to) in new[] { ("elementType", "type"), ("elementTarget", "target") })
        {
            if (value.TryGetProperty(from, out var key))
            {
                flat[to] = key.GetString()!;
            }
        }

        return JsonSerializer.SerializeToElement(flat);
    }

    // The defaults test: per described struct or tagged union, its instance default-constructed through its
    // description and visited through it, each value it reaches against the default the description records
    // (FenrirTypes/Testing/Defaults.hpp walks it). A bound type has no defaults of its own to check, and an enum none.
    private static string WriteTest(Registration registration, List<Described> types, Dictionary<string, UsedType> used)
    {
        var jsons = types.ToDictionary(type => type.Name, type => type.Json, StringComparer.Ordinal);
        foreach (var (name, type) in used)
        {
            jsons.Add(name, type.Json);
        }

        var text = new Text();
        text.Line($"// Generated by {Tool} from {registration.DescriptionName}. Do not edit.");
        text.Line("// clang-format off");
        text.Line($"// {registration.Package}'s registration against its description: each described struct and tagged union,");
        text.Line("// default-constructed through its registered description and visited through it, holds the defaults");
        text.Line($"// {registration.DescriptionName} records, field by field.");
        text.Line();
        text.Line($"#include \"{registration.HeaderInclude}\"");
        text.Line();
        text.Line("#include \"FenrirTypes/Testing/Defaults.hpp\"");
        text.Line();
        text.Line("#include <catch2/catch_test_macros.hpp>");
        text.Line();
        text.Line("#include <cstdint>");
        text.Line("#include <limits>");
        text.Line("#include <string>");
        var tests = 0;
        foreach (var type in types)
        {
            var category = type.Json.GetProperty("category").GetString();
            if (category is not ("struct" or "tagged-union"))
            {
                continue;
            }

            var expected = new List<(string Path, string Value)>();
            Expect(type.Name, "", type.Json, null, jsons, expected);
            if (expected.Count == 0)
            {
                continue;
            }

            ++tests;
            text.Line();
            text.Line($"TEST_CASE({Literal($"{type.Name}, default-constructed through its registration, has its description's defaults")}, " +
                      "\"[types][defaults]\")");
            text.Line("{");
            text.Line($"    Fenrir::Testing::CheckDefaults(Fenrir::Describe<{type.Name}>::Description(), {{");
            foreach (var (path, value) in expected)
            {
                text.Line($"        {{{Literal(path)}, {value}}},");
            }

            text.Line("    });");
            text.Line("}");
        }

        if (tests == 0)
        {
            text.Line();
            text.Line($"TEST_CASE({Literal($"{registration.Package} describes no struct whose defaults are recorded")}, \"[types][defaults]\")");
            text.Line("{");
            text.Line("    SUCCEED();");
            text.Line("}");
        }

        text.Line();
        text.Line("// clang-format on");
        return text.ToString();
    }

    // Adds the defaults a value of `type` holds below `prefix`, as `overrides` sets them (an object of the members a
    // field's initialiser sets; null or `{}` for the type's own defaults), to `expected`.
    private static void Expect(string owner, string prefix, JsonElement type, JsonElement? overrides,
                               Dictionary<string, JsonElement> jsons, List<(string Path, string Value)> expected)
    {
        var set = overrides is { ValueKind: JsonValueKind.Object } given ? given : (JsonElement?)null;
        switch (type.GetProperty("category").GetString())
        {
            case "bound-type":
                // Only what the initialiser spells: a bound type's own defaults are its library's.
                foreach (var component in Fields(type))
                {
                    var name = component.GetProperty("name").GetString()!;
                    if (set is { } values && values.TryGetProperty(name, out var value))
                    {
                        ExpectValue(owner, prefix + name, component, value, jsons, expected);
                    }
                }

                break;
            case "tagged-union":
                if (set is { } unionSet && unionSet.EnumerateObject().Any())
                {
                    throw new GenerateException(
                        $"{owner}::{prefix.TrimEnd('.')}: a tagged union initialised with members, which the defaults test cannot follow");
                }

                var tag = type.GetProperty("tag");
                ExpectValue(owner, prefix + tag.GetProperty("name").GetString()!, tag, tag.GetProperty("default"), jsons,
                            expected);
                var defaultKind = type.GetProperty("defaultKind").GetString()!;
                var alternative = type.GetProperty("kinds").EnumerateArray()
                                      .First(kind => kind.GetProperty("name").GetString() == defaultKind);
                ExpectValue(owner, prefix + defaultKind, alternative,
                            alternative.TryGetProperty("default", out var held) ? held : null, jsons, expected);
                break;
            default:
                foreach (var field in Fields(type))
                {
                    var name = field.GetProperty("name").GetString()!;
                    JsonElement? value = set is { } values && values.TryGetProperty(name, out var setValue) ? setValue
                                         : field.TryGetProperty("default", out var own) ? own
                                         : null;
                    ExpectValue(owner, prefix + name, field, value, jsons, expected);
                }

                break;
        }
    }

    // Adds the default `value` records for the value `field` describes at `path`: a leaf's expected value, or the
    // values below a struct or array. A missing default (a tagged union's alternative) is the kind's own: zero, false,
    // or the held type's defaults.
    private static void ExpectValue(string owner, string path, JsonElement field, JsonElement? value,
                                    Dictionary<string, JsonElement> jsons, List<(string Path, string Value)> expected)
    {
        var kind = field.GetProperty("kind").GetString()!;
        var given = value is { ValueKind: not JsonValueKind.Null } present ? present : (JsonElement?)null;
        switch (kind)
        {
            case "struct":
                Expect(owner, path + ".", jsons[field.GetProperty("type").GetString()!], given, jsons, expected);
                return;
            case "array":
                var element = ElementOf(field);
                var length = field.GetProperty("length").GetInt32();
                for (var i = 0; i < length; ++i)
                {
                    JsonElement? elementValue = given is { ValueKind: JsonValueKind.Array } list && i < list.GetArrayLength()
                                                    ? list[i]
                                                    : null;
                    ExpectValue(owner, $"{path}[{i}]", element, elementValue, jsons, expected);
                }

                return;
            case "pointer":
                return;
        }

        expected.Add((path, kind switch
        {
            "f32" or "f64" => given is { } number ? Float(number) : "0.0",
            "bool" => given is { } flag && flag.GetBoolean() ? "true" : "false",
            "enum" => $"std::int64_t{{{EnumValue(owner, path, field, given, jsons)}}}",
            "entity" => "Fenrir::EntityValue{}",
            "reference" => "Fenrir::ReferenceValue{}",
            "fixed-string" or "string" or "path" => $"std::string({Literal(given?.GetString() ?? "")})",
            "list" or "optional" => "Fenrir::Testing::Empty{}",
            _ when IsInteger(kind) || kind == "char16" => $"std::int64_t{{{(given is { } integer ? Integer(integer) : "0")}}}",
            _ => throw new GenerateException($"{owner}::{path}: its kind '{kind}' has no default the defaults test knows"),
        }));
    }

    // A float default as a C++ double: its number, or infinity for "inf" and "-inf".
    private static string Float(JsonElement value) => value.ValueKind == JsonValueKind.String
        ? value.GetString() switch
        {
            "inf" => "std::numeric_limits<double>::infinity()",
            "-inf" => "-std::numeric_limits<double>::infinity()",
            var other => throw new GenerateException($"a float default '{other}' is not a number, \"inf\" or \"-inf\""),
        }
        : Number(value);

    // An enum default's value: its enumerator's, by name from the enum's description, or the integer recorded for an
    // enum with no enumerators; zero when none is recorded.
    private static string EnumValue(string owner, string path, JsonElement field, JsonElement? value,
                                    Dictionary<string, JsonElement> jsons)
    {
        if (value is not { } given)
        {
            return "0";
        }

        if (given.ValueKind == JsonValueKind.Number)
        {
            return Integer(given);
        }

        var enumerator = given.GetString()!;
        foreach (var known in jsons[field.GetProperty("type").GetString()!].GetProperty("values").EnumerateArray())
        {
            if (known.GetProperty("name").GetString() == enumerator)
            {
                return Integer(known.GetProperty("value"));
            }
        }

        throw new GenerateException($"{owner}::{path}: its default '{enumerator}' is not an enumerator of its enum");
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

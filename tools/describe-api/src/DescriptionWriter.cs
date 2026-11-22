using System.Globalization;
using System.Text;
using System.Text.Encodings.Web;
using System.Text.Json;

namespace DescribeApi;

/// <summary>Writes a description as the JSON SCHEMA.md documents: two-space indents, LF line ends, a final newline.</summary>
internal static class DescriptionWriter
{
    public static string Write(Description description)
    {
        var buffer = new MemoryStream();
        var options = new JsonWriterOptions
        {
            Indented = true,
            IndentSize = 2,
            NewLine = "\n",
            // Doc comments keep their quotes, backticks and angle brackets readable; only JSON's own escapes apply.
            Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
        };
        using (var json = new Utf8JsonWriter(buffer, options))
        {
            json.WriteStartObject();
            json.WriteString("package", description.Package);
            json.WriteNumber("pointerSize", Description.PointerSize);
            json.WriteString("alignment", Description.AlignmentModel);
            json.WriteStartArray("types");
            foreach (var type in description.Types)
            {
                WriteType(json, type);
            }

            json.WriteEndArray();
            json.WriteEndObject();
        }

        return Encoding.UTF8.GetString(buffer.ToArray()) + "\n";
    }

    private static void WriteType(Utf8JsonWriter json, TypeDescription type)
    {
        json.WriteStartObject();
        json.WriteString("name", type.Name);
        switch (type)
        {
            case EnumDescription:
                json.WriteString("category", "enum");
                break;
            case StructDescription structType:
                json.WriteString("category", "struct");
                json.WriteString("role", Roles.Spelling(structType.Role));
                if (structType.Base is not null)
                {
                    json.WriteString("base", structType.Base);
                }

                break;
            case TaggedUnionDescription union:
                json.WriteString("category", "tagged-union");
                json.WriteString("role", Roles.Spelling(union.Role));
                break;
            case BoundTypeDescription:
                json.WriteString("category", "bound-type");
                json.WriteString("role", Roles.Spelling(Role.BoundType));
                break;
        }

        // A type reached by member has the standard library's layout, which the description leaves to the compiler.
        if (type is not StructDescription { ByMember: true })
        {
            json.WriteNumber("size", type.Size);
            json.WriteNumber("alignment", type.Alignment);
        }

        WriteDoc(json, type.Doc);
        WriteTypeCommands(json, type.Commands);
        switch (type)
        {
            case BoundTypeDescription { Components: { } components }:
                json.WriteStartArray("components");
                foreach (var component in components)
                {
                    json.WriteStartObject();
                    json.WriteString("name", component.Name);
                    json.WriteNumber("offset", component.Offset);
                    json.WriteString("kind", component.Kind);
                    json.WriteEndObject();
                }

                json.WriteEndArray();
                break;
            case EnumDescription enumType:
                json.WriteString("underlying", enumType.Underlying);
                json.WriteStartArray("values");
                foreach (var value in enumType.Values)
                {
                    json.WriteStartObject();
                    json.WriteString("name", value.Name);
                    json.WritePropertyName("value");
                    WriteNumber(json, value.Value);
                    WriteDoc(json, value.Doc);
                    json.WriteEndObject();
                }

                json.WriteEndArray();
                break;
            case StructDescription structType:
                json.WriteStartArray("fields");
                foreach (var field in structType.Fields)
                {
                    WriteField(json, field);
                }

                json.WriteEndArray();
                break;
            case TaggedUnionDescription union:
                json.WritePropertyName("tag");
                WriteField(json, union.Tag);
                json.WriteNumber("unionOffset", union.UnionOffset);
                json.WriteStartArray("kinds");
                foreach (var kind in union.Kinds)
                {
                    json.WriteStartObject();
                    json.WriteString("name", kind.Name);
                    json.WriteString("tag", kind.Tag);
                    WriteFieldType(json, kind.Type);
                    WriteDoc(json, kind.Doc);
                    json.WriteEndObject();
                }

                json.WriteEndArray();
                json.WriteString("defaultKind", union.DefaultKind);
                break;
        }

        json.WriteEndObject();
    }

    private static void WriteField(Utf8JsonWriter json, FieldDescription field)
    {
        json.WriteStartObject();
        json.WriteString("name", field.Name);
        if (field.Offset is { } offset)
        {
            json.WriteNumber("offset", offset);
        }

        WriteFieldType(json, field.Type);
        json.WritePropertyName("default");
        WriteDefault(json, field.Default);
        WriteDoc(json, field.Doc);
        WriteFormerly(json, field.Formerly);
        WriteMetadata(json, field.Metadata);
        json.WriteEndObject();
    }

    // Each key only when its command is present, so a type with none reads as before (SCHEMA.md, "Type and field doc commands").
    private static void WriteTypeCommands(Utf8JsonWriter json, TypeCommands commands)
    {
        if (commands.Version != TypeCommands.None.Version)
        {
            json.WriteNumber("version", commands.Version);
        }

        WriteFormerly(json, commands.Formerly);
        if (commands.Transient)
        {
            json.WriteBoolean("transient", true);
        }

        if (commands.EditorOnly)
        {
            json.WriteBoolean("editorOnly", true);
        }
    }

    private static void WriteFormerly(Utf8JsonWriter json, IReadOnlyList<string> formerly)
    {
        if (formerly.Count == 0)
        {
            return;
        }

        json.WriteStartArray("formerly");
        foreach (var name in formerly)
        {
            json.WriteStringValue(name);
        }

        json.WriteEndArray();
    }

    private static void WriteMetadata(Utf8JsonWriter json, FieldMetadata metadata)
    {
        if (metadata == FieldMetadata.None)
        {
            return;
        }

        json.WriteStartObject("metadata");
        if (metadata.Range is var (min, max))
        {
            json.WriteStartObject("range");
            json.WriteNumber("min", min);
            json.WriteNumber("max", max);
            json.WriteEndObject();
        }

        if (metadata.Step is { } step)
        {
            json.WriteNumber("step", step);
        }

        WriteOptional("display", metadata.Display);
        WriteOptional("group", metadata.Group);
        WriteFlag("readonly", metadata.Readonly);
        WriteFlag("hidden", metadata.Hidden);
        WriteFlag("multiline", metadata.Multiline);
        WriteOptional("units", metadata.Units);
        json.WriteEndObject();

        void WriteOptional(string key, string? value)
        {
            if (value is not null)
            {
                json.WriteString(key, value);
            }
        }

        void WriteFlag(string key, bool value)
        {
            if (value)
            {
                json.WriteBoolean(key, true);
            }
        }
    }

    private static void WriteFieldType(Utf8JsonWriter json, FieldType type)
    {
        json.WriteString("kind", type.Kind);
        if (type.TypeName is not null)
        {
            json.WriteString("type", type.TypeName);
        }

        if (type.Target is not null)
        {
            json.WriteString("target", type.Target);
        }

        if (type.Element is not null)
        {
            json.WriteString("elementKind", type.Element.Kind);
            if (type.Element.TypeName is not null)
            {
                json.WriteString("elementType", type.Element.TypeName);
            }

            if (type.Element.Target is not null)
            {
                json.WriteString("elementTarget", type.Element.Target);
            }

            // The element keys above say one level only, so an element with keys of its own (a list of lists, of arrays, of
            // fixed strings) is recorded whole, as a field's kind would be.
            if (type.Element.Element is not null || type.Element.Kind == "fixed-string")
            {
                json.WriteStartObject("element");
                WriteFieldType(json, type.Element);
                json.WriteEndObject();
            }
        }

        if (type.Kind is "array" or "fixed-string")
        {
            json.WriteNumber("length", type.Length);
        }
    }

    private static void WriteDefault(Utf8JsonWriter json, DefaultValue value)
    {
        switch (value)
        {
            case DefaultValue.Number number:
                WriteNumber(json, number.Spelling);
                break;
            case DefaultValue.Text text:
                json.WriteStringValue(text.Value);
                break;
            case DefaultValue.Bool boolean:
                json.WriteBooleanValue(boolean.Value);
                break;
            case DefaultValue.Null:
                json.WriteNullValue();
                break;
            case DefaultValue.Object members:
                json.WriteStartObject();
                foreach (var (name, member) in members.Members)
                {
                    json.WritePropertyName(name);
                    WriteDefault(json, member);
                }

                json.WriteEndObject();
                break;
            case DefaultValue.Array elements:
                json.WriteStartArray();
                foreach (var element in elements.Elements)
                {
                    WriteDefault(json, element);
                }

                json.WriteEndArray();
                break;
        }
    }

    // Raw values break the writer's indentation inside arrays, so a number is written from the value it spells; the
    // spellings are shortest round-trip, which is how the writer formats them again.
    private static void WriteNumber(Utf8JsonWriter json, string spelling)
    {
        if (long.TryParse(spelling, NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out var signed))
        {
            json.WriteNumberValue(signed);
        }
        else if (ulong.TryParse(spelling, NumberStyles.None, CultureInfo.InvariantCulture, out var unsigned))
        {
            json.WriteNumberValue(unsigned);
        }
        else
        {
            json.WriteNumberValue(double.Parse(spelling, NumberStyles.Float, CultureInfo.InvariantCulture));
        }
    }

    private static void WriteDoc(Utf8JsonWriter json, string? doc)
    {
        if (doc is not null)
        {
            json.WriteString("doc", doc);
        }
    }
}

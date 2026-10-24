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

        json.WriteNumber("size", type.Size);
        json.WriteNumber("alignment", type.Alignment);
        WriteDoc(json, type.Doc);
        switch (type)
        {
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
        json.WriteNumber("offset", field.Offset);
        WriteFieldType(json, field.Type);
        json.WritePropertyName("default");
        WriteDefault(json, field.Default);
        WriteDoc(json, field.Doc);
        json.WriteEndObject();
    }

    private static void WriteFieldType(Utf8JsonWriter json, FieldType type)
    {
        json.WriteString("kind", type.Kind);
        if (type.TypeName is not null)
        {
            json.WriteString("type", type.TypeName);
        }

        if (type.Element is not null)
        {
            json.WriteString("elementKind", type.Element.Kind);
            if (type.Element.TypeName is not null)
            {
                json.WriteString("elementType", type.Element.TypeName);
            }

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

using System.Globalization;
using System.Text.Json;

namespace Iris.Bindings;

/// <summary>
/// A recorded scalar default as a C# literal: the spelling the mirrors' initialisers use. Each throws
/// <see cref="BindingException" /> naming <c>where</c> for a value that is not one of its kind.
/// </summary>
internal static class CSharpLiteral
{
    /// <summary>A pointer's default, which is null: C# holds it as <c>0</c>.</summary>
    public static string Pointer(JsonElement value, string where) =>
        value.ValueKind == JsonValueKind.Null
            ? "0"
            : throw new BindingException($"{where}: a pointer's default is null, not {value}");

    public static string Bool(JsonElement value, string where) => value.ValueKind switch
    {
        JsonValueKind.True => "true",
        JsonValueKind.False => "false",
        _ => throw new BindingException($"{where}: a bool's default is true or false, not {value}"),
    };

    public static string Integer(JsonElement value, string kind, string where)
    {
        if (value.ValueKind != JsonValueKind.Number || value.GetRawText().Any(c => c is '.' or 'e' or 'E'))
        {
            throw new BindingException($"{where}: an integer's default is a JSON integer, not {value}");
        }

        _ = Binding.CSharpScalar(kind);
        return value.GetRawText();
    }

    public static string Char(JsonElement value, string where)
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
    public static string Float(JsonElement value, bool single, string where)
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
}

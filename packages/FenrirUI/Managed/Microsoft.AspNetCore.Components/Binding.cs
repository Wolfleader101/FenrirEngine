using System.Globalization;

namespace Microsoft.AspNetCore.Components;

/// <summary>
/// Declares a handler directive, <c>@name</c>, on a class named <c>EventHandlers</c>, which is how the Razor generator
/// finds them: the argument type is what an <c>Action&lt;T&gt;</c> handler takes.
/// </summary>
[AttributeUsage(AttributeTargets.Class, AllowMultiple = true, Inherited = true)]
public sealed class EventHandlerAttribute : Attribute
{
    public EventHandlerAttribute(string attributeName, Type eventArgsType)
        : this(attributeName, eventArgsType, false, false)
    {
    }

    public EventHandlerAttribute(string attributeName, Type eventArgsType, bool enableStopPropagation,
                                 bool enablePreventDefault)
    {
        AttributeName = attributeName;
        EventArgsType = eventArgsType;
        EnableStopPropagation = enableStopPropagation;
        EnablePreventDefault = enablePreventDefault;
    }

    public string AttributeName { get; }

    public Type EventArgsType { get; }

    public bool EnableStopPropagation { get; }

    public bool EnablePreventDefault { get; }
}

/// <summary>
/// Declares that <c>@bind</c> on <see cref="Element"/> binds its <see cref="ValueAttribute"/> and its
/// <see cref="ChangeAttribute"/> event, on a class named <c>BindAttributes</c>, which is how the Razor generator finds it.
/// </summary>
[AttributeUsage(AttributeTargets.Class, AllowMultiple = true, Inherited = true)]
public sealed class BindElementAttribute : Attribute
{
    public BindElementAttribute(string element, string? suffix, string valueAttribute, string changeAttribute)
    {
        Element = element;
        Suffix = suffix;
        ValueAttribute = valueAttribute;
        ChangeAttribute = changeAttribute;
    }

    public string Element { get; }

    public string? Suffix { get; }

    public string ValueAttribute { get; }

    public string ChangeAttribute { get; }
}

/// <summary>
/// Declares <c>@bind</c> on an <c>input</c> of a given type. FenrirUI has no <c>input</c> element, so nothing declares
/// one; the Razor generator looks for <c>@bind</c>'s rules only where both this and <see cref="BindElementAttribute"/>
/// exist.
/// </summary>
[AttributeUsage(AttributeTargets.Class, AllowMultiple = true, Inherited = true)]
public sealed class BindInputElementAttribute : Attribute
{
    public BindInputElementAttribute(string? type, string? suffix, string? valueAttribute, string? changeAttribute,
                                     bool isInvariantCulture, string? format)
    {
        Type = type;
        Suffix = suffix;
        ValueAttribute = valueAttribute;
        ChangeAttribute = changeAttribute;
        IsInvariantCulture = isInvariantCulture;
        Format = format;
    }

    public string? Type { get; }

    public string? Suffix { get; }

    public string? ValueAttribute { get; }

    public string? ChangeAttribute { get; }

    public bool IsInvariantCulture { get; }

    public string? Format { get; }
}

/// <summary>
/// <c>@bind</c>'s conversions: a field to the value attribute's text, and a control's value back to the field's type,
/// in the invariant culture (ADR-0069 has no format strings or cultures).
/// </summary>
public static class BindConverter
{
    public static string? FormatValue(string? value, CultureInfo? culture = null) => value;

    /// <summary>A checkbox's checked state, as <c>"true"</c> or <c>"false"</c>.</summary>
    public static string FormatValue(bool value, CultureInfo? culture = null) => value ? "true" : "false";

    /// <summary>A number, or anything else, as its invariant text.</summary>
    public static string? FormatValue<T>(T value, CultureInfo? culture = null) => value switch
    {
        null => null,
        bool flag => FormatValue(flag),
        IFormattable formattable => formattable.ToString(null, CultureInfo.InvariantCulture),
        _ => value.ToString(),
    };

    /// <summary>
    /// The control's value as a <typeparamref name="T"/>: a number rounds to an integer type, a nonzero number is a
    /// true <c>bool</c>, and text parses in the invariant culture. A value that does not convert gives the default.
    /// </summary>
    public static T ConvertTo<T>(object? value)
    {
        if (value is T same)
            return same;
        Type target = Nullable.GetUnderlyingType(typeof(T)) ?? typeof(T);
        if (value == null)
            return default!;
        try
        {
            object converted;
            if (target == typeof(string))
                converted = FormatValue(value) ?? "";
            else if (target == typeof(bool))
                converted = value is string text ? bool.Parse(text) : Convert.ToDouble(value, CultureInfo.InvariantCulture) != 0;
            else if (target.IsEnum)
                converted = Enum.ToObject(target, Convert.ToInt64(Math.Round(Convert.ToDouble(value, CultureInfo.InvariantCulture))));
            else if (IsInteger(target) && value is float or double)
                converted = Convert.ChangeType(Math.Round(Convert.ToDouble(value, CultureInfo.InvariantCulture)), target, CultureInfo.InvariantCulture);
            else
                converted = Convert.ChangeType(value, target, CultureInfo.InvariantCulture);
            return (T)converted;
        }
        catch (Exception exception) when (exception is FormatException or InvalidCastException or OverflowException)
        {
            return default!;
        }
    }

    private static bool IsInteger(Type type) =>
        type == typeof(int) || type == typeof(long) || type == typeof(short) || type == typeof(byte) ||
        type == typeof(uint) || type == typeof(ulong) || type == typeof(ushort) || type == typeof(sbyte);
}

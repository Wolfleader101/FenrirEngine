namespace Fenrir.Scripting;

// The attributes that mirror the API description's doc commands on a C# type or field (ADR-0074), each with the
// command's meaning and nothing more. The Fenrir.Scripting source generator reads them into the description it reports.

/// <summary>
/// <c>@transient</c>. On a component: the component is never written to a scene file. On a component's field: the field
/// is kept in the layout but never written, so a file read back leaves it at its default.
/// </summary>
[AttributeUsage(AttributeTargets.Struct | AttributeTargets.Class | AttributeTargets.Field, AllowMultiple = false, Inherited = false)]
public sealed class TransientAttribute : Attribute
{
}

/// <summary><c>@editor-only</c>: the type is authoring data for an editor only (ADR-0082).</summary>
[AttributeUsage(AttributeTargets.Struct | AttributeTargets.Class, AllowMultiple = false, Inherited = false)]
public sealed class EditorOnlyAttribute : Attribute
{
}

/// <summary>
/// The field is UTF-8 text in a fixed buffer, written to files as text: a <c>fixed byte</c> buffer, or a struct marked
/// <c>[InlineArray]</c> whose one field is a <c>byte</c>. The buffer's last byte is always its terminator, so it holds
/// one byte less of text.
/// </summary>
[AttributeUsage(AttributeTargets.Field, AllowMultiple = false, Inherited = false)]
public sealed class FixedStringAttribute : Attribute
{
}

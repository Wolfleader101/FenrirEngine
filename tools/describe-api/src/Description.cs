namespace DescribeApi;

/// <summary>What a selected type is to the bindings that read the description.</summary>
internal enum Role
{
    Component,
    Event,
    SharedStruct,
    BoundType,

    /// <summary>A resource of a scene: plain data, as a component is.</summary>
    SceneResource,

    /// <summary>A resource of the app: plain data, as a component is.</summary>
    AppResource,

    /// <summary>
    /// An enum no field holds, selected so it is described all the same (a native function takes it). Only selections
    /// spell it: a described enum has no role.
    /// </summary>
    Enum,

    /// <summary>Not selected: described because a selected type's field uses it.</summary>
    Nested,
}

internal static class Roles
{
    public static string Spelling(Role role) => role switch
    {
        Role.Component => "component",
        Role.Event => "event",
        Role.SharedStruct => "shared-struct",
        Role.BoundType => "bound-type",
        Role.SceneResource => "scene-resource",
        Role.AppResource => "app-resource",
        _ => "nested",
    };

    public static Role? Parse(string spelling) => spelling switch
    {
        "component" => Role.Component,
        "event" => Role.Event,
        "shared-struct" => Role.SharedStruct,
        "bound-type" => Role.BoundType,
        "scene-resource" => Role.SceneResource,
        "app-resource" => Role.AppResource,
        "enum" => Role.Enum,
        _ => null,
    };
}

/// <summary>A field's kind: what its bytes hold (SCHEMA.md, "Kinds").</summary>
internal sealed record FieldType(string Kind, string? TypeName = null, FieldType? Element = null, long Length = 0)
{
    /// <summary>The described types this kind refers to, for the dependency order.</summary>
    public IEnumerable<string> References()
    {
        if (TypeName is not null)
        {
            yield return TypeName;
        }

        if (Element is not null)
        {
            foreach (var name in Element.References())
            {
                yield return name;
            }
        }
    }
}

/// <summary>A recorded default value, as the description writes it (SCHEMA.md, "Defaults").</summary>
internal abstract record DefaultValue
{
    /// <summary>A number, written as it is spelled here (shortest round-trip for floats).</summary>
    public sealed record Number(string Spelling) : DefaultValue;

    /// <summary>A string: an enumerator's name, or <c>inf</c> / <c>-inf</c> for an infinite float.</summary>
    public sealed record Text(string Value) : DefaultValue;

    public sealed record Bool(bool Value) : DefaultValue;

    /// <summary>A null pointer.</summary>
    public sealed record Null : DefaultValue;

    /// <summary>Named values in storage order; empty means the field's type's own defaults.</summary>
    public sealed record Object(IReadOnlyList<KeyValuePair<string, DefaultValue>> Members) : DefaultValue;

    public sealed record Array(IReadOnlyList<DefaultValue> Elements) : DefaultValue;

    public static readonly Object TypeDefaults = new(System.Array.Empty<KeyValuePair<string, DefaultValue>>());
}

internal sealed record FieldDescription(string Name, long Offset, FieldType Type, DefaultValue Default, string? Doc);

internal sealed record EnumValue(string Name, string Value, string? Doc);

internal sealed record UnionKind(string Name, string Tag, FieldType Type, string? Doc);

internal abstract record TypeDescription(string Name, long Size, long Alignment, string? Doc)
{
    public abstract IEnumerable<string> References();
}

internal sealed record EnumDescription(string Name, long Size, long Alignment, string? Doc, string Underlying,
                                       IReadOnlyList<EnumValue> Values) : TypeDescription(Name, Size, Alignment, Doc)
{
    public override IEnumerable<string> References() => [];
}

internal sealed record StructDescription(string Name, long Size, long Alignment, string? Doc, Role Role,
                                         IReadOnlyList<FieldDescription> Fields)
    : TypeDescription(Name, Size, Alignment, Doc)
{
    public override IEnumerable<string> References() => Fields.SelectMany(field => field.Type.References());
}

internal sealed record TaggedUnionDescription(string Name, long Size, long Alignment, string? Doc, Role Role,
                                              FieldDescription Tag, long UnionOffset, IReadOnlyList<UnionKind> Kinds,
                                              string DefaultKind) : TypeDescription(Name, Size, Alignment, Doc)
{
    public override IEnumerable<string> References() =>
        Tag.Type.References().Concat(Kinds.SelectMany(kind => kind.Type.References()));
}

internal sealed record BoundTypeDescription(string Name, long Size, long Alignment, string? Doc)
    : TypeDescription(Name, Size, Alignment, Doc)
{
    public override IEnumerable<string> References() => [];
}

/// <summary>A package's API description: every shared type, dependencies first, then by name.</summary>
internal sealed record Description(string Package, IReadOnlyList<TypeDescription> Types)
{
    /// <summary>The one target a description is read for: 64-bit, 8-byte pointers.</summary>
    public const int PointerSize = 8;

    /// <summary>The layout rules MSVC, Clang and GCC share for standard-layout data on that target.</summary>
    public const string AlignmentModel = "natural";
}

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

    /// <summary>
    /// A game's data in the asset store (ADR-0075): may hold the owning kinds and extend one other data asset, whose data
    /// members it inherits. No virtual functions.
    /// </summary>
    DataAsset,

    /// <summary>An `Install` config or an importer's options (ADR-0084): may hold the owning kinds, extends nothing with data.</summary>
    Settings,

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
        Role.DataAsset => "data-asset",
        Role.Settings => "settings",
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
        "data-asset" => Role.DataAsset,
        "settings" => Role.Settings,
        "enum" => Role.Enum,
        _ => null,
    };

    /// <summary>The role as a diagnostic names a type of it: "a component", "a data asset".</summary>
    public static string Phrase(Role role) => role switch
    {
        Role.Component => "a component",
        Role.Event => "an event",
        Role.SharedStruct => "a shared struct",
        Role.BoundType => "a bound type",
        Role.SceneResource => "a scene resource",
        Role.AppResource => "an app resource",
        Role.DataAsset => "a data asset",
        Role.Settings => "a settings type",
        Role.Enum => "an enum",
        _ => "a nested type",
    };

    /// <summary>
    /// Whether a type of the role holds its fields by member rather than by offset, and so may hold the owning kinds
    /// (SCHEMA.md, "Owning kinds").
    /// </summary>
    public static bool IsOwning(Role role) => role is Role.DataAsset or Role.Settings;
}

/// <summary>
/// A field's kind: what its bytes hold (SCHEMA.md, "Kinds"). <see cref="TypeName" /> names the described type of an
/// <c>enum</c> or <c>struct</c>; <see cref="Element" /> and <see cref="Length" /> are an <c>array</c>'s, and
/// <see cref="Length" /> alone a <c>fixed-string</c>'s bytes; <see cref="Element" /> alone is a <c>list</c>'s or an
/// <c>optional</c>'s; <see cref="Target" /> is the type a <c>reference</c> names, which is never described for it.
/// </summary>
internal sealed record FieldType(string Kind, string? TypeName = null, FieldType? Element = null, long Length = 0,
                                 string? Target = null)
{
    /// <summary>
    /// The kinds a type holds by member rather than at an offset, allowed only where the role is owning
    /// (<see cref="Roles.IsOwning" />).
    /// </summary>
    public static bool IsOwningKind(string kind) => kind is "string" or "list" or "optional" or "path";

    public bool IsOwning => IsOwningKind(Kind);

    /// <summary>
    /// For a <c>reference</c> whose handle the selection also selects as a bound type (for a binding's mapping): that
    /// type's name, which the dependency order keeps before the types holding the reference, as it did when they held
    /// the handle as a struct. Never written.
    /// </summary>
    public string? SelectedHandle { get; init; }

    /// <summary>The described types this kind refers to, for the dependency order.</summary>
    public IEnumerable<string> References()
    {
        if (TypeName is not null)
        {
            yield return TypeName;
        }

        if (SelectedHandle is not null)
        {
            yield return SelectedHandle;
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

/// <summary>
/// The editor metadata a field's doc commands give (SCHEMA.md, "Type and field doc commands"): each null or false when its command is
/// absent.
/// </summary>
internal sealed record FieldMetadata((double Min, double Max)? Range, double? Step, string? Display, string? Group,
                                     bool Readonly, bool Hidden, bool Multiline, string? Units)
{
    public static readonly FieldMetadata None = new(null, null, null, null, false, false, false, null);
}

/// <summary>A field: <see cref="Offset" /> is null for an owning kind, which a generated visitor reaches by member.</summary>
internal sealed record FieldDescription(string Name, long? Offset, FieldType Type, DefaultValue Default, string? Doc)
{
    /// <summary>The names the field had before, from its <c>@formerly</c> commands, in the order written.</summary>
    public IReadOnlyList<string> Formerly { get; init; } = [];

    public FieldMetadata Metadata { get; init; } = FieldMetadata.None;
}

internal sealed record EnumValue(string Name, string Value, string? Doc);

internal sealed record UnionKind(string Name, string Tag, FieldType Type, string? Doc);

/// <summary>
/// What a type's doc commands give (SCHEMA.md, "Type and field doc commands"): its <c>@version</c> (1 when absent), its former names
/// from <c>@formerly</c> in the order written, and whether it is <c>@transient</c> or <c>@editor-only</c>.
/// </summary>
internal sealed record TypeCommands(int Version, IReadOnlyList<string> Formerly, bool Transient, bool EditorOnly)
{
    public static readonly TypeCommands None = new(1, [], false, false);
}

internal abstract record TypeDescription(string Name, long Size, long Alignment, string? Doc)
{
    public TypeCommands Commands { get; init; } = TypeCommands.None;

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
    /// <summary>
    /// For a <c>data-asset</c>: the data asset it extends, whose fields are its base's description's and not repeated in
    /// <see cref="Fields" />; null when it extends none.
    /// </summary>
    public string? Base { get; init; }

    /// <summary>
    /// Whether a generated visitor reaches its fields by member rather than by offset: a data asset, a settings type, or
    /// a type holding an owning kind (itself, or in a type it holds by value). Its size, alignment and offsets are the
    /// standard library's, which differ between them, so the description records none (SCHEMA.md, "Owning kinds").
    /// </summary>
    public bool ByMember { get; init; }

    public override IEnumerable<string> References() =>
        Fields.SelectMany(field => field.Type.References()).Concat(Base is null ? [] : [Base]);
}

internal sealed record TaggedUnionDescription(string Name, long Size, long Alignment, string? Doc, Role Role,
                                              FieldDescription Tag, long UnionOffset, IReadOnlyList<UnionKind> Kinds,
                                              string DefaultKind) : TypeDescription(Name, Size, Alignment, Doc)
{
    public override IEnumerable<string> References() =>
        Tag.Type.References().Concat(Kinds.SelectMany(kind => kind.Type.References()));
}

/// <summary>One scalar a bound type is made of: its member's name (a path for a nested one), offset and kind.</summary>
internal sealed record BoundComponent(string Name, long Offset, string Kind);

/// <summary>
/// A bound type: its layout and, when every value it holds is a scalar, those scalars in storage order (null otherwise).
/// </summary>
internal sealed record BoundTypeDescription(string Name, long Size, long Alignment, string? Doc,
                                            IReadOnlyList<BoundComponent>? Components)
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

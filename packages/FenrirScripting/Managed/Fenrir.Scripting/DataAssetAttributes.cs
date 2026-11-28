namespace Fenrir.Scripting;

/// <summary>
/// A data asset type (ADR-0075, ADR-0083): a class whose public instance fields and public read-write auto-properties
/// are its fields, held by the asset store as a value tree and read into a new instance on demand. The class extends
/// <c>object</c>, another <see cref="IDataAsset"/> class, or the generated class of a C++ data asset type, and holds no
/// behaviour. Its canonical name is its full C# name. The Fenrir.Scripting source generator writes its description and its
/// codecs; a struct implementing this is refused.
/// </summary>
public interface IDataAsset
{
}

// The attributes that mirror the API description's doc commands on a data asset type and its fields (ADR-0074), each
// with the command's meaning and nothing more. The Fenrir.Scripting source generator reads them into the description.
// TransientAttribute, EditorOnlyAttribute and FixedStringAttribute are in SerialisationAttributes.cs.

/// <summary><c>@version n</c>: the version files of the type are written at; 1 when absent.</summary>
[AttributeUsage(AttributeTargets.Class, AllowMultiple = false, Inherited = false)]
public sealed class VersionAttribute : Attribute
{
    public VersionAttribute(int version) => Version = version;

    public int Version { get; }
}

/// <summary>
/// <c>@formerly name</c>: a name the type or field had before, which files written then spell. A file holding the old
/// name reads into this one. Give it once per former name.
/// </summary>
[AttributeUsage(AttributeTargets.Class | AttributeTargets.Field | AttributeTargets.Property, AllowMultiple = true, Inherited = false)]
public sealed class FormerlyAttribute : Attribute
{
    public FormerlyAttribute(string name) => Name = name;

    public string Name { get; }
}

/// <summary>
/// An upgrader (ADR-0077): a <c>static</c> method of a data asset class taking and returning a <see cref="TreeNode"/>,
/// which turns the tree of a file written at <paramref name="fromVersion"/> into the tree at the next version. A type at
/// <see cref="VersionAttribute"/> n has one for each version from 1 to n - 1.
/// </summary>
[AttributeUsage(AttributeTargets.Method, AllowMultiple = false, Inherited = false)]
public sealed class UpgraderAttribute : Attribute
{
    public UpgraderAttribute(int fromVersion) => FromVersion = fromVersion;

    /// <summary>The version of the trees the method takes.</summary>
    public int FromVersion { get; }
}

/// <summary><c>path</c>: the string field is a path relative to the asset root, written as a path is.</summary>
[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property, AllowMultiple = false, Inherited = false)]
public sealed class PathAttribute : Attribute
{
}

/// <summary>
/// The class is generated from a C++ data asset type (ADR-0083 §3), named here by its canonical name, and its codecs come
/// with it; the Fenrir.Scripting source generator writes nothing for it. A class written by hand never carries this.
/// </summary>
[AttributeUsage(AttributeTargets.Class, AllowMultiple = false, Inherited = false)]
public sealed class NativeDataAssetAttribute : Attribute
{
    public NativeDataAssetAttribute(string name) => Name = name;

    /// <summary>The C++ type's canonical name, such as <c>AssetsTest::WeaponData</c>.</summary>
    public string Name { get; }
}

/// <summary><c>@range min max</c>: the inclusive range an inspector offers for a number.</summary>
[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property, AllowMultiple = false, Inherited = false)]
public sealed class RangeAttribute : Attribute
{
    public RangeAttribute(double min, double max)
    {
        Min = min;
        Max = max;
    }

    public double Min { get; }
    public double Max { get; }
}

/// <summary><c>@step s</c>: the increment an inspector steps a number by.</summary>
[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property, AllowMultiple = false, Inherited = false)]
public sealed class StepAttribute : Attribute
{
    public StepAttribute(double step) => Step = step;

    public double Step { get; }
}

/// <summary><c>@display text</c>: the name an inspector shows in place of the field's.</summary>
[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property, AllowMultiple = false, Inherited = false)]
public sealed class DisplayNameAttribute : Attribute
{
    public DisplayNameAttribute(string text) => Text = text;

    public string Text { get; }
}

/// <summary><c>@group text</c>: the group an inspector shows the field under.</summary>
[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property, AllowMultiple = false, Inherited = false)]
public sealed class GroupAttribute : Attribute
{
    public GroupAttribute(string text) => Text = text;

    public string Text { get; }
}

/// <summary><c>@units text</c>: the units an inspector shows beside the value.</summary>
[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property, AllowMultiple = false, Inherited = false)]
public sealed class UnitsAttribute : Attribute
{
    public UnitsAttribute(string text) => Text = text;

    public string Text { get; }
}

/// <summary><c>@readonly</c>: an inspector shows the value but does not edit it. Code still writes it.</summary>
[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property, AllowMultiple = false, Inherited = false)]
public sealed class ReadOnlyAttribute : Attribute
{
}

/// <summary><c>@hidden</c>: an inspector does not show the field. Files still hold it.</summary>
[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property, AllowMultiple = false, Inherited = false)]
public sealed class HiddenAttribute : Attribute
{
}

/// <summary><c>@multiline</c>: an inspector edits the text over several lines.</summary>
[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property, AllowMultiple = false, Inherited = false)]
public sealed class MultilineAttribute : Attribute
{
}

namespace Fenrir.Scripting;

/// <summary>
/// Says which component the engine has registered under a name this C# struct mirrors. The struct's layout must match
/// the native component exactly, which the host checks when the script assembly loads (a mismatch fails the load and
/// names the field). Components defined only in C# do not need this.
/// </summary>
[AttributeUsage(AttributeTargets.Struct, AllowMultiple = false, Inherited = false)]
public sealed class NativeComponentAttribute : Attribute
{
    public NativeComponentAttribute(string name) => Name = name;

    /// <summary>The name the host registered the component under, for example <c>Fenrir.Transform</c>.</summary>
    public string Name { get; }
}

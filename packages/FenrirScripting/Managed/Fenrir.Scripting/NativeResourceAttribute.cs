namespace Fenrir.Scripting;

/// <summary>
/// Says which native resource the engine has registered under a name this C# struct mirrors (an extension's
/// <c>MakeSceneResource</c> or <c>MakeAppResource</c>). The struct implements <see cref="IResource"/>, or
/// <see cref="IReadOnlyResource"/> for a resource scripts only read, and its layout must match the native resource exactly,
/// which the host checks when the script assembly loads (a mismatch fails the load and names the field).
/// </summary>
[AttributeUsage(AttributeTargets.Struct, AllowMultiple = false, Inherited = false)]
public sealed class NativeResourceAttribute : Attribute
{
    public NativeResourceAttribute(string name) => Name = name;

    /// <summary>The name the host registered the resource under, for example <c>Fenrir.Renderer.Sky</c>.</summary>
    public string Name { get; }
}

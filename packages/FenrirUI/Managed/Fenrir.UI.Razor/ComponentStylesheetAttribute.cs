namespace Fenrir.UI.Razor;

/// <summary>
/// A Razor component's own stylesheet, <c>Foo.razor.css</c> beside <c>Foo.razor</c> (ADR-0070). FenrirUI's Razor
/// generator puts it on a partial of the Razor component for each such file: the file's absolute path at build time and
/// its text. On the type's first mount into a UI root, the runtime loads it there with the type's scope class:
/// from the path when the file is there, so editing it hot-reloads, and from the text otherwise.
/// </summary>
[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class ComponentStylesheetAttribute(string path, string text) : Attribute
{
    /// <summary>The sheet's absolute path when the Razor component was compiled.</summary>
    public string Path { get; } = path;

    /// <summary>The sheet's text when the Razor component was compiled.</summary>
    public string Text { get; } = text;
}

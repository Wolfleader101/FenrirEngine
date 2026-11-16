namespace Microsoft.AspNetCore.Components.Rendering;

/// <summary>
/// The calls a Razor component's generated <c>BuildRenderTree</c> makes, with Blazor's signatures. Every sequence number is
/// an integer literal the Razor generator numbers per file, from 0. The engine supplies the implementation, which builds
/// FenrirUI elements in place.
/// </summary>
public abstract class RenderTreeBuilder
{
    /// <summary>Opens an element of tag <paramref name="elementName"/>; its attributes follow, then its content.</summary>
    public abstract void OpenElement(int sequence, string elementName);

    /// <summary>Closes the element the matching <see cref="OpenElement"/> opened.</summary>
    public abstract void CloseElement();

    /// <summary>
    /// Static markup the generator folded into one string: whitespace between elements, or a run of elements with no
    /// C# in them.
    /// </summary>
    public abstract void AddMarkupContent(int sequence, string? markupContent);

    /// <summary>A run of text, written in the markup or from a <c>string</c> expression.</summary>
    public abstract void AddContent(int sequence, string? textContent);

    /// <summary>A run of text from an expression of any other type, shown as its <c>ToString()</c>.</summary>
    public abstract void AddContent(int sequence, object? textContent);

    /// <summary>An attribute with no value.</summary>
    public abstract void AddAttribute(int sequence, string name);

    /// <summary>An attribute from a <c>bool</c> expression.</summary>
    public abstract void AddAttribute(int sequence, string name, bool value);

    /// <summary>An attribute from text, or text with expressions in it (<c>class="a @b"</c>).</summary>
    public abstract void AddAttribute(int sequence, string name, string? value);

    /// <summary>An attribute from an expression of any other type.</summary>
    public abstract void AddAttribute(int sequence, string name, object? value);

    /// <summary>
    /// The open element's <c>@key</c>, written after its attributes: it keeps the element when siblings come, go or move.
    /// </summary>
    public abstract void SetKey(object? value);
}

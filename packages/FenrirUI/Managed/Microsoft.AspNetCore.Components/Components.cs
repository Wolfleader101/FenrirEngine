using Microsoft.AspNetCore.Components.Rendering;

namespace Microsoft.AspNetCore.Components;

/// <summary>
/// What makes a class a Razor component to the Razor generator, which matches this interface by its full name. A
/// <c>.razor</c> file's class implements it through <see cref="ComponentBase"/>.
/// </summary>
public interface IComponent
{
}

/// <summary>
/// The base the Razor generator gives a <c>.razor</c> file's class when it names none. Fenrir's script projects name
/// <c>Fenrir.UI.Razor.UIComponent</c> through the default imports, which derives from this.
/// </summary>
public abstract class ComponentBase : IComponent
{
    /// <summary>
    /// The component's markup, which the generated class overrides: one call on <paramref name="__builder"/> per
    /// element, attribute and text run, each with its compile-time sequence number.
    /// </summary>
    protected virtual void BuildRenderTree(RenderTreeBuilder __builder)
    {
    }
}

using Fenrir.Scripting;
using Microsoft.AspNetCore.Components;
using Microsoft.AspNetCore.Components.Rendering;

namespace Fenrir.UI.Razor;

/// <summary>
/// The base of a Razor component (ADR-0068): one managed object per mount, whose fields hold UI-local state and whose
/// markup the builder makes into FenrirUI elements under <see cref="Element"/>. A <c>.razor</c> file in a script project
/// that imports <c>Fenrir.UI.Razor.props</c> derives from it through the default imports' <c>@inherits</c>. Mount one
/// with <see cref="UIRootRazor.Mount{T}"/>.
/// </summary>
public abstract class UIComponent : ComponentBase
{
    /// <summary>The UI root it is mounted on.</summary>
    public UIRoot Root { get; internal set; }

    /// <summary>
    /// Its root element: the host panel its mount made, which carries its readable class and holds the elements its
    /// markup builds.
    /// </summary>
    public ElementId Element { get; internal set; }

    /// <summary>The frame's system context while it builds, for reading game state from the ECS.</summary>
    public SystemContext Context { get; internal set; }

    internal void BuildInto(RenderTreeBuilder builder) => BuildRenderTree(builder);
}

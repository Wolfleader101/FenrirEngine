using Microsoft.AspNetCore.Components;
using Microsoft.AspNetCore.Components.Rendering;

namespace Fenrir.UI.Razor;

/// <summary>
/// Places a markup document inside a Razor component (ADR-0067): <c>&lt;Document Src="ui/credits.ui" /&gt;</c> makes a
/// host <c>panel</c> with the class <c>document</c> and loads the document into it, as <c>UIRoot.LoadDocument</c> does,
/// its errors logged with their files and lines. What the document made is not the builder's: a rebuild never visits,
/// updates or removes it. When <see cref="Src"/> changes, the old content is removed at once and the new document
/// loaded; when the <c>Document</c> goes, its host goes with everything in it.
/// </summary>
public sealed class Document : UIComponent
{
    /// <summary>The document's path, relative to the asset root or absolute; null or empty loads nothing.</summary>
    [Parameter]
    public string? Src { get; set; }

    protected override void BuildRenderTree(RenderTreeBuilder builder)
    {
        if (builder is not ElementBuilder elements)
            throw new InvalidOperationException("A Document builds only through the Razor runtime's builder");
        elements.PlaceDocument(0, Src);
    }
}

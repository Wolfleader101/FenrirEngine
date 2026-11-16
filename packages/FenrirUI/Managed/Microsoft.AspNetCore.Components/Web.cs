using Fenrir.UI;

namespace Microsoft.AspNetCore.Components.Web;

/// <summary>
/// The handler directives (ADR-0069), one per <see cref="UIEventKind"/>, found by the Razor generator through this
/// class's name. Each handler is an <c>Action</c> or an <c>Action&lt;UIEvent&gt;</c>. The default imports bring this
/// namespace in; without it, <c>@onclick</c> is plain markup.
/// </summary>
[EventHandler("onclick", typeof(UIEvent))]
[EventHandler("ondblclick", typeof(UIEvent))]
[EventHandler("onpointerdown", typeof(UIEvent))]
[EventHandler("onpointerup", typeof(UIEvent))]
[EventHandler("onpointerenter", typeof(UIEvent))]
[EventHandler("onpointerleave", typeof(UIEvent))]
[EventHandler("onpointermove", typeof(UIEvent))]
[EventHandler("onwheel", typeof(UIEvent))]
[EventHandler("onkeydown", typeof(UIEvent))]
[EventHandler("onkeyup", typeof(UIEvent))]
[EventHandler("ontextinput", typeof(UIEvent))]
[EventHandler("onfocus", typeof(UIEvent))]
[EventHandler("onblur", typeof(UIEvent))]
[EventHandler("onchange", typeof(UIEvent))]
[EventHandler("onsubmit", typeof(UIEvent))]
[EventHandler("oncancel", typeof(UIEvent))]
public static class EventHandlers
{
}

/// <summary>
/// <c>@bind</c>'s elements (ADR-0069), found by the Razor generator through this class's name: a slider's and a
/// progress bar's value, a checkbox's checked state and a text entry's text, each read back on its change.
/// </summary>
[BindElement("slider", null, "value", "onchange")]
[BindElement("progress", null, "value", "onchange")]
[BindElement("checkbox", null, "value", "onchange")]
[BindElement("textentry", null, "value", "onchange")]
public static class BindAttributes
{
}

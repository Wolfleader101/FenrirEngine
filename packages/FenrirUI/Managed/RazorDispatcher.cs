// The Razor dispatcher (ADR-0068, ADR-0069), shipped as source: Fenrir.UI.Razor.props compiles it into every script
// project that has .razor files, because only a script assembly's own systems are scheduled. It reads the frame's UI
// events and builds the mounted Razor components in FenrirUI's build stage, paints the ones that override OnPaint in
// its paint stage, and lets go of them when the script generation is released.

using Fenrir.Scripting;
using Fenrir.UI;
using Fenrir.UI.Razor;

namespace Fenrir.UI.Razor.Generated;

/// <summary>
/// Runs every mounted Razor component once a frame, in <c>Fenrir.UI.Build</c>: first the handlers of the frame's UI
/// events (ADR-0069), then the builds.
/// </summary>
[StageSystem(UIStages.Build)]
public sealed class RazorDispatcher : ISystem
{
    private readonly EventReader<UIEvent> events = new();

    public void OnUpdate(in SystemContext context) => RazorRuntime.Build(context, events.Read());

    public void OnDestroy(in SystemContext context) => RazorRuntime.Release();
}

/// <summary>
/// Paints every mounted Razor component whose type overrides <c>OnPaint</c> once a frame, in <c>Fenrir.UI.Paint</c>,
/// into its root element (ADR-0068).
/// </summary>
[StageSystem(UIStages.Paint)]
public sealed class RazorPaintDispatcher : ISystem
{
    public void OnUpdate(in SystemContext context) => RazorRuntime.Paint(context);
}

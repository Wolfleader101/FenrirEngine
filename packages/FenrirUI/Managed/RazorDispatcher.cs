// The Razor dispatcher (ADR-0068), shipped as source: Fenrir.UI.Razor.props compiles it into every script project that
// has .razor files, because only a script assembly's own systems are scheduled. It builds the mounted Razor components
// in FenrirUI's build stage, and lets go of them when the script generation is released.

using Fenrir.Scripting;
using Fenrir.UI;
using Fenrir.UI.Razor;

namespace Fenrir.UI.Razor.Generated;

/// <summary>Runs every mounted Razor component once a frame, in <c>Fenrir.UI.Build</c>.</summary>
[StageSystem(UIStages.Build)]
public sealed class RazorDispatcher : ISystem
{
    public void OnUpdate(in SystemContext context) => RazorRuntime.Build(context);

    public void OnDestroy(in SystemContext context) => RazorRuntime.Release();
}

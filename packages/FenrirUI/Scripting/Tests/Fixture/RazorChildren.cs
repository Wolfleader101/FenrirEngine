using Fenrir.Input;
using Fenrir.Scripting;
using Fenrir.UI;
using Fenrir.UI.Razor;

namespace UIScriptFixture;

// Mounts a Dashboard, from OnCreate, on the root C++ made on window 1's overlay at DashboardLayer before the script
// loaded (Razor.cpp's DashboardScriptingApp). Other tests make no root at that layer, so it mounts nothing there. Its
// OnUpdate, in the Update phase, sets the level the test asked for and calls StateHasChanged, as game code would.
[System(Phase.Update)]
public sealed class DashboardMounter : ISystem
{
    public const int DashboardLayer = 9;

    public void OnCreate(in SystemContext context)
    {
        UIRoot root = UIRoot.Find(new WindowId { Value = 1 }, DashboardLayer);
        if (root.Value == 0)
            return;
        DashboardProbe.Mounted = root.Mount<Dashboard>(root.GetRootElement());
    }

    public void OnUpdate(in SystemContext context)
    {
        if (DashboardProbe.PendingLevel is not int level || DashboardProbe.Mounted.Instance is not Dashboard dashboard)
            return;
        DashboardProbe.PendingLevel = null;
        dashboard.Level = level;
        dashboard.StateHasChanged();
    }
}

public static class DashboardProbe
{
    internal static ComponentRef<Dashboard> Mounted;
    internal static int? PendingLevel;

    // Has the next Update set the Dashboard's level and call StateHasChanged.
    public static void ShowFromUpdate(int level) => PendingLevel = level;

    public static ulong Host() => Ids.Pack(Mounted.Host);

    // The Gauge the Dashboard's @ref captured holds the level it was last given; -1 when none was captured.
    public static int ShownValue() => Mounted.Instance?.Shown?.Value ?? -1;
}

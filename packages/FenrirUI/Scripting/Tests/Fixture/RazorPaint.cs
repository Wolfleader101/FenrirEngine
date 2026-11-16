using Fenrir.UI;
using Fenrir.UI.Razor;

namespace UIScriptFixture;

// What RazorPaint.cpp's paint override tests call: a Needle, its host a red square of 100 by 100, and a Quiet beside
// it, mounted on a root that lays out (a capturing target's).
public static class NeedleProbe
{
    private static ComponentRef<Needle> needle;
    private static ComponentRef<Quiet> quiet;

    // Mounts both under the root's element; they build in the next frame's build stage and paint in its paint stage.
    public static void Mount(uint root)
    {
        UIRoot ui = Ids.Root(root);
        needle = ui.Mount<Needle>(ui.GetRootElement());
        ui.SetStyle(needle.Host, "width", "100px");
        ui.SetStyle(needle.Host, "height", "100px");
        ui.SetStyle(needle.Host, "background-color", "#f00");
        quiet = ui.Mount<Quiet>(ui.GetRootElement());
    }

    public static ulong Host() => Ids.Pack(needle.Host);

    public static int NeedlePaints() => needle.Instance!.Paints;

    public static int PipPaints() => needle.Instance!.PipRef?.Paints ?? -1;

    // The top of the box the Pip was handed, the last time it painted.
    public static float PipBorderY() => needle.Instance!.PipRef?.BorderY ?? -1;

    public static void UnmountNeedle() => needle.Unmount(RemoveMode.Immediate);
}

// What RazorPaint.cpp's document tests call: a Credits placing the document at a path the test wrote.
public static class CreditsProbe
{
    private static ComponentRef<Credits> mounted;

    private static Credits Instance => mounted.Instance!;

    public static ulong Mount(uint root, string src)
    {
        UIRoot ui = Ids.Root(root);
        mounted = ui.Mount<Credits>(ui.GetRootElement(), init: credits => credits.Src = src);
        return Ids.Pack(mounted.Host);
    }

    public static void SetTitle(string title)
    {
        Instance.Title = title;
        Instance.StateHasChanged();
    }

    public static void SetSrc(string src)
    {
        Instance.Src = src;
        Instance.StateHasChanged();
    }

    public static bool Unmount() => mounted.Unmount(RemoveMode.Immediate);
}

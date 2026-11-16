using Fenrir.UI;
using Fenrir.UI.Razor;

namespace UIScriptFixture;

// Hot's sheet: the file RazorSheets.cpp writes and rewrites, at a path the fixture's build fixes (HotSheet.Path, from
// the project file), and the text it loads when that file is not there.
[ComponentStylesheet(HotSheet.Path, ".title { color: #ff0000; }")]
public partial class Hot
{
}

// What RazorSheets.cpp's scoped stylesheet tests call: Razor components with sheets of their own, mounted on a root that
// lays out (a capturing target's).
public static class SheetsProbe
{
    // Alpha, Beta, Outer and Inline, side by side under the root's element; they build in the next frame.
    public static void Mount(uint root)
    {
        UIRoot ui = Ids.Root(root);
        ui.Mount<Alpha>(ui.GetRootElement());
        ui.Mount<Fenrir.UIScriptFixture.Beta>(ui.GetRootElement());
        ui.Mount<Outer>(ui.GetRootElement());
        ui.Mount<Inline>(ui.GetRootElement());
    }

    // Alpha's host, which carries its readable class.
    public static ulong MountAlpha(uint root)
    {
        UIRoot ui = Ids.Root(root);
        return Ids.Pack(ui.Mount<Alpha>(ui.GetRootElement()).Host);
    }

    public static void MountHot(uint root)
    {
        UIRoot ui = Ids.Root(root);
        ui.Mount<Hot>(ui.GetRootElement());
    }
}

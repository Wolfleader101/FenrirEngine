using Fenrir.Input;
using Fenrir.Scripting;
using Fenrir.UI;
using Fenrir.UI.Razor;

namespace UIScriptFixture;

// Mounts a Scoreboard as a game would, from OnCreate, on the root C++ made on window 1's overlay at RazorLayer before
// the script loaded (Razor.cpp's RazorScriptingApp). Every generation mounts its own: after a reload, the new one's
// OnCreate mounts again. Other tests make no root at that layer before the script loads, so it mounts nothing there.
[System(Phase.Update)]
public sealed class RazorMounter : ISystem
{
    public const int RazorLayer = 7;

    public void OnCreate(in SystemContext context)
    {
        UIRoot root = UIRoot.Find(new WindowId { Value = 1 }, RazorLayer);
        if (root.Value == 0)
            return;
        RazorProbe.Mounted = root.Mount<Scoreboard>(root.GetRootElement(), init: board => board.Score = 42);
    }

    public void OnUpdate(in SystemContext context)
    {
    }
}

public static class RazorProbe
{
    internal static ComponentRef<Scoreboard> Mounted;

    // The mounted Scoreboard's host, packed as the tests compare it; none when nothing mounted.
    public static ulong Host() => Ids.Pack(Mounted.Host);

    // Whether this generation's Scoreboard is still mounted.
    public static bool IsMounted() => Mounted.IsMounted;
}

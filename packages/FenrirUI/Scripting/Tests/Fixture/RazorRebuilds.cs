using Fenrir.Input;
using Fenrir.Scripting;
using Fenrir.UI;
using Fenrir.UI.Razor;

namespace UIScriptFixture;

// Mounts a Ticker and a Fragile, from OnCreate, on the root C++ made on window 1's overlay at RebuildLayer before the
// script loaded (Razor.cpp's RebuildScriptingApp). Other tests make no root at that layer, so it mounts nothing there.
// Its OnUpdate, in the Update phase, applies a value the test asked for and calls StateHasChanged, as game code would.
[System(Phase.Update)]
public sealed class RebuildMounter : ISystem
{
    public const int RebuildLayer = 8;

    public void OnCreate(in SystemContext context)
    {
        UIRoot root = UIRoot.Find(new WindowId { Value = 1 }, RebuildLayer);
        if (root.Value == 0)
            return;
        RebuildProbe.Ticker = root.Mount<Ticker>(root.GetRootElement());
        RebuildProbe.Fragile = root.Mount<Fragile>(root.GetRootElement());
    }

    public void OnUpdate(in SystemContext context)
    {
        if (RebuildProbe.PendingValue is not int value || RebuildProbe.Ticker.Instance is not Ticker ticker)
            return;
        RebuildProbe.PendingValue = null;
        ticker.Value = value;
        ticker.StateHasChanged();
    }
}

public static class RebuildProbe
{
    internal static ComponentRef<Ticker> Ticker;
    internal static ComponentRef<Fragile> Fragile;
    internal static int? PendingValue;

    // Has the next Update set the Ticker's value and call StateHasChanged.
    public static void ShowFromUpdate(int value) => PendingValue = value;

    // Sets what the Ticker's BuildHash returns, with no StateHasChanged.
    public static void SetHash(int hash) => Ticker.Instance!.Hash = hash;

    public static int Builds() => Ticker.Instance!.Builds;

    public static ulong TickerHost() => Ids.Pack(Ticker.Host);

    public static bool UnmountTicker() => Ticker.Unmount();

    // Sets the Fragile's value and asks for a rebuild, which throws when told to.
    public static void RebuildFragile(int value, int fail)
    {
        Fragile fragile = Fragile.Instance!;
        fragile.Value = value;
        fragile.Throw = fail != 0;
        fragile.StateHasChanged();
    }
}

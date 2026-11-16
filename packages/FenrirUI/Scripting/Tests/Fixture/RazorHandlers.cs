using Fenrir.UI;
using Fenrir.UI.Razor;

namespace UIScriptFixture;

// What Razor.cpp's handler tests call: they mount a Mixer on a root that lays out (a capturing target's), feed input
// through FenrirInput's test feed, and read back the fields its handlers and bindings wrote.
public static class MixerProbe
{
    private static ComponentRef<Mixer> mounted;

    private static Mixer Instance => mounted.Instance!;

    // Mounts a Mixer under the root's element; it builds in the next frame's build stage. Returns its host, packed.
    public static ulong Mount(uint root)
    {
        UIRoot ui = Ids.Root(root);
        mounted = ui.Mount<Mixer>(ui.GetRootElement());
        return Ids.Pack(mounted.Host);
    }

    public static int Count() => Instance.Count;

    public static int RowClicks() => Instance.RowClicks;

    public static int BareClicks() => Instance.BareClicks;

    // The last pointer press the cell's Action<UIEvent> took: its kind (-1 for none) and position.
    public static int PressedKind() => Instance.Pressed is { } e ? (int)e.Kind : -1;

    public static float PressedX() => Instance.Pressed?.Position.X ?? -1;

    public static float PressedY() => Instance.Pressed?.Position.Y ?? -1;

    public static float Volume() => Instance.Volume;

    public static int VolumeChanges() => Instance.VolumeChanges;

    public static bool NameIs(string name) => Instance.Name == name;

    public static float Level() => Instance.Level;

    // Sets the bound fields as game code would, outside the build stage, and asks for a rebuild.
    public static void SetFields(float volume, float level)
    {
        Instance.Volume = volume;
        Instance.Level = level;
        Instance.StateHasChanged();
    }

    public static void SetName(string name)
    {
        Instance.Name = name;
        Instance.StateHasChanged();
    }
}

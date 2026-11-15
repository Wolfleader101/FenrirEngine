using Fenrir.Input;
using Fenrir.Scripting;
using Fenrir.UI;
using Fenrir.UI.Razor;

// Outside any Fenrir.* namespace, as a game's scripts are.
namespace UIDemo.Scripts;

// What the Razor settings page edits, kept by SettingsSetup across the menu's openings: the page is mounted again each
// time the menu loads its document, and reads and writes these through @bind. The engine has no audio and no subtitles;
// the values are shown and logged.
public sealed class DemoSettings
{
    public float Volume = 70;
    public bool Subtitles = true;
    public string PlayerName = "Wanderer";

    // The key bindings, in order. Each has an id of its own, the @key its row is matched by across rebuilds.
    public readonly List<KeyBinding> Bindings = [new(1, "Q", "Lean left"), new(2, "E", "Lean right")];

    // The bindings Add takes from, in turn.
    private static readonly (string Key, string Action)[] Spare =
    [
        ("C", "Crouch"), ("Space", "Jump"), ("V", "Melee"), ("G", "Grenade"), ("Tab", "Scoreboard"), ("M", "Map"),
    ];

    private int nextId = 3;
    private int nextSpare;

    public KeyBinding Add()
    {
        (string key, string action) = Spare[nextSpare++ % Spare.Length];
        var binding = new KeyBinding(nextId++, key, action);
        Bindings.Add(binding);
        return binding;
    }
}

public sealed record KeyBinding(int Id, string Key, string Action);

// Mounts Settings.razor into the pause menu's razor-settings slot (assets/ui/menu.ui) whenever the menu has one the
// page is not in: each time the menu opens, since closing removes its document, and after a hot reload of menu.ui. The
// slot is display: none in menu.css until it is given the class filled, so without scripting the menu is unchanged.
[System(Phase.Update)]
public sealed class SettingsSetup : ISystem
{
    // The menu root's window and layer (UIDemo.hpp's MenuLayer).
    private static readonly WindowId Window = new() { Value = 1 };
    private const int MenuLayer = 10;

    private readonly DemoSettings settings = new();
    private ComponentRef<Settings> page;

    public void OnUpdate(in SystemContext context)
    {
        UIRoot menu = UIRoot.Find(Window, MenuLayer);
        if (menu.Value == 0)
            return;
        ElementId slot = menu.FindById("razor-settings");
        if (!menu.IsAlive(slot))
            return;
        if (page.IsMounted && menu.IsAlive(page.Host) && menu.GetParent(page.Host).Equals(slot))
            return;
        // The page's host went with the last menu's document: let it go before mounting into the new slot.
        page.Unmount(RemoveMode.Immediate);
        menu.AddClass(slot, "filled");
        page = menu.Mount<Settings>(slot, init: p => p.State = settings);
        context.Log.Info("UIDemo: the Razor settings page is in the pause menu");
    }
}

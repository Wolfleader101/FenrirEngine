using Fenrir.Input;
using Fenrir.Scripting;
using Fenrir.UI;

// Outside any Fenrir.* namespace, as a game's scripts are.
namespace UIDemo.Scripts;

// Reads the pause menu's clicks through EventReader<UIEvent> (ADR-0065), the queue C++'s Menu reads too, and logs each
// by the id of the element clicked or the nearest ancestor that has one: resume, settings, quit, back, and the
// settings' controls.
[System(Phase.Update)]
public sealed class MenuClicks : ISystem
{
    // The menu root's layer (UIDemo.hpp's MenuLayer).
    private const int MenuLayer = 10;

    private readonly EventReader<UIEvent> events = new();

    public void OnUpdate(in SystemContext context)
    {
        UIRoot menu = UIRoot.Find(new WindowId { Value = 1 }, MenuLayer);
        foreach (UIEvent e in events.Read())
        {
            if (e.Kind != UIEventKind.Click || menu.Value == 0 || e.Root.Value != menu.Value)
                continue;
            context.Log.Info($"UIDemo (C#): the menu's {IdOf(menu, e.Target)} was clicked");
        }
    }

    // The id string of `element`, or of its nearest ancestor that has one.
    private static string IdOf(UIRoot menu, ElementId element)
    {
        for (ElementId at = element; menu.IsAlive(at); at = menu.GetParent(at))
        {
            int length = menu.GetIdString(at, Span<char>.Empty);
            if (length > 0)
            {
                var id = new char[length];
                menu.GetIdString(at, id);
                return new string(id);
            }
        }
        return "(element with no id)";
    }
}

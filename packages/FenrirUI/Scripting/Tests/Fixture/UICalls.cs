using Fenrir.Math;
using Fenrir.Painter;
using Fenrir.Scripting;
using Fenrir.Text;
using Fenrir.UI;

// Outside any Fenrir.* namespace, as a game's scripts are.
namespace UIScriptFixture;

// Element ids as the tests compare them: the index in the high half, the version in the low.
internal static class Ids
{
    public static ulong Pack(ElementId element) => ((ulong)element.Index << 32) | element.Version;

    public static UIRoot Root(uint value) => new UIRoot { Value = value };

    public static string Text(int length, Func<Span<char>, int> read)
    {
        var buffer = new char[Math.Max(length, 0)];
        read(buffer);
        return new string(buffer);
    }
}

// Trees as C# builds them, from the tests outside any system, in roots the tests made and name by id. The tests build
// the same in C++ (Roots.cpp) and compare what each reads back.
public static class TreeCalls
{
    private static ElementId kept;

    // The tests' three-panel tree: a row 600 by 200 with 10 of padding holding three panels that share its width, each
    // with a margin of 5, the middle one twice as wide. Returns how many panels the row holds.
    public static int BuildThreePanels(uint value)
    {
        UIRoot root = Ids.Root(value);
        ElementId row = root.CreateElement(root.GetRootElement(), "panel", -1);
        root.SetStyle(row, "flex-direction", "row");
        root.SetStyle(row, "width", "600px");
        root.SetStyle(row, "height", "200px");
        root.SetStyle(row, "padding", "10px");
        foreach (string grow in new[] { "1", "2", "1" })
        {
            ElementId panel = root.CreateElement(row, "panel", -1);
            root.SetStyle(panel, "flex-grow", grow);
            root.SetStyle(panel, "margin", "5px");
        }
        return root.GetChildren(row, Span<ElementId>.Empty);
    }

    // The row's children.
    private static ElementId[] Panels(UIRoot root)
    {
        Span<ElementId> rows = stackalloc ElementId[1];
        root.GetChildren(root.GetRootElement(), rows);
        var panels = new ElementId[root.GetChildren(rows[0], Span<ElementId>.Empty)];
        root.GetChildren(rows[0], panels);
        return panels;
    }

    public static int RowChildren(uint value) => Panels(Ids.Root(value)).Length;

    // The border box of panel `panel`: its x, y, width or height by `which`.
    public static float PanelBox(uint value, int panel, int which)
    {
        UIRoot root = Ids.Root(value);
        Rect2D box = root.GetBoxes(Panels(root)[panel]).Border;
        return which switch { 0 => box.X, 1 => box.Y, 2 => box.Width, _ => box.Height };
    }

    // Loads the document at `path` under the root element: whether it loaded with no errors and made an element.
    public static bool LoadMenu(uint value, string path)
    {
        UIRoot root = Ids.Root(value);
        return root.LoadDocument(root.GetRootElement(), path, out ElementId first) && root.IsAlive(first);
    }

    public static ulong Find(uint value, string id) => Ids.Pack(Ids.Root(value).FindById(id));

    // Whether the element with id string `id` has the tag and text given.
    public static bool TagAndTextAre(uint value, string id, string tag, string text)
    {
        UIRoot root = Ids.Root(value);
        ElementId element = root.FindById(id);
        string readTag = Ids.Text(root.GetTag(element, Span<char>.Empty), buffer => root.GetTag(element, buffer));
        string readText = Ids.Text(root.GetText(element, Span<char>.Empty), buffer => root.GetText(element, buffer));
        return readTag == tag && readText == text;
    }

    // Keeps the element with id string `id`, for StaleCallsThatSucceeded: 1 when there is one.
    public static int Keep(uint value, string id)
    {
        kept = Ids.Root(value).FindById(id);
        return Ids.Root(value).IsAlive(kept) ? 1 : 0;
    }

    // Calls every element native on the kept element, now stale, and counts those that say they did something.
    public static int StaleCallsThatSucceeded(uint value)
    {
        UIRoot root = Ids.Root(value);
        ElementId parent = root.GetRootElement();
        int succeeded = 0;
        void Count(bool result) => succeeded += result ? 1 : 0;
        Count(root.IsAlive(kept));
        Count(root.CreateElement(kept, "panel", -1).Index != uint.MaxValue);
        Count(root.Remove(kept, RemoveMode.Immediate));
        Count(root.Reparent(kept, parent, -1));
        Count(root.Reorder(kept, 0));
        Count(root.GetParent(kept).Index != uint.MaxValue);
        Count(root.GetChildren(kept, Span<ElementId>.Empty) != 0);
        Count(root.GetTag(kept, Span<char>.Empty) != 0);
        Count(root.GetIdString(kept, Span<char>.Empty) != 0);
        Count(root.SetIdString(kept, "again"));
        Count(root.AddClass(kept, "a"));
        Count(root.RemoveClass(kept, "a"));
        Count(root.ToggleClass(kept, "a"));
        Count(root.HasClass(kept, "a"));
        Count(root.SetStyle(kept, "width", "10px"));
        Count(root.SetText(kept, "text"));
        Count(root.GetText(kept, Span<char>.Empty) != 0);
        Count(root.SetAttribute(kept, "value", "1"));
        Count(root.GetValue(kept, out _));
        Count(root.SetValue(kept, 1));
        Count(root.GetEntryText(kept, Span<char>.Empty) != 0);
        Count(root.SetEntryText(kept, "text"));
        Count(root.GetBoxes(kept).Border.Width != 0);
        Count(root.SetFocusable(kept, true));
        Count(root.Focus(kept));
        Count(root.LoadDocument(kept, "missing.ui", out _));
        Count(root.PaintInto(kept).Value != 0);
        return succeeded;
    }

    public static bool DestroyRoot(uint value) => Ids.Root(value).Destroy();

    // An element made under a root's element: none once the root is destroyed.
    public static ulong CreateUnderRoot(uint value)
    {
        UIRoot root = Ids.Root(value);
        return Ids.Pack(root.CreateElement(root.GetRootElement(), "panel", -1));
    }
}

// What C# reads of the UI's clicks, in an Update system, for the tests to compare with C++.
[System(Phase.Update)]
public sealed class ClickRecorder : ISystem
{
    private readonly EventReader<UIEvent> events = new();

    public void OnUpdate(in SystemContext context)
    {
        foreach (UIEvent e in events.Read())
        {
            if (e.Kind == UIEventKind.Click)
                Clicks.Add(e);
        }
    }
}

public static class Clicks
{
    private static readonly List<UIEvent> clicks = new();

    internal static void Add(UIEvent e) => clicks.Add(e);

    public static void Clear() => clicks.Clear();

    public static int Count() => clicks.Count;

    public static ulong LastTarget() => Ids.Pack(clicks[^1].Target);

    public static uint LastRoot() => clicks[^1].Root.Value;
}

// A paint stage system that paints a green square into the armed element, as UIDemo's speedometer paints its gauge.
[StageSystem(UIStages.Paint)]
public sealed class SquarePainter : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
        if (!PaintProbe.Armed)
            return;
        UIRoot root = Ids.Root(PaintProbe.Root);
        ElementId element = root.FindById(PaintProbe.Id);
        PainterId painter = root.PaintInto(element);
        if (painter.Value == 0)
            return;
        Rect2D box = root.GetBoxes(element).Border;
        Painter.DrawRect(painter, new Rect2D { X = box.X + 10, Y = box.Y + 10, Width = 20, Height = 20 },
                         new Paint { Colour = new Colour(0, 1, 0, 1) });
        ++PaintProbe.Paints;
    }
}

public static class PaintProbe
{
    internal static uint Root;
    internal static string Id = "";
    internal static int Paints;
    internal static bool Armed;

    // Arms the paint stage system on the element with id string `id` in root `root`.
    public static void Arm(uint root, string id)
    {
        Root = root;
        Id = id;
        Paints = 0;
        Armed = true;
    }

    public static void Disarm() => Armed = false;

    // How many frames the stage system was given a painter and painted.
    public static int Painted() => Paints;

    // PaintInto called outside the stage: its painter id, which names none.
    public static ulong PainterOutsideStage(uint root, string id)
    {
        UIRoot ui = Ids.Root(root);
        return ui.PaintInto(ui.FindById(id)).Value;
    }
}

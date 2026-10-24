using Fenrir.DebugDraw;
using Fenrir.Math;
using Fenrir.Scripting;

// Outside any Fenrir.* namespace, as a game's scripts are, so `DebugDraw` names the class and not the namespace.
namespace DebugDrawScriptFixture;

// Every DebugDraw shape as C# calls it, from the tests outside any system. The tests make the same calls in C++, with the
// same values (Shapes.cpp's CppShape), and compare what each tessellates to.
public static class ShapeCalls
{
    public const int Count = 22;

    private static readonly Colour Paint = new(0.25f, 0.5f, 0.75f, 0.5f);
    // A quarter turn about +Y, and a turn about an oblique axis: (x, y, z, w).
    private static readonly Quaternion Turn = new(0.0f, 0.38268343f, 0.0f, 0.9238795f);
    private static readonly Quaternion Tilt = new(0.18257418f, 0.36514837f, 0.0f, 0.9128709f);

    public static int Shapes() => Count;

    // Draws shape `index`, with every shape option changed from its default (group "Shapes", thickness 3, on top and 8
    // segments) when `withOptions` is non-zero, else with none.
    public static void Draw(int index, int withOptions)
    {
        ShapeOptions? options = withOptions != 0
            ? new ShapeOptions { Group = "Shapes", Thickness = 3.0f, Depth = DepthMode.OnTop, Segments = 8 }
            : null;
        switch (index)
        {
            case 0: DebugDraw.Line(new Vector3(1, 2, 3), new Vector3(4, 5, 6), Paint, options); break;
            case 1: DebugDraw.Ray(new Vector3(0, 1, 0), new Vector3(2, 0, 1), Paint, options); break;
            case 2: DebugDraw.Arrow(new Vector3(0, 0, 0), new Vector3(0, 2, 0), 0.5f, Paint, options); break;
            case 3:
                DebugDraw.Polyline([new(0, 0, 0), new(1, 0, 0), new(1, 1, 0), new(0, 1, 1)], true, Paint, options);
                break;
            case 4: DebugDraw.Point(new Vector3(1, 1, 1), 6.0f, Paint, options); break;
            case 5: DebugDraw.Box(new Bounds(new Vector3(-1, -2, -3), new Vector3(1, 2, 3)), Paint, options); break;
            case 6: DebugDraw.Box(new Vector3(1, 2, 3), new Vector3(0.5f, 1, 1.5f), Turn, Paint, options); break;
            case 7: DebugDraw.Sphere(new Vector3(1, 2, 3), 1.5f, Paint, options); break;
            case 8: DebugDraw.Capsule(new Vector3(0, 0, 0), new Vector3(0, 2, 0), 0.5f, Paint, options); break;
            case 9: DebugDraw.Cylinder(new Vector3(0, 0, 0), new Vector3(1, 2, 0), 0.5f, Paint, options); break;
            case 10: DebugDraw.Cone(new Vector3(0, 2, 0), new Vector3(0, 0, 0), 0.75f, Paint, options); break;
            case 11: DebugDraw.Circle(new Vector3(1, 0, 0), new Vector3(0, 1, 0), 2.0f, Paint, options); break;
            case 12: DebugDraw.Arc(new Vector3(0, 0, 0), new Vector3(0, 0, 1), new Vector3(1, 0, 0), 2.0f, Paint, options); break;
            case 13: DebugDraw.Plane(new Vector3(0, 1, 0), new Vector3(0, 0.6f, 0.8f), 1.5f, Paint, options); break;
            case 14: DebugDraw.Grid(new Vector3(0, 0, 0), Tilt, 3, 0.5f, Paint, options); break;
            case 15: DebugDraw.Axes(Placed, 2.0f, options); break;
            case 16: DebugDraw.Frustum(Perspective, Paint, options, 0.25f); break;
            case 17: DebugDraw.SolidBox(new Bounds(new Vector3(-1, -2, -3), new Vector3(1, 2, 3)), Paint, options); break;
            case 18: DebugDraw.SolidBox(new Vector3(1, 2, 3), new Vector3(0.5f, 1, 1.5f), Turn, Paint, options); break;
            case 19: DebugDraw.SolidSphere(new Vector3(1, 2, 3), 1.5f, Paint, options); break;
            case 20: DebugDraw.SolidCapsule(new Vector3(0, 0, 0), new Vector3(0, 2, 0), 0.5f, Paint, options); break;
            case 21:
                // Two triangles and a remainder of one vertex, which is ignored.
                DebugDraw.Triangles([new(0, 0, 0), new(1, 0, 0), new(0, 1, 0), new(1, 0, 0), new(1, 1, 0), new(0, 1, 0),
                                     new(5, 5, 5)], Paint, options);
                break;
        }
    }

    // A frame turned and moved to (1, 2, 3), by columns.
    private static Matrix4x4 Placed => new(new Vector4(0, 0, -1, 0), new Vector4(0, 1, 0, 0), new Vector4(1, 0, 0, 0),
                                           new Vector4(1, 2, 3, 1));

    // A reversed-Z perspective projection with a focal length of 2 and its near plane at 0.5, by columns.
    private static Matrix4x4 Perspective => new(new Vector4(2, 0, 0, 0), new Vector4(0, 2, 0, 0), new Vector4(0, 0, 0, -1),
                                                new Vector4(0, 0, 0.5f, 0));
}

// Text as C# draws it, from the tests outside any system; Text.cpp makes the same calls in C++.
public static class TextCalls
{
    // Screen text, two plain messages and two keyed ones (the second replacing the first), and world text, with Latin-1
    // and a character outside it.
    public static void Draw()
    {
        DebugDraw.ScreenText(new Vector2(16, 120), "Screen text: café", Colour.White, new TextOptions { Size = 20 });
        DebugDraw.Message("first", Colour.Green);
        DebugDraw.Message(7, "keyed one", Colour.Yellow);
        DebugDraw.Message("second", Colour.Cyan, new TextOptions { Group = "Messages" });
        DebugDraw.Message(7, "keyed two", Colour.Red);
        DebugDraw.WorldText(new Vector3(1, 2, 3), "label Ærø €", Colour.Magenta, new TextOptions { Size = 14 });
    }
}

// Durations, groups and relativeTo from C#, called by the tests outside any system.
public static class ItemCalls
{
    public static void LineFor(float duration) =>
        DebugDraw.Line(Vector3.Zero, Vector3.One, Colour.Red, new ShapeOptions { Duration = duration });

    public static void LineForever() => LineFor(DebugDraw.Forever);

    public static void LineIn(int group) =>
        DebugDraw.Line(Vector3.Zero, Vector3.One, Colour.Red, new ShapeOptions { Group = GroupName(group) });

    public static void Hide(int group) => DebugDraw.HideGroup(GroupName(group));

    public static void Show(int group) => DebugDraw.ShowGroup(GroupName(group));

    public static bool IsShown(int group) => DebugDraw.IsGroupShown(GroupName(group));

    public static void Clear(int group) => DebugDraw.Clear(GroupName(group));

    public static void ClearAll() => DebugDraw.Clear();

    // A line from the entity's origin along its X axis, and world text one up from it, both kept forever and relative
    // to it.
    public static void RelativeTo(uint entity)
    {
        DebugDraw.Line(Vector3.Zero, new Vector3(1, 0, 0), Colour.Red,
                       new ShapeOptions { Duration = DebugDraw.Forever, RelativeTo = new Entity(entity) });
        DebugDraw.WorldText(new Vector3(0, 1, 0), "mover", Colour.White,
                            new TextOptions { Duration = DebugDraw.Forever, RelativeTo = new Entity(entity) });
    }

    // Group 0 is "Default", 1 "Probe" and 2 "Other".
    private static string GroupName(int group) => group switch { 0 => "Default", 1 => "Probe", _ => "Other" };
}

// A Tick system that, once armed, draws one line with duration 0 on its next tick.
[System(Phase.Tick)]
public sealed class TickDrawer : ISystem
{
    private static bool armed;
    private static int ticks;

    public static void Arm()
    {
        armed = true;
        ticks = 0;
    }

    public static int Ticks() => ticks;

    public void OnUpdate(in SystemContext context)
    {
        ++ticks;
        if (!armed)
            return;
        armed = false;
        DebugDraw.Line(Vector3.Zero, Vector3.One, Colour.Red);
    }
}

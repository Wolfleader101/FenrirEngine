using Fenrir.Math;
using Fenrir.Scripting;

namespace Fenrir.DebugDraw;

/// <summary>
/// Debug shapes and text from C#, drawn into the active scene's debug drawing: FenrirDebugDraw's <c>DebugDraw</c> scene
/// resource, call for call (ADR-0025; its C++ documentation says what each shape is). It is a facade of native functions,
/// not a mirror, so durations, groups and tessellation follow C++'s rules exactly (ADR-0029).
/// </summary>
/// <remarks>
/// Inside a <c>Fenrir.*</c> namespace, <c>DebugDraw</c> names this namespace: write <c>Fenrir.DebugDraw.DebugDraw</c>
/// there, or use an alias. Every call takes a <see cref="Colour"/> and, optionally, <see cref="ShapeOptions"/> or
/// <see cref="TextOptions"/>; leaving them out is the same as passing new ones. Duration 0, the default, draws once: until
/// the next tick from a <see cref="Phase.Tick"/> system, otherwise until the end of this frame's drawing.
/// <para>
/// Nothing here throws. With no debug drawing in the active scene (FenrirDebugDraw not installed) or a
/// <see cref="ShapeOptions.RelativeTo"/> entity that is not alive, a call draws nothing, and
/// <see cref="IsGroupShown"/> returns false. In a debug build of the engine a warning is logged once for each place in
/// the engine's native code that noticed and each reason (not once per C# call site).
/// </para>
/// </remarks>
public static unsafe class DebugDraw
{
    /// <summary>The duration that keeps an item until <see cref="Clear()"/> or <see cref="Clear(ReadOnlySpan{char})"/>.</summary>
    public const float Forever = float.PositiveInfinity;

    // Wire shapes.

    /// <summary>A line from <paramref name="from"/> to <paramref name="to"/>.</summary>
    public static void Line(Vector3 from, Vector3 to, Colour colour, ShapeOptions? options = null)
    {
        NativeShapeOptions native = Shape(options, out string group);
        DebugDrawNative.Line(in from, in to, in colour, in native, group);
    }

    /// <summary>A line from <paramref name="origin"/> along <paramref name="direction"/>, as long as it is.</summary>
    public static void Ray(Vector3 origin, Vector3 direction, Colour colour, ShapeOptions? options = null)
    {
        NativeShapeOptions native = Shape(options, out string group);
        DebugDrawNative.Ray(in origin, in direction, in colour, in native, group);
    }

    /// <summary>A line from <paramref name="from"/> to <paramref name="to"/> with a head <paramref name="headLength"/> long at <paramref name="to"/>.</summary>
    public static void Arrow(Vector3 from, Vector3 to, float headLength, Colour colour, ShapeOptions? options = null)
    {
        NativeShapeOptions native = Shape(options, out string group);
        DebugDrawNative.Arrow(in from, in to, headLength, in colour, in native, group);
    }

    /// <summary>Lines joining <paramref name="points"/> in order, and the last back to the first when <paramref name="closed"/>.</summary>
    public static void Polyline(ReadOnlySpan<Vector3> points, bool closed, Colour colour, ShapeOptions? options = null)
    {
        NativeShapeOptions native = Shape(options, out string group);
        fixed (Vector3* first = points)
            DebugDrawNative.Polyline(first, points.Length, closed, in colour, in native, group);
    }

    /// <summary>A dot at <paramref name="position"/>, <paramref name="pixelSize"/> pixels across whatever its distance.</summary>
    public static void Point(Vector3 position, float pixelSize, Colour colour, ShapeOptions? options = null)
    {
        NativeShapeOptions native = Shape(options, out string group);
        DebugDrawNative.Point(in position, pixelSize, in colour, in native, group);
    }

    /// <summary>The twelve edges of an axis-aligned box.</summary>
    public static void Box(Bounds box, Colour colour, ShapeOptions? options = null)
    {
        NativeShapeOptions native = Shape(options, out string group);
        DebugDrawNative.Box(in box, in colour, in native, group);
    }

    /// <summary>The twelve edges of a box <paramref name="halfExtents"/> from <paramref name="centre"/> along its axes, turned by <paramref name="rotation"/>.</summary>
    public static void Box(Vector3 centre, Vector3 halfExtents, Quaternion rotation, Colour colour, ShapeOptions? options = null)
    {
        NativeShapeOptions native = Shape(options, out string group);
        DebugDrawNative.OrientedBox(in centre, in halfExtents, in rotation, in colour, in native, group);
    }

    /// <summary>Three circles of a sphere, square to the world's X, Y and Z axes.</summary>
    public static void Sphere(Vector3 centre, float radius, Colour colour, ShapeOptions? options = null)
    {
        NativeShapeOptions native = Shape(options, out string group);
        DebugDrawNative.Sphere(in centre, radius, in colour, in native, group);
    }

    /// <summary>A capsule round the segment from <paramref name="from"/> to <paramref name="to"/>.</summary>
    public static void Capsule(Vector3 from, Vector3 to, float radius, Colour colour, ShapeOptions? options = null)
    {
        NativeShapeOptions native = Shape(options, out string group);
        DebugDrawNative.Capsule(in from, in to, radius, in colour, in native, group);
    }

    /// <summary>A cylinder round the segment from <paramref name="from"/> to <paramref name="to"/>.</summary>
    public static void Cylinder(Vector3 from, Vector3 to, float radius, Colour colour, ShapeOptions? options = null)
    {
        NativeShapeOptions native = Shape(options, out string group);
        DebugDrawNative.Cylinder(in from, in to, radius, in colour, in native, group);
    }

    /// <summary>A cone from <paramref name="apex"/> to a circular base round <paramref name="baseCentre"/>.</summary>
    public static void Cone(Vector3 apex, Vector3 baseCentre, float radius, Colour colour, ShapeOptions? options = null)
    {
        NativeShapeOptions native = Shape(options, out string group);
        DebugDrawNative.Cone(in apex, in baseCentre, radius, in colour, in native, group);
    }

    /// <summary>A circle round <paramref name="centre"/> in the plane square to <paramref name="normal"/>.</summary>
    public static void Circle(Vector3 centre, Vector3 normal, float radius, Colour colour, ShapeOptions? options = null)
    {
        NativeShapeOptions native = Shape(options, out string group);
        DebugDrawNative.Circle(in centre, in normal, radius, in colour, in native, group);
    }

    /// <summary>
    /// Part of a circle round <paramref name="centre"/>: from <c>centre + start</c>, turning <paramref name="angle"/> radians
    /// about <paramref name="normal"/> (counter-clockwise looking down it).
    /// </summary>
    public static void Arc(Vector3 centre, Vector3 normal, Vector3 start, float angle, Colour colour, ShapeOptions? options = null)
    {
        NativeShapeOptions native = Shape(options, out string group);
        DebugDrawNative.Arc(in centre, in normal, in start, angle, in colour, in native, group);
    }

    /// <summary>A square <paramref name="halfSize"/> from <paramref name="point"/> in the plane square to <paramref name="normal"/>, and its normal.</summary>
    public static void Plane(Vector3 point, Vector3 normal, float halfSize, Colour colour, ShapeOptions? options = null)
    {
        NativeShapeOptions native = Shape(options, out string group);
        DebugDrawNative.Plane(in point, in normal, halfSize, in colour, in native, group);
    }

    /// <summary>
    /// A grid of <paramref name="cells"/> by <paramref name="cells"/> squares, <paramref name="cellSize"/> across, centred on
    /// <paramref name="centre"/> in the plane of its local X and Z axes turned by <paramref name="rotation"/>.
    /// </summary>
    public static void Grid(Vector3 centre, Quaternion rotation, int cells, float cellSize, Colour colour, ShapeOptions? options = null)
    {
        NativeShapeOptions native = Shape(options, out string group);
        DebugDrawNative.Grid(in centre, in rotation, cells, cellSize, in colour, in native, group);
    }

    /// <summary>The frame of <paramref name="transform"/>: its X, Y and Z axes, <paramref name="length"/> long, in red, green and blue.</summary>
    public static void Axes(Matrix4x4 transform, float length, ShapeOptions? options = null)
    {
        NativeShapeOptions native = Shape(options, out string group);
        DebugDrawNative.Axes(in transform, length, in native, group);
    }

    /// <summary>
    /// The twelve edges of the volume <paramref name="viewProjection"/> sees, between its near plane and depth
    /// <paramref name="farDepth"/> in reversed-Z [0, 1] depth (ADR-0026).
    /// </summary>
    public static void Frustum(Matrix4x4 viewProjection, Colour colour, ShapeOptions? options = null, float farDepth = 0.0f)
    {
        NativeShapeOptions native = Shape(options, out string group);
        DebugDrawNative.Frustum(in viewProjection, in colour, in native, group, farDepth);
    }

    // Solid shapes: triangles with the colour's alpha, depth tested without writing depth and not sorted.

    /// <summary>A solid axis-aligned box.</summary>
    public static void SolidBox(Bounds box, Colour colour, ShapeOptions? options = null)
    {
        NativeShapeOptions native = Shape(options, out string group);
        DebugDrawNative.SolidBox(in box, in colour, in native, group);
    }

    /// <summary>A solid box <paramref name="halfExtents"/> from <paramref name="centre"/> along its axes, turned by <paramref name="rotation"/>.</summary>
    public static void SolidBox(Vector3 centre, Vector3 halfExtents, Quaternion rotation, Colour colour, ShapeOptions? options = null)
    {
        NativeShapeOptions native = Shape(options, out string group);
        DebugDrawNative.SolidOrientedBox(in centre, in halfExtents, in rotation, in colour, in native, group);
    }

    /// <summary>A solid sphere.</summary>
    public static void SolidSphere(Vector3 centre, float radius, Colour colour, ShapeOptions? options = null)
    {
        NativeShapeOptions native = Shape(options, out string group);
        DebugDrawNative.SolidSphere(in centre, radius, in colour, in native, group);
    }

    /// <summary>A solid capsule round the segment from <paramref name="from"/> to <paramref name="to"/>.</summary>
    public static void SolidCapsule(Vector3 from, Vector3 to, float radius, Colour colour, ShapeOptions? options = null)
    {
        NativeShapeOptions native = Shape(options, out string group);
        DebugDrawNative.SolidCapsule(in from, in to, radius, in colour, in native, group);
    }

    /// <summary>The caller's triangles: every three of <paramref name="vertices"/> are one. A remainder of one or two is ignored.</summary>
    public static void Triangles(ReadOnlySpan<Vector3> vertices, Colour colour, ShapeOptions? options = null)
    {
        NativeShapeOptions native = Shape(options, out string group);
        fixed (Vector3* first = vertices)
            DebugDrawNative.Triangles(first, vertices.Length, in colour, in native, group);
    }

    // Text.

    /// <summary>Text at a pixel position in each window showing the scene, from its top-left.</summary>
    public static void ScreenText(Vector2 position, ReadOnlySpan<char> text, Colour colour, TextOptions? options = null)
    {
        NativeTextOptions native = Text(options, out string group);
        DebugDrawNative.ScreenText(in position, text, in colour, in native, group);
    }

    /// <summary>An on-screen message: stacked from the window's top-left, newest first.</summary>
    public static void Message(ReadOnlySpan<char> text, Colour colour, TextOptions? options = null)
    {
        NativeTextOptions native = Text(options, out string group);
        DebugDrawNative.Message(text, in colour, in native, group);
    }

    /// <summary>An on-screen message under <paramref name="key"/>: it replaces the message under the same key, in its place.</summary>
    public static void Message(long key, ReadOnlySpan<char> text, Colour colour, TextOptions? options = null)
    {
        NativeTextOptions native = Text(options, out string group);
        DebugDrawNative.KeyedMessage(key, text, in colour, in native, group);
    }

    /// <summary>Text at a point in the world, facing the screen at a constant pixel size, never hidden.</summary>
    public static void WorldText(Vector3 position, ReadOnlySpan<char> text, Colour colour, TextOptions? options = null)
    {
        NativeTextOptions native = Text(options, out string group);
        DebugDrawNative.WorldText(in position, text, in colour, in native, group);
    }

    // Groups: every group is shown until hidden; a hidden group's items still record and expire.

    /// <summary>Shows the group's items.</summary>
    public static void ShowGroup(ReadOnlySpan<char> group) => DebugDrawNative.ShowGroup(group);

    /// <summary>Hides the group's items.</summary>
    public static void HideGroup(ReadOnlySpan<char> group) => DebugDrawNative.HideGroup(group);

    /// <summary>Whether the group's items are shown; false with no debug drawing in the active scene.</summary>
    public static bool IsGroupShown(ReadOnlySpan<char> group) => DebugDrawNative.IsGroupShown(group);

    /// <summary>Drops every item.</summary>
    public static void Clear() => DebugDrawNative.Clear();

    /// <summary>Drops the group's items.</summary>
    public static void Clear(ReadOnlySpan<char> group) => DebugDrawNative.ClearGroup(group);

    private static NativeShapeOptions Shape(ShapeOptions? options, out string group)
    {
        ShapeOptions chosen = options ?? new ShapeOptions();
        group = chosen.Group ?? "Default";
        return new NativeShapeOptions(in chosen);
    }

    private static NativeTextOptions Text(TextOptions? options, out string group)
    {
        TextOptions chosen = options ?? new TextOptions();
        group = chosen.Group ?? "Default";
        return new NativeTextOptions(in chosen);
    }
}

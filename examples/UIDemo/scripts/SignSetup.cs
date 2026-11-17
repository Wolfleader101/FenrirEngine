using Fenrir.Input;
using Fenrir.Math;
using Fenrir.Renderer;
using Fenrir.Scripting;
using Fenrir.UI;

// Outside any Fenrir.* namespace, as a game's scripts are.
namespace UIDemo.Scripts;

// A small sign beside the name plate, made wholly from C#: an entity with a WorldQuad2D, a world root on it
// (UIRoot.CreateWorldRoot) holding assets/ui/sign.ui under world.css, and each frame a ray from the window's camera
// through the cursor (Renderer.ScreenPointToRay), under the light terminal's rules: none while the menu is open, while a
// window root has the pointer, or while the right mouse button turns the camera. Its button's click is logged. The root
// ends with the script generation, and the system destroys the quad's entity as it goes.
[System(Phase.Update)]
public sealed class SignSetup : ISystem
{
    private static readonly WindowId Window = new() { Value = 1 };
    // The menu root's layer (UIDemo.hpp's MenuLayer).
    private const int MenuLayer = 10;
    // Beside the name plate, which floats over the capsule at (-2.6, 2.05, 1.8): 0.6 by 0.3 metres, facing +Z towards
    // the camera's start, at the plate's 500 UI units a metre, so the root is 300 by 150 UI units.
    private static readonly Vector3 Position = new(-1.75f, 1.6f, 1.8f);
    private static readonly Vector2 Size = new(0.6f, 0.3f);
    private const float UnitsPerMetre = 500;

    private readonly EventReader<UIEvent> events = new();
    private Entity sign = Entity.None;
    private UIRoot root;

    public void OnCreate(in SystemContext context) => MakeQuad();

    // The release ends the root; the quad goes with it, so a reload does not leave one behind.
    public void OnDestroy(in SystemContext context)
    {
        if (sign.IsValid)
            sign.Destroy();
    }

    public void OnUpdate(in SystemContext context)
    {
        // The quad's components appear when the system that added them returns, so the root is made a frame later.
        if (!sign.IsValid)
        {
            MakeQuad();
            return;
        }
        if (root.Value == 0 && sign.Has<WorldQuad2D>())
            MakeRoot(context);
        if (root.Value == 0)
            return;

        if (RayThroughCursor() is { } ray)
            root.SetPointerRay(ray, Mouse.IsDown(MouseButton.Left));
        else
            root.ClearPointerRay();

        ElementId button = root.FindById("sign-button");
        foreach (UIEvent e in events.Read())
        {
            if (e.Kind == UIEventKind.Click && e.Root.Value == root.Value && e.Target.Index == button.Index &&
                e.Target.Version == button.Version)
                context.Log.Info("UIDemo (C#): the C# sign was clicked");
        }
    }

    private void MakeQuad()
    {
        sign = Entity.Create();
        sign.Add(new Transform { Position = Position });
        sign.Add(new WorldQuad2D { Size = Size, UnitsPerWorldUnit = UnitsPerMetre });
    }

    private void MakeRoot(in SystemContext context)
    {
        root = UIRoot.CreateWorldRoot(new WorldRootSettings { Entity = sign, HotReload = true });
        if (root.Value == 0)
            return;
        root.LoadStylesheet("ui/world.css", "");
        root.LoadDocument(root.GetRootElement(), "ui/sign.ui", out _);
        context.Log.Info("UIDemo: the C# sign is beside the name plate, a world root made from C#");
    }

    // The ray from the window's camera through the cursor, or none under the terminal's rules.
    private static Ray? RayThroughCursor()
    {
        UIRoot menu = UIRoot.Find(Window, MenuLayer);
        bool menuOpen = menu.Value != 0 && menu.GetChildren(menu.GetRootElement(), Span<ElementId>.Empty) > 0;
        bool pointerTaken = Resources.TryGetReadOnly<InputClaims>(out var claims) && claims.Value.For(Window).Pointer;
        if (menuOpen || pointerTaken || Mouse.IsDown(MouseButton.Right))
            return null;
        Entity camera = Renderer.CameraAt(Window, Mouse.Position);
        if (!camera.IsValid || !Renderer.ScreenPointToRay(camera, Mouse.Position, out Ray ray))
            return null;
        return ray;
    }
}

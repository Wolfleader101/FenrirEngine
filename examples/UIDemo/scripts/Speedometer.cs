using Fenrir.Input;
using Fenrir.Math;
using Fenrir.Painter;
using Fenrir.Renderer;
using Fenrir.Scripting;
using Fenrir.Text;
using Fenrir.UI;

// Outside any Fenrir.* namespace, as a game's scripts are.
namespace UIDemo.Scripts;

// The speedometer (examples/UIDemo/README.md, "The C# variant"): a second HUD element made and painted from C#. It is a
// custom element as C# makes one, by composition and painting (ADR-0061): a panel and a label made through the HUD
// root's UIRoot, and a gauge painted into the panel in the Fenrir.UI.Paint stage through the Painter facade.
internal static class Speedometer
{
    // The HUD root's window and layer (UIDemo.hpp's HudLayer), by which the script finds the root C++ made.
    public static readonly WindowId Window = new() { Value = 1 };
    public const int HudLayer = 0;

    // The element's id string, and the speed the gauge reads full at, in metres a second.
    public const string Id = "speedometer";
    public const float FullScale = 20.0f;

    // The window camera's speed, smoothed, as the last Update read it.
    public static float Speed;
}

// Finds the HUD root, makes the speedometer in it once (again after the root loses it), and reads the window camera's
// speed from how far it moved since the last frame.
[System(Phase.Update)]
public sealed class SpeedometerSetup : ISystem
{
    private Vector3? last;

    public void OnUpdate(in SystemContext context)
    {
        UIRoot hud = UIRoot.Find(Speedometer.Window, Speedometer.HudLayer);
        if (hud.Value != 0 && !hud.IsAlive(hud.FindById(Speedometer.Id)))
        {
            Make(hud);
            context.Log.Info("UIDemo: the speedometer is on the HUD, painted from C#");
        }

        float deltaTime = context.Time.DeltaTime;
        foreach (var (_, transform, camera) in context.Query<Transform, Camera>())
        {
            if (!camera.Enabled || camera.Target.Type != RenderTargetType.Window)
                continue;
            Vector3 position = transform.Position;
            if (last is { } before && deltaTime > 0)
            {
                float speed = (position - before).Length / deltaTime;
                // Eased over a few frames, so the needle does not shake with the frame time.
                Speedometer.Speed += (speed - Speedometer.Speed) * MathF.Min(1, deltaTime * 8);
            }
            last = position;
            break;
        }

        ElementId readout = hud.FindById("speedometer-readout");
        hud.SetText(readout, $"{Speedometer.Speed:0.0} m/s");
    }

    // The panel, bottom right over the ammo counter, and its readout label: inline style, so hud.css stays C++'s.
    private static void Make(UIRoot hud)
    {
        ElementId gauge = hud.CreateElement(hud.GetRootElement(), "panel", -1);
        hud.SetIdString(gauge, Speedometer.Id);
        hud.SetStyle(gauge, "position", "absolute");
        hud.SetStyle(gauge, "right", "48px");
        hud.SetStyle(gauge, "bottom", "170px");
        hud.SetStyle(gauge, "width", "180px");
        hud.SetStyle(gauge, "height", "180px");
        hud.SetStyle(gauge, "justify-content", "flex-end");
        hud.SetStyle(gauge, "align-items", "center");
        hud.SetStyle(gauge, "padding-bottom", "24px");
        hud.SetStyle(gauge, "pointer-events", "none");

        ElementId readout = hud.CreateElement(gauge, "label", -1);
        hud.SetIdString(readout, "speedometer-readout");
        hud.SetStyle(readout, "color", "#e8eef6");
        hud.SetStyle(readout, "font-size", "22px");
        hud.SetText(readout, "0.0 m/s");
    }
}

// Paints the gauge into the speedometer element, after its own (empty) box and before its readout: a dark disc, a
// track of three quarters of a turn, the speed's share of it in the accent colour, and a needle.
[StageSystem(UIStages.Paint)]
public sealed class SpeedometerPainter : ISystem
{
    private const float Start = MathF.PI * 0.75f;
    private const float Sweep = MathF.PI * 1.5f;

    public void OnUpdate(in SystemContext context)
    {
        UIRoot hud = UIRoot.Find(Speedometer.Window, Speedometer.HudLayer);
        ElementId gauge = hud.FindById(Speedometer.Id);
        PainterId painter = hud.PaintInto(gauge);
        if (painter.Value == 0)
            return;

        Rect2D box = hud.GetBoxes(gauge).Border;
        var centre = new Vector2(box.X + box.Width * 0.5f, box.Y + box.Height * 0.5f);
        float radius = MathF.Min(box.Width, box.Height) * 0.5f - 8;
        float share = Math.Clamp(Speedometer.Speed / Speedometer.FullScale, 0, 1);

        Painter.DrawCircle(painter, centre, radius + 6, new Paint { Colour = new Colour(0.03f, 0.05f, 0.08f, 0.65f) });
        Painter.DrawArc(painter, centre, radius - 6, Start, Sweep, 8, new Paint { Colour = new Colour(1, 1, 1, 0.18f) });
        if (share > 0)
        {
            Painter.DrawArc(painter, centre, radius - 6, Start, Sweep * share, 8,
                            new Paint { Colour = new Colour(0.36f, 0.78f, 1.0f, 1.0f) });
        }

        float angle = Start + Sweep * share;
        var tip = centre + new Vector2(MathF.Cos(angle), MathF.Sin(angle)) * (radius - 18);
        ReadOnlySpan<Vector2> needle = [centre, tip];
        Painter.StrokePath(painter, needle, false, new StrokeStyle { Width = 3, Cap = LineCap.Round },
                           new Paint { Colour = new Colour(1.0f, 0.55f, 0.2f, 1.0f) });
        Painter.DrawCircle(painter, centre, 6, new Paint { Colour = new Colour(1.0f, 0.55f, 0.2f, 1.0f) });
    }
}

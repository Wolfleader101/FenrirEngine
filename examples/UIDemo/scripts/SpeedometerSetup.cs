using Fenrir.Input;
using Fenrir.Math;
using Fenrir.Renderer;
using Fenrir.Scripting;
using Fenrir.UI;
using Fenrir.UI.Razor;

// Outside any Fenrir.* namespace, as a game's scripts are.
namespace UIDemo.Scripts;

// Mounts Speedometer.razor on the HUD root C++ made (UIRoot.Find on the main window at HudLayer), from OnCreate and
// again whenever the root has lost it, and gives it the window camera's speed each frame, from how far the camera moved
// since the last.
[System(Phase.Update)]
public sealed class SpeedometerSetup : ISystem
{
    // The HUD root's window and layer (UIDemo.hpp's HudLayer).
    private static readonly WindowId Window = new() { Value = 1 };
    private const int HudLayer = 0;

    private ComponentRef<Speedometer> gauge;
    private Vector3? last;
    private float speed;

    public void OnCreate(in SystemContext context) => MountIfLost(context);

    public void OnUpdate(in SystemContext context)
    {
        MountIfLost(context);

        float deltaTime = context.Time.DeltaTime;
        foreach (var (_, transform, camera) in context.Query<Transform, Camera>())
        {
            if (!camera.Enabled || camera.Target.Type != RenderTargetType.Window)
                continue;
            Vector3 position = transform.Position;
            if (last is { } before && deltaTime > 0)
            {
                float measured = (position - before).Length / deltaTime;
                // Eased over a few frames, so the needle does not shake with the frame time.
                speed += (measured - speed) * MathF.Min(1, deltaTime * 8);
            }
            last = position;
            break;
        }

        // Its BuildHash rounds this to the readout's precision, so it rebuilds only when the readout changes; it paints
        // the exact speed every frame.
        if (gauge.Instance is { } instance)
            instance.Speed = speed;
    }

    private void MountIfLost(in SystemContext context)
    {
        UIRoot hud = UIRoot.Find(Window, HudLayer);
        // IsMounted is false at once when something else removed the host, a hot reload of the HUD's markup say.
        if (hud.Value == 0 || gauge.IsMounted)
            return;
        gauge = hud.Mount<Speedometer>(hud.GetRootElement(), init: s => s.Speed = speed);
        context.Log.Info("UIDemo: the speedometer is on the HUD, a Razor component painted from C#");
    }
}

using Fenrir.Input;
using Fenrir.Renderer;
using Fenrir.Scripting;

// Outside any Fenrir.* namespace, as a game's scripts are, so `Renderer` names the class and not the namespace.
namespace ScriptingDemo.Scripts;

// THE RENDERER'S CULLING FROM C#
//
// Every camera's draws go through a cull pass on the GPU (ADR-0057), and C# reaches its switches through the
// Fenrir.Renderer contract: G turns the frustum test off or on with Renderer.SetGpuCulling (off, every candidate is drawn
// at full detail, for comparison), and a few frames later, once the count has been read back from a frame that has left
// flight, this logs how many of the submitted instances the cull passes drew, from Renderer.Stats. The null backend
// runs no shader, so its smoke run logs 0 drawn; on the GPU the two boxes read 2 of 2 with culling off.
[System(Phase.Update, Order = 6)]
public sealed class CullingControls : ISystem
{
    // How many frames after the switch the read-back holds the switched frame: usually two, so three is safe.
    private const int ReadBackFrames = 3;

    private readonly EventReader<KeyEvent> keys = new();
    private bool gpuCulling = true;
    private int framesUntilReport = -1;

    public void OnUpdate(in SystemContext context)
    {
        foreach (var key in keys.Read())
        {
            if (!key.Pressed || key.Repeat || key.Key != Key.G)
                continue;
            gpuCulling = !gpuCulling;
            // False with no renderer in the app: then there is nothing to report.
            if (Renderer.SetGpuCulling(gpuCulling))
                framesUntilReport = ReadBackFrames;
        }

        if (framesUntilReport < 0)
            return;
        if (framesUntilReport-- > 0)
            return;
        Renderer.Stats(out RendererStats stats);
        context.Log.Info(
            $"G: GPU culling {(gpuCulling ? "on" : "off")}; {stats.InstancesDrawn} of {stats.Instances} instances drawn.");
    }
}

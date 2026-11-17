using Fenrir.Demo;
using Fenrir.Scripting;
using Fenrir.UI;
using Fenrir.UI.Razor;

// Outside any Fenrir.* namespace, as a game's scripts are.
namespace RendererDemo.Scripts;

// Mounts DebugDrawPanel.razor into Debug draw (C#)'s demo slot (src/DebugDrawScene.cpp's razorPanel) whenever the slot
// is there without it, an update after it appears: so each script generation mounts its own, and each entry of the
// scene, since leaving it removes the slot and the panel with it.
[System(Phase.Update)]
public sealed class DebugDrawPanelSetup : ISystem
{
    // Debug draw (C#)'s DemoScene::razorPanel.
    private const string Slot = "debug-draw-razor";

    private ComponentRef<DebugDrawPanel> panel;
    // Whether the slot was there, without the panel, at the last update.
    private bool slotWaiting;

    public void OnCreate(in SystemContext context) => MountIfLost(context);

    public void OnUpdate(in SystemContext context) => MountIfLost(context);

    private void MountIfLost(in SystemContext context)
    {
        // IsMounted is false at once when the slot, and the panel's host in it, is gone.
        if (panel.IsMounted || !DemoUI.TryGetSceneSlot(Slot, out UIRoot root, out ElementId slot))
        {
            slotWaiting = false;
            return;
        }
        // A scene's DebugDraw arrives in the PreUpdate after the scene is made, and the panel reads its groups when it
        // builds, so it is mounted an update after its slot appears.
        if (!slotWaiting)
        {
            slotWaiting = true;
            return;
        }
        slotWaiting = false;
        panel = root.Mount<DebugDrawPanel>(slot);
        context.Log.Info("RendererDemo: the Razor panel is in Debug draw (C#)'s demo slot");
    }
}

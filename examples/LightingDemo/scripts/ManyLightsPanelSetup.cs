using Fenrir.Demo;
using Fenrir.Scripting;
using Fenrir.UI;
using Fenrir.UI.Razor;

namespace LightingDemo.Scripts;

// Mounts ManyLightsPanel.razor into Many lights (C#)'s demo slot (src/ManyLights.cpp's razorPanel) whenever the slot is
// there without it: from OnCreate, so each script generation mounts its own, and again for each entry of the scene,
// since leaving it removes the slot and the panel with it.
[System(Phase.Update)]
public sealed class ManyLightsPanelSetup : ISystem
{
    // Many lights (C#)'s DemoScene::razorPanel.
    private const string Slot = "many-lights-razor";

    private ComponentRef<ManyLightsPanel> panel;

    public void OnCreate(in SystemContext context) => MountIfLost(context);

    public void OnUpdate(in SystemContext context) => MountIfLost(context);

    private void MountIfLost(in SystemContext context)
    {
        // IsMounted is false at once when the slot, and the panel's host in it, is gone.
        if (panel.IsMounted || !DemoUI.TryGetSceneSlot(Slot, out UIRoot root, out ElementId slot))
            return;
        panel = root.Mount<ManyLightsPanel>(slot);
        context.Log.Info("Many lights (C#): the Razor panel is in the scene's demo slot");
    }
}

using Fenrir.Demo;
using Fenrir.Scripting;
using Fenrir.UI;
using Fenrir.UI.Razor;

// Outside any Fenrir.* namespace, as a game's scripts are.
namespace ScriptingDemo.Scripts;

// Mounts ShowcasePanel.razor into Showcase's demo slot (src/Showcase.cpp's razorPanel) whenever the slot is there
// without it: from OnCreate, so each script generation mounts its own, and again for each entry of the scene, since
// leaving it removes the slot and the panel with it.
[System(Phase.Update)]
public sealed class ShowcasePanelSetup : ISystem
{
    // Showcase's DemoScene::razorPanel.
    private const string Slot = "showcase-razor";

    private ComponentRef<ShowcasePanel> panel;

    public void OnCreate(in SystemContext context) => MountIfLost(context);

    public void OnUpdate(in SystemContext context) => MountIfLost(context);

    private void MountIfLost(in SystemContext context)
    {
        // IsMounted is false at once when the slot, and the panel's host in it, is gone.
        if (panel.IsMounted || !DemoUI.TryGetSceneSlot(Slot, out UIRoot root, out ElementId slot))
            return;
        panel = root.Mount<ShowcasePanel>(slot);
        context.Log.Info("ScriptingDemo: the Razor panel is in Showcase's demo slot");
    }
}

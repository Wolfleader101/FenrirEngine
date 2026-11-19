using Fenrir.Demo;
using Fenrir.Scripting;
using Fenrir.UI;
using Fenrir.UI.Razor;

namespace PhysicsDemo.Scripts;

// Mounts ReadingsPanel.razor into the demo slot of whichever C# scene with readings is active (src/Joints.cpp's,
// src/Character.cpp's and src/Vehicles.cpp's razorPanel), whenever the slot is there without it: from OnCreate, so each
// script generation mounts its own, and again for each entry of a scene, since leaving it removes the slot and the panel
// with it.
[System(Phase.Update)]
public sealed class ReadingsPanelSetup : ISystem
{
    // The C# scenes' DemoScene::razorPanel ids; only the active scene's slot is there.
    private static readonly string[] Slots = ["joints-csharp-readings", "character-csharp-readings", "vehicles-csharp-readings"];

    private ComponentRef<ReadingsPanel> panel;

    public void OnCreate(in SystemContext context) => MountIfLost(context);

    public void OnUpdate(in SystemContext context) => MountIfLost(context);

    private void MountIfLost(in SystemContext context)
    {
        // IsMounted is false at once when the slot, and the panel's host in it, is gone.
        if (panel.IsMounted)
            return;
        foreach (string id in Slots)
        {
            if (DemoUI.TryGetSceneSlot(id, out UIRoot root, out ElementId slot))
            {
                Looks.ClearReadouts();
                panel = root.Mount<ReadingsPanel>(slot);
                context.Log.Info($"PhysicsDemo: the readings panel is in the {id} demo slot");
                return;
            }
        }
    }
}

using Fenrir.Demo;
using Fenrir.Scripting;
using Fenrir.UI;
using Fenrir.UI.Razor;

namespace AnimationDemo.Scripts;

// Mount the C# twins' Razor readouts into their demo slots (src/CSharpScenes.cpp's razorPanel) whenever the slot is there
// without the panel: from OnCreate, so each script generation mounts its own, and again for each entry of the scene,
// since leaving it removes the slot and the panel with it.

[System(Phase.Update)]
public sealed class PlaybackPanelSetup : ISystem
{
    // Playback (C#)'s DemoScene::razorPanel, PlaybackCSharpSlot in src/AnimationScenes.hpp.
    private const string Slot = "playback-csharp-razor";

    private ComponentRef<PlaybackPanel> panel;

    public void OnCreate(in SystemContext context) => MountIfLost(context);

    public void OnUpdate(in SystemContext context) => MountIfLost(context);

    private void MountIfLost(in SystemContext context)
    {
        // IsMounted is false at once when the slot, and the panel's host in it, is gone.
        if (panel.IsMounted || !DemoUI.TryGetSceneSlot(Slot, out UIRoot root, out ElementId slot))
            return;
        panel = root.Mount<PlaybackPanel>(slot);
        context.Log.Info("AnimationDemo: the Razor panel is in Playback (C#)'s demo slot");
    }
}

[System(Phase.Update)]
public sealed class AttachmentsPanelSetup : ISystem
{
    // Bone attachments (C#)'s DemoScene::razorPanel, BoneAttachmentsCSharpSlot in src/AnimationScenes.hpp.
    private const string Slot = "attachments-csharp-razor";

    private ComponentRef<AttachmentsPanel> panel;

    public void OnCreate(in SystemContext context) => MountIfLost(context);

    public void OnUpdate(in SystemContext context) => MountIfLost(context);

    private void MountIfLost(in SystemContext context)
    {
        if (panel.IsMounted || !DemoUI.TryGetSceneSlot(Slot, out UIRoot root, out ElementId slot))
            return;
        panel = root.Mount<AttachmentsPanel>(slot);
        context.Log.Info("AnimationDemo: the Razor panel is in Bone attachments (C#)'s demo slot");
    }
}

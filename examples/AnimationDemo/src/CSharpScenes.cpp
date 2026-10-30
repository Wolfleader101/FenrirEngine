// Scenes 8 and 9, the C# twins (scripts/Playback.cs, scripts/BoneAttachments.cs): C++ gives each its floor and sun and
// its CSharpScene number; everything that animates is C#'s.
#include "AnimationScenes.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirInput/InputTypes.hpp"
#include "FenrirScene/Scene.hpp"
#include "Rigs.hpp"

namespace AnimationDemo
{
    FenrirDemo::DemoScene PlaybackCSharp()
    {
        FenrirDemo::DemoScene scene;
        scene.name = "Playback (C#)";
        scene.keyHelp =
            "C# plays the human. 1-5: crossfade to Idle, Walk, Run, Sprint, Call. Up/Down: speed. R: reverse. "
            "L: Loop or Once. Space: pause. Left/Right: seek 0.25 s. Home: seek to 0. M: ramp the upper-body "
            "layer in or out.";
        scene.keys = {Fenrir::Key::Num3,  Fenrir::Key::M,     Fenrir::Key::Up,   Fenrir::Key::R, Fenrir::Key::Space,
                      Fenrir::Key::Right, Fenrir::Key::Space, Fenrir::Key::Num5, Fenrir::Key::L, Fenrir::Key::Left,
                      Fenrir::Key::Home,  Fenrir::Key::M,     Fenrir::Key::Num2};
        scene.camera = {{0.0f, 1.4f, 4.5f}, 0.0f, -0.1f};
        scene.spawn = [](Fenrir::App& app, Fenrir::Scene& spawned) { AddStage(app, spawned); };
        scene.csharpScene = PlaybackCSharpScene;
        return scene;
    }

    FenrirDemo::DemoScene BoneAttachmentsCSharp()
    {
        FenrirDemo::DemoScene scene;
        scene.name = "Bone attachments (C#)";
        scene.keyHelp = "C# attaches a blade to hand_r, a hat to Head and a tag to no bone. W: drop the blade "
                        "(KeepWorld). K: leave the hat (KeepLocal). R: attach both again. Left/Right: slide the blade "
                        "along its bone. N: the C# Modify system's nod on or off. C: the next clip.";
        scene.keys = {Fenrir::Key::Right, Fenrir::Key::W, Fenrir::Key::K, Fenrir::Key::N,
                      Fenrir::Key::C,     Fenrir::Key::R, Fenrir::Key::N, Fenrir::Key::Left};
        scene.camera = {{0.0f, 1.5f, 4.0f}, 0.0f, -0.12f};
        scene.spawn = [](Fenrir::App& app, Fenrir::Scene& spawned) { AddStage(app, spawned); };
        scene.csharpScene = BoneAttachmentsCSharpScene;
        return scene;
    }
} // namespace AnimationDemo

using Fenrir.Scripting;

namespace DemoScripts;

// SCENES MADE BY C#
//
// C++ makes a scene for C# to build empty but for one entity carrying this marker (examples/DemoApp/src/CSharpScenes.hpp),
// and the scene switcher steps to it. Scene names which scene it is, by an id C++ and the scene's script both declare
// (Playground.SceneId is the Physics (C#) scene's, JointsScene.SceneId the Joints (C#) scene's, CharacterScene.SceneId
// the Character (C#) scene's). The script that builds that scene looks for its own id with Built == 0, builds there and sets Built, which stays in the ECS, so the scene is
// built once, even across a hot reload. A scene built over several frames counts its stages in Built.
[NativeComponent("Demo.CSharpScene")]
public struct CSharpScene : IComponent
{
    public int Scene;
    public int Built;
}

using Fenrir.Math;
using Fenrir.Scripting;

namespace Fenrir.Demo;

// The engine fills these fields in native memory; the compiler cannot see that.
#pragma warning disable CS0649

/// <summary>
/// The marker a C# scene's FenrirScene holds, the scaffold's <c>FenrirDemo::CSharpScene</c>: which C# scene it is (the C++
/// <c>DemoScene::csharpScene</c> number) and which entry. A C# scene's systems do nothing unless <see cref="IsActive"/>, and
/// spawn the scene when <see cref="Entry"/> is one they have not seen.
/// </summary>
[NativeResource("Fenrir.Demo.CSharpScene")]
public readonly struct CSharpScene : IReadOnlyResource
{
    /// <summary>The scene's number, as the app's C++ scene list gives it.</summary>
    public readonly uint Scene;
    /// <summary>A new value each time a scene is entered.</summary>
    public readonly uint Entry;

    /// <summary>Whether the active scene is the C# scene <paramref name="scene"/>; if so, <paramref name="entry"/> is its entry.</summary>
    public static bool IsActive(uint scene, out uint entry)
    {
        if (Resources.TryGetReadOnly(out ReadOnlyRef<CSharpScene> marker) && marker.Value.Scene == scene)
        {
            entry = marker.Value.Entry;
            return true;
        }
        entry = 0;
        return false;
    }
}

/// <summary>
/// Sent when the scene is left-clicked (without the right mouse button held): the scaffold's
/// <c>FenrirDemo::DemoSceneClickEvent</c>, the ray from the camera drawn at the clicked point through it.
/// </summary>
[NativeEvent("Fenrir.Demo.DemoSceneClickEvent")]
public readonly struct DemoSceneClickEvent : IEvent
{
    public readonly Ray Ray;
    /// <summary>Where the click was, in the window's logical coordinates.</summary>
    public readonly Vector2 Position;
}

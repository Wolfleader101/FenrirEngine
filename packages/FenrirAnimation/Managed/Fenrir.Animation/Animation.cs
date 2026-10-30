using Fenrir.Scripting;

namespace Fenrir.Animation;

// Seek and Playback are generated (Generated/Animation.cs, from the natives' @facade marks in
// Scripting/src/AnimationScripting.cpp). This part holds the class doc and the form of Playback that returns the info.

/// <summary>
/// Clip playback from C#, as FenrirAnimation's <c>Animation</c> does in C++ (whose documentation says what each does).
/// Playback itself is the <see cref="Animator"/> on a skeleton entity and its <see cref="AnimationLayer"/>s, written as
/// components; this class moves a playback cursor and reads where it stands. Clip events arrive as
/// <see cref="AnimationEvent"/>, <see cref="ClipFinished"/> and <see cref="AnimationEventsDropped"/> through an
/// <c>EventReader</c>.
/// </summary>
/// <remarks>
/// Inside a <c>Fenrir.*</c> namespace, <c>Animation</c> names this namespace: write <c>Fenrir.Animation.Animation</c>
/// there, or use an alias. Nothing here throws: on an entity that is not alive each does nothing and returns
/// <c>false</c>, and a debug build of the engine warns once per place in its native code that noticed.
/// </remarks>
public static partial class Animation
{
    /// <summary>Where the entity's playback stands: its Animator's, or else its AnimationLayer's.</summary>
    /// <returns>The playback; <see cref="Fenrir.Skeleton.PoseState.Missing"/> with nothing playing.</returns>
    public static PlaybackInfo Playback(Entity entity)
    {
        Playback(entity, out PlaybackInfo info);
        return info;
    }
}

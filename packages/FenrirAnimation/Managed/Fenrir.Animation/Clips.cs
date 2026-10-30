using Fenrir.Assets;

namespace Fenrir.Animation;

// Duration, EventCount, GetEvent, EventName, FindEvents, AddEvent and RemoveEvent are generated (Generated/Clips.cs,
// from the natives' @facade marks in Scripting/src/AnimationScripting.cpp), filling a caller's span. This part holds the
// class doc and the forms that make their own string or array.

/// <summary>
/// A clip asset's timed events, read and authored by its handle (FenrirAssets' clip event edits, ADR-0034). Events are
/// kept in time order; each edit bumps the clip's revision, so an <see cref="AnimationEvent"/>'s <c>Index</c> names an
/// event only in its <c>ClipRevision</c>. Load a clip from a model file by label, <c>rig.glb#Clip0</c>. With an empty or
/// stale handle, each returns 0, <c>false</c> or nothing.
/// </summary>
public static partial class Clips
{
    /// <summary>The name of the clip's event at <paramref name="index"/>; empty when there is none.</summary>
    public static string EventName(AssetHandle<Clip> clip, uint index)
    {
        Span<char> buffer = stackalloc char[128];
        int length = EventName(clip, index, buffer);
        if (length <= buffer.Length)
            return new string(buffer[..length]);
        char[] larger = new char[length];
        EventName(clip, index, larger);
        return new string(larger);
    }

    /// <summary>The index of every event of the clip named exactly <paramref name="name"/>, in time order.</summary>
    public static uint[] FindEvents(AssetHandle<Clip> clip, ReadOnlySpan<char> name)
    {
        uint[] found = new uint[FindEvents(clip, name, Span<uint>.Empty)];
        FindEvents(clip, name, found);
        return found;
    }
}

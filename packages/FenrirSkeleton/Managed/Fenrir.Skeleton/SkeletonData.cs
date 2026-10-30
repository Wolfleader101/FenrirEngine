using Fenrir.Assets;
using SkeletonAsset = Fenrir.Assets.Skeleton;

namespace Fenrir.Skeleton;

// BoneCount, Parent, IsDescendant, EngineName, SourceName and Subtree are generated (Generated/SkeletonData.cs, from the
// natives' @facade marks in Scripting/src/SkeletonScripting.cpp), filling a caller's span. This part holds the class doc
// and the forms that make their own string or array.

/// <summary>
/// A skeleton asset's bones, by its handle (FenrirAssets' <c>SkeletonData</c>): their count, parents and names, each
/// after its parent in index order. A skeleton entity's asset is its <see cref="SkeletonPose"/>'s <c>Skeleton</c>. With
/// an empty or stale handle, each returns 0, -1, <c>false</c> or nothing.
/// </summary>
public static partial class SkeletonData
{
    /// <summary>The bone's engine name, unique in the skeleton; empty for a bone out of range or a stale handle.</summary>
    public static string EngineName(AssetHandle<SkeletonAsset> skeleton, int bone) => Read(skeleton, bone, EngineName);

    /// <summary>The bone's name in its file; empty for a bone out of range or a stale handle.</summary>
    public static string SourceName(AssetHandle<SkeletonAsset> skeleton, int bone) => Read(skeleton, bone, SourceName);

    /// <summary>The bone and every bone below it, in index order; empty for a bone out of range.</summary>
    public static int[] Subtree(AssetHandle<SkeletonAsset> skeleton, int bone)
    {
        int[] bones = new int[Subtree(skeleton, bone, Span<int>.Empty)];
        Subtree(skeleton, bone, bones);
        return bones;
    }

    private delegate int NameReader(AssetHandle<SkeletonAsset> skeleton, int bone, Span<char> name);

    // A name read again into a larger buffer when the first is too small: the native gives the whole length.
    private static string Read(AssetHandle<SkeletonAsset> skeleton, int bone, NameReader read)
    {
        Span<char> buffer = stackalloc char[256];
        int length = read(skeleton, bone, buffer);
        if (length <= buffer.Length)
            return new string(buffer[..length]);
        char[] larger = new char[length];
        read(skeleton, bone, larger);
        return new string(larger);
    }
}

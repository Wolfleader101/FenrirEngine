using Fenrir.Assets;

namespace Fenrir.Animation;

// Create, Include and Clear are generated (Generated/BoneMasks.cs, from the natives' @facade marks in
// Scripting/src/AnimationScripting.cpp). This part holds the class doc and the overloads that default the owner and the
// scope.

/// <summary>
/// Bone masks (ADR-0036): which bones an <see cref="AnimationLayer"/> reaches, by engine name, and with what weight.
/// Later entries override earlier ones for the bones they cover, and bones no entry covers get 0. Each edit bumps the
/// mask's revision, and every layer using it binds it again. With no asset store or a stale handle, each does nothing
/// and returns <c>false</c> or the empty handle.
/// </summary>
public static partial class BoneMasks
{
    /// <summary>A new bone mask with no entries for the active scene, released with it.</summary>
    /// <returns>The mask; the empty handle with no asset store.</returns>
    public static AssetHandle<BoneMask> Create() => Create(AssetOwner.Scene);

    /// <summary>Appends an entry giving the bone named <paramref name="bone"/> alone <paramref name="weight"/>.</summary>
    /// <returns>Whether it was added.</returns>
    public static bool Include(AssetHandle<BoneMask> mask, ReadOnlySpan<char> bone, float weight) =>
        Include(mask, bone, weight, MaskScope.Bone);
}

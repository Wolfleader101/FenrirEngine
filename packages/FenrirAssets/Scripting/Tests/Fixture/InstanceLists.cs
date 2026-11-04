using System.Globalization;
using Fenrir.Assets;
using Fenrir.Math;

namespace AssetsScriptFixture;

// Instance lists as C# makes, edits and reads them. Copy i of a list is at (i, y, 0), unrotated and unscaled, and
// coloured (i / 4, 0.5, 1 - i / 4) when the list has colours.
public static class InstanceListCalls
{
    private static Matrix4x4[] CopiesAt(int count, float y)
    {
        var transforms = new Matrix4x4[count];
        for (int i = 0; i < count; ++i)
        {
            transforms[i] = Matrix4x4.Identity;
            transforms[i].Column3 = new Vector4(i, y, 0, 1);
        }
        return transforms;
    }

    private static Colour[] ColoursOf(int count)
    {
        var colours = new Colour[count];
        for (int i = 0; i < count; ++i)
            colours[i] = new Colour(i / 4.0f, 0.5f, 1 - i / 4.0f, 1);
        return colours;
    }

    // `count` copies at height 0 with `colourCount` colours: none for white, one per copy, or a wrong number the store
    // refuses. For `owner`, 0 the active scene and 1 the app.
    public static ulong Add(int count, int colourCount, int owner) =>
        TestHandle.Of(Assets.AddInstanceList(CopiesAt(count, 0), ColoursOf(colourCount), (AssetOwner)owner));

    // The overload with no colours and no owner: white copies for the active scene.
    public static ulong AddWhite(int count) => TestHandle.Of(Assets.AddInstanceList(CopiesAt(count, 0)));

    // Swaps the list's copies for `count` at height `y` with `colourCount` colours, or (with -1) through the overload
    // that takes none.
    public static bool Replace(ulong list, int count, float y, int colourCount) =>
        colourCount < 0
            ? Assets.ReplaceInstanceList(TestHandle.To<InstanceList>(list), CopiesAt(count, y))
            : Assets.ReplaceInstanceList(TestHandle.To<InstanceList>(list), CopiesAt(count, y), ColoursOf(colourCount));

    // The list's transforms read into a span of `capacity`, as "count: (x, y, z) ..." with each copy's translation, and
    // "!" after any copy whose other columns are not the identity's.
    public static string ReadTransforms(ulong list, int capacity)
    {
        var transforms = new Matrix4x4[capacity];
        int count = Assets.GetInstanceTransforms(TestHandle.To<InstanceList>(list), transforms);
        var text = $"{count}:";
        foreach (Matrix4x4 transform in transforms.AsSpan(0, System.Math.Min(count, capacity)))
        {
            Matrix4x4 rest = transform;
            rest.Column3 = new Vector4(0, 0, 0, 1);
            text += string.Create(CultureInfo.InvariantCulture,
                $" ({transform.Column3.X}, {transform.Column3.Y}, {transform.Column3.Z}){(rest == Matrix4x4.Identity ? "" : "!")}");
        }
        return text;
    }

    // The list's colours read into a span of `capacity`, as "count: (r, g, b, a) ...".
    public static string ReadColours(ulong list, int capacity)
    {
        var colours = new Colour[capacity];
        int count = Assets.GetInstanceColours(TestHandle.To<InstanceList>(list), colours);
        var text = $"{count}:";
        foreach (Colour colour in colours.AsSpan(0, System.Math.Min(count, capacity)))
            text += string.Create(CultureInfo.InvariantCulture, $" ({colour.R}, {colour.G}, {colour.B}, {colour.A})");
        return text;
    }
}

using System.Runtime.InteropServices;
using Fenrir.Assets;
using Fenrir.Math;
using Fenrir.Scripting;

namespace Fenrir.Renderer;

/// <summary>
/// The constant light every lit surface receives, a scene resource (ADR-0024): FenrirRenderer's <c>AmbientLight</c>. A scene
/// without one is lit by a default one, a dim grey; <c>Resources.Insert(new AmbientLight())</c> gives the active scene one.
/// </summary>
[NativeResource("Fenrir.Renderer.AmbientLight")]
public struct AmbientLight : IResource
{
    /// <summary>Linear.</summary>
    public Colour Colour = new(0.1f, 0.1f, 0.1f, 1.0f);
    /// <summary>A plain multiplier on the colour.</summary>
    public float Intensity = 1.0f;

    public AmbientLight()
    {
    }
}

/// <summary>A sky of one colour.</summary>
public struct ColourSky
{
    /// <summary>Linear. A dark grey by default, the sky of a scene with none.</summary>
    public Colour Colour = new(0.015f, 0.015f, 0.015f, 1.0f);

    public ColourSky()
    {
    }
}

/// <summary>A sky drawn from a cubemap image, such as one loaded from a <c>.cubemap</c> file.</summary>
public struct CubemapSky
{
    public AssetHandle<Image> Cubemap;
}

/// <summary>Which kind of sky a <see cref="Sky"/> holds.</summary>
public enum SkyType : byte
{
    Colour,
    Cubemap,
}

/// <summary>
/// What a scene shows where nothing was drawn, a scene resource beside <see cref="AmbientLight"/> (ADR-0026):
/// FenrirRenderer's <c>Sky</c>, exactly one of <see cref="ColourSky"/> and <see cref="CubemapSky"/> (a one-byte tag, then the
/// kinds at offset 4). A scene without one has the default colour sky. Assign a kind to change it
/// (<c>sky.Value = new CubemapSky { Cubemap = cubemap }</c> through <c>Resources.Get&lt;Sky&gt;()</c>); each kind's view
/// throws <see cref="InvalidOperationException"/> if the sky is the other.
/// </summary>
[NativeResource("Fenrir.Renderer.Sky")]
[TaggedUnion(Noun = "sky")]
[StructLayout(LayoutKind.Explicit)]
public partial struct Sky : IResource
{
    [FieldOffset(0)] private SkyType _type;
    [FieldOffset(4)] private ColourSky _colour;
    [FieldOffset(4)] private CubemapSky _cubemap;
}

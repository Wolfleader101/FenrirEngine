using System;
using Fenrir.Math;
using Fenrir.Scripting;

namespace Fenrir.Renderer;

// Hand-written, not generated: C++ no longer has an AmbientLight (ADR-0047), so FenrirRenderer's description does not
// describe one. FenrirRendererScripting registers a stand-in of the same layout, so scripts that still insert or write
// one keep compiling and running; the lighting spec's C# ticket removes both.

/// <summary>
/// Deprecated, and does nothing: the scene's <see cref="Sky"/> is its ambient light (a <see cref="ColourSky"/> lights
/// every surface with its colour from every side). Scripts may still insert, read and write it.
/// </summary>
[Obsolete("The Sky is the scene's ambient light: use a ColourSky's colour. AmbientLight does nothing.")]
[NativeResource("Fenrir.Renderer.AmbientLight")]
public partial struct AmbientLight : IResource
{
    /// <summary>Linear. Read by nothing.</summary>
    public Colour Colour = new() { R = 0.1f, G = 0.1f, B = 0.1f, A = 1 };
    /// <summary>A plain multiplier on the colour. Read by nothing.</summary>
    public float Intensity = 1;

    public AmbientLight()
    {
    }
}

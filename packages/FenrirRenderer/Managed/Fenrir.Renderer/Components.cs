using System.Runtime.InteropServices;
using Fenrir.Assets;
using Fenrir.Input;
using Fenrir.Math;
using Fenrir.Scripting;

namespace Fenrir.Renderer;

/// <summary>
/// Draws one mesh with one material at its entity's world transform (ADR-0023): one MeshRenderer, one draw. FenrirRenderer's
/// <c>MeshRenderer</c>, whose documentation says what an empty or stale mesh or material does.
/// </summary>
[NativeComponent("Fenrir.Renderer.MeshRenderer")]
public struct MeshRenderer : IComponent
{
    public AssetHandle<Mesh> Mesh;
    public AssetHandle<Material> Material;
    /// <summary>False hides this entity only; its children are drawn as their own MeshRenderers say.</summary>
    public bool Visible = true;

    public MeshRenderer()
    {
    }
}

/// <summary>
/// A perspective projection with an infinite far plane and reversed-Z depth (ADR-0026). The aspect comes from the viewport's
/// pixel size.
/// </summary>
public struct Perspective
{
    /// <summary>The vertical field of view in radians; 60 degrees by default.</summary>
    public float VerticalFov = 1.04719755f;
    /// <summary>The distance to the near plane, in world units.</summary>
    public float Near = 0.1f;

    public Perspective()
    {
    }
}

/// <summary>An orthographic projection with reversed-Z depth (ADR-0026). The width comes from the height and the viewport.</summary>
public struct Orthographic
{
    /// <summary>The full height of what the view shows, in world units.</summary>
    public float Height = 10.0f;
    /// <summary>The distance to the near plane along the camera's -Z, in world units.</summary>
    public float Near = 0.1f;
    /// <summary>The distance to the far plane along the camera's -Z, in world units.</summary>
    public float Far = 1000.0f;

    public Orthographic()
    {
    }
}

/// <summary>Which kind of projection a <see cref="CameraProjection"/> holds.</summary>
public enum ProjectionType : byte
{
    Perspective,
    Orthographic,
}

/// <summary>
/// A camera's projection: exactly one of <see cref="Fenrir.Renderer.Perspective"/> and
/// <see cref="Fenrir.Renderer.Orthographic"/>, laid out as FenrirRenderer's <c>Projection</c> (a one-byte tag, then the kinds
/// at offset 4). Assign a kind to change it; each kind's view reads or edits it in place and throws
/// <see cref="InvalidOperationException"/> if it holds the other. New, it is the default perspective.
/// </summary>
[TaggedUnion(Noun = "projection")]
[StructLayout(LayoutKind.Explicit)]
public partial struct CameraProjection
{
    [FieldOffset(0)] private ProjectionType _type;
    [FieldOffset(4)] private Perspective _perspective;
    [FieldOffset(4)] private Orthographic _orthographic;
}

/// <summary>A camera drawing into a window. The foundation renderer draws to the main window only.</summary>
public struct WindowTarget
{
    /// <summary>The main window by default. A camera naming another window is not drawn.</summary>
    public WindowId Window = new(1);

    public WindowTarget()
    {
    }
}

/// <summary>
/// A camera drawing into an image (ADR-0026): a render target made with <c>Assets.AddRenderTarget</c>. Materials sample it
/// like any image.
/// </summary>
public struct ImageTarget
{
    public AssetHandle<Image> Image;
}

/// <summary>Which kind of render target a <see cref="RenderTarget"/> holds.</summary>
public enum RenderTargetType : byte
{
    Window,
    Image,
}

/// <summary>
/// Where a camera's view goes: exactly one of <see cref="WindowTarget"/> and <see cref="ImageTarget"/>, laid out as
/// FenrirRenderer's <c>RenderTarget</c>. New, it is the main window.
/// </summary>
[TaggedUnion(Noun = "render target")]
[StructLayout(LayoutKind.Explicit)]
public partial struct RenderTarget
{
    [FieldOffset(0)] private RenderTargetType _type;
    [FieldOffset(4)] private WindowTarget _window;
    [FieldOffset(4)] private ImageTarget _image;
}

// The engine never reads these bytes; they stand for the one byte an empty C++ struct takes.
#pragma warning disable CS0169

/// <summary>A camera background that is the scene's own: its <see cref="Sky"/>.</summary>
public struct SceneBackground
{
    private byte _unused;
}

#pragma warning restore CS0169

/// <summary>A camera background of one colour, overriding the scene's.</summary>
public struct ColourBackground
{
    /// <summary>Linear.</summary>
    public Colour Colour = Colour.Black;

    public ColourBackground()
    {
    }
}

/// <summary>Which kind of background a <see cref="CameraBackground"/> holds.</summary>
public enum BackgroundType : byte
{
    Scene,
    Colour,
}

/// <summary>
/// What a camera's view shows where nothing was drawn: exactly one of <see cref="SceneBackground"/> and
/// <see cref="ColourBackground"/>, laid out as FenrirRenderer's <c>CameraBackground</c>. New, it is the scene's own.
/// </summary>
[TaggedUnion(Noun = "background")]
[StructLayout(LayoutKind.Explicit)]
public partial struct CameraBackground
{
    [FieldOffset(0)] private BackgroundType _type;
    [FieldOffset(4)] private SceneBackground _scene;
    [FieldOffset(4)] private ColourBackground _colour;
}

/// <summary>
/// The rectangle of its render target a view draws into, as fractions of the target's width and height from its top-left
/// corner. The whole target by default.
/// </summary>
public struct Viewport
{
    public float X = 0.0f;
    public float Y = 0.0f;
    public float Width = 1.0f;
    public float Height = 1.0f;

    public Viewport()
    {
    }
}

/// <summary>
/// Makes a view of the active scene from its entity, looking along the entity's local -Z with +Y up (ADR-0026): FenrirRenderer's
/// <c>Camera</c>, whose documentation says how views are ordered and which cameras are not drawn.
/// </summary>
[NativeComponent("Fenrir.Renderer.Camera")]
public struct Camera : IComponent
{
    public CameraProjection Projection = new();
    /// <summary>False draws no view.</summary>
    public bool Enabled = true;
    /// <summary>Views into one target draw lowest order first, so a higher order draws over a lower one.</summary>
    public int Order = 0;
    public Viewport Viewport = new();
    public RenderTarget Target = new();
    public CameraBackground Background = new();
    /// <summary>Whether the view draws the scene's debug shapes and world text (ADR-0025).</summary>
    public bool DrawDebug = true;

    public Camera()
    {
    }
}

/// <summary>
/// The light shining along its entity's world -Z (ADR-0024). The renderer uses the first one in the active scene and warns
/// once if there are more.
/// </summary>
[NativeComponent("Fenrir.Renderer.DirectionalLight")]
public struct DirectionalLight : IComponent
{
    /// <summary>Linear.</summary>
    public Colour Colour = Colour.White;
    /// <summary>A plain multiplier on the colour.</summary>
    public float Intensity = 1.0f;

    public DirectionalLight()
    {
    }
}

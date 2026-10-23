using Fenrir.Math;
using Fenrir.Scripting;

namespace Fenrir.Assets;

/// <summary>The type of a shader's material parameter. FenrirAssets' <c>ShaderParameterType</c>.</summary>
public enum ShaderParameterType : byte
{
    Float,
    Vector2,
    Vector3,
    /// <summary>A <c>vec4</c>, which is also how a colour is set.</summary>
    Vector4,
    Texture,
}

/// <summary>
/// Which of the renderer's 1x1 default textures a texture parameter binds when a material sets none. FenrirAssets'
/// <c>DefaultTexture</c>.
/// </summary>
public enum DefaultTexture : byte
{
    White,
    Black,
    /// <summary>The flat tangent-space normal (0.5, 0.5, 1), linear.</summary>
    Normal,
}

/// <summary>
/// One material parameter of a shader, with its parameter default: FenrirAssets' <c>ShaderParameter</c> without its name,
/// which <see cref="Shaders.GetParameter"/> writes to a span.
/// </summary>
public struct ShaderParameter
{
    public ShaderParameterType Type;
    /// <summary>A number parameter's default: one value per component, the rest zero. Unused for a texture.</summary>
    public Vector4 Value;
    /// <summary>A texture parameter's default. Unused for a number.</summary>
    public DefaultTexture Texture;
}

/// <summary>
/// A shader's material parameters from C#, read from its shader container alone, as FenrirAssets'
/// <c>AssetStore::GetShaderParameters</c> reads them in C++: for an editor, or anything showing what a material inherits.
/// </summary>
/// <remarks>
/// Nothing here throws. With no asset store in the app, or a stale or empty handle, a function does nothing and returns
/// zero. In a debug build of the engine a warning is logged once per process for each place in the engine's native code that
/// noticed.
/// </remarks>
public static class Shaders
{
    /// <summary>How many material parameters <paramref name="shader"/> has.</summary>
    public static int GetParameterCount(AssetHandle<Shader> shader) => ShadersNative.GetParameterCount(shader.Bits);

    /// <summary>
    /// Reads parameter <paramref name="index"/> of <paramref name="shader"/>: its type and default into
    /// <paramref name="parameter"/>, and its name into <paramref name="name"/>, as much as fits.
    /// </summary>
    /// <returns>
    /// The name's whole length in chars, so a result larger than the span says how large a span would hold it; zero, with
    /// <paramref name="parameter"/> zeroed, if there is no such parameter.
    /// </returns>
    public static unsafe int GetParameter(AssetHandle<Shader> shader, int index, out ShaderParameter parameter, Span<char> name)
    {
        fixed (char* first = name)
            return ShadersNative.GetParameter(shader.Bits, index, out parameter, first, name.Length);
    }
}

// The native functions behind Shaders, each named "Fenrir.Assets.ShadersNative.<Method>" in FenrirAssetsScripting's
// extension.
internal static unsafe partial class ShadersNative
{
    [NativeFunction]
    public static partial int GetParameterCount(ulong shader);

    // Writes at most `capacity` chars of the name from `name` on, and returns the name's whole length.
    [NativeFunction]
    public static partial int GetParameter(ulong shader, int index, out ShaderParameter parameter, char* name, int capacity);
}

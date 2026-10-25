namespace Fenrir.Assets;

/// <summary>
/// A shader's material parameters from C#, read from its shader container alone, as FenrirAssets'
/// <c>AssetStore::GetShaderParameters</c> reads them in C++: for an editor, or anything showing what a material inherits.
/// </summary>
/// <remarks>
/// Nothing here throws. With no asset store in the app, or a stale or empty handle, a function does nothing and returns
/// zero. In a debug build of the engine a warning is logged once per scripting install for each place in the engine's native
/// code that noticed.
/// </remarks>
public static class Shaders
{
    /// <summary>How many material parameters <paramref name="shader"/> has.</summary>
    public static int GetParameterCount(AssetHandle<Shader> shader) => AssetsNative.GetShaderParameterCount(shader.Bits);

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
            return AssetsNative.GetShaderParameter(shader.Bits, index, out parameter, first, name.Length);
    }
}

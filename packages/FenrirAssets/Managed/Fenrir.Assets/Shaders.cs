namespace Fenrir.Assets;

// GetParameterCount and GetParameter are generated (Generated/Shaders.cs, from the natives' @facade marks in
// Scripting/src/AssetsScripting.cpp). This part holds the class doc.

/// <summary>
/// A shader's material parameters from C#, read from its shader container alone, as FenrirAssets'
/// <c>AssetStore::GetShaderParameters</c> reads them in C++: for an editor, or anything showing what a material inherits.
/// </summary>
/// <remarks>
/// Nothing here throws. With no asset store in the app, or a stale or empty handle, a function does nothing and returns
/// zero. In a debug build of the engine a warning is logged once per scripting install for each place in the engine's native
/// code that noticed.
/// </remarks>
public static partial class Shaders
{
}

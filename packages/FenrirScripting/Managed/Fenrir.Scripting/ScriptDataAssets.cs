using System.ComponentModel;

namespace Fenrir.Scripting;

/// <summary>
/// How a script assembly tells the engine which data asset types it defines (ADR-0083). The generated
/// <c>DataAssetTable</c> calls <see cref="Report{T}"/> once for every <see cref="IDataAsset"/> class in the script, bases
/// first, as it registers their codecs, and binds the engine's upgrade step to <see cref="Upgrade{T}"/>; scripts do not
/// call them themselves.
/// </summary>
[EditorBrowsable(EditorBrowsableState.Never)]
public static class ScriptDataAssets
{
    /// <summary>
    /// Reports <typeparamref name="T"/> to the engine: its description entry, as a tree in the tree encoding, which the
    /// engine checks with the rest of the script report and registers as a type. Only meaningful while the script
    /// assembly is loading.
    /// </summary>
    /// <exception cref="InvalidOperationException">
    /// The type has no codecs, or the engine did not accept it, as when nothing is loading.
    /// </exception>
    public static void Report<T>() where T : class, IDataAsset
    {
        DataAssetCodec<T> codec = DataAssetCodecs.Require<T>();
        byte[] entry = TreeWriter.Write(codec.Entry());
        if (!Native.RegisterScriptDataAsset(entry))
            throw new InvalidOperationException($"The engine did not accept the data asset type {codec.Name}: data asset types are reported only while a script assembly loads.");
    }

    /// <summary>
    /// The tree <paramref name="tree"/>, in the tree encoding, of a <typeparamref name="T"/> written at
    /// <paramref name="fromVersion"/>, upgraded one version by its upgrader, in the tree encoding.
    /// </summary>
    /// <exception cref="KeyNotFoundException">The type has no upgrader from that version.</exception>
    /// <exception cref="TreeFormatException">The bytes are not one tree encoding.</exception>
    public static byte[] Upgrade<T>(int fromVersion, byte[] tree) where T : class, IDataAsset =>
        TreeWriter.Write(DataAssetCodecs.Require<T>().Upgrade(fromVersion, TreeReader.Read(tree)));
}

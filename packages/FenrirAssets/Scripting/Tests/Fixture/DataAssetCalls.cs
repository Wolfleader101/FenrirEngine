using AssetsScriptFixture.DataAssets;
using Fenrir.Scripting;

namespace AssetsScriptFixture;

/// <summary>
/// The C# half of DataAssets.cpp: the generated classes of FenrirAssets' test package's data assets (DataAssets/, from
/// DataAssetsTest.binding.toml) reading the tree C++ wrote and writing it back through their codecs.
/// </summary>
public static class DataAssetCalls
{
    /// <summary>The tree read into a <see cref="Sword"/> and written again, fields at their defaults left out.</summary>
    public static byte[] SwordRoundTrip(byte[] tree) => TreeWriter.Write(Sword.Codec.ToTree(Sword.Codec.FromTree(TreeReader.Read(tree))));

    /// <summary>The tree read into a <see cref="GoldenSword"/> and written again.</summary>
    public static byte[] GoldenSwordRoundTrip(byte[] tree) =>
        TreeWriter.Write(GoldenSword.Codec.ToTree(GoldenSword.Codec.FromTree(TreeReader.Read(tree))));

    /// <summary>The tree read into a <see cref="ParticleEffect"/> and written again.</summary>
    public static byte[] ParticleEffectRoundTrip(byte[] tree) =>
        TreeWriter.Write(ParticleEffect.Codec.ToTree(ParticleEffect.Codec.FromTree(TreeReader.Read(tree))));

    /// <summary>What C# read of a sword's tree: its name, tags, damage and edge, joined by '|'.</summary>
    public static string SwordSummary(byte[] tree)
    {
        Sword sword = Sword.Codec.FromTree(TreeReader.Read(tree));
        return string.Join('|', sword.DisplayName, string.Join(',', sword.Tags), sword.Stats.Damage.ToString(System.Globalization.CultureInfo.InvariantCulture),
            sword.Edge.ToString(System.Globalization.CultureInfo.InvariantCulture));
    }

    /// <summary>
    /// What the binding's module initialiser left unregistered of the classes' codecs, with their C++ names, versions and bases, and of the
    /// codecs of the structs they hold, one line each; empty when it registered them all.
    /// </summary>
    public static string UnregisteredCodecs()
    {
        var missing = new List<string>();
        if (DataAssetCodecs.Find<Sword>() is not { Name: "AssetsTest::Sword", BaseName: "AssetsTest::WeaponData", Version: 2 })
            missing.Add($"Sword by type: {DataAssetCodecs.Find<Sword>()?.Name ?? "none"}");
        // By type, not by name: nothing forgets an unloaded generation's codecs yet, so a name may still find those of an
        // earlier load of this fixture.
        if (DataAssetCodecs.Find<WeaponData>()?.Name != "AssetsTest::WeaponData")
            missing.Add($"WeaponData by type: {DataAssetCodecs.Find<WeaponData>()?.Name ?? "none"}");
        if (DataAssetCodecs.Find(typeof(GoldenSword))?.Name != "MyGame::Sword")
            missing.Add($"GoldenSword by type: {DataAssetCodecs.Find(typeof(GoldenSword))?.Name ?? "none"}");
        try
        {
            if (DataAssetCodecs.RequireValue<WeaponStats>().Name != "AssetsTest::WeaponStats")
                missing.Add("WeaponStats's name");
        }
        catch (InvalidOperationException exception)
        {
            missing.Add(exception.Message);
        }
        return string.Join("; ", missing);
    }
}

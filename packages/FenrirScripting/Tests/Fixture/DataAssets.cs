using Fenrir.Assets;
using Fenrir.Math;
using Fenrir.Scripting;
using Fenrir.Scripting.Generated;

// FenrirAssets' handle and asset kinds, which this package's fixture cannot reference (FenrirAssets' contract is built on
// this one): stand-ins of the same names, which the data asset generator recognises by name, as it does the real ones.
namespace Fenrir.Assets
{
    public struct AssetHandle<T> where T : class
    {
        public uint Index;
        public uint Generation;

        public AssetHandle(uint index, uint generation)
        {
            Index = index;
            Generation = generation;
        }
    }

    public sealed class Mesh
    {
        private Mesh()
        {
        }
    }
}

namespace Fenrir.ScriptFixture
{
    public enum Rarity : byte
    {
        Common,
        Rare,
        Epic,
    }

    public enum Element
    {
        Fire = -1,
        Ice = 2,
    }

    // A struct of the script's own, held by value, in a list and as an optional.
    public struct Stats
    {
        public int Strength;
        [Range(0, 10)] public float Speed = 1.5f;
        public List<string> Tags = new();

        public Stats()
        {
        }
    }

    // The base: a data asset extending object, with metadata and a former name.
    [Formerly("Fenrir.ScriptFixture.Thing")]
    public class ItemData : IDataAsset
    {
        public string Name = "item";
        [Range(0, 100)] [Step(0.5)] [Units("kg")] public float Weight = 1;
        [DisplayName("Value (gold)")] [Group("Economy")] public uint Price = 10;
        public Rarity Rarity = Rarity.Common;
    }

    // A data asset extending another, with every kind a data asset holds, two upgraders and the members it leaves out.
    [Version(3)]
    public class WeaponData : ItemData
    {
        public sbyte I8 = -1;
        public short I16 = -2;
        public int I32 = -3;
        public long I64 = long.MinValue;
        public byte U8 = 1;
        public ushort U16 = 2;
        public uint U32 = 3;
        public ulong U64 = ulong.MaxValue;
        public double F64 = 0.25;
        public bool Sharp = true;
        public char Letter = 'w';
        public Element Element = Element.Ice;
        [Formerly("Title")] [Multiline] public string Description { get; set; } = "";
        [Path] public string Icon = "icons/sword.png";
        public List<int> Combo = new() { 1, 2, 3 };
        public List<Stats> Levels = new();
        public int? Charges;
        public Stats? Bonus = new Stats { Strength = 2 };
        public Stats Base = new();
        public Vector3 Offset = new(0, 1, 0);
        public Quaternion Grip = Quaternion.Identity;
        public Matrix4x4 Pose = Matrix4x4.Identity;
        public AssetHandle<Mesh> Model;
        public AssetHandle<ItemData> Ammo;
        [Transient] public int Scratch;
        [Hidden] [ReadOnly] public int Secret { get; set; }

        // Not described: a readonly field, a static, a computed property, a non-public field.
        public readonly int Fixed = 7;
        public static int Count;
        public int Doubled => I32 * 2;
        internal int Private = 0;

        // Version 1 named the damage "Dmg"; version 2 added nothing a file holds.
        [Upgrader(1)]
        public static TreeNode From1(TreeNode tree)
        {
            tree.Rename("Dmg", "I32");
            return tree;
        }

        [Upgrader(2)]
        internal static TreeNode From2(TreeNode tree) => tree;
    }

    // The resolver a test installs: an asset is named `assets/<target>/<index>`, its generation one more than its index.
    public sealed class FakeResolver : ITreeReferenceResolver
    {
        public string? PathOf(uint index, uint generation, string target) => $"assets/{target}/{index}";

        public bool TryResolve(string path, string target, out uint index, out uint generation)
        {
            string prefix = $"assets/{target}/";
            if (path.StartsWith(prefix, StringComparison.Ordinal) && uint.TryParse(path.AsSpan(prefix.Length), out index))
            {
                generation = index + 1;
                return true;
            }
            index = generation = 0;
            return false;
        }
    }

    // The data asset codecs from C#: each check gives null when it holds, or what went wrong.
    public static class DataAssets
    {
        private static DataAssetCodec<WeaponData> Weapons()
        {
            DataAssetTable.Register();
            TreeReferences.Resolver = new FakeResolver();
            return DataAssetCodecs.Find<WeaponData>() ?? throw new InvalidOperationException("WeaponData has no codecs");
        }

        // A weapon with every field away from its default.
        private static WeaponData Sample() => new()
        {
            Name = "Sword π",
            Weight = 3.5f,
            Price = 250,
            Rarity = Rarity.Epic,
            I8 = sbyte.MinValue,
            I16 = short.MaxValue,
            I32 = 42,
            I64 = long.MaxValue,
            U8 = byte.MaxValue,
            U16 = 7,
            U32 = uint.MaxValue,
            U64 = 9,
            F64 = -1e300,
            Sharp = false,
            Letter = 'é',
            Element = Element.Fire,
            Description = "two\nlines",
            Icon = "icons/axe.png",
            Combo = new() { 9 },
            Levels = new() { new Stats { Strength = 1, Tags = new() { "a", "b" } }, new Stats() },
            Charges = 0,
            Bonus = null,
            Base = new Stats { Speed = 4 },
            Offset = new(1, 2, 3),
            Grip = new(0.5f, 0.5f, 0.5f, 0.5f),
            Pose = new(new(1, 2, 3, 4), new(5, 6, 7, 8), new(9, 10, 11, 12), new(13, 14, 15, 16)),
            Model = new(4, 5),
            Ammo = new(6, 7),
            Scratch = 99,
            Secret = 5,
        };

        // Every field, and what it holds, as text: the base's first, as the codec walks them.
        private static string Show(WeaponData w) =>
            string.Join("; ", new object?[]
            {
                w.Name, w.Weight, w.Price, w.Rarity, w.I8, w.I16, w.I32, w.I64, w.U8, w.U16, w.U32, w.U64, w.F64, w.Sharp, w.Letter,
                w.Element, w.Description, w.Icon, string.Join(",", w.Combo),
                string.Join(",", w.Levels.Select(static s => Show(s))), w.Charges?.ToString() ?? "none", w.Bonus is { } b ? Show(b) : "none",
                Show(w.Base), w.Offset, w.Grip, w.Pose, $"{w.Model.Index}/{w.Model.Generation}", $"{w.Ammo.Index}/{w.Ammo.Generation}",
                w.Scratch, w.Secret,
            });

        private static string Show(Stats s) => $"({s.Strength} {s.Speed} [{string.Join(",", s.Tags)}])";

        // Through ToTree, the tree encoding both ways and FromTree, every field comes back (but the transient one).
        public static string? RoundTrip()
        {
            DataAssetCodec<WeaponData> codec = Weapons();
            WeaponData sample = Sample();
            WeaponData back = codec.FromTree(TreeReader.Read(TreeWriter.Write(codec.ToTree(sample))));
            sample.Scratch = 0;
            string expected = Show(sample);
            string actual = Show(back);
            return expected == actual ? null : $"expected {expected}\n   found {actual}";
        }

        // A tree holding a few fields reads them, defaults the rest, drops what no field has, and reads a former name.
        public static string? MissingFieldsDefault()
        {
            DataAssetCodec<WeaponData> codec = Weapons();
            TreeNode tree = TreeNode.NewObject()
                .Add("I32", TreeNode.From(5L))
                .Add("Title", TreeNode.From("old name"))
                .Add("Name", TreeNode.From(5L))
                .Add("U8", TreeNode.From(300L))
                .Add("Unknown", TreeNode.From(true))
                .Add("Combo", TreeNode.NewArray().Add(TreeNode.From(1L)).Add(TreeNode.From("x")))
                .Add("Charges", TreeNode.Null)
                .Add("Bonus", TreeNode.Null)
                .Add("Scratch", TreeNode.From(4L));
            WeaponData read = codec.FromTree(tree);
            var expected = new WeaponData { I32 = 5, Description = "old name", Combo = new() { 1, 0 }, Bonus = null };
            return Show(expected) == Show(read) ? null : $"expected {Show(expected)}\n   found {Show(read)}";
        }

        // A field at its default is left out, so a new value's tree is empty and one change is one member.
        public static string? DefaultsLeftOut()
        {
            DataAssetCodec<WeaponData> codec = Weapons();
            string empty = codec.ToTree(new WeaponData()).ToString();
            if (empty != "{}")
                return "a new WeaponData wrote " + empty;
            var changed = new WeaponData { Price = 11, Bonus = null };
            changed.Base.Tags.Add("x");
            string written = codec.ToTree(changed).ToString();
            const string expected = "{\"Price\": 11u, \"Bonus\": null, \"Base\": {\"Tags\": [\"x\"]}}";
            return written == expected ? null : $"expected {expected}\n   found {written}";
        }

        // The upgraders run by the version they take.
        public static string? Upgrades()
        {
            DataAssetCodec<WeaponData> codec = Weapons();
            TreeNode upgraded = codec.Upgrade(2, codec.Upgrade(1, TreeNode.NewObject().Add("Dmg", TreeNode.From(8L))));
            if (upgraded.ToString() != "{\"I32\": 8}")
                return "upgraded to " + upgraded;
            if (string.Join(",", codec.UpgraderVersions) != "1,2" || codec.Version != 3 || codec.BaseName != "Fenrir.ScriptFixture.ItemData")
                return $"versions {string.Join(",", codec.UpgraderVersions)}, version {codec.Version}, base {codec.BaseName}";
            return DataAssetCodecs.Find<ItemData>() is { Version: 1, BaseName: null } ? null : "ItemData is not registered as a root";
        }

        // The tree of the sample weapon, and the description entries of both types, in the tree encoding, as hex.
        public static string SampleTree() => Convert.ToHexString(TreeWriter.Write(Weapons().ToTree(Sample())));

        public static string Entry(string name)
        {
            DataAssetCodec weapons = Weapons();
            DataAssetCodec codec = name == "item" ? DataAssetCodecs.Find<ItemData>()! : weapons;
            return Convert.ToHexString(TreeWriter.Write(codec.Entry()));
        }
    }

    // The tree encoding from C#, over bytes C++'s EncodeTree wrote, as hex both ways.
    public static class Trees
    {
        // Read, then written again: the hex of the bytes, or "error: " and why they were refused.
        public static string Rewrite(string hex)
        {
            if (!TreeReader.TryRead(Convert.FromHexString(hex), out TreeNode? tree, out string? error))
                return "error: " + error;
            return Convert.ToHexString(TreeWriter.Write(tree!));
        }

        // The tree read, one node a line, as FixtureTrees.hpp dumps a C++ value tree.
        public static string Dump(string hex)
        {
            var text = new System.Text.StringBuilder();
            Dump(TreeReader.Read(Convert.FromHexString(hex)), text);
            return text.ToString();
        }

        private static void Dump(TreeNode node, System.Text.StringBuilder text)
        {
            switch (node.Kind)
            {
            case TreeKind.Null:
                text.Append("null");
                break;
            case TreeKind.Bool:
                text.Append(node.AsBool ? "true" : "false");
                break;
            case TreeKind.Signed:
                text.Append("i:").Append(node.AsSigned);
                break;
            case TreeKind.Unsigned:
                text.Append("u:").Append(node.AsUnsigned);
                break;
            case TreeKind.Single:
                text.Append("f:").Append(BitConverter.SingleToUInt32Bits(node.AsSingle).ToString("X8"));
                break;
            case TreeKind.Double:
                text.Append("d:").Append(BitConverter.DoubleToUInt64Bits(node.AsDouble).ToString("X16"));
                break;
            case TreeKind.String:
                text.Append("s:").Append(Convert.ToHexString(Utf16(node.AsString)));
                break;
            case TreeKind.Object:
                text.Append('{');
                foreach (KeyValuePair<string, TreeNode> member in node.Members)
                {
                    text.Append(Convert.ToHexString(Utf16(member.Key))).Append('=');
                    Dump(member.Value, text);
                    text.Append(';');
                }
                text.Append('}');
                break;
            case TreeKind.Array:
                text.Append('[');
                foreach (TreeNode element in node.Elements)
                {
                    Dump(element, text);
                    text.Append(';');
                }
                text.Append(']');
                break;
            }
        }

        // A string's UTF-16 code units, big-endian, so a lone surrogate shows as itself.
        private static byte[] Utf16(string text)
        {
            var units = new byte[text.Length * 2];
            for (int i = 0; i < text.Length; ++i)
            {
                units[2 * i] = (byte)(text[i] >> 8);
                units[2 * i + 1] = (byte)text[i];
            }
            return units;
        }

        // The byte offset a refused buffer is refused at, or -1 when it reads.
        public static long RefusedAt(string hex)
        {
            try
            {
                TreeReader.Read(Convert.FromHexString(hex));
                return -1;
            }
            catch (TreeFormatException refused)
            {
                return refused.Offset;
            }
        }

        // A tree built in C#, with what no text holds: single floats, their -0 and NaN, an unsigned 64-bit integer, a
        // lone surrogate and an empty key.
        public static string Built()
        {
            TreeNode tree = TreeNode.NewObject()
                .Add("single", TreeNode.From(0.7071068f))
                .Add("negativeZero", TreeNode.From(-0.0f))
                .Add("nan", TreeNode.From(BitConverter.UInt32BitsToSingle(0x7FC00001)))
                .Add("double", TreeNode.From(1.0 / 3.0))
                .Add("unsigned", TreeNode.From(ulong.MaxValue))
                .Add("signed", TreeNode.From(long.MinValue))
                .Add("", TreeNode.NewArray().Add(TreeNode.From("\ud800")).Add(TreeNode.From("\U0001F600")).Add(TreeNode.Null)
                                           .Add(TreeNode.From(true)).Add(TreeNode.From(false)).Add(TreeNode.NewObject()));
            return Convert.ToHexString(TreeWriter.Write(tree));
        }
    }
}

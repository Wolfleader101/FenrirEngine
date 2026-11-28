using Fenrir.Math;
using Fenrir.Scripting;

namespace CompileFail;

public enum Tier
{
    Low,
    High,
}

public struct Range2
{
    public float Min;
    public float Max = 1;
    public List<Range2> Children = new();

    public Range2()
    {
    }
}

// Every kind a data asset holds, a base, metadata and upgraders: the codecs the generator writes must compile.
[Version(2)]
[Formerly("CompileFail.OldItem")]
public class Item : IDataAsset
{
    public string Name = "";
    [Path] public string Icon { get; set; } = "";
    [Range(0, 1)] [Step(0.1)] [DisplayName("Chance")] [Group("Drops")] [Units("%")] public float Chance;
    [ReadOnly] [Hidden] [Multiline] public string Notes = "";
    public Tier Tier;
    public List<List<int>> Grid = new();
    public long? Limit;
    public Range2? Spread;
    public Vector3 Offset;
    public Quaternion Turn;
    public Matrix4x4 Pose;
    [Transient] public int Cache;

    [Upgrader(1)]
    public static TreeNode From1(TreeNode tree) => tree;
}

public sealed class Sword : Item
{
    public double Reach = 1.5;
    public List<Range2> Arcs = new();
}

using Fenrir.Scripting;

namespace CompileFail;

[Version(2)]
public class Loot : IDataAsset
{
    public int Gold;

    [Upgrader(1)]
    public static TreeNode From1(TreeNode tree) => tree;

    // A second upgrader from version 1: which one runs?
    [Upgrader(1)]
    public static TreeNode AlsoFrom1(TreeNode tree) => tree;
}

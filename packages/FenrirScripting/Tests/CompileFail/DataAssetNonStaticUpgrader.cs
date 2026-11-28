using Fenrir.Scripting;

namespace CompileFail;

[Version(2)]
public class Loot : IDataAsset
{
    public int Gold;

    // An upgrader runs with no instance: it must be static.
    [Upgrader(1)]
    public TreeNode From1(TreeNode tree) => tree;
}

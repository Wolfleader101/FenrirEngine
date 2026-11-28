using Fenrir.Scripting;

namespace CompileFail;

// A dictionary is no kind a description has.
public class Loot : IDataAsset
{
    public Dictionary<string, int> Counts = new();
}

using Fenrir.Scripting;

namespace CompileFail;

// A data asset type has one canonical name.
public class Loot<T> : IDataAsset
{
    public int Gold;
}

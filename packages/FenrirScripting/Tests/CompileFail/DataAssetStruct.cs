using Fenrir.Scripting;

namespace CompileFail;

// A data asset is a class: the store hands each reader an instance.
public struct Loot : IDataAsset
{
    public int Gold;
}

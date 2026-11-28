using Fenrir.Scripting;

namespace CompileFail;

public class Gem : IDataAsset
{
    public int Shine;
}

// Another data asset is named through an AssetHandle<T>, never held by value.
public class Loot : IDataAsset
{
    public Gem Gem = new();
}

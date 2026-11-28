using Fenrir.Scripting;

namespace CompileFail;

public class Plain
{
    public int Value;
}

// A data asset extends object or another data asset, not any class.
public class Loot : Plain, IDataAsset
{
    public int Gold;
}

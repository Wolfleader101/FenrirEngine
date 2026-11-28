using Fenrir.Scripting;

namespace CompileFail;

// Every value read is a new instance, so an abstract class cannot be one.
public abstract class Loot : IDataAsset
{
    public int Gold;
}

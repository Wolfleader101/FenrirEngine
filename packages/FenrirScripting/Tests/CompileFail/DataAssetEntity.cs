using Fenrir.Scripting;

namespace CompileFail;

// An entity belongs to one scene; a data asset to none.
public class Loot : IDataAsset
{
    public int Gold;
    public Entity Owner;
}

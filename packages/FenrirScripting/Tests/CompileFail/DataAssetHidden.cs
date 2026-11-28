using Fenrir.Scripting;

namespace CompileFail;

public class Outer
{
    // Generated codecs cannot name a type nested in a private one.
    private class Loot : IDataAsset
    {
        public int Gold;
    }
}

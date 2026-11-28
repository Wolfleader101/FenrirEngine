using Fenrir.Assets;
using Fenrir.Scripting;

namespace AssetsScriptFixture;

// A component the script defines, with an entity, an asset handle, a fixed string and a transient field: the scene
// writer saves it by its full name, as it saves a native component, and leaves the transient field out.
public unsafe struct Tagged : IComponent
{
    public Entity Target;
    public AssetHandle<Image> Icon;
    [FixedString] public fixed byte Label[16];
    [Transient] public int Frames;
}

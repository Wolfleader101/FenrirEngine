using System.Runtime.InteropServices;
using Fenrir.Scripting;

namespace CompileFail;

// A union nested in a partial class, whose kinds and tag are nested too.
public static partial class Level
{
    public enum DoorType : byte
    {
        Open,
        Locked,
    }

    public struct Open
    {
        public float Width;
    }

    public struct Locked
    {
        public int Key;
    }

    [TaggedUnion]
    [StructLayout(LayoutKind.Explicit)]
    public partial struct Door
    {
        [FieldOffset(0)] private DoorType _type;
        [FieldOffset(4)] private Open _open;
        [FieldOffset(4)] private Locked _locked;
    }

    public static int Use()
    {
        Door door = new Locked { Key = 7 };
        door.Locked.Key += 1;
        return door.Type == DoorType.Locked ? door.Locked.Key : (int)new Door().Open.Width;
    }
}
